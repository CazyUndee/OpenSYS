/*
 * paging64.c - 64-bit Paging (4-Level Hierarchy)
 *
 * x86_64 uses 4 levels: PML4 -> PDPT -> PD -> PT -> Page
 * Each table has 512 entries (9 bits per level).
 * Virtual address: [16 unused][9 PML4][9 PDPT][9 PD][9 PT][12 offset]
 */

#include <stdint.h>
#include "../include/pmm.h"

#define PAGE_SIZE       4096
#define PAGE_PRESENT    0x01
#define PAGE_WRITABLE   0x02
#define PAGE_USER       0x04
#define PAGE_HUGE       0x80

#define ENTRIES_PER_TABLE 512
#define ENTRY_SHIFT       9

#define PML4_INDEX(vaddr) (((vaddr) >> 39) & 0x1FF)
#define PDPT_INDEX(vaddr) (((vaddr) >> 30) & 0x1FF)
#define PD_INDEX(vaddr)   (((vaddr) >> 21) & 0x1FF)
#define PT_INDEX(vaddr)   (((vaddr) >> 12) & 0x1FF)

typedef uint64_t pte_t;

static pte_t* pml4 = 0;

static inline void invlpg(uint64_t vaddr) {
    __asm__ volatile("invlpg (%0)" : : "r"(vaddr) : "memory");
}

static inline void load_cr3(uint64_t addr) {
    __asm__ volatile("mov %0, %%cr3" : : "r"(addr));
}

static pte_t* get_or_create_table(pte_t* parent, uint64_t index, uint64_t flags) {
    if (parent[index] & PAGE_PRESENT) {
        return (pte_t*)(parent[index] & 0x000FFFFFFFFFF000ULL);
    }
    
    void* page = pmm_alloc_page();
    if (!page) return 0;
    
    pte_t* table = (pte_t*)page;
    for (int i = 0; i < ENTRIES_PER_TABLE; i++) {
        table[i] = 0;
    }
    
    parent[index] = ((uint64_t)table) | flags | PAGE_PRESENT | PAGE_WRITABLE;
    return table;
}

void paging_map(uint64_t vaddr, uint64_t paddr, uint64_t flags) {
    vaddr &= 0xFFFFFFFFFFFFF000ULL;
    paddr &= 0xFFFFFFFFFFFFF000ULL;
    
    pte_t* pdpt = get_or_create_table(pml4, PML4_INDEX(vaddr), flags);
    if (!pdpt) return;
    
    pte_t* pd = get_or_create_table(pdpt, PDPT_INDEX(vaddr), flags);
    if (!pd) return;
    
    pte_t* pt = get_or_create_table(pd, PD_INDEX(vaddr), flags);
    if (!pt) return;
    
    pt[PT_INDEX(vaddr)] = paddr | flags | PAGE_PRESENT;
    invlpg(vaddr);
}

void paging_map_huge(uint64_t vaddr, uint64_t paddr, uint64_t flags) {
    vaddr &= 0xFFFFFFFFFFFFF000ULL;
    paddr &= 0xFFFFFFFFFFFFF000ULL;
    
    pte_t* pdpt = get_or_create_table(pml4, PML4_INDEX(vaddr), flags);
    if (!pdpt) return;
    
    pte_t* pd = get_or_create_table(pdpt, PDPT_INDEX(vaddr), flags);
    if (!pd) return;
    
    pd[PD_INDEX(vaddr)] = paddr | flags | PAGE_PRESENT | PAGE_HUGE;
    invlpg(vaddr);
}

void paging_init(void) {
    pml4 = (pte_t*)pmm_alloc_page();
    if (!pml4) return;
    
    for (int i = 0; i < ENTRIES_PER_TABLE; i++) {
        pml4[i] = 0;
    }
    
    /* Identity map first 4GB using 2MB pages */
    for (uint64_t addr = 0; addr < 0x100000000ULL; addr += 0x200000) {
        paging_map_huge(addr, addr, PAGE_WRITABLE);
    }
    
    /* Higher-half kernel mapping at -2GB (0xFFFFFFFF80000000) */
    uint64_t kernel_base = 0xFFFFFFFF80000000ULL;
    for (uint64_t offset = 0; offset < 0x200000; offset += 0x200000) {
        paging_map_huge(kernel_base + offset, offset, PAGE_WRITABLE);
    }
    
    load_cr3((uint64_t)pml4);
}

uint64_t paging_get_physical(uint64_t vaddr) {
    uint64_t pdpt_idx = PDPT_INDEX(vaddr);
    uint64_t pd_idx = PD_INDEX(vaddr);
    uint64_t pt_idx = PT_INDEX(vaddr);
    uint64_t offset = vaddr & 0xFFF;
    
    if (!(pml4[PML4_INDEX(vaddr)] & PAGE_PRESENT)) return 0;
    pte_t* pdpt = (pte_t*)(pml4[PML4_INDEX(vaddr)] & 0x000FFFFFFFFFF000ULL);
    
    if (!(pdpt[pdpt_idx] & PAGE_PRESENT)) return 0;
    pte_t* pd = (pte_t*)(pdpt[pdpt_idx] & 0x000FFFFFFFFFF000ULL);
    
    if (pd[pd_idx] & PAGE_HUGE) {
        return (pd[pd_idx] & 0xFFFFFFFFFFE00000ULL) + (vaddr & 0x1FFFFF);
    }
    
    if (!(pd[pd_idx] & PAGE_PRESENT)) return 0;
    pte_t* pt = (pte_t*)(pd[pd_idx] & 0x000FFFFFFFFFF000ULL);
    
    if (!(pt[pt_idx] & PAGE_PRESENT)) return 0;
    return (pt[pt_idx] & 0x000FFFFFFFFFF000ULL) + offset;
}

void* paging_alloc(uint64_t vaddr, uint64_t flags) {
    void* phys = pmm_alloc_page();
    if (!phys) return 0;
    
    paging_map(vaddr, (uint64_t)phys, flags);
    return (void*)vaddr;
}
