/*
 * kernel.c - OpenCode OS main kernel
 */

#include <stdint.h>
#include "../include/pmm.h"
#include "../include/paging.h"
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
    
    puts("OpenCode OS v0.3\n");
    puts("================\n\n");
    
    /* Initialize physical memory manager */
    puts("[INIT] Physical Memory Manager...\n");
    pmm_init(mbi_addr);
    
    puts("  Total: ");
    put_dec(pmm_get_total() / (1024 * 1024));
    puts(" MB\n");
    puts("  Free:  ");
    put_dec(pmm_get_free() / (1024 * 1024));
    puts(" MB\n\n");
    
    /* Initialize paging */
    puts("[INIT] Paging...\n");
    
    /* CR0 before paging */
    uint32_t cr0_before;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0_before));
    
    paging_init();
    
    /* CR0 after paging */
    uint32_t cr0_after;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0_after));
    
    puts("  CR0 before: ");
    put_hex(cr0_before);
    puts("\n");
    puts("  CR0 after:  ");
    put_hex(cr0_after);
    puts("\n");
    puts("  Paging enabled: ");
    puts((cr0_after & 0x80000000) ? "YES" : "NO");
    puts("\n\n");
    
    /* Test virtual memory */
    puts("[TEST] Virtual Memory...\n");
    
    /* Allocate a page at a high virtual address */
    uint32_t test_vaddr = 0xC0000000;  /* 3GB mark */
    void* test_page = paging_alloc(test_vaddr, PTE_WRITABLE | PTE_PRESENT);
    
    if (test_page) {
        puts("  Mapped vaddr: ");
        put_hex(test_vaddr);
        puts(" -> paddr: ");
        put_hex(paging_get_physical(test_vaddr));
        puts("\n");
        puts("  Status:       MAPPED\n");
    } else {
        puts("  FAILED to allocate test page\n");
    }
    
    puts("\n[DONE] Virtual memory working!\n");
    puts("Halting.\n");
    
    while (1) {
        __asm__ volatile("hlt");
    }
}