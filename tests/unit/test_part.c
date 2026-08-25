/*
 * test_part.c - Unit Tests for the GPT Partition Manager (part.c)
 *
 * Copyright (C) 2026 CazyUndee
 * SPDX-License-Identifier: AGPL-3.0
 *
 * Compiles the real part.c against mocked disk_* / gpt_* drivers and
 * validates partition-relative LBA translation, bounds checking, the
 * dense partition listing, and part_get_info field extraction.
 */

#include <string.h>
#include "../test_framework.h"
#include "../../include/part.h"
#include "../../include/gpt.h"

/* Mock control hooks provided by tests/mocks/mock_kernel.c */
void mock_disk_set_ready(int ready);
void mock_disk_io_reset(void);
void mock_disk_last_io(uint64_t* lba, uint32_t* count, int* is_write, int* calls);
void mock_gpt_setup(const gpt_entry_t* entries, uint32_t count);

#define PART_TEST_NUM_ENTRIES 3

static uint8_t part_buf[512 * 8];

/* Build a table with a gap: slot 0 unused, slot 1 "plan0-test" (LBA
 * 2048-4095), slot 2 Linux-FS typed (LBA 4096-8191, no label). */
static void make_table(gpt_entry_t* t) {
    memset(t, 0, sizeof(gpt_entry_t) * PART_TEST_NUM_ENTRIES);

    t[1].start_lba = 2048;
    t[1].end_lba   = 4095;              /* 2048 sectors = 1024 KB */
    t[1].type_guid[0] = 0x11;           /* any non-zero type */
    const char* label = "plan0-test";
    for (int i = 0; label[i]; i++) t[1].name[i] = (uint16_t)label[i];

    memcpy(t[2].type_guid, GPT_TYPE_LINUX_FS, 16);
    t[2].start_lba = 4096;
    t[2].end_lba   = 8191;
    t[2].type_guid[0] = GPT_TYPE_LINUX_FS[0];
}

static void setup_ready(gpt_entry_t* t) {
    make_table(t);
    mock_disk_set_ready(1);
    mock_gpt_setup(t, PART_TEST_NUM_ENTRIES);
    mock_disk_io_reset();
    part_init();
}

static void test_part_init_no_gpt(void) {
    mock_disk_set_ready(1);
    mock_gpt_setup(0, 0);
    part_init();
    ASSERT(part_is_ready() == 0, "no GPT table -> part not ready");
    TEST_PASS();
}

static void test_part_init_with_gpt(void) {
    gpt_entry_t t[PART_TEST_NUM_ENTRIES];
    setup_ready(t);
    ASSERT(part_is_ready() == 1, "valid GPT -> part ready");
    TEST_PASS();
}

static void test_part_read_translates_lba(void) {
    gpt_entry_t t[PART_TEST_NUM_ENTRIES];
    setup_ready(t);

    ASSERT(part_read_sectors(1, 10, part_buf, 4) == 0, "read within partition succeeds");

    uint64_t lba; uint32_t count; int is_write, calls;
    mock_disk_last_io(&lba, &count, &is_write, &calls);
    ASSERT_EQ(lba, 2058, "partition-relative lba 10 -> absolute 2058");
    ASSERT_EQ(count, 4, "sector count passes through");
    ASSERT_EQ(is_write, 0, "read is not a write");
    ASSERT_EQ(calls, 1, "exactly one disk call");
    TEST_PASS();
}

static void test_part_write_translates_lba(void) {
    gpt_entry_t t[PART_TEST_NUM_ENTRIES];
    setup_ready(t);

    ASSERT(part_write_sectors(1, 0, part_buf, 2) == 0, "write within partition succeeds");

    uint64_t lba; uint32_t count; int is_write;
    mock_disk_last_io(&lba, &count, &is_write, 0);
    ASSERT_EQ(lba, 2048, "partition-relative lba 0 -> partition start");
    ASSERT_EQ(count, 2, "sector count passes through");
    ASSERT_EQ(is_write, 1, "write reaches disk_write");
    TEST_PASS();
}

