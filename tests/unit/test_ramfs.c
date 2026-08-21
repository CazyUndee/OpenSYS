/*
 * test_ramfs.c - Unit Tests for the RAM Filesystem
 *
 * Copyright (C) 2026 CazyUndee
 * SPDX-License-Identifier: AGPL-3.0
 *
 * Compiles the real ramfs.c (with mocked kmalloc/kfree from mock_kernel.c)
 * and validates create/find/write/read/delete semantics plus usage stats.
 */

#include <string.h>
#include "../test_framework.h"
#include "../../include/ramfs.h"

static void test_ramfs_init(void) {
    ramfs_init();
    /* Root directory should exist after init */
    ASSERT(ramfs_find("/") >= 0, "root / should exist after init");
    ASSERT(ramfs_is_dir(ramfs_find("/")) == 1, "root should be a directory");
    TEST_PASS();
}

static void test_ramfs_create_find(void) {
    ramfs_init();
    int fd = ramfs_create("a.txt");
    ASSERT(fd >= 0, "create a.txt should succeed");
    int found = ramfs_find("a.txt");
    ASSERT(found == fd, "find should locate the created file");
    TEST_PASS();
}

static void test_ramfs_mkdir(void) {
    ramfs_init();
    int r = ramfs_mkdir("docs");
    ASSERT(r == 0, "mkdir docs should succeed");
    int fd = ramfs_find("docs");
    ASSERT(fd >= 0, "docs should be findable");
    ASSERT(ramfs_is_dir(fd) == 1, "docs should be a directory");
    TEST_PASS();
}

static void test_ramfs_write_read_roundtrip(void) {
    ramfs_init();
    int fd = ramfs_create("b.txt");
    ASSERT(fd >= 0, "create b.txt should succeed");

    const char* msg = "hello world";
    int written = ramfs_write(fd, msg, (uint32_t)strlen(msg));
    ASSERT(written == (int)strlen(msg), "write should return byte count");
    ASSERT(ramfs_size(fd) == (uint32_t)strlen(msg), "size should match written bytes");

    char buf[64];
    memset(buf, 0, sizeof(buf));
    int n = ramfs_read(fd, buf, sizeof(buf), 0);
    ASSERT(n == (int)strlen(msg), "read should return byte count");
    ASSERT(strcmp(buf, msg) == 0, "read data should match written data");
    TEST_PASS();
}

static void test_ramfs_write_grows_capacity(void) {
    ramfs_init();
    int fd = ramfs_create("big.txt");
    ASSERT(fd >= 0, "create big.txt should succeed");

    /* Write more than the initial 256-byte buffer */
    char data[1000];
    for (int i = 0; i < 1000; i++) data[i] = (char)('a' + (i % 26));
    int written = ramfs_write(fd, data, 1000);
    ASSERT(written == 1000, "write of 1000 bytes should succeed");

    char buf[1000];
    memset(buf, 0, sizeof(buf));
    int n = ramfs_read(fd, buf, sizeof(buf), 0);
    ASSERT(n == 1000, "read should return 1000 bytes");
    ASSERT(memcmp(buf, data, 1000) == 0, "data should round-trip after growth");
    TEST_PASS();
}

static void test_ramfs_append(void) {
    ramfs_init();
    int fd = ramfs_create("log.txt");
    ramfs_write(fd, "abc", 3);
    ramfs_write(fd, "def", 3);  /* appends at end-of-file */

    ASSERT(ramfs_size(fd) == 6, "size should be 6 after two appends");

    char buf[16];
    memset(buf, 0, sizeof(buf));
    ramfs_read(fd, buf, sizeof(buf), 0);
    ASSERT(strcmp(buf, "abcdef") == 0, "appended data should follow original");
    TEST_PASS();
}

static void test_ramfs_read_offset(void) {
    ramfs_init();
    int fd = ramfs_create("off.txt");
    ramfs_write(fd, "0123456789", 10);

    char buf[8];
    memset(buf, 0, sizeof(buf));
    int n = ramfs_read(fd, buf, sizeof(buf), 4);
    ASSERT(n == 6, "read from offset 4 should return 6 bytes");
    ASSERT(strcmp(buf, "456789") == 0, "offset read should return tail");
    TEST_PASS();
}

static void test_ramfs_read_past_end(void) {
    ramfs_init();
    int fd = ramfs_create("eof.txt");
    ramfs_write(fd, "hi", 2);

    char buf[16];
    int n = ramfs_read(fd, buf, sizeof(buf), 10);
    ASSERT(n == 0, "read past end should return 0");
    TEST_PASS();
}

