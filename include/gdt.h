#ifndef GDT_H
#define GDT_H

#include <stdint.h>

// GDT Access Byte Flags
// ---------------------
// Bit 7: Present (1 = segment is valid)
// Bits 6-5: Privilege (0 = ring 0/kernel, 3 = ring 3/user)
// Bit 4: Segment type (1 = code/data, 0 = system)
// Bit 3: Executable (1 = code, 0 = data)
// Bit 2: Direction/Conforming
//        - Data: 0=grows up, 1=grows down
//        - Code: 0=only ring=X, 1=ring=X or lower
// Bit 1: Read/Write
//        - Code: 1=readable, 0=not
//        - Data: 1=writable, 0=not
// Bit 0: Accessed (CPU sets this, we clear)

// Common access byte values
#define GDT_ACCESS_KERNEL_CODE  0x9A  // Present, ring 0, code, read
#define GDT_ACCESS_KERNEL_DATA  0x92  // Present, ring 0, data, write
#define GDT_ACCESS_USER_CODE    0xFA  // Present, ring 3, code, read
#define GDT_ACCESS_USER_DATA    0xF2  // Present, ring 3, data, write
#define GDT_ACCESS_TSS          0x89  // Present, ring 0, TSS

// Granularity Byte Flags
// ----------------------
// Bit 7: Page granularity (1 = 4KB pages, 0 = 1 byte)
// Bit 6: 32-bit mode (1 = 32-bit, 0 = 16-bit)
// Bit 5: Reserved (0)
// Bit 4: Reserved (0)
// Bits 3-0: Limit high bits (bits 16-19 of limit)

#define GDT_GRAN_32BIT          0xCF  // 4KB pages, 32-bit mode, limit high bits = 0xF
#define GDT_GRAN_16BIT          0x00  // Byte granularity, 16-bit mode

// Segment Selectors (Index << 3 | RPL)
#define GDT_SELECTOR_NULL       0x00
#define GDT_SELECTOR_KERNEL_CODE 0x08
#define GDT_SELECTOR_KERNEL_DATA 0x10
#define GDT_SELECTOR_USER_CODE  0x18
#define GDT_SELECTOR_USER_DATA  0x20
#define GDT_SELECTOR_TSS        0x28

void gdt_init(void);
void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran);

#endif
