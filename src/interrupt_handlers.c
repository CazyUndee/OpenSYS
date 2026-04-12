/*
 * interrupt_handlers.c - C handlers for interrupts
 */

#include <stdint.h>
#include "../include/io.h"
#include "../include/idt.h"
#include "../include/interrupts.h"

// External VGA functions
extern void terminal_writestring(const char* str);
extern void terminal_putchar(char c);

// PIC ports
#define PIC1_COMMAND    0x20
#define PIC1_DATA       0x21
#define PIC2_COMMAND    0xA0
#define PIC2_DATA       0xA1

// Exception names
static const char* exception_names[] = {
    "Divide by zero",
    "Debug",
    "NMI",
    "Breakpoint",
    "Overflow",
    "Bound range exceeded",
    "Invalid opcode",
    "Device not available",
    "Double fault",
    "Coprocessor segment overrun",
    "Invalid TSS",
    "Segment not present",
    "Stack fault",
    "General protection fault",
    "Page fault",
    "Reserved",
    "x87 FPU error",
    "Alignment check",
    "Machine check",
    "SIMD FPU error",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Security exception",
    "Reserved"
};

// Handler tables
static void* isr_handlers[32] = {0};
static void* irq_handlers[16] = {0};

/*
 * Send End Of Interrupt to PIC
 */
void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) {
        outb(PIC2_COMMAND, 0x20);
    }
    outb(PIC1_COMMAND, 0x20);
}

/*
 * Remap PIC to avoid overlap with CPU exceptions (0x08-0x0F)
 * Master: 0x20-0x27 (32-39)
 * Slave:  0x28-0x2F (40-47)
 */
void pic_remap(void) {
    uint8_t a1, a2;

    a1 = inb(PIC1_DATA);
    a2 = inb(PIC2_DATA);

    // ICW1: Start initialization
    outb(PIC1_COMMAND, 0x11);
    io_wait();
    outb(PIC2_COMMAND, 0x11);
    io_wait();

    // ICW2: Vector offset
    outb(PIC1_DATA, 0x20);  // Master starts at 32
    io_wait();
    outb(PIC2_DATA, 0x28);  // Slave starts at 40
    io_wait();

    // ICW3: Cascade identity
    outb(PIC1_DATA, 0x04);  // Slave at IRQ2
    io_wait();
    outb(PIC2_DATA, 0x02);  // Slave identity
    io_wait();

    // ICW4: Environment info
    outb(PIC1_DATA, 0x01);  // 8086 mode
    io_wait();
    outb(PIC2_DATA, 0x01);
    io_wait();

    // Restore masks (enable all for now)
    outb(PIC1_DATA, a1);
    outb(PIC2_DATA, a2);
}

/*
 * Enable IRQ
 */
void irq_enable(uint8_t irq) {
    uint16_t port;
    uint8_t value;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    value = inb(port) & ~(1 << irq);
    outb(port, value);
}

/*
 * Disable IRQ
 */
void irq_disable(uint8_t irq) {
    uint16_t port;
    uint8_t value;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    value = inb(port) | (1 << irq);
    outb(port, value);
}

/*
 * Exception handler
 */
void isr_handler(struct cpu_state* regs) {
    // Call registered handler if exists
    if (regs->int_no < 32 && isr_handlers[regs->int_no]) {
        void (*handler)(struct cpu_state*) = isr_handlers[regs->int_no];
        handler(regs);
        return;
    }

    // Default handling
    if (regs->int_no < 32) {
        terminal_writestring("EXCEPTION: ");
        terminal_writestring(exception_names[regs->int_no]);
        terminal_writestring(" (");
        // Would print hex number
        terminal_writestring(")\n");

        if (regs->err_code != 0) {
            terminal_writestring("Error code: ");
            // Would print hex
            terminal_writestring("\n");
        }

        // Halt on serious exceptions
        if (regs->int_no == EXCEPTION_DOUBLE_FAULT ||
            regs->int_no == EXCEPTION_GPF ||
            regs->int_no == EXCEPTION_PAGE_FAULT) {
            terminal_writestring("SYSTEM HALTED\n");
            __asm__ volatile ("cli; hlt");
        }
    }
}

/*
 * IRQ handler
 */
void irq_handler(struct cpu_state* regs) {
    uint8_t irq = (uint8_t)(regs->int_no - 32);

    // Call registered handler if exists
    if (irq < 16 && irq_handlers[irq]) {
        void (*handler)(struct cpu_state*) = irq_handlers[irq];
        handler(regs);
    }

    pic_send_eoi(irq);
}

/*
 * Register IRQ handler
 */
void irq_register_handler(uint8_t irq, void (*handler)(struct cpu_state*)) {
    if (irq < 16) {
        irq_handlers[irq] = (void*)handler;
    }
}

/*
 * Register ISR handler
 */
void isr_register_handler(uint8_t num, void (*handler)(struct cpu_state*)) {
    if (num < 32) {
        isr_handlers[num] = (void*)handler;
    }
}
