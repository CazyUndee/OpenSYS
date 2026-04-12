/*
 * pmm64.c - Physical Memory Manager (64-bit)
 */

#include <stdint.h>
#include "../include/multiboot.h"

#define PAGE_SIZE       4096
#define PAGE_SHIFT      12

/* Bitmap: 1 bit per page */
static uint64_t* pmm_bitmap = 0;
static uint64_t pmm_bitmap_size = 0;
static uint64_t pmm_total_pages = 0;
static uint64_t pmm_free_pages = 0;

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
        struct mboot_mmap_entry* entry = (struct mboot_mmap_entry*)(uint64_t)mbi->mmap_addr;
        struct mboot_mmap_entry* end = (struct mboot_mmap_entry*)((uint64_t)mbi->mmap_addr + mbi->mmap_length);
        
        uint64_t max_addr = 0;
        while ((uint8_t*)entry < (uint8_t*)end) {
            if (entry->type == 1) {  /* Available memory */
                uint64_t base = ((uint64_t)entry->base_high << 32) | entry->base_low;
                uint64_t len = ((uint64_t)entry->length_high << 32) | entry->length_low;
                if (base + len > max_addr) max_addr = base + len;
            }
            entry = MMAP_ENTRY_NEXT(entry);
        }
        
        total_mem = max_addr;
    }
    
    pmm_total_pages = total_mem / PAGE_SIZE;
    pmm_bitmap_size = (pmm_total_pages + 63) / 64;
    
    for (uint64_t i = 0; i < pmm_bitmap_size; i++) {
        pmm_bitmap[i] = 0xFFFFFFFFFFFFFFFFULL;
    }
    pmm_free_pages = 0;
    
    if (mbi->flags & MBOOT_FLAG_MMAP) {
        struct mboot_mmap_entry* entry = (struct mboot_mmap_entry*)(uint64_t)mbi->mmap_addr;
        struct mboot_mmap_entry* end = (struct mboot_mmap_entry*)((uint64_t)mbi->mmap_addr + mbi->mmap_length);
        
        while ((uint8_t*)entry < (uint8_t*)end) {
            if (entry->type == 1) {  /* Available memory */
                uint64_t base = ((uint64_t)entry->base_high << 32) | entry->base_low;
                uint64_t len = ((uint64_t)entry->length_high << 32) | entry->length_low;
                
                uint64_t start_page = (base + PAGE_SIZE - 1) / PAGE_SIZE;
                uint64_t end_page = (base + len) / PAGE_SIZE;
                
                for (uint64_t page = start_page; page < end_page && page < pmm_total_pages; page++) {
                    if (page < 256) continue; /* First 1MB */
                    
                    uint64_t page_addr = page * PAGE_SIZE;
                    uint64_t bitmap_end = bitmap_start + pmm_bitmap_size * 8;
                    if (page_addr >= bitmap_start && page_addr < bitmap_end + PAGE_SIZE) continue;
                    
                    if (bitmap_test(page)) {
                        bitmap_clear(page);
                        pmm_free_pages++;
                    }
                }
            }
            entry = MMAP_ENTRY_NEXT(entry);
        }
    }
}

void* pmm_alloc_page(void) {
    if (pmm_free_pages == 0) return 0;
    
    for (uint64_t i = 0; i < pmm_bitmap_size; i++) {
        if (pmm_bitmap[i] != 0xFFFFFFFFFFFFFFFFULL) {
            for (uint64_t j = 0; j < 64; j++) {
                if (!(pmm_bitmap[i] & (1ULL << j))) {
                    uint64_t page_index = i * 64 + j;
                    if (page_index >= pmm_total_pages) continue;
                    
                    bitmap_set(page_index);
                    pmm_free_pages--;
                    
                    return (void*)(page_index * PAGE_SIZE);
                }
            }
        }
    }
    return 0;
}

void pmm_free_page(void* addr) {
    uint64_t page = (uint64_t)addr / PAGE_SIZE;
    if (page >= pmm_total_pages) return;
    if (!bitmap_test(page)) return;
    
    bitmap_clear(page);
    pmm_free_pages++;
}

uint64_t pmm_get_total(void) { return pmm_total_pages * PAGE_SIZE; }
uint64_t pmm_get_free(void) { return pmm_free_pages * PAGE_SIZE; }
