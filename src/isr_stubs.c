/*
 * isr_handlers.c - Interrupt Handler Implementations
 */

#include <stdint.h>

/* VGA buffer for error output */
static volatile uint16_t* vga = (volatile uint16_t*)0xB8000;
static int cursor_y = 0;

/* Exception names */
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

/* Register frame pushed by interrupt stub */
typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rbx, rdx, rcx, rax;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags, rsp, ss;
} __attribute__((packed)) interrupt_frame_t;

static void panic_puts(const char* s) {
    while (*s) {
        vga[cursor_y * 80 + (cursor_y == 0 ? 0 : cursor_y * 80)] = 
            (uint16_t)*s | 0x4F00; /* Red on white */
        s++;
    }
}

static void panic_put_hex(uint64_t n) {
    const char hex[] = "0123456789ABCDEF";
    char buf[19];
    buf[0] = '0'; buf[1] = 'x';
    buf[18] = 0;
    for (int i = 17; i >= 2; i--) {
        buf[i] = hex[n & 0xF];
        n >>= 4;
    }
    panic_puts(buf);
}

static void panic(const char* msg, interrupt_frame_t* frame) {
    /* Clear screen with red */
    for (int i = 0; i < 80 * 25; i++) {
        vga[i] = 0x4F20; /* Red on white, space */
    }
    cursor_y = 0;
    
    panic_puts("\n!!! KERNEL PANIC !!!\n\n");
    panic_puts(msg);
    panic_puts("\n\n");
    
    if (frame->int_no < 32) {
        panic_puts("Exception: ");
        panic_puts(exception_names[frame->int_no]);
        panic_puts(" (");
        panic_put_hex(frame->int_no);
        panic_puts(")\n\n");
    }
    
    panic_puts("RIP:  ");
    panic_put_hex(frame->rip);
    panic_puts("\n");
    panic_puts("RSP:  ");
    panic_put_hex(frame->rsp);
    panic_puts("\n");
    panic_puts("RFLAGS: ");
    panic_put_hex(frame->rflags);
    panic_puts("\n");
    panic_puts("CS:   ");
    panic_put_hex(frame->cs);
    panic_puts("  SS: ");
    panic_put_hex(frame->ss);
    panic_puts("\n\n");
    
    if (frame->err_code != 0) {
        panic_puts("Error Code: ");
        panic_put_hex(frame->err_code);
        panic_puts("\n");
        
        /* Page fault specific info */
        if (frame->int_no == 14) {
            uint64_t cr2;
            __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
            panic_puts("Faulting Address: ");
            panic_put_hex(cr2);
            panic_puts("\n");
            
            panic_puts("  Present: ");
            panic_puts(frame->err_code & 1 ? "Yes" : "No");
            panic_puts("\n");
            panic_puts("  Write:   ");
            panic_puts(frame->err_code & 2 ? "Yes" : "No");
            panic_puts("\n");
            panic_puts("  User:    ");
            panic_puts(frame->err_code & 4 ? "Yes" : "No");
            panic_puts("\n");
        }
    }
    
    panic_puts("\nRegisters:\n");
    panic_puts("RAX: "); panic_put_hex(frame->rax);
    panic_puts("  RBX: "); panic_put_hex(frame->rbx);
    panic_puts("  RCX: "); panic_put_hex(frame->rcx);
    panic_puts("\n");
    panic_puts("RDX: "); panic_put_hex(frame->rdx);
    panic_puts("  RSI: "); panic_put_hex(frame->rsi);
    panic_puts("  RDI: "); panic_put_hex(frame->rdi);
    panic_puts("\n");
    panic_puts("RBP: "); panic_put_hex(frame->rbp);
    panic_puts("  R8:  "); panic_put_hex(frame->r8);
    panic_puts("  R9:  "); panic_put_hex(frame->r9);
    panic_puts("\n");
    panic_puts("R10: "); panic_put_hex(frame->r10);
    panic_puts("  R11: "); panic_put_hex(frame->r11);
    panic_puts("  R12: "); panic_put_hex(frame->r12);
    panic_puts("\n");
    panic_puts("R13: "); panic_put_hex(frame->r13);
    panic_puts("  R14: "); panic_put_hex(frame->r14);
    panic_puts("  R15: "); panic_put_hex(frame->r15);
    panic_puts("\n");
    
    panic_puts("\n\nSystem halted.\n");
    
    /* Halt forever */
    while (1) {
        __asm__ volatile ("cli; hlt");
    }
}

/* CPU Exception Handler */
void isr_handler(void* frame_ptr) {
    interrupt_frame_t* frame = (interrupt_frame_t*)frame_ptr;
    
    /* Page faults can sometimes be handled (demand paging, etc) */
    if (frame->int_no == 14) {
        /* For now, just panic on page faults */
        panic("Page Fault", frame);
    }
    
    /* All other exceptions are fatal */
    panic("CPU Exception", frame);
}

/* IRQ Handler */
void irq_handler(void* frame_ptr) {
    interrupt_frame_t* frame = (interrupt_frame_t*)frame_ptr;
    
    /* IRQs 32-47 are remapped from 0-15 */
    uint64_t irq = frame->int_no - 32;
    
    switch (irq) {
        case 0:  /* Timer - handled, nothing to do */
            break;
        case 1:  /* Keyboard - handled in assembly */
            break;
        case 2:  /* Cascade */
            break;
        case 3:  /* COM2 */
            break;
        case 4:  /* COM1 */
            break;
        case 5:  /* LPT2 */
            break;
        case 6:  /* Floppy */
            break;
        case 7:  /* LPT1 */
            break;
        case 8:  /* CMOS RTC */
            break;
        case 9:  /* Free */
            break;
        case 10: /* Free */
            break;
        case 11: /* Free */
            break;
        case 12: /* PS/2 Mouse */
            break;
        case 13: /* FPU */
            break;
        case 14: /* Primary IDE */
            break;
        case 15: /* Secondary IDE */
            break;
        default:
            break;
    }
}
