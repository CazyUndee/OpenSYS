/*
 * paging.c - 64-bit Paging System
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

#include "paging.h"
#include "memory.h"
#include "pmm.h"
#include "vga.h"
#include <stddef.h>
#include <stdint.h>

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
    
    void* page = (void*)(uint64_t)pmm_alloc_page();
    if (!page) return 0;
    
    pte_t* table = (pte_t*)page;
    for (int i = 0; i < ENTRIES_PER_TABLE; i++) {
        table[i] = 0;
    }
    
    parent[index] = ((uint64_t)table) | flags | PAGE_PRESENT | PAGE_WRITABLE;
    return table;
}

// Check if PAE is enabled (required for our page table format)
static inline int pae_is_enabled(void) {
    uint64_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    return (cr4 >> 5) & 1;
}

// Initialize paging system
void paging_init(void) {
    terminal_writestring("[PAGING] Initializing 64-bit Paging...\n");
    
    if (!pae_is_enabled()) {
        terminal_writestring("  ERROR: PAE not enabled!\n");
        return;
    }
    
    /* 
     * Step 1: Extend the existing boot identity map to cover ALL physical
     * memory. The boot asm only identity-maps the first 4MB (two 2MB huge
     * pages in PD[0] and PD[1]). When we call pmm_alloc_page()
     * below, it may return pages above 4MB — we need them accessible
     * through the current page tables to zero them out.
     */
    pte_t* boot_pml4;
    __asm__ volatile("mov %%cr3, %0" : "=r"(boot_pml4));
    
    if ((uint64_t)boot_pml4 & 0xFFF) {
        terminal_writestring("  WARNING: CR3 has low bits set, masking\n");
        boot_pml4 = (pte_t*)((uint64_t)boot_pml4 & 0x000FFFFFFFFFF000ULL);
    }
    
    /* Get the boot PDPT and PD from PML4[0] */
    if (!(boot_pml4[0] & PAGE_PRESENT)) {
        terminal_writestring("  ERROR: Boot PML4[0] not present!\n");
        return;
    }
    pte_t* boot_pdpt = (pte_t*)(boot_pml4[0] & 0x000FFFFFFFFFF000ULL);
    
    if (!(boot_pdpt[0] & PAGE_PRESENT)) {
        terminal_writestring("  ERROR: Boot PDPT[0] not present!\n");
        return;
    }
    pte_t* boot_pd = (pte_t*)(boot_pdpt[0] & 0x000FFFFFFFFFF000ULL);
    
    /* Extend the boot PD's identity map to cover all physical memory.
     * PD[0] = 0-2MB, PD[1] = 2-4MB (set by boot.asm).
     * Fill PD[2..n] for 4MB up to total physical RAM using 2MB huge pages. */
    uint64_t total_phys = pmm_get_total();
    uint64_t identity_limit = total_phys < 0x100000000ULL ? total_phys : 0x100000000ULL;
    
    for (uint64_t addr = 0x400000; addr < identity_limit; addr += 0x200000) {
        uint64_t pd_idx = PD_INDEX(addr);
        if (pd_idx < 512) {
            boot_pd[pd_idx] = addr | PAGE_WRITABLE | PAGE_PRESENT | PAGE_HUGE;
        }
    }
    
    /* 
     * Step 2: Now that all physical memory is accessible, allocate and
     * set up the permanent page table hierarchy.
     */
    pml4 = (pte_t*)(uint64_t)pmm_alloc_page();
    if (!pml4) {
        terminal_writestring("  ERROR: Failed to allocate PML4!\n");
        return;
    }
    
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
    
    terminal_writestring("  4-level paging enabled\n");
    terminal_writestring("  IA-32e long mode active\n");
    terminal_writestring("[PAGING] Paging System Ready!\n");
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
    void* phys = (void*)(uint64_t)pmm_alloc_page();
    if (!phys) return 0;
    
    paging_map(vaddr, (uint64_t)phys, flags);
    return (void*)vaddr;
}

// Get paging statistics
void paging_get_stats(paging_stats_t* stats) {
    if (!stats) return;
    
    // Get memory stats
    memory_stats_t mem_stats;
    memory_get_stats(&mem_stats);
    
    // Calculate paging statistics (simplified)
    stats->total_pages = mem_stats.total_mb * 256;  // Approximate
    stats->free_pages = mem_stats.free_mb * 256;    // Approximate
}
