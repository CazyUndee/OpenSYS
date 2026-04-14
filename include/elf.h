/*
 * elf.h - ELF Binary Loader
 */

#ifndef ELF_H
#define ELF_H

#include <stdint.h>

/* ELF magic */
#define ELF_MAGIC 0x464C457F  /* "\x7FELF" */

/* ELF class */
#define ELFCLASS64 2

/* ELF data encoding */
#define ELFDATA2LSB 1

/* ELF type */
#define ET_EXEC 2

/* ELF machine */
#define EM_X86_64 62

/* Program header types */
#define PT_LOAD 1

/* ELF64 header */
typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} elf64_header_t;

/* ELF64 program header */
typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} elf64_phdr_t;

/* ELF load result */
typedef struct {
    uint64_t entry;       /* Entry point */
    uint64_t heap_start;  /* End of loaded segments */
} elf_info_t;

/* Load ELF binary into current address space */
int elf_load(const void* data, size_t size, elf_info_t* info);

/* Validate ELF header */
int elf_validate(const void* data);

#endif
