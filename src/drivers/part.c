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

/* Set once GPT was parsed successfully; guards every public entry point */
static int part_ready = 0;

static int part_disk_read(uint64_t lba, uint32_t count, void* buffer) {
  return disk_read((uint32_t)lba, count, buffer);
}

static int part_disk_write(uint64_t lba, uint32_t count, const void* buffer) {
  return disk_write((uint32_t)lba, count, buffer);
}

int part_is_ready(void) {
    return part_ready;
}

/* Namespace storage-class name of the device this partition table
 * belongs to ("ssd"/"hdd"), or 0 when no disk/GPT is attached. Until
 * multi-device support lands there is exactly one owning device. */
const char* part_storage_device(void) {
    if (!part_ready || !disk_is_ready()) return 0;
    return disk_is_ssd() == 1 ? "ssd" : "hdd";
}

// Initialize Part system
void part_init(void) {
  terminal_writestring("[PART] Initializing Partition Management...\n");

  part_ready = 0;

  // Initialize disk driver (skip if already up — kernel_main probes it first)
  if (!disk_is_ready() && disk_init() < 0) {
    terminal_writestring(" ERROR: Failed to initialize disk\n");
    return;
  }

  /* Wire disk ops for GPT */
  part_disk_ops.read  = part_disk_read;
  part_disk_ops.write = part_disk_write;

  // Initialize GPT
  if (gpt_init(&part_disk_ops) < 0) {
        terminal_writestring("  No GPT partition table found\n");
        return;
    }
    
    part_ready = 1;
    
    terminal_writestring("  Disk initialized\n");
    terminal_writestring("  GPT support enabled\n");
    
    // Count used partitions (the table can contain unused/skip entries)
    int used = 0;
    int total = gpt_list_partitions();
    for (int i = 0; i < total; i++) {
        if (gpt_get_partition((uint32_t)i)) used++;
    }
    terminal_writestring("  Found ");
    terminal_put_dec(used);
    terminal_writestring(" partitions\n");
    
    terminal_writestring("[PART] Partition Management Ready!\n");
}

/* Map a well-known GPT type GUID to the part_type enum */
static partition_type_t part_classify_type(const uint8_t type_guid[16]) {
    /* Compare against the known types byte-for-byte */
    static const struct { const uint8_t* guid; partition_type_t type; } known[] = {
        { GPT_TYPE_EFI_SYSTEM, PARTITION_TYPE_EFI_SYSTEM },
        { GPT_TYPE_LINUX_SWAP, PARTITION_TYPE_LINUX_SWAP },
        { GPT_TYPE_LINUX_FS,   PARTITION_TYPE_LINUX_DATA },
        { GPT_TYPE_FAT32,      PARTITION_TYPE_FS_DATA },
    };
    for (size_t i = 0; i < sizeof(known) / sizeof(known[0]); i++) {
        int same = 1;
        for (int b = 0; b < 16; b++) {
            if (known[i].guid[b] != type_guid[b]) { same = 0; break; }
        }
        if (same) return known[i].type;
    }
    return PARTITION_TYPE_FS_DATA;  /* Unknown but present */
}

// Get partition information
int part_get_info(int partition_num, part_info_t* info) {
    if (!part_ready || !info || partition_num < 0) return -1;
    
    const gpt_entry_t* part = gpt_get_partition((uint32_t)partition_num);
    if (!part) return -1;
    
    info->partition_number = partition_num;
    info->type = part_classify_type(part->type_guid);
    info->start_lba = part->start_lba;
    info->size_sectors = part->end_lba - part->start_lba + 1;
    info->filesystem_type = 0;
    
    /* GPT label is UTF-16LE; convert low-byte ASCII (36 chars max) */
    int i = 0;
    for (; i < 36; i++) {
        uint16_t c = part->name[i];
        if (!c) break;
        info->label[i] = (c < 128) ? (char)c : '?';
    }
    info->label[i] = '\0';
    
    return 0;
}

// List all used partition table entries (dense; gaps from unused slots dropped)
int part_list_partitions(part_info_t* partitions, int max_count) {
    if (!part_ready || !partitions || max_count <= 0) return -1;
    
    int out = 0;
    int total = gpt_list_partitions();
    for (int i = 0; i < total && out < max_count; i++) {
        if (part_get_info(i, &partitions[out]) == 0) out++;
    }
    
    return out;
}

/* Translate a partition-relative LBA range to absolute disk LBA, checking
 * that the range fits inside the partition and the LBA28 disk driver limit. */
static int part_translate(int partition_num, uint64_t start_lba, size_t sectors,
                          uint32_t* abs_lba) {
    if (!part_ready || sectors == 0 || sectors > 256) return -1;
    
    const gpt_entry_t* part = gpt_get_partition((uint32_t)partition_num);
    if (!part) return -1;
    
    uint64_t part_sectors = part->end_lba - part->start_lba + 1;
    if (start_lba >= part_sectors) return -1;
    if ((uint64_t)sectors > part_sectors - start_lba) return -1;
    
    uint64_t abs = part->start_lba + start_lba;
    if (abs > 0x0FFFFFFF) return -1;  /* disk driver is LBA28 */
    
    *abs_lba = (uint32_t)abs;
    return 0;
}

// Read partition sectors (start_lba is relative to the partition start)
int part_read_sectors(int partition_num, uint64_t start_lba, void* buffer, size_t sectors) {
    if (!buffer) return -1;
    uint32_t abs_lba;
    if (part_translate(partition_num, start_lba, sectors, &abs_lba) < 0) return -1;
    return disk_read(abs_lba, (uint32_t)sectors, buffer);
}

// Write partition sectors (start_lba is relative to the partition start)
int part_write_sectors(int partition_num, uint64_t start_lba, const void* buffer, size_t sectors) {
    if (!buffer) return -1;
    uint32_t abs_lba;
    if (part_translate(partition_num, start_lba, sectors, &abs_lba) < 0) return -1;
    return disk_write(abs_lba, (uint32_t)sectors, buffer);
}

// Disk information
int part_get_disk_info(disk_info_t* out) {
    if (!out || !disk_is_ready()) return -1;
    const disk_info_t* src = disk_get_info();
    if (!src) return -1;
    *out = *src;
    return 0;
}
