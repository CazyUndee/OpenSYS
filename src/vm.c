/*
 * vm.c - Virtual Memory Management
 */

#include <stdint.h>
#include "../include/vm.h"
#include "../include/pmm.h"
#include "../include/kheap.h"
#include "../include/paging.h"

/* Kernel's PML4 */
static uint64_t* kernel_pml4 = 0;

/* Get page table entry */
static uint64_t* get_pte(uint64_t* pml4, uint64_t virt, int create) {
    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt >> 21) & 0x1FF;
    uint64_t pt_idx   = (virt >> 12) & 0x1FF;

    uint64_t* table = pml4;
    uint64_t* entry;
    
    /* PML4 */
    entry = &table[pml4_idx];
    if (!(*entry & 1)) {
        if (!create) return 0;
        uint64_t* new_table = (uint64_t*)pmm_alloc_page();
        for (int i = 0; i < 512; i++) new_table[i] = 0;
        *entry = (uint64_t)new_table | VM_FLAG_PRESENT | VM_FLAG_WRITABLE | VM_FLAG_USER;
    }
    table = (uint64_t*)(*entry & 0x000FFFFFFFFFF000ULL);
    
    /* PDPT */
    entry = &table[pdpt_idx];
    if (!(*entry & 1)) {
        if (!create) return 0;
        uint64_t* new_table = (uint64_t*)pmm_alloc_page();
        for (int i = 0; i < 512; i++) new_table[i] = 0;
        *entry = (uint64_t)new_table | VM_FLAG_PRESENT | VM_FLAG_WRITABLE | VM_FLAG_USER;
    }
    table = (uint64_t*)(*entry & 0x000FFFFFFFFFF000ULL);
    
    /* PD */
    entry = &table[pd_idx];
    if (!(*entry & 1)) {
        if (!create) return 0;
        uint64_t* new_table = (uint64_t*)pmm_alloc_page();
        for (int i = 0; i < 512; i++) new_table[i] = 0;
        *entry = (uint64_t)new_table | VM_FLAG_PRESENT | VM_FLAG_WRITABLE | VM_FLAG_USER;
    }
    table = (uint64_t*)(*entry & 0x000FFFFFFFFFF000ULL);
    
    return &table[pt_idx];
}

vm_space_t* vm_create_space(void) {
    vm_space_t* space = (vm_space_t*)kmalloc(sizeof(vm_space_t));
    if (!space) return 0;
    
    /* Allocate PML4 */
    uint64_t* pml4 = (uint64_t*)pmm_alloc_page();
    if (!pml4) {
        kfree(space);
        return 0;
    }
    
    /* Clear PML4 */
    for (int i = 0; i < 512; i++) pml4[i] = 0;
    
    /* Copy kernel mappings (upper half) */
    for (int i = 256; i < 512; i++) {
        pml4[i] = kernel_pml4[i];
    }
    
    space->pml4 = (uint64_t)pml4;
    space->code_start = 0x400000;       /* 4MB - user code */
    space->code_end = 0x400000;
    space->data_start = 0x800000;       /* 8MB - user data */
    space->data_end = 0x800000;
    space->heap_start = 0x10000000;     /* 256MB - user heap */
    space->heap_end = 0x10000000;
    space->stack_top = 0x7FFFFFFFFFFF;  /* Max user addr - stack */
    space->stack_bottom = 0x7FFFFFFFFFFF;
    
    return space;
}

vm_space_t* vm_clone_space(vm_space_t* src) {
    vm_space_t* dst = vm_create_space();
    if (!dst) return 0;
    
    /* Copy user mappings (lower half) */
    uint64_t* src_pml4 = (uint64_t*)src->pml4;
    uint64_t* dst_pml4 = (uint64_t*)dst->pml4;
    
    for (int i = 0; i < 256; i++) {
        dst_pml4[i] = src_pml4[i];
        /* TODO: Mark pages copy-on-write */
    }
    
    dst->code_start = src->code_start;
    dst->code_end = src->code_end;
    dst->data_start = src->data_start;
    dst->data_end = src->data_end;
    dst->heap_start = src->heap_start;
    dst->heap_end = src->heap_end;
    dst->stack_top = src->stack_top;
    dst->stack_bottom = src->stack_bottom;
    
    return dst;
}

