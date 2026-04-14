#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include <stddef.h>

/*
 * Physical Memory Manager
 * Manages free physical pages using a bitmap allocator
 */

#define PAGE_SIZE 4096
#define PAGE_SHIFT 12

/* Physical address type - NOT a dereferenceable pointer */
typedef uint64_t phys_addr_t;

/* Memory region types (from multiboot spec) */
#define MBOOT_MEM_AVAILABLE  1
#define MBOOT_MEM_RESERVED   2
#define MBOOT_MEM_ACPI_RECLAIM 3
#define MBOOT_MEM_NVS        4
#define MBOOT_MEM_BADRAM     5

/* Initialize PMM with multiboot info */
void pmm_init(uintptr_t mbi_addr);

/* Allocate a single physical page */
phys_addr_t pmm_alloc_page(void);

/* Allocate n contiguous physical pages */
phys_addr_t pmm_alloc_pages(size_t count);

/* Free a single physical page */
void pmm_free_page(phys_addr_t addr);

/* Free n contiguous physical pages */
void pmm_free_pages(phys_addr_t addr, size_t count);

/* Reserve a physical range (MMIO, kernel image, ACPI) */
void pmm_reserve_range(phys_addr_t start, size_t length);

/* Mark a range as free (from memory map parsing) */
void pmm_free_range(phys_addr_t start, size_t length);

/* Get total/free memory in bytes */
size_t pmm_get_total(void);
size_t pmm_get_free(void);

/* Get total/free page counts */
size_t pmm_get_total_pages(void);
size_t pmm_get_free_pages(void);

/* Debug: print memory map */
void pmm_print_map(void);

#endif
