/*
 * elf.c - ELF Binary Loader
 */

#include <stdint.h>
#include <stddef.h>
#include "../include/elf.h"
#include "../include/vm.h"
#include "../include/pmm.h"
#include "../include/kheap.h"

int elf_validate(const void* data) {
    const elf64_header_t* hdr = (const elf64_header_t*)data;
    
    /* Check magic */
    if (*(uint32_t*)hdr->e_ident != ELF_MAGIC) return -1;
    
    /* Check class (64-bit) */
    if (hdr->e_ident[4] != ELFCLASS64) return -1;
    
    /* Check endianness */
    if (hdr->e_ident[5] != ELFDATA2LSB) return -1;
    
    /* Check type (executable) */
    if (hdr->e_type != ET_EXEC) return -1;
    
    /* Check machine */
    if (hdr->e_machine != EM_X86_64) return -1;
    
    return 0;
}

int elf_load(const void* data, size_t size, elf_info_t* info) {
    if (!data || size < sizeof(elf64_header_t)) return -1;
    
    const elf64_header_t* hdr = (const elf64_header_t*)data;
    
    /* Validate */
    if (elf_validate(data) < 0) return -1;
    
    /* Get current address space */
    extern vm_space_t* process_current_vm(void);
    vm_space_t* space = process_current_vm();
    if (!space) return -1;
    
    uint64_t max_addr = 0;
    
    /* Load program segments */
    for (int i = 0; i < hdr->e_phnum; i++) {
        const elf64_phdr_t* phdr = (const elf64_phdr_t*)(
            (const uint8_t*)data + hdr->e_phoff + i * hdr->e_phentsize
        );
        
        /* Only load PT_LOAD segments */
        if (phdr->p_type != PT_LOAD) continue;
        
        /* Calculate pages needed */
        uint64_t start = phdr->p_vaddr;
        uint64_t end = start + phdr->p_memsz;
        start &= ~0xFFF;  /* Page align */
        end = (end + 0xFFF) & ~0xFFF;
        
        size_t pages = (end - start) / PAGE_SIZE;
        
        /* Allocate and map pages */
        for (size_t p = 0; p < pages; p++) {
            phys_addr_t phys = pmm_alloc_page();
            if (!phys) return -1;
            
            /* Map as user-accessible */
            uint64_t flags = VM_FLAG_USER | VM_FLAG_PRESENT;
            if (phdr->p_flags & 2) flags |= VM_FLAG_WRITABLE;  /* W */
            
            vm_map_page(space, start + p * PAGE_SIZE, phys, flags);
        }
        
        /* Zero the memory */
        for (uint64_t addr = start; addr < end; addr += PAGE_SIZE) {
            uint8_t* page = (uint8_t*)addr;
            for (size_t j = 0; j < PAGE_SIZE; j++) {
                page[j] = 0;
            }
        }
        
        /* Copy file data */
        if (phdr->p_filesz > 0) {
            const uint8_t* src = (const uint8_t*)data + phdr->p_offset;
            uint8_t* dst = (uint8_t*)phdr->p_vaddr;
            
            for (uint64_t j = 0; j < phdr->p_filesz; j++) {
                dst[j] = src[j];
            }
        }
        
        if (end > max_addr) max_addr = end;
    }
    
    if (info) {
        info->entry = hdr->e_entry;
        info->heap_start = max_addr;
    }
    
    return 0;
}
