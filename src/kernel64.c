/*
 * kernel.c - OpenSYS OS main kernel (64-bit)
 */

#include <stdint.h>

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
            }
        }
        s++;
    }
}

static void put_hex(uint64_t n) {
    const char hex[] = "0123456789ABCDEF";
    char buf[19];
    buf[0] = '0';
    buf[1] = 'x';
    buf[18] = 0;
    for (int i = 17; i >= 2; i--) {
        buf[i] = hex[n & 0xF];
        n >>= 4;
    }
    puts(buf);
}

static void put_dec(uint64_t n) {
    char buf[24];
    int i = 23;
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

void kernel_main(uint64_t magic, uint64_t mbi) {
    (void)mbi;
    
    clear();
    
    puts("OpenSYS OS v1.0 (64-bit)\n");
    puts("========================\n\n");
    
    /* Check multiboot */
    if (magic == 0x36D76289 || magic == 0x2BADB002) {
        puts("[BOOT] Multiboot: OK\n");
    } else {
        puts("[BOOT] Multiboot: UNKNOWN\n");
        puts("       Magic: ");
        put_hex(magic);
        puts("\n");
    }
    
    /* CPU detection */
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0));
    
    puts("\n[CPU] Vendor: ");
    char vendor[13];
    ((uint32_t*)vendor)[0] = ebx;
    ((uint32_t*)vendor)[1] = edx;
    ((uint32_t*)vendor)[2] = ecx;
    vendor[12] = 0;
    puts(vendor);
    puts("\n");
    
    /* Check for long mode */
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x80000001));
    if (edx & (1 << 29)) {
        puts("[CPU] Long Mode: SUPPORTED\n");
    } else {
        puts("[CPU] Long Mode: NOT SUPPORTED!\n");
    }
    
    puts("\n[MEM] Kernel loaded at: ");
    put_hex(0x200000);
    puts("\n");
    
    /* Test 64-bit arithmetic */
    uint64_t a = 0xFFFFFFFF;
    uint64_t b = 1;
    uint64_t c = a + b;
    
    puts("\n[TEST] 64-bit arithmetic:\n");
    puts("       0xFFFFFFFF + 1 = ");
    put_hex(c);
    puts("\n");
    
    if (c == 0x100000000ULL) {
        puts("       Result: CORRECT\n\n");
    } else {
        puts("       Result: WRONG\n\n");
    }
    
    puts("[DONE] 64-bit kernel running!\n");
    puts("Ready for 64-bit memory management.\n");
    
    while (1) {
        __asm__ volatile("hlt");
    }
}
