/*
 * pmm64.c - Physical Memory Manager (64-bit)
 */

#include <stdint.h>
#include "../include/pmm.h"
#include "../include/multiboot.h"

/* Bitmap: 1 bit per page */
static uint64_t* pmm_bitmap = 0;
static uint64_t pmm_bitmap_size = 0;
static uint64_t pmm_total_pages_count = 0;
static uint64_t pmm_free_pages_count = 0;

extern uint64_t kernel_end;

static inline void bitmap_set(uint64_t index) {
    pmm_bitmap[index / 64] |= (1ULL << (index % 64));
}

static inline void bitmap_clear(uint64_t index) {
    pmm_bitmap[index / 64] &= ~(1ULL << (index % 64));
}

static inline int bitmap_test(uint64_t index) {
    return pmm_bitmap[index / 64] & (1ULL << (index % 64));
}

void pmm_init(uint64_t mbi_addr) {
    struct mboot_info* mbi = (struct mboot_info*)mbi_addr;

    /* Bitmap after kernel, aligned */
    uint64_t bitmap_start = (kernel_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    pmm_bitmap = (uint64_t*)bitmap_start;

    uint64_t total_mem = 0;

    if (mbi->flags & MBOOT_FLAG_MEM) {
        total_mem = ((uint64_t)mbi->mem_upper + 1024) * 1024;
    }

    if (mbi->flags & MBOOT_FLAG_MMAP) {
        struct mboot_mmap_entry* entry = (struct mboot_mmap_entry*)(uintptr_t)mbi->mmap_addr;
        struct mboot_mmap_entry* end = (struct mboot_mmap_entry*)((uintptr_t)mbi->mmap_addr + mbi->mmap_length);

        uint64_t max_addr = 0;
        while ((uint8_t*)entry < (uint8_t*)end) {
            if (entry->type == MBOOT_MEM_AVAILABLE) {
                uint64_t base = ((uint64_t)entry->base_high << 32) | entry->base_low;
                uint64_t len = ((uint64_t)entry->length_high << 32) | entry->length_low;
                if (base + len > max_addr) max_addr = base + len;
            }
            entry = MMAP_ENTRY_NEXT(entry);
        }
        total_mem = max_addr;
    }

    pmm_total_pages_count = total_mem / PAGE_SIZE;
    pmm_bitmap_size = (pmm_total_pages_count + 63) / 64;

    /* Mark all pages as used */
    for (uint64_t i = 0; i < pmm_bitmap_size; i++) {
        pmm_bitmap[i] = 0xFFFFFFFFFFFFFFFFULL;
    }
    pmm_free_pages_count = 0;

    /* Free available pages from memory map */
    if (mbi->flags & MBOOT_FLAG_MMAP) {
        struct mboot_mmap_entry* entry = (struct mboot_mmap_entry*)(uintptr_t)mbi->mmap_addr;
        struct mboot_mmap_entry* end = (struct mboot_mmap_entry*)((uintptr_t)mbi->mmap_addr + mbi->mmap_length);

        while ((uint8_t*)entry < (uint8_t*)end) {
            if (entry->type == MBOOT_MEM_AVAILABLE) {
                uint64_t base = ((uint64_t)entry->base_high << 32) | entry->base_low;
                uint64_t len = ((uint64_t)entry->length_high << 32) | entry->length_low;
                pmm_free_range(base, len);
            }
            entry = MMAP_ENTRY_NEXT(entry);
        }
    }
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

                    return page_index * PAGE_SIZE;
                }
            }
        }
    }
    return 0;
}

phys_addr_t pmm_alloc_pages(size_t count) {
    if (count == 0) return 0;
    if (count == 1) return pmm_alloc_page();

    /* Find contiguous free pages */
    for (uint64_t start = 0; start < pmm_total_pages_count - count; start++) {
        int found = 1;
        for (size_t i = 0; i < count; i++) {
            if (bitmap_test(start + i)) {
                found = 0;
                break;
            }
        }
        if (found) {
            for (size_t i = 0; i < count; i++) {
                bitmap_set(start + i);
            }
            pmm_free_pages_count -= count;
            return start * PAGE_SIZE;
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

void pmm_free_pages(phys_addr_t addr, size_t count) {
    for (size_t i = 0; i < count; i++) {
        pmm_free_page(addr + i * PAGE_SIZE);
    }
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

    /* Skip first 1MB (reserved) */
    if (first_page < 256) first_page = 256;

    uint64_t bitmap_start = (uint64_t)pmm_bitmap;
    uint64_t bitmap_end = bitmap_start + pmm_bitmap_size * 8;

    for (uint64_t page = first_page; page < last_page && page < pmm_total_pages_count; page++) {
        phys_addr_t page_addr = page * PAGE_SIZE;

        /* Skip bitmap region */
        if (page_addr >= bitmap_start && page_addr < bitmap_end + PAGE_SIZE) continue;

        if (bitmap_test(page)) {
            bitmap_clear(page);
            pmm_free_pages_count++;
        }
    }
}

size_t pmm_get_total(void) { return pmm_total_pages_count * PAGE_SIZE; }
size_t pmm_get_free(void) { return pmm_free_pages_count * PAGE_SIZE; }
size_t pmm_get_total_pages(void) { return pmm_total_pages_count; }
size_t pmm_get_free_pages(void) { return pmm_free_pages_count; }
