/*
 * kernel.c - OpenCode OS main kernel
 */

#include <stdint.h>

#define VGA_BUFFER 0xB8000
#define VGA_COLOR 0x07  // Light grey on black

static volatile uint16_t* vga = (volatile uint16_t*)VGA_BUFFER;

static void puts(const char* s) {
    while (*s) {
        *vga++ = (uint16_t)(*s) | (uint16_t)(VGA_COLOR << 8);
        s++;
    }
}

void kernel_main(uint32_t magic, uint32_t mbi) {
    (void)mbi;
    
    // Clear screen
    for (int i = 0; i < 80 * 25; i++) {
        vga[i] = (uint16_t)' ' | (uint16_t)(VGA_COLOR << 8);
    }
    vga = (volatile uint16_t*)VGA_BUFFER;
    
    // Check multiboot
    if (magic == 0x2BADB002) {
        puts("OpenCode OS v0.1 - Multiboot OK");
    } else {
        puts("OpenCode OS v0.1 - NO MULTIBOOT");
    }
    
    // Halt
    while (1) {
        __asm__ volatile ("hlt");
    }
}
