#ifndef INTERRUPTS_H
#define INTERRUPTS_H

#include <stdint.h>

/*
 * CPU state saved on interrupt stack
 * Layout matches boot/interrupts.asm common stubs.
 */
struct cpu_state {
    uint32_t gs;
    uint32_t fs;
    uint32_t es;
    uint32_t ds;
    uint32_t edi, esi, ebp, esp;    // Pushed by pusha
    uint32_t ebx, edx, ecx, eax;    // Pushed by pusha
    uint32_t int_no, err_code;      // Interrupt number and error code
    uint32_t eip, cs, eflags;       // Pushed by CPU
    uint32_t useresp, ss;           // Only if from user mode
} __attribute__((packed));

// PIC I/O
void pic_remap(void);
void pic_send_eoi(uint8_t irq);
void irq_enable(uint8_t irq);
void irq_disable(uint8_t irq);

// Handlers
void isr_handler(struct cpu_state* regs);
void irq_handler(struct cpu_state* regs);
void irq_register_handler(uint8_t irq, void (*handler)(struct cpu_state*));

// Exception handler registration (optional)
void isr_register_handler(uint8_t num, void (*handler)(struct cpu_state*));

#endif
