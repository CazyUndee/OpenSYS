/*
 * isr_handlers.c - Interrupt Handler Implementations
 */

#include <stdint.h>

static volatile uint16_t* vga = (volatile uint16_t*)0xB8000;
static int px = 0;
static int py = 0;

static const char* exception_names[] = {
    "Divide Error",
    "Debug Exception",
    "NMI Interrupt",
    "Breakpoint",
    "Overflow",
    "BOUND Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack-Segment Fault",
    "General Protection",
    "Page Fault",
    "Reserved",
    "x87 FPU Error",
    "Alignment Check",
    "Machine Check",
    "SIMD Exception",
    "Virtualization",
    "Control Protection",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Security", "Reserved"
};

typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rbx, rdx, rcx, rax;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags, rsp, ss;
} __attribute__((packed)) interrupt_frame_t;

static void panic_putch(char c) {
    if (c == '\n') {
        px = 0;
        py++;
        return;
    }
    if (px >= 80) {
        px = 0;
        py++;
    }
    if (py >= 25) {
        for (int y = 0; y < 24; y++) {
            for (int x = 0; x < 80; x++) {
                vga[y * 80 + x] = vga[(y + 1) * 80 + x];
            }
        }
        for (int x = 0; x < 80; x++) {
            vga[24 * 80 + x] = 0x4F20;
        }
        py = 24;
    }
    vga[py * 80 + px] = (uint16_t)c | 0x4F00;
    px++;
}

static void panic_puts(const char* s) {
    while (*s) panic_putch(*s++);
}

static void panic_put_hex(uint64_t n) {
    const char hex[] = "0123456789ABCDEF";
    panic_putch('0'); panic_putch('x');
    int started = 0;
    for (int i = 60; i >= 0; i -= 4) {
        int digit = (n >> i) & 0xF;
        if (digit || started || i == 0) {
            panic_putch(hex[digit]);
            started = 1;
        }
    }
}

static void panic_put_dec(uint64_t n) {
    if (n == 0) { panic_putch('0'); return; }
    char buf[24];
    int i = 23;
    while (n > 0) { buf[--i] = '0' + (n % 10); n /= 10; }
    while (buf[i]) panic_putch(buf[i++]);
}

static void panic(const char* msg, interrupt_frame_t* frame) {
    for (int i = 0; i < 80 * 25; i++) vga[i] = 0x4F20;
    px = 0; py = 0;

    panic_puts("\n!!! KERNEL PANIC !!!\n\n");
    panic_puts(msg);
    panic_puts("\n\n");

    if (frame->int_no < 32) {
        panic_puts("Exception: ");
        panic_puts(exception_names[frame->int_no]);
        panic_puts(" (#");
        panic_put_dec(frame->int_no);
        panic_puts(")\n\n");
    }

    panic_puts("RIP:    "); panic_put_hex(frame->rip);
    panic_puts("\nRSP:    "); panic_put_hex(frame->rsp);
    panic_puts("\nRFLAGS: "); panic_put_hex(frame->rflags);
    panic_puts("\nCS: "); panic_put_hex(frame->cs);
    panic_puts("  SS: "); panic_put_hex(frame->ss);
    panic_puts("\n\n");

    if (frame->int_no == 14) {
        uint64_t cr2;
        __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
        panic_puts("Error Code: ");
        panic_put_hex(frame->err_code);
        panic_puts("\nFault Address: ");
        panic_put_hex(cr2);
        panic_puts("\n\n  P: ");
        panic_puts(frame->err_code & 1 ? "1" : "0");
        panic_puts("  W: ");
        panic_puts(frame->err_code & 2 ? "1" : "0");
        panic_puts("  U: ");
        panic_puts(frame->err_code & 4 ? "1" : "0");
        panic_puts("  R: ");
        panic_puts(frame->err_code & 8 ? "1" : "0");
        panic_puts("  I: ");
        panic_puts(frame->err_code & 16 ? "1" : "0");
        panic_puts("\n");
    }

    panic_puts("\nRegisters:\n");
    panic_puts("RAX: "); panic_put_hex(frame->rax);
    panic_puts("  RBX: "); panic_put_hex(frame->rbx);
    panic_puts("  RCX: "); panic_put_hex(frame->rcx);
    panic_puts("\nRDX: "); panic_put_hex(frame->rdx);
    panic_puts("  RSI: "); panic_put_hex(frame->rsi);
    panic_puts("  RDI: "); panic_put_hex(frame->rdi);
    panic_puts("\nRBP: "); panic_put_hex(frame->rbp);
    panic_puts("  R8:  "); panic_put_hex(frame->r8);
    panic_puts("  R9:  "); panic_put_hex(frame->r9);
    panic_puts("\nR10: "); panic_put_hex(frame->r10);
    panic_puts("  R11: "); panic_put_hex(frame->r11);
    panic_puts("  R12: "); panic_put_hex(frame->r12);
    panic_puts("\nR13: "); panic_put_hex(frame->r13);
    panic_puts("  R14: "); panic_put_hex(frame->r14);
    panic_puts("  R15: "); panic_put_hex(frame->r15);
    panic_puts("\n\nSystem halted.\n");

    while (1) __asm__ volatile ("cli; hlt");
}

void isr_handler(void* frame_ptr) {
    interrupt_frame_t* frame = (interrupt_frame_t*)frame_ptr;

    switch (frame->int_no) {
        case 0:  panic("Divide Error (#DE)", frame); break;
        case 1:  panic("Debug Exception (#DB)", frame); break;
        case 2:  panic("NMI Interrupt", frame); break;
        case 3:  return; /* Breakpoint - just return */
        case 4:  panic("Overflow (#OF)", frame); break;
        case 5:  panic("BOUND Range Exceeded (#BR)", frame); break;
        case 6:  panic("Invalid Opcode (#UD)", frame); break;
        case 7:  panic("Device Not Available (#NM)", frame); break;
        case 8:  panic("Double Fault (#DF)", frame); break;
        case 10: panic("Invalid TSS (#TS)", frame); break;
        case 11: panic("Segment Not Present (#NP)", frame); break;
        case 12: panic("Stack-Segment Fault (#SS)", frame); break;
        case 13: panic("General Protection (#GP)", frame); break;
        case 14: panic("Page Fault (#PF)", frame); break;
        case 16: panic("x87 FPU Error (#MF)", frame); break;
        case 17: panic("Alignment Check (#AC)", frame); break;
        case 18: panic("Machine Check (#MC)", frame); break;
        case 19: panic("SIMD Exception (#XM)", frame); break;
        default: panic("Unknown Exception", frame); break;
    }
}

void irq_handler(void* frame_ptr) {
    interrupt_frame_t* frame = (interrupt_frame_t*)frame_ptr;
    uint64_t irq = frame->int_no - 32;
    
    switch (irq) {
        case 2: break; /* Cascade */
        case 3: case 4: break; /* Serial */
        case 5: case 7: break; /* Parallel */
        case 6: break; /* Floppy */
        case 8: break; /* RTC */
        case 9: case 10: case 11: break; /* Free */
        case 12: break; /* Mouse */
        case 13: __asm__ volatile ("fnclex"); break; /* FPU */
        case 14: case 15: break; /* IDE */
        default: break;
    }
}
