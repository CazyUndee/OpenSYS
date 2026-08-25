/*
 * memory.c - Physical Memory Manager
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

#include "memory.h"
#include "pmm.h"
#include "multiboot.h"
#include "vga.h"
#include <stddef.h>
#include <stdint.h>

/* Bitmap: 1 bit per page */
static uint64_t* pmm_bitmap = 0;
static uint64_t pmm_bitmap_size = 0;
static uint64_t pmm_total_pages_count = 0;
static uint64_t pmm_free_pages_count = 0;

static uint32_t* pmm_ref_counts = 0;
static uint64_t pmm_ref_start = 0;
static uint64_t pmm_ref_end = 0;

/* Linker symbol: the address after kernel BSS end */
extern uint64_t kernel_end[];

/* Forward declarations for helpers defined after pmm_init */
void pmm_reserve_range(phys_addr_t start, size_t length);
void pmm_free_range(phys_addr_t start, size_t length);

static inline void bitmap_set(uint64_t index) {
    pmm_bitmap[index / 64] |= (1ULL << (index % 64));
}

static inline void bitmap_clear(uint64_t index) {
    pmm_bitmap[index / 64] &= ~(1ULL << (index % 64));
}

static inline int bitmap_test(uint64_t index) {
    return pmm_bitmap[index / 64] & (1ULL << (index % 64));
}

