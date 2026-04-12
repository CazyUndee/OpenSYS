/*
 * pmm.c - Physical Memory Manager
 * 
 * Uses a bitmap to track free/allocated physical pages.
 * Bitmap is placed right after the kernel in memory.
 */

#include <stdint.h>
#include <stddef.h>
#include "../include/pmm.h"
#include "../include/multiboot.h"

/* Bitmap: 1 bit per page, 1 = used, 0 = free */
static uint32_t* pmm_bitmap = 0;
static size_t    pmm_bitmap_size = 0;   /* Number of uint32_t entries */
static size_t    pmm_total_pages = 0;
static size_t    pmm_free_pages = 0;

/* Kernel end symbol from linker */
extern uint32_t kernel_end;

/* Helper: set/clear/test bit in bitmap */
static inline void bitmap_set(size_t index) {
    pmm_bitmap[index / 32] |= (1 << (index % 32));
}

static inline void bitmap_clear(size_t index) {
    pmm_bitmap[index / 32] &= ~(1 << (index % 32));
}

static inline int bitmap_test(size_t index) {
    return pmm_bitmap[index / 32] & (1 << (index % 32));
}

/* Helper: print hex number */
static void print_hex(uint32_t n) {
    const char hex[] = "0123456789ABCDEF";
    char buf[9];
    buf[8] = 0;
    for (int i = 7; i >= 0; i--) {
        buf[i] = hex[n & 0xF];
        n >>= 4;
    }
    /* This is called from kernel, we'll use a simple output */
}

/*
 * Initialize physical memory manager
 * 
 * 1. Parse multiboot memory map
 * 2. Place bitmap after kernel
 * 3. Mark available regions as free
 */
void pmm_init(uint32_t mbi_addr) {
    struct mboot_info* mbi = (struct mboot_info*)mbi_addr;
    
    /* Calculate bitmap location (after kernel, aligned to page) */
    uint32_t bitmap_start = ((uint32_t)&kernel_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    pmm_bitmap = (uint32_t*)bitmap_start;
    
    /* Get total memory (simple estimate from mem_upper if no mmap) */
    uint32_t total_mem = 0;
    
    if (mbi->flags & MBOOT_FLAG_MEM) {
        /* mem_upper is KB above 1MB, add 1MB for total */
        total_mem = (mbi->mem_upper + 1024) * 1024;
    }
    
    /* Parse memory map if available */
    if (mbi->flags & MBOOT_FLAG_MMAP) {
        struct mboot_mmap_entry* entry = (struct mboot_mmap_entry*)mbi->mmap_addr;
        struct mboot_mmap_entry* end = (struct mboot_mmap_entry*)(mbi->mmap_addr + mbi->mmap_length);
        
        /* First pass: find highest address to determine bitmap size */
        uint64_t max_addr = 0;
        while ((uint8_t*)entry < (uint8_t*)end) {
            if (entry->type == MBOOT_MEM_AVAILABLE) {
                uint64_t region_end = ((uint64_t)entry->base_high << 32) | entry->base_low;
                region_end += ((uint64_t)entry->length_high << 32) | entry->length_low;
                if (region_end > max_addr) max_addr = region_end;
            }
            entry = MMAP_ENTRY_NEXT(entry);
        }
        
        /* Limit to 4GB for 32-bit system */
        if (max_addr > 0xFFFFFFFF) max_addr = 0xFFFFFFFF;
        
        total_mem = (uint32_t)max_addr;
    }
    
    /* Calculate bitmap size: 1 bit per page */
    pmm_total_pages = total_mem / PAGE_SIZE;
    pmm_bitmap_size = (pmm_total_pages + 31) / 32;
    
    /* Initialize bitmap: mark all pages as used */
    for (size_t i = 0; i < pmm_bitmap_size; i++) {
        pmm_bitmap[i] = 0xFFFFFFFF;
    }
    pmm_free_pages = 0;
    
    /* Second pass: mark available regions as free */
    if (mbi->flags & MBOOT_FLAG_MMAP) {
        struct mboot_mmap_entry* entry = (struct mboot_mmap_entry*)mbi->mmap_addr;
        struct mboot_mmap_entry* end = (struct mboot_mmap_entry*)(mbi->mmap_addr + mbi->mmap_length);
        
        while ((uint8_t*)entry < (uint8_t*)end) {
            if (entry->type == MBOOT_MEM_AVAILABLE) {
                uint64_t base = ((uint64_t)entry->base_high << 32) | entry->base_low;
                uint64_t len = ((uint64_t)entry->length_high << 32) | entry->length_low;
                
                /* Align to page boundaries */
                uint64_t start_page = (base + PAGE_SIZE - 1) / PAGE_SIZE;
                uint64_t end_page = (base + len) / PAGE_SIZE;
                
                /* Mark pages as free */
                for (uint64_t page = start_page; page < end_page && page < pmm_total_pages; page++) {
                    /* Don't free pages below 1MB (BIOS, kernel, etc) */
                    if (page < 256) continue;  /* First 1MB */
                    
                    /* Don't free pages used by bitmap */
                    uint32_t page_addr = page * PAGE_SIZE;
                    uint32_t bitmap_end = bitmap_start + pmm_bitmap_size * 4;
                    if (page_addr >= bitmap_start && page_addr < bitmap_end + PAGE_SIZE) continue;
                    
                    /* Don't free kernel pages */
                    if (page_addr >= 0x100000 && page_addr < (uint32_t)&kernel_end) continue;
                    
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

/* Allocate a physical page */
void* pmm_alloc_page(void) {
    if (pmm_free_pages == 0) return 0;
    
    /* Find first free page */
    for (size_t i = 0; i < pmm_bitmap_size; i++) {
        if (pmm_bitmap[i] != 0xFFFFFFFF) {
            /* Find first zero bit */
            for (size_t j = 0; j < 32; j++) {
                if (!(pmm_bitmap[i] & (1 << j))) {
                    size_t page_index = i * 32 + j;
                    if (page_index >= pmm_total_pages) continue;
                    
                    bitmap_set(page_index);
                    pmm_free_pages--;
                    
                    return (void*)(page_index * PAGE_SIZE);
                }
            }
        }
    }
    
    return 0;  /* Out of memory */
}

/* Free a physical page */
void pmm_free_page(void* addr) {
    uint32_t page = (uint32_t)addr / PAGE_SIZE;
    
    if (page >= pmm_total_pages) return;
    if (!bitmap_test(page)) return;  /* Already free */
    
    bitmap_clear(page);
    pmm_free_pages++;
}

/* Get statistics */
size_t pmm_get_total(void) {
    return pmm_total_pages * PAGE_SIZE;
}

size_t pmm_get_free(void) {
    return pmm_free_pages * PAGE_SIZE;
}
