#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include <stddef.h>

/* 
 * Physical Memory Manager
 * Manages free physical pages using a bitmap allocator
 */

#define PAGE_SIZE       4096
#define PAGE_SHIFT      12

/* Memory region types (from multiboot spec) */
#define MBOOT_MEM_AVAILABLE     1
#define MBOOT_MEM_RESERVED      2
#define MBOOT_MEM_ACPI_RECLAIM  3
#define MBOOT_MEM_NVS           4
#define MBOOT_MEM_BADRAM        5

/* Initialize PMM with multiboot info */
void pmm_init(uint32_t mbi_addr);

/* Allocate a single physical page (returns physical address) */
void* pmm_alloc_page(void);

/* Free a physical page */
void pmm_free_page(void* addr);

/* Get total and free memory in bytes */
size_t pmm_get_total(void);
size_t pmm_get_free(void);

/* Debug: print memory map */
void pmm_print_map(void);

#endif