void vm_destroy_space(vm_space_t* space) {
    if (!space) return;
    
    uint64_t* pml4 = (uint64_t*)space->pml4;
    
    /* Free user page tables (not kernel's) */
    for (int i = 0; i < 256; i++) {
        if (pml4[i] & 1) {
            uint64_t* pdpt = (uint64_t*)(pml4[i] & 0x000FFFFFFFFFF000ULL);
            for (int j = 0; j < 512; j++) {
                if (pdpt[j] & 1) {
                    uint64_t* pd = (uint64_t*)(pdpt[j] & 0x000FFFFFFFFFF000ULL);
                    for (int k = 0; k < 512; k++) {
                        if (pd[k] & 1) {
                            uint64_t* pt = (uint64_t*)(pd[k] & 0x000FFFFFFFFFF000ULL);
                            pmm_free_page(pt);
                        }
                    }
                    pmm_free_page(pd);
                }
            }
            pmm_free_page(pdpt);
        }
    }
    
    pmm_free_page(pml4);
    kfree(space);
}

int vm_map_page(vm_space_t* space, uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t* pml4 = (uint64_t*)space->pml4;
    uint64_t* pte = get_pte(pml4, virt, 1);
    
    if (!pte) return -1;
    
    *pte = (phys & 0x000FFFFFFFFFF000ULL) | flags | VM_FLAG_PRESENT;
    
    /* Invalidate TLB */
    __asm__ volatile ("invlpg (%0)" : : "r"(virt));
    
    return 0;
}

int vm_unmap_page(vm_space_t* space, uint64_t virt) {
    uint64_t* pml4 = (uint64_t*)space->pml4;
    uint64_t* pte = get_pte(pml4, virt, 0);
    
    if (!pte || !(*pte & 1)) return -1;
    
    *pte = 0;
    __asm__ volatile ("invlpg (%0)" : : "r"(virt));
    
    return 0;
}

uint64_t vm_get_physical(vm_space_t* space, uint64_t virt) {
    uint64_t* pml4 = (uint64_t*)space->pml4;
    uint64_t* pte = get_pte(pml4, virt, 0);
    
    if (!pte || !(*pte & 1)) return 0;
    
    return *pte & 0x000FFFFFFFFFF000ULL;
}

void* vm_alloc_pages(vm_space_t* space, uint64_t count, uint64_t flags) {
    /* Find free region in user space */
    uint64_t start = space->heap_end;
    uint64_t size = count * 0x1000;
    
    for (uint64_t i = 0; i < count; i++) {
        void* phys = pmm_alloc_page();
        if (!phys) return 0;
        
        if (vm_map_page(space, start + i * 0x1000, (uint64_t)phys, flags) < 0) {
            pmm_free_page(phys);
            return 0;
        }
    }
    
    space->heap_end += size;
    return (void*)start;
}

void vm_free_pages(vm_space_t* space, void* virt, uint64_t count) {
    uint64_t addr = (uint64_t)virt;
    
    for (uint64_t i = 0; i < count; i++) {
        uint64_t phys = vm_get_physical(space, addr + i * 0x1000);
        vm_unmap_page(space, addr + i * 0x1000);
        if (phys) pmm_free_page((void*)phys);
    }
}

void vm_switch_space(vm_space_t* space) {
    __asm__ volatile ("mov %0, %%cr3" : : "r"(space->pml4));
}

void vm_init(void) {
    /* Get kernel's current PML4 */
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    kernel_pml4 = (uint64_t*)cr3;
}
