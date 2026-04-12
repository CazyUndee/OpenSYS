/*
 * kernel.c - OpenSYS OS main kernel
 */

#include <stdint.h>
#include "../include/pmm.h"
#include "../include/paging.h"
#include "../include/kheap.h"
#include "../include/multiboot.h"
#include "../include/disk.h"
#include "../include/gpt.h"

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

static void put_hex64(uint64_t n) {
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

void kernel_main(uint32_t magic, uint32_t mbi_addr) {
    clear();
    
    if (magic != MBOOT_MAGIC) {
        puts("ERROR: Invalid multiboot magic!\n");
        while (1) __asm__ volatile("hlt");
    }
    
    puts("OpenSYS OS v0.5\n");
    puts("================\n\n");
    
    /* Initialize memory */
    puts("[INIT] Physical Memory Manager...\n");
    pmm_init(mbi_addr);
    puts("  Total: ");
    put_dec(pmm_get_total() / (1024 * 1024));
    puts(" MB\n\n");
    
    puts("[INIT] Paging...\n");
    paging_init();
    puts("  Paging enabled\n\n");
    
    puts("[INIT] Kernel Heap...\n");
    kheap_init(0x40000000, 16 * 1024 * 1024);
    puts("  Heap ready\n\n");
    
    /* Initialize disk */
    puts("[INIT] Disk Driver...\n");
    if (disk_init() < 0) {
        puts("  No disk detected!\n\n");
    } else {
        const disk_info_t* info = disk_get_info();
        puts("  Model: ");
        puts(info->model);
        puts("\n  Size:  ");
        put_dec(disk_get_size() / (1024 * 1024));
        puts(" MB\n\n");
    }
    
    /* Initialize GPT */
    puts("[INIT] GPT Partition Table...\n");
    disk_ops_t ops = { disk_read, disk_write };
    
    if (gpt_init(&ops) < 0) {
        puts("  No GPT found (unpartitioned or MBR)\n\n");
    } else {
        puts("  GPT found!\n");
        puts("  Partitions: ");
        put_dec(gpt_get_partition_count());
        puts("\n\n");
        
        puts("  Partition List:\n");
        for (uint32_t i = 0; i < gpt_get_partition_count(); i++) {
            const gpt_entry_t* part = gpt_get_partition(i);
            if (part) {
                puts("    [");
                put_dec(i);
                puts("] ");
                put_dec(gpt_get_partition_size(i) / (1024 * 1024));
                puts(" MB\n");
            }
        }
    }
    
    puts("\n[DONE] GPT driver ready!\n");
    
    while (1) __asm__ volatile("hlt");
}
