/*
 * interrupts.c - Interrupt Service Routines
 *
 * Low-level interrupt entry/exit code using GCC inline assembly.
 * Handles CPU exceptions and hardware IRQs.
 */

#include <stdint.h>

/*
 * CPU state saved on interrupt stack
 * Pushed in this order by hardware and our stubs
 */
struct cpu_state {
    uint32_t ds;                    // Data segment (pushed by stub)
    uint32_t edi, esi, ebp, esp;    // Pushed by pusha
    uint32_t ebx, edx, ecx, eax;    // Pushed by pusha
    uint32_t int_no, err_code;      // Interrupt number and error code
    uint32_t eip, cs, eflags;       // Pushed by CPU
    uint32_t useresp, ss;           // Only if from user mode
};

/*
 * External C handlers
 */
extern void isr_handler(struct cpu_state* regs);
extern void irq_handler(struct cpu_state* regs);

/*
 * Macro to define ISR stub (no error code)
 */
#define ISR_STUB(n)                     \
    __asm__ (                           \
        ".global isr" #n "\n"           \
        "isr" #n ":\n"                  \
        "    cli\n"                      \
        "    push $0\n"                  \
        "    push $" #n "\n"            \
        "    jmp isr_common\n"          \
    );

/*
 * Macro to define ISR stub (with error code)
 */
#define ISR_STUB_ERR(n)               \
    __asm__ (                         \
        ".global isr" #n "\n"         \
        "isr" #n ":\n"                \
        "    cli\n"                    \
        "    push $" #n "\n"          \
        "    jmp isr_common\n"        \
    );

/*
 * Macro to define IRQ stub
 */
#define IRQ_STUB(n, irq)              \
    __asm__ (                         \
        ".global irq" #n "\n"        \
        "irq" #n ":\n"                \
        "    cli\n"                   \
        "    push $0\n"              \
        "    push $" #irq "\n"        \
        "    jmp irq_common\n"       \
    );

/*
 * ISR Stubs - CPU Exceptions 0-31
 */
ISR_STUB(0)   // Divide by zero
ISR_STUB(1)   // Debug
ISR_STUB(2)   // NMI
ISR_STUB(3)   // Breakpoint
ISR_STUB(4)   // Overflow
ISR_STUB(5)   // Bound range
ISR_STUB(6)   // Invalid opcode
ISR_STUB(7)   // Device not available
ISR_STUB_ERR(8)   // Double fault
ISR_STUB(9)   // Coprocessor segment overrun
ISR_STUB_ERR(10)  // Invalid TSS
ISR_STUB_ERR(11)  // Segment not present
ISR_STUB_ERR(12)  // Stack fault
ISR_STUB_ERR(13)  // General protection fault
ISR_STUB_ERR(14)  // Page fault
ISR_STUB(15)  // Reserved
ISR_STUB(16)  // x87 FPU error
ISR_STUB_ERR(17)  // Alignment check
ISR_STUB(18)  // Machine check
ISR_STUB(19)  // SIMD FPU error
ISR_STUB(20)  // Reserved
ISR_STUB(21)  // Reserved
ISR_STUB(22)  // Reserved
ISR_STUB(23)  // Reserved
ISR_STUB(24)  // Reserved
ISR_STUB(25)  // Reserved
ISR_STUB(26)  // Reserved
ISR_STUB(27)  // Reserved
ISR_STUB(28)  // Reserved
ISR_STUB(29)  // Reserved
ISR_STUB(30)  // Security exception
ISR_STUB(31)  // Reserved

/*
 * IRQ Stubs - Hardware interrupts 0-15 (remapped to 32-47)
 */
IRQ_STUB(0, 32)   // Timer
IRQ_STUB(1, 33)   // Keyboard
IRQ_STUB(2, 34)   // Cascade
IRQ_STUB(3, 35)   // COM2
IRQ_STUB(4, 36)   // COM1
IRQ_STUB(5, 37)   // LPT2
IRQ_STUB(6, 38)   // Floppy
IRQ_STUB(7, 39)   // LPT1
IRQ_STUB(8, 40)   // CMOS RTC
IRQ_STUB(9, 41)   // Free
IRQ_STUB(10, 42)  // Free
IRQ_STUB(11, 43)  // Free
IRQ_STUB(12, 44)  // PS/2 Mouse
IRQ_STUB(13, 45)  // FPU
IRQ_STUB(14, 46)  // Primary IDE
IRQ_STUB(15, 47)  // Secondary IDE

/*
 * Common ISR entry
 * Saves CPU state and calls C handler
 */
__asm__ (
    ".global isr_common\n"
    "isr_common:\n"
    "    pusha\n"           // Save all GPRs
    "    push %ds\n"        // Save data segment
    "    push %es\n"
    "    push %fs\n"
    "    push %gs\n"
    "    mov $0x10, %ax\n"  // Load kernel data segment
    "    mov %ax, %ds\n"
    "    mov %ax, %es\n"
    "    mov %ax, %fs\n"
    "    mov %ax, %gs\n"
    "    push %esp\n"       // Pass pointer to cpu_state
    "    call isr_handler\n"
    "    pop %esp\n"
    "    pop %gs\n"         // Restore segments
    "    pop %fs\n"
    "    pop %es\n"
    "    pop %ds\n"
    "    popa\n"            // Restore GPRs
    "    add $8, %esp\n"    // Clean up int_no and err_code
    "    sti\n"
    "    iret\n"            // Return from interrupt
);

/*
 * Common IRQ entry
 * Same as ISR but sends EOI to PIC
 */
__asm__ (
    ".global irq_common\n"
    "irq_common:\n"
    "    pusha\n"
    "    push %ds\n"
    "    push %es\n"
    "    push %fs\n"
    "    push %gs\n"
    "    mov $0x10, %ax\n"
    "    mov %ax, %ds\n"
    "    mov %ax, %es\n"
    "    mov %ax, %fs\n"
    "    mov %ax, %gs\n"
    "    push %esp\n"
    "    call irq_handler\n"
    "    pop %esp\n"
    "    pop %gs\n"
    "    pop %fs\n"
    "    pop %es\n"
    "    pop %ds\n"
    "    popa\n"
    "    add $8, %esp\n"
    "    sti\n"
    "    iret\n"
);
