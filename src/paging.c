/*
 * paging.c - Virtual Memory Management
 *
 * Implements x86 32-bit paging with a two-level page table hierarchy.
 */

#include <stdint.h>
#include <stddef.h>
#include "../include/paging.h"
#include "../include/pmm.h"
#include "../include/io.h"

/* Current page directory */
static pte_t* current_pd = 0;

/* Page directory for kernel (identity mapped) */
static pte_t* kernel_pd = 0;

/* Array of page tables (we need to keep them mapped) */
static pte_t* page_tables[PD_ENTRIES];

/*
 * Invalidate TLB entry for a virtual address
 */
static inline void invlpg(uint32_t vaddr) {
    __asm__ volatile("invlpg (%0)" : : "r"(vaddr) : "memory");
}

/*
 * Load CR3 (page directory base register)
 */
static inline void load_cr3(uint32_t addr) {
    __asm__ volatile("mov %0, %%cr3" : : "r"(addr));
}

/*
 * Get current CR3 value
 */
uint32_t paging_get_cr3(void) {
    uint32_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

/*
 * Enable paging by setting CR0.PG bit
 */
static void enable_paging(void) {
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;  /* Set PG bit */
    __asm__ volatile("mov %0, %%cr3" : : "r"((uint32_t)kernel_pd));
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));
}

/*
 * Get or create a page table for a page directory index
 */
static pte_t* get_or_create_pt(uint32_t pd_index, uint32_t flags) {
    if (pd_index >= PD_ENTRIES) return 0;
    
    /* Check if page table already exists */
    if (kernel_pd[pd_index] & PTE_PRESENT) {
        return page_tables[pd_index];
    }
    
    /* Allocate a new page table */
    void* pt_phys = pmm_alloc_page();
    if (!pt_phys) return 0;
    
    /* Map the page table into our tracking array */
    /* We use a fixed virtual address range for page tables */
    uint32_t pt_vaddr = 0xFFC00000 + pd_index * PAGE_SIZE;
    
    /* Map the page table page so we can write to it */
    pte_t* pt = (pte_t*)pt_vaddr;
    
    /* Store in tracking array */
    page_tables[pd_index] = pt;
    
    /* Clear the page table */
    for (int i = 0; i < PT_ENTRIES; i++) {
        pt[i] = 0;
    }
    
    /* Set page directory entry */
    kernel_pd[pd_index] = (uint32_t)pt_phys | flags | PTE_PRESENT | PTE_WRITABLE;
    
    return pt;
}

/*
 * Map a virtual page to a physical page
 */
void paging_map(uint32_t vaddr, uint32_t paddr, uint32_t flags) {
    /* Align addresses to page boundaries */
    vaddr &= PAGE_MASK;
    paddr &= PAGE_MASK;
    
    uint32_t pd_index = PD_INDEX(vaddr);
    uint32_t pt_index = PT_INDEX(vaddr);
    
    /* Get or create the page table */
    pte_t* pt = get_or_create_pt(pd_index, flags);
    if (!pt) return;
    
    /* Set the page table entry */
    pt[pt_index] = paddr | flags | PTE_PRESENT;
    
    /* Invalidate TLB entry */
    invlpg(vaddr);
}

/*
 * Unmap a virtual page
 */
void paging_unmap(uint32_t vaddr) {
    vaddr &= PAGE_MASK;
    
    uint32_t pd_index = PD_INDEX(vaddr);
    uint32_t pt_index = PT_INDEX(vaddr);
    
    if (pd_index >= PD_ENTRIES) return;
    if (!(kernel_pd[pd_index] & PTE_PRESENT)) return;
    
    pte_t* pt = page_tables[pd_index];
    if (!pt) return;
    
    pt[pt_index] = 0;
    invlpg(vaddr);
}

/*
 * Get physical address for a virtual address
 */
uint32_t paging_get_physical(uint32_t vaddr) {
    uint32_t pd_index = PD_INDEX(vaddr);
    uint32_t pt_index = PT_INDEX(vaddr);
    uint32_t offset = vaddr & PAGE_OFFSET_MASK;
    
    if (pd_index >= PD_ENTRIES) return 0;
    if (!(kernel_pd[pd_index] & PTE_PRESENT)) return 0;
    
    pte_t* pt = page_tables[pd_index];
    if (!pt) return 0;
    
    pte_t pte = pt[pt_index];
    if (!(pte & PTE_PRESENT)) return 0;
    
    return (pte & PAGE_MASK) + offset;
}

/*
 * Identity map a range of physical memory
 */
void paging_identity_map(uint32_t start, uint32_t end, uint32_t flags) {
    start &= PAGE_MASK;
    end = (end + PAGE_SIZE - 1) & PAGE_MASK;
    
    for (uint32_t addr = start; addr < end; addr += PAGE_SIZE) {
        paging_map(addr, addr, flags);
    }
}

/*
 * Initialize paging
 */
void paging_init(void) {
    /* Allocate the page directory */
    kernel_pd = (pte_t*)pmm_alloc_page();
    if (!kernel_pd) return;
    
    /* Clear page directory */
    for (int i = 0; i < PD_ENTRIES; i++) {
        kernel_pd[i] = 0;
    }
    
    /* Clear page table tracking */
    for (int i = 0; i < PD_ENTRIES; i++) {
        page_tables[i] = 0;
    }
    
    /* Identity map the first 4MB (kernel + low memory) */
    /* This ensures the kernel continues to work after paging is enabled */
    paging_identity_map(0x00000000, 0x00400000, PTE_WRITABLE);
    
    /* Identity map VGA buffer region */
    paging_identity_map(0x000B8000, 0x000C0000, PTE_WRITABLE);
    
    /* Create self-mapping: last entry of PD points to PD itself */
    /* This allows accessing page tables at virtual address 0xFFC00000 */
    uint32_t pd_phys = paging_get_physical((uint32_t)kernel_pd);
    kernel_pd[PD_ENTRIES - 1] = pd_phys | PTE_PRESENT | PTE_WRITABLE;
    
    current_pd = kernel_pd;
    
    /* Enable paging */
    load_cr3(pd_phys);
    enable_paging();
}

/*
 * Allocate and map a page at a virtual address
 */
void* paging_alloc(uint32_t vaddr, uint32_t flags) {
    void* phys = pmm_alloc_page();
    if (!phys) return 0;
    
    paging_map(vaddr, (uint32_t)phys, flags);
    return (void*)vaddr;
}
