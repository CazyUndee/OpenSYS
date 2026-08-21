/*
 * kheap.c - Kernel Heap Manager
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

#include "kheap.h"
#include "memory.h"
#include "vga.h"
#include "pmm.h"
#include "paging.h"
#include <stddef.h>
#include <stdint.h>

typedef struct kheap_block {
    struct kheap_block* next;
    uint64_t size;
    uint16_t flags;
    uint16_t magic;
} kheap_block_t;

#define KHEAP_FREE      0x00
#define KHEAP_ALLOCATED 0x01
#define KHEAP_MAGIC     0xBEEF
#define MIN_BLOCK_SIZE  (sizeof(kheap_block_t) + 16)

#define ALIGN_UP(x, a)   (((x) + (a) - 1) & ~((a) - 1))
#define BLOCK_DATA(b)    ((void*)((uint64_t)(b) + sizeof(kheap_block_t)))
#define DATA_BLOCK(d)    ((kheap_block_t*)((uint64_t)(d) - sizeof(kheap_block_t)))

static kheap_block_t* heap_first = 0;
static uint64_t bytes_used = 0;
static uint64_t bytes_free = 0;

#define PAGE_PRESENT  0x01
#define PAGE_WRITABLE 0x02

// Initialize kernel heap
void kheap_init(uint64_t start, uint64_t size) {
    terminal_writestring("[KHEAP] Initializing Kernel Heap...\n");
    
    /* Allocate and map heap pages */
    for (uint64_t addr = start; addr < start + size; addr += 4096) {
        phys_addr_t phys = pmm_alloc_page();
        if (phys) {
            paging_map(addr, phys, PAGE_WRITABLE);
        }
    }
    
    heap_first = (kheap_block_t*)start;
    heap_first->next = 0;
    heap_first->size = size - sizeof(kheap_block_t);
    heap_first->flags = KHEAP_FREE;
    heap_first->magic = KHEAP_MAGIC;
    
    bytes_used = 0;
    bytes_free = size - sizeof(kheap_block_t);
    
    terminal_writestring("  Heap start: 0x");
    terminal_put_hex(start);
    terminal_writestring("\n");
    terminal_writestring("  Heap size: ");
    terminal_put_dec(size / (1024 * 1024));
    terminal_writestring(" MB\n");
    
    terminal_writestring("[KHEAP] Kernel Heap Ready!\n");
}

static void split_block(kheap_block_t* block, uint64_t size) {
    uint64_t remaining = block->size - size - sizeof(kheap_block_t);
    if (remaining < MIN_BLOCK_SIZE) return;
    
    kheap_block_t* new_block = (kheap_block_t*)((uint64_t)block + sizeof(kheap_block_t) + size);
    new_block->next = block->next;
    new_block->size = remaining;
    new_block->flags = KHEAP_FREE;
    new_block->magic = KHEAP_MAGIC;
    
    block->size = size;
    block->next = new_block;
    
    bytes_free -= sizeof(kheap_block_t);
}

static void merge_blocks(void) {
    kheap_block_t* block = heap_first;
    while (block && block->next) {
        if (!(block->flags & KHEAP_ALLOCATED) && !(block->next->flags & KHEAP_ALLOCATED)) {
            uint64_t expected = (uint64_t)block + sizeof(kheap_block_t) + block->size;
            if ((uint64_t)block->next == expected) {
                kheap_block_t* next = block->next;
                block->size += sizeof(kheap_block_t) + next->size;
                block->next = next->next;
                bytes_free += sizeof(kheap_block_t);
                continue;
            }
        }
        block = block->next;
    }
}

// Allocate memory from kernel heap
void* kmalloc(size_t size) {
    if (size == 0) return 0;
    size = ALIGN_UP(size, 16);
    
    kheap_block_t* block = heap_first;
    while (block) {
        if (block->magic != KHEAP_MAGIC) return 0;
        if (!(block->flags & KHEAP_ALLOCATED) && block->size >= size) {
            split_block(block, size);
            block->flags = KHEAP_ALLOCATED;
            bytes_used += block->size;
            bytes_free -= block->size;
            return BLOCK_DATA(block);
        }
        block = block->next;
    }
    return 0;
}

