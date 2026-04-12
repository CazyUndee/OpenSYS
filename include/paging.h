#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>
#include <stddef.h>

/*
 * Paging - Virtual Memory Management
 * 
 * x86 32-bit paging uses a two-level hierarchy:
 *   Page Directory (PD) - 1024 entries, each points to a Page Table
 *   Page Table (PT)    - 1024 entries, each points to a 4KB page
 * 
 * Each process has its own Page Directory.
 * Virtual address format: [31:22] PD index, [21:12] PT index, [11:0] offset
 */

/* Page size constants */
#define PAGE_SIZE           4096
#define PAGE_MASK           0xFFFFF000
#define PAGE_OFFSET_MASK    0x00000FFF

/* Entries per table (1024 for 32-bit) */
#define PD_ENTRIES          1024
#define PT_ENTRIES          1024

/* Page directory/table entry flags */
#define PTE_PRESENT         0x001   /* Page is present in memory */
#define PTE_WRITABLE        0x002   /* Page is writable */
#define PTE_USER            0x004   /* User-mode accessible */
#define PTE_PWT             0x008   /* Write-through caching */
#define PTE_PCD             0x010   /* Cache disabled */
#define PTE_ACCESSED        0x020   /* Page has been accessed */
#define PTE_DIRTY           0x040   /* Page has been written to */
#define PTE_PS              0x080   /* Page size (4MB if set) */
#define PTE_GLOBAL          0x100   /* Global page (not flushed on CR3 reload) */

/* Macros to extract indices from virtual address */
#define PD_INDEX(vaddr)     (((uint32_t)(vaddr) >> 22) & 0x3FF)
#define PT_INDEX(vaddr)     (((uint32_t)(vaddr) >> 12) & 0x3FF)

/* Page table entry type */
typedef uint32_t pte_t;

/* Initialize paging */
void paging_init(void);

/* Get current page directory physical address */
uint32_t paging_get_cr3(void);

/* Map a virtual page to a physical page */
void paging_map(uint32_t vaddr, uint32_t paddr, uint32_t flags);

/* Unmap a virtual page */
void paging_unmap(uint32_t vaddr);

/* Get physical address for a virtual address */
uint32_t paging_get_physical(uint32_t vaddr);

/* Allocate and map a page at virtual address */
void* paging_alloc(uint32_t vaddr, uint32_t flags);

/* Identity map a range */
void paging_identity_map(uint32_t start, uint32_t end, uint32_t flags);

#endif
