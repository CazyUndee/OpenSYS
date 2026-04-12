/*
 * gdt.c - Global Descriptor Table setup
 *
 * The GDT defines memory segments for the CPU in protected mode.
 * We use a "flat" model where all segments span the full 4GB address space,
 * with privilege levels separating kernel (ring 0) from user (ring 3).
 */

#include <stdint.h>
#include <stddef.h>
#include "../include/gdt.h"

struct gdt_entry {
    uint16_t limit_low;     // Limit bits 0-15
    uint16_t base_low;      // Base bits 0-15
    uint8_t  base_middle;   // Base bits 16-23
    uint8_t  access;        // Access byte
    uint8_t  granularity;   // Granularity byte + limit bits 16-19
    uint8_t  base_high;     // Base bits 24-31
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;         // Size of GDT - 1
    uint32_t base;          // Physical address of GDT
} __attribute__((packed));

// GDT entries - null, kernel code, kernel data, user code, user data, TSS
#define GDT_ENTRIES 6
static struct gdt_entry gdt[GDT_ENTRIES];
static struct gdt_ptr gdt_ptr;

// Assembly function to load GDT and reload segments
extern void gdt_flush(uint32_t gdt_ptr_addr);

/*
 * Set a GDT gate (descriptor entry)
 *
 * num:     Entry index (0 = null, 1 = kernel code, etc.)
 * base:    Segment base address (0 for flat model)
 * limit:   Segment limit (max addressable byte, 0xFFFFF for 4GB)
 * access:  Access byte (see gdt.h for flags)
 * gran:    Granularity byte (see gdt.h for flags)
 */
void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    if (num < 0 || num >= GDT_ENTRIES) return;

    // Base address (split across 3 fields)
    gdt[num].base_low = (uint16_t)(base & 0xFFFF);
    gdt[num].base_middle = (uint8_t)((base >> 16) & 0xFF);
    gdt[num].base_high = (uint8_t)((base >> 24) & 0xFF);

    // Limit (split: low 16 bits + high 4 bits in granularity byte)
    gdt[num].limit_low = (uint16_t)(limit & 0xFFFF);
    gdt[num].granularity = (uint8_t)(((limit >> 16) & 0x0F) | (gran & 0xF0));

    // Access byte
    gdt[num].access = access;
}

/*
 * Initialize the GDT
 *
 * Sets up:
 *   0: Null descriptor (required)
 *   1: Kernel code segment (ring 0, executable)
 *   2: Kernel data segment (ring 0, read/write)
 *   3: User code segment (ring 3, executable)
 *   4: User data segment (ring 3, read/write)
 *   5: TSS (Task State Segment) - placeholder
 *
 * Note: Flat memory model - all segments span 0-4GB
 */
void gdt_init(void) {
    // Null descriptor (index 0) - required by x86 architecture
    gdt_set_gate(0, 0, 0, 0, 0);

    // Kernel code segment (index 1, selector 0x08)
    // Full 4GB (limit=0xFFFFF, gran=4KB pages), ring 0, executable, readable
    gdt_set_gate(1, 0, 0xFFFFF, GDT_ACCESS_KERNEL_CODE, GDT_GRAN_32BIT);

    // Kernel data segment (index 2, selector 0x10)
    // Full 4GB, ring 0, writable
    gdt_set_gate(2, 0, 0xFFFFF, GDT_ACCESS_KERNEL_DATA, GDT_GRAN_32BIT);

    // User code segment (index 3, selector 0x18)
    // Full 4GB, ring 3, executable, readable
    gdt_set_gate(3, 0, 0xFFFFF, GDT_ACCESS_USER_CODE, GDT_GRAN_32BIT);

    // User data segment (index 4, selector 0x20)
    // Full 4GB, ring 3, writable
    gdt_set_gate(4, 0, 0xFFFFF, GDT_ACCESS_USER_DATA, GDT_GRAN_32BIT);

    // TSS - will be set up later when we enable multitasking
    gdt_set_gate(5, 0, 0, 0, 0);

    // Set up GDT pointer
    gdt_ptr.limit = sizeof(gdt) - 1;
    gdt_ptr.base = (uint32_t)&gdt;

    // Load GDT and reload segment registers
    gdt_flush((uint32_t)&gdt_ptr);
}
