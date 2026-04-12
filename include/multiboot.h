#ifndef MULTIBOOT_H
#define MULTIBOOT_H

#include <stdint.h>

/*
 * Multiboot 1 information structure
 * Passed by GRUB in EBX register
 */

#define MBOOT_MAGIC         0x2BADB002

/* Multiboot info flags */
#define MBOOT_FLAG_MEM      0x001   /* Memory info valid */
#define MBOOT_FLAG_DEVICE   0x002   /* Boot device valid */
#define MBOOT_FLAG_CMDLINE  0x004   /* Command line valid */
#define MBOOT_FLAG_MODS     0x008   /* Modules valid */
#define MBOOT_FLAG_MMAP     0x040   /* Memory map valid */

/* Multiboot info header */
struct mboot_info {
    uint32_t flags;
    uint32_t mem_lower;     /* KB below 640KB */
    uint32_t mem_upper;     /* KB above 1MB */
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
};

/* Memory map entry */
struct mboot_mmap_entry {
    uint32_t size;          /* Size of this entry (minus size field itself) */
    uint32_t base_low;
    uint32_t base_high;
    uint32_t length_low;
    uint32_t length_high;
    uint32_t type;
} __attribute__((packed));

#define MMAP_ENTRY_NEXT(entry) \
    ((struct mboot_mmap_entry*)((uint8_t*)(entry) + (entry)->size + 4))

#endif
