/*
 * volume.c - Storage volume binding for the filesystem
 *
 * Copyright (C) 2026 CazyUndee
 * SPDX-License-Identifier: AGPL-3.0
 */

#include "volume.h"
#include "part.h"

static uint64_t vol_base = 0;      /* first LBA of the bound volume */
static uint64_t vol_sectors = 0;   /* volume size in sectors (0 = whole disk) */

int volume_use_partition(int partition_num) {
    part_info_t list[16];
    int count = part_list_partitions(list, 16);
    if (partition_num < 1 || partition_num > count) return -1;
    vol_base = list[partition_num - 1].start_lba;
    vol_sectors = list[partition_num - 1].size_sectors;
    return 0;
}

void volume_use_whole_disk(void) {
    vol_base = 0;
    vol_sectors = 0;
}

uint64_t volume_base_lba(void) {
    return vol_base;
}

uint64_t volume_sectors(void) {
    return vol_sectors;
}
