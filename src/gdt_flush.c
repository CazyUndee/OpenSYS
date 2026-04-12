/*
 * gdt_flush.c - Load GDT using GCC inline assembly
 *
 * C version of gdt_flush.asm - no external assembler required.
 * Uses GCC extended inline assembly for portability.
 */

#include <stdint.h>

/*
 * Load GDT and reload segment registers
 *
 * This is the most critical initialization step. A mistake here
 * causes an immediate triple fault (CPU reset).
 *
 * Steps:
 * 1. lgdt - Load GDT pointer into GDTR
 * 2. Reload segment registers with kernel data selector
 * 3. Far jump to reload CS (code segment)
 */
void gdt_flush(uint32_t gdt_ptr_addr) {
    __asm__ volatile (
        // Load GDT pointer into GDTR
        "lgdt (%0)\n\t"

        // Reload data segment registers with kernel data selector (0x10)
        // 0x10 = GDT index 2 << 3 | RPL 0
        "mov $0x10, %%ax\n\t"
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%fs\n\t"
        "mov %%ax, %%gs\n\t"
        "mov %%ax, %%ss\n\t"

        // Reload code segment with far jump
        // 0x08 = GDT index 1 << 3 | RPL 0 (kernel code selector)
        // Use ljmp $selector, $offset syntax
        "ljmp $0x08, $1f\n\t"
        "1:"
        :
        : "r" (gdt_ptr_addr)
        : "ax", "memory"
    );
}
