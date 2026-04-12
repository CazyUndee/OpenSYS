/*
 * kernel.c - OpenSYS OS main kernel
 */

#include <stdint.h>
#include "../include/pmm.h"
#include "../include/paging.h"
#include "../include/kheap.h"
#include "../include/multiboot.h"

#define VGA_BUFFER 0xB8000
#define VGA_COLOR 0x07

static volatile uint16_t* vga = (volatile uint16_t*)VGA_BUFFER;
static int cursor_x = 0;
static int cursor_y = 0;

static void clear(void) {
    for (int i = 0; i < 80 * 25; i++) {
        vga[i] = (uint16_t)' ' | (uint16_t)(VGA_COLOR << 8);
    }
    cursor_x = 0;
    cursor_y = 0;
    vga = (volatile uint16_t*)VGA_BUFFER;
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
            if (cursor_x >= 80) {
                cursor_x = 0;
                cursor_y++;
                if (cursor_y >= 25) cursor_y = 0;
            }
        }
        s++;
    }
}

static void put_hex(uint32_t n) {
    const char hex[] = "0123456789ABCDEF";
    char buf[11];
    buf[0] = '0';
    buf[1] = 'x';
    buf[10] = 0;
    for (int i = 9; i >= 2; i--) {
        buf[i] = hex[n & 0xF];
        n >>= 4;
    }
    puts(buf);
}

static void put_dec(uint32_t n) {
    char buf[12];
    int i = 11;
    buf[i] = 0;
    
    if (n == 0) {
        puts("0");
        return;
    }
    
    while (n > 0) {
        buf[--i] = '0' + (n % 10);
        n /= 10;
    }
    puts(&buf[i]);
}

void kernel_main(uint32_t magic, uint32_t mbi_addr) {
    clear();
    
    if (magic != MBOOT_MAGIC) {
        puts("ERROR: Invalid multiboot magic!\n");
        puts("Halting.\n");
        while (1) __asm__ volatile("hlt");
    }
    
    puts("OpenSYS OS v0.4\n");
    puts("================\n\n");
    
    /* Initialize physical memory manager */
    puts("[INIT] Physical Memory Manager...\n");
    pmm_init(mbi_addr);
    puts("  Total: ");
    put_dec(pmm_get_total() / (1024 * 1024));
    puts(" MB\n  Free:  ");
    put_dec(pmm_get_free() / (1024 * 1024));
    puts(" MB\n\n");
    
    /* Initialize paging */
    puts("[INIT] Paging...\n");
    paging_init();
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    puts("  Paging: ");
    puts((cr0 & 0x80000000) ? "ENABLED\n\n" : "FAILED\n\n");
    
    /* Initialize heap */
    puts("[INIT] Kernel Heap...\n");
    kheap_init(0x40000000, 16 * 1024 * 1024);
    puts("  Heap at 0x40000000 (16MB)\n\n");
    
    /* Test heap allocator */
    puts("[TEST] Heap Allocator...\n");
    
    void* ptr1 = kmalloc(128);
    void* ptr2 = kmalloc(256);
    void* ptr3 = kmalloc(512);
    
    puts("  kmalloc(128) = ");
    put_hex((uint32_t)ptr1);
    puts("\n  kmalloc(256) = ");
    put_hex((uint32_t)ptr2);
    puts("\n  kmalloc(512) = ");
    put_hex((uint32_t)ptr3);
    puts("\n");
    
    /* Write test */
    if (ptr1) {
        char* str = (char*)ptr1;
        str[0] = 'H'; str[1] = 'E'; str[2] = 'L';
        str[3] = 'L'; str[4] = 'O'; str[5] = 0;
        puts("  Write 'HELLO' -> ");
        puts(ptr1);
        puts("\n");
    }
    
    puts("\n  Used: ");
    put_dec(kheap_get_used());
    puts(" bytes\n");
    
    kfree(ptr2);
    void* ptr4 = kmalloc(64);
    puts("  kfree(256), kmalloc(64) = ");
    put_hex((uint32_t)ptr4);
    puts("\n\n");
    
    puts("[DONE] Memory system complete!\n");
    puts("Ready for cool stuff.\n");
    
    while (1) {
        __asm__ volatile("hlt");
    }
}