// Free memory from kernel heap
void kfree(void* ptr) {
    if (!ptr) return;
    
    kheap_block_t* block = DATA_BLOCK(ptr);
    if (block->magic != KHEAP_MAGIC) return;
    if (!(block->flags & KHEAP_ALLOCATED)) return;
    
    block->flags = KHEAP_FREE;
    bytes_used -= block->size;
    bytes_free += block->size;
    
    merge_blocks();
}

void* krealloc(void* ptr, size_t new_size) {
    if (new_size == 0) { kfree(ptr); return 0; }
    if (!ptr) return kmalloc(new_size);
    
    kheap_block_t* block = DATA_BLOCK(ptr);
    if (block->magic != KHEAP_MAGIC) return 0;
    if (new_size <= block->size) return ptr;
    
    void* new_ptr = kmalloc(new_size);
    if (!new_ptr) return 0;
    
    uint8_t* src = (uint8_t*)ptr;
    uint8_t* dst = (uint8_t*)new_ptr;
    for (uint64_t i = 0; i < block->size && i < new_size; i++) {
        dst[i] = src[i];
    }
    
    kfree(ptr);
    return new_ptr;
}

// Get heap statistics
void kheap_get_stats(kheap_stats_t* stats) {
    if (!stats) return;

    stats->total_size = bytes_used + bytes_free;
    stats->used_size = bytes_used;
    stats->free_blocks = 0;
    stats->largest_free = 0;
    stats->block_count = 0;
    stats->allocated_blocks = 0;

    kheap_block_t* block = heap_first;
    while (block && block->magic == KHEAP_MAGIC) {
        stats->block_count++;
        if (block->flags & KHEAP_ALLOCATED) {
            stats->allocated_blocks++;
        } else {
            stats->free_blocks++;
            if (block->size > stats->largest_free) {
                stats->largest_free = block->size;
            }
        }
        block = block->next;
    }
}

// Walk the block list and verify structural integrity.
// Checks: magic number on every block, flags are valid, the linked
// list is contiguous (next block starts exactly where this one ends),
// and sizes are sane. Returns the number of corruptions found
// (0 = healthy heap).
static uint64_t walk_blocks(uint64_t* out_allocated, uint64_t* out_largest_free) {
    uint64_t corruptions = 0;
    uint64_t allocated = 0;
    uint64_t largest_free = 0;

    kheap_block_t* block = heap_first;
    while (block) {
        if (block->magic != KHEAP_MAGIC) {
            corruptions++;
            break;  /* Cannot trust ->next after a bad magic */
        }

        if (block->flags != KHEAP_FREE && block->flags != KHEAP_ALLOCATED) {
            corruptions++;
        }

        if (block->next) {
            uint64_t expected = (uint64_t)block + sizeof(kheap_block_t) + block->size;
            if ((uint64_t)block->next != expected) {
                corruptions++;  /* Non-contiguous list */
            }
        }

        if (block->flags & KHEAP_ALLOCATED) {
            allocated++;
        } else if (block->size > largest_free) {
            largest_free = block->size;
        }

        block = block->next;
    }

    if (out_allocated) *out_allocated = allocated;
    if (out_largest_free) *out_largest_free = largest_free;
    return corruptions;
}

// Validate heap integrity. Returns number of corruptions (0 = healthy).
// Silent — use kheap_dump() for diagnostics output.
int kheap_validate(void) {
    uint64_t allocated = 0;
    uint64_t largest_free = 0;
    uint64_t corruptions = walk_blocks(&allocated, &largest_free);
    (void)allocated;
    (void)largest_free;
    return (int)corruptions;
}

// Dump the entire block list to the terminal (debugging aid).
void kheap_dump(void) {
    terminal_writestring("[KHEAP] block dump:\n");

    kheap_block_t* block = heap_first;
    uint32_t i = 0;
    while (block && block->magic == KHEAP_MAGIC) {
        terminal_writestring("  #");
        terminal_put_dec(i);
        terminal_writestring(" 0x");
        terminal_put_hex((uint64_t)block);
        terminal_writestring(" size=");
        terminal_put_dec(block->size);
        terminal_writestring(" ");
        terminal_writestring((block->flags & KHEAP_ALLOCATED) ? "ALLOC" : "free");
        terminal_writestring("\n");
        block = block->next;
        i++;
        if (i > 256) break;
    }
    if (!block && i == 0) {
        terminal_writestring("  (heap not initialized)\n");
    }
}

// Legacy compatibility functions
uint64_t kheap_get_used(void) { return bytes_used; }
uint64_t kheap_get_free(void) { return bytes_free; }
