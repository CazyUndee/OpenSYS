/*
 * idt.c - Interrupt Descriptor Table setup
 *
 * The IDT tells the CPU where to jump when interrupts occur.
 * There are 256 possible interrupt vectors:
 *   0-31:    CPU exceptions
 *   32-47:   Hardware IRQs (from PIC)
 *   48-255:  Software interrupts (available for system calls, etc.)
 */

#include <stdint.h>
#include "../include/idt.h"
#include "../include/gdt.h"

struct idt_entry {
    uint16_t base_low;      // Offset bits 0-15
    uint16_t sel;           // Code segment selector (GDT)
    uint8_t  always0;       // Reserved, must be 0
    uint8_t  flags;         // Type and attributes
    uint16_t base_high;     // Offset bits 16-31
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;         // Size of IDT - 1
    uint32_t base;          // Physical address of IDT
} __attribute__((packed));

static struct idt_entry idt[IDT_ENTRIES];
static struct idt_ptr idt_ptr;

/*
 * Set an IDT gate (interrupt handler entry)
 *
 * num:     Interrupt vector number (0-255)
 * base:    Address of handler function
 * sel:     Code segment selector (usually kernel code: 0x08)
 * flags:   Gate type + present + DPL
 */
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_low = (uint16_t)(base & 0xFFFF);
    idt[num].base_high = (uint16_t)((base >> 16) & 0xFFFF);
    idt[num].sel = sel;
    idt[num].always0 = 0;
    idt[num].flags = flags | IDT_PRESENT;
}

/*
 * Load IDT into IDTR register
 * Uses inline assembly instead of external ASM file
 */
void idt_load(void) {
    __asm__ volatile (
        "lidt (%0)"
        :
        : "r" ((uint32_t)&idt_ptr)
        : "memory"
    );
}

/*
 * Initialize the IDT
 *
 * Sets up all 256 entries:
 *   0-31: CPU exception handlers (ISR 0-31)
 *   32-47: Hardware IRQ handlers (IRQ 0-15, remapped)
 *   48-255: Default handler (for now)
 */
