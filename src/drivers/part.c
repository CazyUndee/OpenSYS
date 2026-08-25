/*
 * part.c - GPT Partition Manager
 * 
 * Copyright (C) 2026 CazyUndee
 * SPDX-License-Identifier: AGPL-3.0
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

#include "part.h"
#include "disk.h"
#include "gpt.h"
#include "memory.h"
#include "vga.h"
#include <stddef.h>
#include <stdint.h>

/* Disk operations passed to GPT */
static disk_ops_t part_disk_ops = {0};

static int part_disk_read(uint64_t lba, uint32_t count, void* buffer) {
  return disk_read((uint32_t)lba, count, buffer);
}

static int part_disk_write(uint64_t lba, uint32_t count, const void* buffer) {
  return disk_write((uint32_t)lba, count, buffer);
}

// Initialize Part system
void part_init(void) {
  terminal_writestring("[PART] Initializing Partition Management...\n");

  // Initialize disk driver
  if (disk_init() < 0) {
    terminal_writestring(" ERROR: Failed to initialize disk\n");
    return;
  }

  /* Wire disk ops for GPT */
  part_disk_ops.read  = part_disk_read;
  part_disk_ops.write = part_disk_write;

  // Initialize GPT
  if (gpt_init(&part_disk_ops) < 0) {
        terminal_writestring("  ERROR: Failed to initialize GPT\n");
        return;
    }
    
    terminal_writestring("  Disk initialized\n");
    terminal_writestring("  GPT support enabled\n");
    
    // List partitions
    int partition_count = gpt_list_partitions();
    terminal_writestring("  Found ");
    terminal_put_dec(partition_count);
    terminal_writestring(" partitions\n");
    
    terminal_writestring("[PART] Partition Management Ready!\n");
}

// Get partition information
int part_get_info(int partition_num, part_info_t* info) {
    if (!info || partition_num < 0) return -1;
    
    // This would need GPT implementation details
    info->partition_number = partition_num;
    info->type = 0;  // Would be determined from GPT
    info->start_lba = 0;
    info->size_sectors = 0;
    info->filesystem_type = 0;
    
    return 0;
}

// List all partitions
int part_list_partitions(part_info_t* partitions, int max_count) {
    if (!partitions || max_count <= 0) return -1;
    
    int count = gpt_list_partitions();
    if (count > max_count) count = max_count;
    
    for (int i = 0; i < count; i++) {
        part_get_info(i, &partitions[i]);
    }
    
    return count;
}

// Read partition sectors
int part_read_sectors(int partition_num, uint64_t start_lba, void* buffer, size_t sectors) {
    (void)partition_num;  // TODO: partition-specific LBA offset
    return disk_read(start_lba, sectors, buffer);
}

// Write partition sectors
int part_write_sectors(int partition_num, uint64_t start_lba, const void* buffer, size_t sectors) {
    (void)partition_num;  // TODO: partition-specific LBA offset
    return disk_write(start_lba, sectors, buffer);
}
