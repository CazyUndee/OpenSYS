/*
 * part.h - Partition Management Interface
 *
 * Copyright (C) 2026 CazyUndee
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 * 
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef PART_H
#define PART_H

#include <stdint.h>
#include "disk.h"

// Partition types
typedef enum {
    PARTITION_TYPE_UNUSED = 0,
    PARTITION_TYPE_EFI_SYSTEM = 1,
    PARTITION_TYPE_BIOS_BOOT = 2,
    PARTITION_TYPE_FS_DATA = 3,
    PARTITION_TYPE_LINUX_SWAP = 4,
    PARTITION_TYPE_LINUX_DATA = 5
} partition_type_t;

// Partition information
typedef struct {
    int partition_number;
    partition_type_t type;
    uint64_t start_lba;
    uint64_t size_sectors;
    uint32_t filesystem_type;
    char label[37];  // GPT partition label
} part_info_t;

// Core API
void part_init(void);
int part_is_ready(void);

/* Namespace storage-class name of the detected device ("hdd"/"ssd"),
 * or 0 when no disk/GPT is attached. */
const char* part_storage_device(void);

int part_get_info(int partition_num, part_info_t* info);
int part_list_partitions(part_info_t* partitions, int max_count);

// Sector operations
int part_read_sectors(int partition_num, uint64_t start_lba, void* buffer, size_t sectors);
int part_write_sectors(int partition_num, uint64_t start_lba, const void* buffer, size_t sectors);

// Partition management
int part_format_partition(int partition_num, partition_type_t type);
int part_delete_partition(int partition_num);

// Disk information
int part_get_disk_info(disk_info_t* info);

#endif // PART_H