static void test_ramfs_delete(void) {
    ramfs_init();
    int fd = ramfs_create("tmp.txt");
    ASSERT(fd >= 0, "create tmp.txt should succeed");
    ASSERT(ramfs_delete("tmp.txt") == 0, "delete should succeed");
    ASSERT(ramfs_find("tmp.txt") < 0, "file should be gone after delete");
    TEST_PASS();
}

static void test_ramfs_delete_missing(void) {
    ramfs_init();
    ASSERT(ramfs_delete("nope.txt") == -1, "delete of missing file should fail");
    TEST_PASS();
}

static void test_ramfs_stats_empty(void) {
    ramfs_init();
    ramfs_stats_t stats;
    ramfs_get_stats(&stats);

    ASSERT(stats.total_capacity == RAMFS_TOTAL_CAPACITY, "total capacity should be 1 MB");
    ASSERT(stats.used_bytes == 0, "used bytes should be 0 on empty fs");
    ASSERT(stats.free_bytes == RAMFS_TOTAL_CAPACITY, "free bytes should equal capacity");
    ASSERT(stats.file_count >= 1, "file count should include root");
    ASSERT(stats.dir_count >= 1, "dir count should include root");
    TEST_PASS();
}

static void test_ramfs_stats_usage_tracks_writes(void) {
    ramfs_init();
    ramfs_stats_t stats;

    /* Initial state */
    ramfs_get_stats(&stats);
    uint64_t used0 = stats.used_bytes;
    uint32_t files0 = stats.file_count;

    int fd = ramfs_create("data.bin");
    ASSERT(fd >= 0, "create data.bin should succeed");
    const char* payload = "payload-data";
    ramfs_write(fd, payload, (uint32_t)strlen(payload));

    ramfs_get_stats(&stats);
    ASSERT(stats.used_bytes == used0 + strlen(payload), "used bytes should grow by payload size");
    ASSERT(stats.file_count == files0 + 1, "file count should grow by one");

    /* Delete frees the accounting */
    ramfs_delete("data.bin");
    ramfs_get_stats(&stats);
    ASSERT(stats.used_bytes == used0, "used bytes should return to baseline after delete");
    ASSERT(stats.file_count == files0, "file count should return to baseline after delete");
    TEST_PASS();
}

static void test_ramfs_stats_dirs(void) {
    ramfs_init();
    ramfs_stats_t stats;

    ramfs_get_stats(&stats);
    uint32_t dirs0 = stats.dir_count;

    ramfs_mkdir("sub");
    ramfs_get_stats(&stats);
    ASSERT(stats.dir_count == dirs0 + 1, "mkdir should increment dir count");
    TEST_PASS();
}

static void test_ramfs_max_files(void) {
    ramfs_init();
    int created = 0;
    char name[16];
    for (int i = 0; i < 100; i++) {
        snprintf(name, sizeof(name), "f%d.txt", i);
        int fd = ramfs_create(name);
        if (fd < 0) break;
        created++;
    }
    /* MAX_FILES is 64 including root, so at most 63 files can be created */
    ASSERT(created <= 63, "should not exceed MAX_FILES limit");
    ASSERT(created >= 60, "should be able to create most files");
    TEST_PASS();
}

/* ---- Create test suite ---- */

test_suite_t* create_ramfs_test_suite(void) {
    static test_suite_t suite;
    test_suite_init(&suite, "RAM Filesystem");

    test_suite_add_test(&suite, "ramfs_init", test_ramfs_init);
    test_suite_add_test(&suite, "ramfs_create_find", test_ramfs_create_find);
    test_suite_add_test(&suite, "ramfs_mkdir", test_ramfs_mkdir);
    test_suite_add_test(&suite, "ramfs_write_read_roundtrip", test_ramfs_write_read_roundtrip);
    test_suite_add_test(&suite, "ramfs_write_grows_capacity", test_ramfs_write_grows_capacity);
    test_suite_add_test(&suite, "ramfs_append", test_ramfs_append);
    test_suite_add_test(&suite, "ramfs_read_offset", test_ramfs_read_offset);
    test_suite_add_test(&suite, "ramfs_read_past_end", test_ramfs_read_past_end);
    test_suite_add_test(&suite, "ramfs_delete", test_ramfs_delete);
    test_suite_add_test(&suite, "ramfs_delete_missing", test_ramfs_delete_missing);
    test_suite_add_test(&suite, "ramfs_stats_empty", test_ramfs_stats_empty);
    test_suite_add_test(&suite, "ramfs_stats_usage_tracks_writes", test_ramfs_stats_usage_tracks_writes);
    test_suite_add_test(&suite, "ramfs_stats_dirs", test_ramfs_stats_dirs);
    test_suite_add_test(&suite, "ramfs_max_files", test_ramfs_max_files);

    return &suite;
}
