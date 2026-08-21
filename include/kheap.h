/*
 * kheap.h - Kernel Heap Interface
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

#ifndef KHEAP_H
#define KHEAP_H

#include <stdint.h>
#include <stddef.h>

// Heap statistics
typedef struct {
    uint64_t total_size;
    uint64_t used_size;
    uint64_t free_blocks;
    uint64_t largest_free;
    uint32_t block_count;
    uint32_t allocated_blocks;
} kheap_stats_t;

// Core API
void kheap_init(uint64_t start, uint64_t size);
void kheap_get_stats(kheap_stats_t* stats);

// Memory allocation
void* kmalloc(size_t size);
void kfree(void* ptr);

// Heap debugging
void kheap_dump(void);

// Walk the block list, verify magic numbers and structural integrity.
// Returns the number of corruptions found (0 = healthy heap).
int kheap_validate(void);

#endif // KHEAP_H