static void test_part_bounds_checks(void) {
    gpt_entry_t t[PART_TEST_NUM_ENTRIES];
    setup_ready(t);
    mock_disk_io_reset();

    /* Partition spans 2048 sectors (rel 0..2047). */
    ASSERT(part_read_sectors(1, 2048, part_buf, 1) < 0, "start at end is out of range");
    ASSERT(part_read_sectors(1, 2047, part_buf, 2) < 0, "read past end rejected");
    ASSERT(part_write_sectors(1, 2047, part_buf, 2) < 0, "write past end rejected");
    ASSERT(part_read_sectors(1, 0, part_buf, 0) < 0, "zero sectors rejected");
    ASSERT(part_read_sectors(1, 0, part_buf, 257) < 0, "over LBA28 driver max rejected");
    ASSERT(part_read_sectors(1, 0, 0, 1) < 0, "NULL buffer rejected");
    ASSERT(part_read_sectors(99, 0, part_buf, 1) < 0, "unknown partition rejected");
    ASSERT(part_read_sectors(0, 0, part_buf, 1) < 0, "unused GPT slot rejected");

    int calls;
    mock_disk_last_io(0, 0, 0, &calls);
    ASSERT_EQ(calls, 0, "rejections never touch the disk");
    TEST_PASS();
}

static void test_part_requires_ready(void) {
    mock_disk_set_ready(0);
    mock_gpt_setup(0, 0);
    part_init();  /* disk not ready -> part stays unready */
    ASSERT(part_is_ready() == 0, "no disk -> part not ready");
    ASSERT(part_read_sectors(0, 0, part_buf, 1) < 0, "read without GPT rejected");
    part_info_t info;
    ASSERT(part_get_info(0, &info) < 0, "info without GPT rejected");
    ASSERT(part_list_partitions(&info, 1) < 0, "list without GPT rejected");
    TEST_PASS();
}

static void test_part_get_info_fields(void) {
    gpt_entry_t t[PART_TEST_NUM_ENTRIES];
    setup_ready(t);

    part_info_t info;
    ASSERT(part_get_info(1, &info) == 0, "get_info on slot 1 succeeds");
    ASSERT_EQ(info.partition_number, 1, "partition number recorded");
    ASSERT_EQ(info.start_lba, 2048, "start_lba from GPT entry");
    ASSERT_EQ(info.size_sectors, 2048, "size computed from inclusive end");
    ASSERT(strcmp(info.label, "plan0-test") == 0, "UTF-16LE label converted to ASCII");

    ASSERT(part_get_info(2, &info) == 0, "get_info on slot 2 succeeds");
    ASSERT_EQ(info.type, PARTITION_TYPE_LINUX_DATA, "Linux FS GUID classified");
    ASSERT(info.label[0] == '\0', "empty label stays empty");

    ASSERT(part_get_info(0, &info) < 0, "unused slot has no info");
    ASSERT(part_get_info(1, 0) < 0, "NULL out-param rejected");
    TEST_PASS();
}

static void test_part_list_partitions_dense(void) {
    gpt_entry_t t[PART_TEST_NUM_ENTRIES];
    setup_ready(t);

    part_info_t list[8];
    int count = part_list_partitions(list, 8);
    ASSERT_EQ(count, 2, "gap slot dropped from listing");
    ASSERT_EQ(list[0].partition_number, 1, "first listed = original slot 1");
    ASSERT_EQ(list[1].partition_number, 2, "second listed = original slot 2");
    TEST_PASS();
}

test_suite_t* create_part_test_suite(void) {
    static test_suite_t suite;
    test_suite_init(&suite, "Partition Manager (GPT)");

    test_suite_add_test(&suite, "part_init_no_gpt", test_part_init_no_gpt);
    test_suite_add_test(&suite, "part_init_with_gpt", test_part_init_with_gpt);
    test_suite_add_test(&suite, "part_read_translates_lba", test_part_read_translates_lba);
    test_suite_add_test(&suite, "part_write_translates_lba", test_part_write_translates_lba);
    test_suite_add_test(&suite, "part_bounds_checks", test_part_bounds_checks);
    test_suite_add_test(&suite, "part_requires_ready", test_part_requires_ready);
    test_suite_add_test(&suite, "part_get_info_fields", test_part_get_info_fields);
    test_suite_add_test(&suite, "part_list_partitions_dense", test_part_list_partitions_dense);

    return &suite;
}
