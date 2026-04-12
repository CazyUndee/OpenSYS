#ifndef IDT_H
#define IDT_H

#include <stdint.h>

// IDT Gate Types
#define IDT_GATE_TASK       0x5     // Task gate (unused)
#define IDT_GATE_INT16      0x6     // 16-bit interrupt gate (unused)
#define IDT_GATE_TRAP16     0x7     // 16-bit trap gate (unused)
#define IDT_GATE_INT32      0xE     // 32-bit interrupt gate
#define IDT_GATE_TRAP32     0xF     // 32-bit trap gate

// IDT Access Byte
#define IDT_PRESENT         0x80    // Present bit
#define IDT_DPL_RING0       0x00    // Ring 0 (kernel)
#define IDT_DPL_RING3       0x60    // Ring 3 (user)

// IDT Size
#define IDT_ENTRIES         256     // x86 has 256 interrupt vectors

// Exception Vectors
#define EXCEPTION_DIV_ZERO      0   // Divide by zero
#define EXCEPTION_DEBUG         1   // Debug
#define EXCEPTION_NMI           2   // Non-maskable interrupt
#define EXCEPTION_BREAKPOINT    3   // Breakpoint (int 3)
#define EXCEPTION_OVERFLOW      4   // Overflow
#define EXCEPTION_BOUND_RANGE   5   // Bound range exceeded
#define EXCEPTION_INVALID_OP    6   // Invalid opcode
#define EXCEPTION_DEVICE_NA     7   // Device not available
#define EXCEPTION_DOUBLE_FAULT  8   // Double fault
#define EXCEPTION_COPROC_SEG    9   // Coprocessor segment overrun
#define EXCEPTION_INVALID_TSS   10  // Invalid TSS
#define EXCEPTION_SEG_NOT_PRESENT 11 // Segment not present
#define EXCEPTION_STACK_FAULT   12  // Stack fault
#define EXCEPTION_GPF           13  // General protection fault
#define EXCEPTION_PAGE_FAULT    14  // Page fault
#define EXCEPTION_FPU_FAULT     16  // x87 FPU error
#define EXCEPTION_ALIGNMENT     17  // Alignment check
#define EXCEPTION_MACHINE_CHECK 18  // Machine check
#define EXCEPTION_SIMD_FAULT    19  // SIMD FPU error

// IRQ Vectors (remapped to 32-47)
#define IRQ0_TIMER          32
#define IRQ1_KEYBOARD       33
#define IRQ2_CASCADE        34
#define IRQ3_COM2           35
#define IRQ4_COM1           36
#define IRQ5_LPT2           37
#define IRQ6_FLOPPY         38
#define IRQ7_LPT1           39
#define IRQ8_CMOS_RTC       40
#define IRQ9_FREE           41
#define IRQ10_FREE          42
#define IRQ11_FREE          43
#define IRQ12_PS2_MOUSE     44
#define IRQ13_FPU           45
#define IRQ14_PRIMARY_IDE   46
#define IRQ15_SECONDARY_IDE 47

// System calls
#define SYSCALL_VECTOR      0x80    // Linux-compatible

void idt_init(void);
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);
void idt_load(void);

// Assembly ISR stubs
extern void isr0(void);   extern void isr1(void);   extern void isr2(void);
extern void isr3(void);   extern void isr4(void);   extern void isr5(void);
extern void isr6(void);   extern void isr7(void);   extern void isr8(void);
extern void isr9(void);   extern void isr10(void);  extern void isr11(void);
extern void isr12(void);  extern void isr13(void);  extern void isr14(void);
extern void isr15(void);  extern void isr16(void);  extern void isr17(void);
extern void isr18(void);  extern void isr19(void);  extern void isr20(void);
extern void isr21(void);  extern void isr22(void);  extern void isr23(void);
extern void isr24(void);  extern void isr25(void);  extern void isr26(void);
extern void isr27(void);  extern void isr28(void);  extern void isr29(void);
extern void isr30(void);  extern void isr31(void);

extern void irq0(void);   extern void irq1(void);   extern void irq2(void);
extern void irq3(void);   extern void irq4(void);   extern void irq5(void);
extern void irq6(void);   extern void irq7(void);   extern void irq8(void);
extern void irq9(void);   extern void irq10(void);  extern void irq11(void);
extern void irq12(void);  extern void irq13(void);  extern void irq14(void);
extern void irq15(void);

#endif
