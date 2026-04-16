/*
 * kernel64.c - OpenSYS OS 64-bit Main Kernel
 */

#include <stdint.h>
#include "../include/user_bin.h"

#define VGA_BUFFER 0xB8000
#define VGA_COLOR 0x07

static volatile uint16_t* vga = (volatile uint16_t*)(VGA_BUFFER);

static int cursor_x = 0;
static int cursor_y = 0;

static void clear(void) {
    for (int i = 0; i < 80 * 25; i++) {
        vga[i] = (uint16_t)' ' | (uint16_t)(VGA_COLOR << 8);
    }
    cursor_x = 0;
    cursor_y = 0;
}

static void puts(const char* s) {
    while (*s) {
        if (*s == '\n') {
            cursor_x = 0;
            cursor_y++;
            if (cursor_y >= 25) cursor_y = 0;
        } else {
            vga[cursor_y * 80 + cursor_x] = (uint16_t)(*s) | (uint16_t)(VGA_COLOR << 8);
            cursor_x++;
            if (cursor_x >= 80) { cursor_x = 0; cursor_y++; }
        }
        s++;
    }
}

static void put_hex(uint64_t n) {
    const char hex[] = "0123456789ABCDEF";
    char buf[19];
    buf[0] = '0'; buf[1] = 'x'; buf[18] = 0;
    for (int i = 17; i >= 2; i--) { buf[i] = hex[n & 0xF]; n >>= 4; }
    puts(buf);
}

static void put_dec(uint64_t n) {
    char buf[24]; int i = 23; buf[i] = 0;
    if (n == 0) { puts("0"); return; }
    while (n > 0) { buf[--i] = '0' + (n % 10); n /= 10; }
    puts(&buf[i]);
}

/* External functions from assembly */
extern uint64_t pmm_get_total(void);
extern uint64_t pmm_get_free(void);
extern void pmm_init(uint64_t mbi);
extern void paging_init(void);
extern void kheap_init(uint64_t start, uint64_t size);
extern void* kmalloc(uint64_t size);
extern void kfree(void* ptr);
extern uint64_t kheap_get_used(void);
extern uint64_t kheap_get_free(void);
extern void idt_init(void);
extern void pic_init(void);
extern int ps2_keyboard_init(void);
extern void enable_interrupts(void);
extern void ramfs_init(void);
extern void timer_init(void);
extern void process_init(void);

/* Test processes */
static void test_proc_a(void* arg) {
    (void)arg;
    volatile uint16_t* vga = (volatile uint16_t*)0xB8000;
    while (1) {
        vga[2 * 80 + 60] = 'A' | 0x0A00;
        extern void process_yield(void);
        process_yield();
        vga[2 * 80 + 60] = ' ' | 0x0A00;
        process_yield();
    }
}

static void test_proc_b(void* arg) {
    (void)arg;
    volatile uint16_t* vga = (volatile uint16_t*)0xB8000;
    while (1) {
        vga[2 * 80 + 62] = 'B' | 0x0C00;
        extern void process_yield(void);
        process_yield();
        vga[2 * 80 + 62] = ' ' | 0x0C00;
        process_yield();
    }
}

void kernel_main(uint64_t magic, uint64_t mbi) {
    clear();
    
    puts("OpenSYS OS v1.0 (64-bit)\n");
    puts("========================\n\n");
    
    if (magic == 0x36D76289 || magic == 0x2BADB002) {
        puts("[BOOT] Multiboot: OK\n\n");
    } else {
        puts("[BOOT] Multiboot: ");
        put_hex(magic);
        puts("\n\n");
    }
    
    /* Initialize memory */
    puts("[INIT] Physical Memory...\n");
    pmm_init(mbi);
    puts("  Total: ");
    put_dec(pmm_get_total() / (1024 * 1024));
    puts(" MB\n  Free:  ");
    put_dec(pmm_get_free() / (1024 * 1024));
    puts(" MB\n\n");
    
    puts("[INIT] 64-bit Paging...\n");
    paging_init();
    puts("  4-level paging enabled\n\n");
    
    puts("[INIT] Kernel Heap...\n");
    kheap_init(0xFFFF800000000000ULL, 64 * 1024 * 1024);
    puts("  Heap: 64MB at 0xFFFF800000000000\n\n");
    
    /* Test allocations */
    puts("[TEST] Memory Allocation...\n");
    
    void* p1 = kmalloc(128);
    void* p2 = kmalloc(1024);
    
    puts("  kmalloc(128)  = "); put_hex((uint64_t)p1); puts("\n");
    puts("  kmalloc(1024) = "); put_hex((uint64_t)p2); puts("\n");
    
    kfree(p2);
    puts("  kfree(1024) done\n\n");
    
    puts("[DONE] 64-bit memory system ready!\n");

    puts("\n[INIT] Interrupts...\n");
    idt_init();
    puts(" IDT loaded\n");
    
    pic_init();
    puts(" PIC remapped (IRQ 32-47)\n");

    timer_init();
    puts(" Timer initialized (1000 Hz)\n");

    puts("\n[INIT] PS/2 Keyboard...\n");
    if (ps2_keyboard_init() == 0) {
        puts(" PS/2 keyboard initialized\n");
        
        /* Unmask IRQ1 (keyboard) and IRQ0 (timer) */
        __asm__ volatile (
            "inb $0x21, %%al\n"
            "and $0xFC, %%al\n"  /* Enable IRQ0 and IRQ1 */
            "outb %%al, $0x21\n"
            : : : "al"
        );
    } else {
        puts(" PS/2 keyboard FAILED\n");
    }

    puts("\n[INIT] RAM Filesystem...\n");
    ramfs_init();
    puts(" RAM filesystem initialized\n");

    puts("\n[INIT] Process Manager...\n");
    process_init();
    puts(" Process manager initialized\n");

/* Create test processes */
typedef uint64_t pid_t;
typedef void (*proc_entry_t)(void*);
extern pid_t process_create(const char* name, proc_entry_t entry, void* arg);
extern pid_t process_create_user(const char* name, const void* elf_data, size_t elf_size);
process_create("test_a", test_proc_a, 0);
process_create("test_b", test_proc_b, 0);
process_create_user("init", user_bin_data, USER_BIN_SIZE);

puts(" Test processes created\n");

    /* Enable interrupts */
    __asm__ volatile ("sti");
    puts(" Interrupts enabled\n\n");

    puts("[DONE] System ready!\n");
    puts("\nStarting shell...\n\n");

    extern void shell_run(void);
    shell_run();
}
