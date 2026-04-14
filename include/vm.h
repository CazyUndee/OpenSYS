/*
 * vm.h - Virtual Memory Management
 */

#ifndef VM_H
#define VM_H

#include <stdint.h>

/* Page flags */
#define VM_FLAG_PRESENT    0x01
#define VM_FLAG_WRITABLE   0x02
#define VM_FLAG_USER       0x04
#define VM_FLAG_WRITETHRU  0x08
#define VM_FLAG_CACHE_DIS  0x10
#define VM_FLAG_ACCESSED   0x20
#define VM_FLAG_DIRTY      0x40
#define VM_FLAG_HUGE       0x80
#define VM_FLAG_NO_EXEC    0x8000000000000000ULL

/* Address space */
typedef struct {
    uint64_t pml4;          /* Physical address of PML4 */
    uint64_t code_start;    /* User code region start */
    uint64_t code_end;
    uint64_t data_start;    /* User data region start */
    uint64_t data_end;
    uint64_t heap_start;    /* User heap (brk) */
    uint64_t heap_end;
    uint64_t stack_top;     /* User stack */
    uint64_t stack_bottom;
} vm_space_t;

/* Create new address space */
vm_space_t* vm_create_space(void);

/* Clone address space (for fork) */
vm_space_t* vm_clone_space(vm_space_t* src);

/* Destroy address space */
void vm_destroy_space(vm_space_t* space);

/* Map page in address space */
int vm_map_page(vm_space_t* space, uint64_t virt, uint64_t phys, uint64_t flags);

/* Unmap page */
int vm_unmap_page(vm_space_t* space, uint64_t virt);

/* Get physical address for virtual */
uint64_t vm_get_physical(vm_space_t* space, uint64_t virt);

/* Allocate and map pages */
void* vm_alloc_pages(vm_space_t* space, uint64_t count, uint64_t flags);

/* Free pages */
void vm_free_pages(vm_space_t* space, void* virt, uint64_t count);

/* Switch to address space */
void vm_switch_space(vm_space_t* space);

/* Initialize VM subsystem */
void vm_init(void);

#endif