void idt_init(void) {
    // Set up IDT pointer
    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base = (uint32_t)&idt;

    // Clear IDT
    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt_set_gate(i, 0, 0, 0);
    }

    // CPU Exceptions (0-31)
    // Use 32-bit interrupt gate, kernel code segment (0x08), ring 0
    uint8_t kernel_gate = IDT_GATE_INT32 | IDT_DPL_RING0;

    idt_set_gate(0,  (uint32_t)isr0,  GDT_SELECTOR_KERNEL_CODE, kernel_gate);
    idt_set_gate(1,  (uint32_t)isr1,  GDT_SELECTOR_KERNEL_CODE, kernel_gate);
    idt_set_gate(2,  (uint32_t)isr2,  GDT_SELECTOR_KERNEL_CODE, kernel_gate);
    idt_set_gate(3,  (uint32_t)isr3,  GDT_SELECTOR_KERNEL_CODE, kernel_gate);
    idt_set_gate(4,  (uint32_t)isr4,  GDT_SELECTOR_KERNEL_CODE, kernel_gate);
    idt_set_gate(5,  (uint32_t)isr5,  GDT_SELECTOR_KERNEL_CODE, kernel_gate);
    idt_set_gate(6,  (uint32_t)isr6,  GDT_SELECTOR_KERNEL_CODE, kernel_gate);
    idt_set_gate(7,  (uint32_t)isr7,  GDT_SELECTOR_KERNEL_CODE, kernel_gate);
    idt_set_gate(8,  (uint32_t)isr8,  GDT_SELECTOR_KERNEL_CODE, kernel_gate);
    idt_set_gate(9,  (uint32_t)isr9,  GDT_SELECTOR_KERNEL_CODE, kernel_gate);
    idt_set_gate(10, (uint32_t)isr10, GDT_SELECTOR_KERNEL_CODE, kernel_gate);
    idt_set_gate(11, (uint32_t)isr11, GDT_SELECTOR_KERNEL_CODE, kernel_gate);
    idt_set_gate(12, (uint32_t)isr12, GDT_SELECTOR_KERNEL_CODE, kernel_gate);
    idt_set_gate(13, (uint32_t)isr13, GDT_SELECTOR_KERNEL_CODE, kernel_gate);
    idt_set_gate(14, (uint32_t)isr14, GDT_SELECTOR_KERNEL_CODE, kernel_gate);
    idt_set_gate(15, (uint32_t)isr15, GDT_SELECTOR_KERNEL_CODE, kernel_gate);
    idt_set_gate(16, (uint32_t)isr16, GDT_SELECTOR_KERNEL_CODE, kernel_gate);
    idt_set_gate(17, (uint32_t)isr17, GDT_SELECTOR_KERNEL_CODE, kernel_gate);
    idt_set_gate(18, (uint32_t)isr18, GDT_SELECTOR_KERNEL_CODE, kernel_gate);
    idt_set_gate(19, (uint32_t)isr19, GDT_SELECTOR_KERNEL_CODE, kernel_gate);
    idt_set_gate(20, (uint32_t)isr20, GDT_SELECTOR_KERNEL_CODE, kernel_gate);
    idt_set_gate(21, (uint32_t)isr21, GDT_SELECTOR_KERNEL_CODE, kernel_gate);
    idt_set_gate(22, (uint32_t)isr22, GDT_SELECTOR_KERNEL_CODE, kernel_gate);
    idt_set_gate(23, (uint32_t)isr23, GDT_SELECTOR_KERNEL_CODE, kernel_gate);
    idt_set_gate(24, (uint32_t)isr24, GDT_SELECTOR_KERNEL_CODE, kernel_gate);
    idt_set_gate(25, (uint32_t)isr25, GDT_SELECTOR_KERNEL_CODE, kernel_gate);
    idt_set_gate(26, (uint32_t)isr26, GDT_SELECTOR_KERNEL_CODE, kernel_gate);
    idt_set_gate(27, (uint32_t)isr27, GDT_SELECTOR_KERNEL_CODE, kernel_gate);
    idt_set_gate(28, (uint32_t)isr28, GDT_SELECTOR_KERNEL_CODE, kernel_gate);
    idt_set_gate(29, (uint32_t)isr29, GDT_SELECTOR_KERNEL_CODE, kernel_gate);
    idt_set_gate(30, (uint32_t)isr30, GDT_SELECTOR_KERNEL_CODE, kernel_gate);
    idt_set_gate(31, (uint32_t)isr31, GDT_SELECTOR_KERNEL_CODE, kernel_gate);

    // Hardware IRQs (32-47, remapped by PIC)
    idt_set_gate(32, (uint32_t)irq0,  GDT_SELECTOR_KERNEL_CODE, kernel_gate);
    idt_set_gate(33, (uint32_t)irq1,  GDT_SELECTOR_KERNEL_CODE, kernel_gate);
    idt_set_gate(34, (uint32_t)irq2,  GDT_SELECTOR_KERNEL_CODE, kernel_gate);
    idt_set_gate(35, (uint32_t)irq3,  GDT_SELECTOR_KERNEL_CODE, kernel_gate);
    idt_set_gate(36, (uint32_t)irq4,  GDT_SELECTOR_KERNEL_CODE, kernel_gate);
    idt_set_gate(37, (uint32_t)irq5,  GDT_SELECTOR_KERNEL_CODE, kernel_gate);
    idt_set_gate(38, (uint32_t)irq6,  GDT_SELECTOR_KERNEL_CODE, kernel_gate);
    idt_set_gate(39, (uint32_t)irq7,  GDT_SELECTOR_KERNEL_CODE, kernel_gate);
    idt_set_gate(40, (uint32_t)irq8,  GDT_SELECTOR_KERNEL_CODE, kernel_gate);
    idt_set_gate(41, (uint32_t)irq9,  GDT_SELECTOR_KERNEL_CODE, kernel_gate);
    idt_set_gate(42, (uint32_t)irq10, GDT_SELECTOR_KERNEL_CODE, kernel_gate);
    idt_set_gate(43, (uint32_t)irq11, GDT_SELECTOR_KERNEL_CODE, kernel_gate);
    idt_set_gate(44, (uint32_t)irq12, GDT_SELECTOR_KERNEL_CODE, kernel_gate);
    idt_set_gate(45, (uint32_t)irq13, GDT_SELECTOR_KERNEL_CODE, kernel_gate);
    idt_set_gate(46, (uint32_t)irq14, GDT_SELECTOR_KERNEL_CODE, kernel_gate);
    idt_set_gate(47, (uint32_t)irq15, GDT_SELECTOR_KERNEL_CODE, kernel_gate);

    // Load IDT into CPU
    idt_load();
}
