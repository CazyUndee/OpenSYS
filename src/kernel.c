/*
 * kernel.c - OpenCode OS main kernel
 */

#include <stdint.h>
#include "../include/pmm.h"
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
        } else {
            vga[cursor_y * 80 + cursor_x] = (uint16_t)(*s) | (uint16_t)(VGA_COLOR << 8);
            cursor_x++;
            if (cursor_x >= 80) {
                cursor_x = 0;
                cursor_y++;
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
        puts("Expected: 0x2BADB002, Got: ");
        put_hex(magic);
        puts("\nHalting.\n");
        while (1) __asm__ volatile("hlt");
    }
    
    puts("OpenCode OS v0.2\n");
    puts("===============\n\n");
    
    struct mboot_info* mbi = (struct mboot_info*)mbi_addr;
    
    /* Show boot info */
    puts("[BOOT] Multiboot info at: ");
    put_hex(mbi_addr);
    puts("\n");
    
    if (mbi->flags & MBOOT_FLAG_MEM) {
        puts("[MEM ] Lower memory: ");
        put_dec(mbi->mem_lower);
        puts(" KB\n");
        puts("[MEM ] Upper memory: ");
        put_dec(mbi->mem_upper);
        puts(" KB\n");
    }
    
    /* Initialize physical memory manager */
    puts("[INIT] Initializing PMM...\n");
    pmm_init(mbi_addr);
    
    puts("[MEM ] Total memory: ");
    put_dec(pmm_get_total() / (1024 * 1024));
    puts(" MB\n");
    
    puts("[MEM ] Free memory: ");
    put_dec(pmm_get_free() / (1024 * 1024));
    puts(" MB\n");
    
    /* Test allocation */
    puts("\n[TEST] Allocating pages...\n");
    
    void* page1 = pmm_alloc_page();
    void* page2 = pmm_alloc_page();
    void* page3 = pmm_alloc_page();
    
    puts("  Page 1: ");
    put_hex((uint32_t)page1);
    puts("\n  Page 2: ");
    put_hex((uint32_t)page2);
    puts("\n  Page 3: ");
    put_hex((uint32_t)page3);
    puts("\n");
    
    puts("\n[MEM ] Free after alloc: ");
    put_dec(pmm_get_free() / (1024 * 1024));
    puts(" MB\n");
    
    pmm_free_page(page1);
    pmm_free_page(page2);
    
    puts("[MEM ] Free after free: ");
    put_dec(pmm_get_free() / (1024 * 1024));
    puts(" MB\n");
    
    puts("\n[DONE] Memory management working!\n");
    puts("Halting.\n");
    
    while (1) {
        __asm__ volatile("hlt");
    }
}