// Initialize memory system
void pmm_init(uint64_t mbi) {
    terminal_writestring("[MEMORY] Initializing Memory System...\n");
    
    struct mboot_info* mbi_struct = (struct mboot_info*)mbi;

    /* Bitmap after kernel, aligned */
    /* Debug: write nibble of mbi high byte */
    uint8_t mbi_nibble = (uint8_t)(mbi >> 4) & 0x0F;
    uint8_t mbi_char = mbi_nibble < 10 ? ('0' + mbi_nibble) : ('A' + mbi_nibble - 10);
    __asm__ volatile ("mov $0x3F8, %%dx; out %%al, %%dx" : : "a"(mbi_char) : "dx");

    uint64_t bitmap_start = ((uint64_t)kernel_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    pmm_bitmap = (uint64_t*)bitmap_start;
    __asm__ volatile ("mov $'b', %%al; mov $0x3F8, %%dx; out %%al, %%dx" ::: "al", "dx");

    /* Cap memory to safe maximum to prevent hangs */
    uint64_t total_mem = 0;
    __asm__ volatile ("mov $'f', %%al; mov $0x3F8, %%dx; out %%al, %%dx" ::: "al", "dx");

    if (mbi_struct->flags & MBOOT_FLAG_MEM) {
        total_mem = ((uint64_t)mbi_struct->mem_upper + 1024) * 1024;
    }
    __asm__ volatile ("mov $'g', %%al; mov $0x3F8, %%dx; out %%al, %%dx" ::: "al", "dx");

    if (mbi_struct->flags & MBOOT_FLAG_MMAP) {
        struct mboot_mmap_entry* entry = (struct mboot_mmap_entry*)(uint64_t)mbi_struct->mmap_addr;
        struct mboot_mmap_entry* end = (struct mboot_mmap_entry*)((uint64_t)mbi_struct->mmap_addr + mbi_struct->mmap_length);

        uint64_t max_addr = 0;
        uint64_t entry_count = 0;
        while ((uint8_t*)entry < (uint8_t*)end && entry_count < 64) {
            if (entry->type == MBOOT_MEM_AVAILABLE) {
                uint64_t base = ((uint64_t)entry->base_high << 32) | entry->base_low;
                uint64_t len = ((uint64_t)entry->length_high << 32) | entry->length_low;
                if (base + len > max_addr && base + len < 0x100000000ULL) max_addr = base + len;
            }
            entry = MMAP_ENTRY_NEXT(entry);
            entry_count++;
        }
        total_mem = max_addr;
    }
    __asm__ volatile ("mov $'h', %%al; mov $0x3F8, %%dx; out %%al, %%dx" ::: "al", "dx");

    if (total_mem == 0 || total_mem > 0x20000000) total_mem = 128 * 1024 * 1024; /* Default 128MB */
    pmm_total_pages_count = total_mem / PAGE_SIZE;
    pmm_bitmap_size = (pmm_total_pages_count + 63) / 64;
    __asm__ volatile ("mov $'i', %%al; mov $0x3F8, %%dx; out %%al, %%dx" ::: "al", "dx");

    /* Ref count array sits right after bitmap */
    uint64_t bitmap_bytes = pmm_bitmap_size * 8;
    uint64_t ref_start = bitmap_start + ((bitmap_bytes + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1));
    pmm_ref_counts = (uint32_t*)ref_start;
    pmm_ref_start = ref_start;
    pmm_ref_end = ref_start + ((pmm_total_pages_count * 4 + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1));
    __asm__ volatile ("mov $'j', %%al; mov $0x3F8, %%dx; out %%al, %%dx" ::: "al", "dx");

    /* Mark all pages as used */
    uint64_t bitmap_limit = pmm_bitmap_size;
    if (bitmap_limit > 1048576) bitmap_limit = 1048576; /* Safety cap: max 8MB bitmap */

    /* Debug: dump key values to serial */
    __asm__ volatile ("mov $'0', %%al; mov $0x3F8, %%dx; out %%al, %%dx" ::: "al", "dx");
    /* Print pmm_total_pages_count in hex (upper nibbles then lower nibbles) */
    uint64_t dbg_val = pmm_total_pages_count;
    for (int dbg_i = 56; dbg_i >= 0; dbg_i -= 8) {
        uint8_t dbg_byte = (dbg_val >> dbg_i) & 0xFF;
        uint8_t dbg_hi = (dbg_byte >> 4) & 0x0F;
        uint8_t dbg_lo = dbg_byte & 0x0F;
        uint8_t dbg_ch1 = dbg_hi < 10 ? ('0' + dbg_hi) : ('A' + dbg_hi - 10);
        uint8_t dbg_ch2 = dbg_lo < 10 ? ('0' + dbg_lo) : ('A' + dbg_lo - 10);
        __asm__ volatile ("mov $0x3F8, %%dx; out %%al, %%dx" : : "a"(dbg_ch1) : "dx");
        __asm__ volatile ("mov $0x3F8, %%dx; out %%al, %%dx" : : "a"(dbg_ch2) : "dx");
    }
    __asm__ volatile ("mov $'|', %%al; mov $0x3F8, %%dx; out %%al, %%dx" ::: "al", "dx");

    /* Print pmm_bitmap_size */
    dbg_val = pmm_bitmap_size;
    for (int dbg_i = 56; dbg_i >= 0; dbg_i -= 8) {
        uint8_t dbg_byte = (dbg_val >> dbg_i) & 0xFF;
        uint8_t dbg_hi = (dbg_byte >> 4) & 0x0F;
        uint8_t dbg_lo = dbg_byte & 0x0F;
        uint8_t dbg_ch1 = dbg_hi < 10 ? ('0' + dbg_hi) : ('A' + dbg_hi - 10);
        uint8_t dbg_ch2 = dbg_lo < 10 ? ('0' + dbg_lo) : ('A' + dbg_lo - 10);
        __asm__ volatile ("mov $0x3F8, %%dx; out %%al, %%dx" : : "a"(dbg_ch1) : "dx");
        __asm__ volatile ("mov $0x3F8, %%dx; out %%al, %%dx" : : "a"(dbg_ch2) : "dx");
    }
    __asm__ volatile ("mov $'|', %%al; mov $0x3F8, %%dx; out %%al, %%dx" ::: "al", "dx");

    /* Print bitmap_start */
    dbg_val = bitmap_start;
    for (int dbg_i = 56; dbg_i >= 0; dbg_i -= 8) {
        uint8_t dbg_byte = (dbg_val >> dbg_i) & 0xFF;
        uint8_t dbg_hi = (dbg_byte >> 4) & 0x0F;
        uint8_t dbg_lo = dbg_byte & 0x0F;
        uint8_t dbg_ch1 = dbg_hi < 10 ? ('0' + dbg_hi) : ('A' + dbg_hi - 10);
        uint8_t dbg_ch2 = dbg_lo < 10 ? ('0' + dbg_lo) : ('A' + dbg_lo - 10);
        __asm__ volatile ("mov $0x3F8, %%dx; out %%al, %%dx" : : "a"(dbg_ch1) : "dx");
        __asm__ volatile ("mov $0x3F8, %%dx; out %%al, %%dx" : : "a"(dbg_ch2) : "dx");
    }
    __asm__ volatile ("mov $'|', %%al; mov $0x3F8, %%dx; out %%al, %%dx" ::: "al", "dx");

    /* Print bitmap_limit */
    dbg_val = bitmap_limit;
    for (int dbg_i = 56; dbg_i >= 0; dbg_i -= 8) {
        uint8_t dbg_byte = (dbg_val >> dbg_i) & 0xFF;
        uint8_t dbg_hi = (dbg_byte >> 4) & 0x0F;
        uint8_t dbg_lo = dbg_byte & 0x0F;
        uint8_t dbg_ch1 = dbg_hi < 10 ? ('0' + dbg_hi) : ('A' + dbg_hi - 10);
        uint8_t dbg_ch2 = dbg_lo < 10 ? ('0' + dbg_lo) : ('A' + dbg_lo - 10);
        __asm__ volatile ("mov $0x3F8, %%dx; out %%al, %%dx" : : "a"(dbg_ch1) : "dx");
        __asm__ volatile ("mov $0x3F8, %%dx; out %%al, %%dx" : : "a"(dbg_ch2) : "dx");
    }
    __asm__ volatile ("mov $'1', %%al; mov $0x3F8, %%dx; out %%al, %%dx" ::: "al", "dx");

    for (uint64_t i = 0; i < bitmap_limit; i++) {
        pmm_bitmap[i] = 0xFFFFFFFFFFFFFFFFULL;
    }
    pmm_free_pages_count = 0;
    __asm__ volatile ("mov $'k', %%al; mov $0x3F8, %%dx; out %%al, %%dx" ::: "al", "dx");

    /* Zero ref counts */
    for (uint64_t i = 0; i < pmm_total_pages_count; i++) {
        pmm_ref_counts[i] = 0;
    }
    __asm__ volatile ("mov $'l', %%al; mov $0x3F8, %%dx; out %%al, %%dx" ::: "al", "dx");

    /* Free available pages from memory map */
    if (mbi_struct->flags & MBOOT_FLAG_MMAP) {
        struct mboot_mmap_entry* entry = (struct mboot_mmap_entry*)(uint64_t)mbi_struct->mmap_addr;
        struct mboot_mmap_entry* end = (struct mboot_mmap_entry*)((uint64_t)mbi_struct->mmap_addr + mbi_struct->mmap_length);

        while ((uint8_t*)entry < (uint8_t*)end) {
            if (entry->type == MBOOT_MEM_AVAILABLE) {
                uint64_t base = ((uint64_t)entry->base_high << 32) | entry->base_low;
                uint64_t len = ((uint64_t)entry->length_high << 32) | entry->length_low;
                pmm_free_range(base, len);
            }
            entry = MMAP_ENTRY_NEXT(entry);
        }
    }
    __asm__ volatile ("mov $'m', %%al; mov $0x3F8, %%dx; out %%al, %%dx" ::: "al", "dx");
    
    terminal_writestring("  Total RAM: ");
    terminal_put_dec(pmm_total_pages_count * PAGE_SIZE / (1024 * 1024));
    terminal_writestring(" MB\n");
    terminal_writestring("  Free RAM: ");
    terminal_put_dec(pmm_free_pages_count * PAGE_SIZE / (1024 * 1024));
    terminal_writestring(" MB\n");
    
    terminal_writestring("[MEMORY] Memory System Ready!\n");
}

phys_addr_t pmm_alloc_page(void) {
    if (pmm_free_pages_count == 0) return 0;

    for (uint64_t i = 0; i < pmm_bitmap_size; i++) {
        if (pmm_bitmap[i] != 0xFFFFFFFFFFFFFFFFULL) {
            for (uint64_t j = 0; j < 64; j++) {
                if (!(pmm_bitmap[i] & (1ULL << j))) {
                    uint64_t page_index = i * 64 + j;
                    if (page_index >= pmm_total_pages_count) continue;

                    bitmap_set(page_index);
                    pmm_free_pages_count--;

                    return (phys_addr_t)(page_index * PAGE_SIZE);
                }
            }
        }
    }
    return 0;
}

void pmm_free_page(phys_addr_t addr) {
    uint64_t page = addr / PAGE_SIZE;
    if (page >= pmm_total_pages_count) return;
    if (!bitmap_test(page)) return;

    bitmap_clear(page);
    pmm_free_pages_count++;
}

void pmm_reserve_range(phys_addr_t start, size_t length) {
    uint64_t first_page = start / PAGE_SIZE;
    uint64_t last_page = (start + length + PAGE_SIZE - 1) / PAGE_SIZE;

    for (uint64_t page = first_page; page < last_page && page < pmm_total_pages_count; page++) {
        if (!bitmap_test(page)) {
            bitmap_set(page);
            pmm_free_pages_count--;
        }
    }
}

void pmm_free_range(phys_addr_t start, size_t length) {
    uint64_t first_page = (start + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t last_page = (start + length) / PAGE_SIZE;

    uint64_t bitmap_start = (uint64_t)pmm_bitmap;
    uint64_t bitmap_end = bitmap_start + pmm_bitmap_size * 8;

    /* Skip everything below the bitmap: the low 1MB (BIOS/IVT) plus the
     * kernel's own code/data/BSS. The bitmap starts right after kernel_end,
     * so this guarantees pmm_alloc_page() never returns a page overlapping
     * the running kernel. */
    uint64_t kernel_end_page = bitmap_start / PAGE_SIZE;
    if (first_page < kernel_end_page) first_page = kernel_end_page;

    for (uint64_t page = first_page; page < last_page && page < pmm_total_pages_count; page++) {
        uint64_t page_addr = page * PAGE_SIZE;

        /* Skip bitmap region */
        if (page_addr >= bitmap_start && page_addr < bitmap_end + PAGE_SIZE) continue;

        /* Skip ref-count array region */
        if (page_addr >= pmm_ref_start && page_addr < pmm_ref_end) continue;

        if (bitmap_test(page)) {
            bitmap_clear(page);
            pmm_free_pages_count++;
        }
    }
}

// Get memory statistics
void memory_get_stats(memory_stats_t* stats) {
    if (!stats) return;
    
    stats->total_mb = pmm_total_pages_count * PAGE_SIZE / (1024 * 1024);
    stats->free_mb = pmm_free_pages_count * PAGE_SIZE / (1024 * 1024);
}



size_t pmm_get_total(void) { return pmm_total_pages_count * PAGE_SIZE; }
size_t pmm_get_free(void) { return pmm_free_pages_count * PAGE_SIZE; }
size_t pmm_get_total_pages(void) { return pmm_total_pages_count; }
size_t pmm_get_free_pages(void) { return pmm_free_pages_count; }



// Reference counting functions for COW
int pmm_ref_count(phys_addr_t addr) {
    uint64_t page = addr / PAGE_SIZE;
    if (page >= pmm_total_pages_count) return 0;
    return pmm_ref_counts[page];
}

void pmm_ref_inc(phys_addr_t addr) {
    uint64_t page = addr / PAGE_SIZE;
    if (page < pmm_total_pages_count) {
        pmm_ref_counts[page]++;
    }
}

int pmm_ref_dec(phys_addr_t addr) {
    uint64_t page = addr / PAGE_SIZE;
    if (page >= pmm_total_pages_count) return -1;
    if (pmm_ref_counts[page] > 0) {
        return --pmm_ref_counts[page];
    }
    return 0;
}

void pmm_ref_init_range(phys_addr_t start, size_t length) {
    uint64_t page_start = start / PAGE_SIZE;
    uint64_t page_end = (start + length + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint64_t page = page_start; page < page_end && page < pmm_total_pages_count; page++) {
        pmm_ref_counts[page] = 1;
    }
}
