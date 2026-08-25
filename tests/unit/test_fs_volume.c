/*
 * test_volume.c - Tests for the storage volume binding layer
 *
 * Copyright (C) 2026 CazyUndee
 * SPDX-License-Identifier: AGPL-3.0
 *
 * Compiles the real volume.c + part.c against mocked disk/gpt and
 * verifies partition selection, validation, and base/size reporting
 * for the fs volume binding (docs/NAMESPACE.md). The actual offset
 * behavior of fs.c is validated end-to-end in QEMU (drive_parts.py).
 */

#include <string.h>
#include "../test_framework.h"
#include "../../include/volume.h"
#include "../../include/part.h"
#include "../../include/gpt.h"

/* Mock control hooks (tests/mocks/mock_kernel.c) */
void mock_disk_set_ready(int ready);
void mock_gpt_setup(const void* entries, unsigned count);

static void setup_two_partitions(void) {
    gpt_entry_t t[2];
    memset(t, 0, sizeof(t));
    memcpy(t[0].type_guid, GPT_TYPE_LINUX_FS, 16);
    t[0].start_lba = 2048;
    t[0].end_lba   = 4095;                     /* 2048 sectors = 1 MB */
    memcpy(t[1].type_guid, GPT_TYPE_LINUX_FS, 16);
    t[1].start_lba = 4096;
    t[1].end_lba   = 6143;

    mock_disk_set_ready(1);
    mock_gpt_setup(t, 2);
    part_init();
    volume_use_whole_disk();   /* isolate from other suites' volume state */
    ASSERT(part_is_ready() == 1, "partition layer ready for volume tests");
}

static void test_selection_and_validation(void) {
    setup_two_partitions();

    ASSERT(volume_use_partition(0) < 0, "partition numbers are 1-based");
    ASSERT(volume_use_partition(99) < 0, "out-of-range partition rejected");
    ASSERT_EQ(volume_base_lba(), 0, "failed binding leaves base at whole-disk");
    ASSERT_EQ(volume_sectors(), 0, "failed binding leaves size unbounded");

    ASSERT(volume_use_partition(1) == 0, "partition 1 binds");
    ASSERT_EQ(volume_base_lba(), 2048, "base is partition 1 start");
    ASSERT_EQ(volume_sectors(), 2048, "size from inclusive GPT range");

    ASSERT(volume_use_partition(2) == 0, "partition 2 binds");
    ASSERT_EQ(volume_base_lba(), 4096, "partition 2 start honored");

    TEST_PASS();
}

static void test_legacy_whole_disk(void) {
    setup_two_partitions();
    ASSERT(volume_use_partition(1) == 0, "bind first");
    volume_use_whole_disk();
    ASSERT_EQ(volume_base_lba(), 0, "legacy base is LBA 0");
    ASSERT_EQ(volume_sectors(), 0, "legacy mode is unbounded");
    TEST_PASS();
}

test_suite_t* create_fs_volume_test_suite(void) {
    static test_suite_t suite;
    test_suite_init(&suite, "FS Volume Binding");

    test_suite_add_test(&suite, "selection_and_validation", test_selection_and_validation);
    test_suite_add_test(&suite, "legacy_whole_disk", test_legacy_whole_disk);

    return &suite;
}
