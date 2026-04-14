/*
 * gdt64.c - Global Descriptor Table for 64-bit
 */

#include <stdint.h>

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct gdt_entry gdt[6];
static struct gdt_ptr gdtr;

/* TSS defined in tss.c */
extern void* tss;

void gdt64_init(void) {
    /* Null descriptor */
    gdt[0].limit_low = 0;
    gdt[0].base_low = 0;
    gdt[0].base_middle = 0;
    gdt[0].access = 0;
    gdt[0].granularity = 0;
    gdt[0].base_high = 0;
    
    /* Kernel code (selector 0x08) */
    gdt[1].limit_low = 0;
    gdt[1].base_low = 0;
    gdt[1].base_middle = 0;
    gdt[1].access = 0x9A;      /* Present, ring 0, code, executable */
    gdt[1].granularity = 0xAF; /* 64-bit, limit = 0 */
    gdt[1].base_high = 0;
    
    /* Kernel data (selector 0x10) */
    gdt[2].limit_low = 0;
    gdt[2].base_low = 0;
    gdt[2].base_middle = 0;
    gdt[2].access = 0x92;      /* Present, ring 0, data, writable */
    gdt[2].granularity = 0xCF;
    gdt[2].base_high = 0;
    
    /* User code (selector 0x18, RPL=3 = 0x1B) */
    gdt[3].limit_low = 0;
    gdt[3].base_low = 0;
    gdt[3].base_middle = 0;
    gdt[3].access = 0xFA;      /* Present, ring 3, code, executable */
    gdt[3].granularity = 0xAF; /* 64-bit */
    gdt[3].base_high = 0;
    
    /* User data (selector 0x20, RPL=3 = 0x23) */
    gdt[4].limit_low = 0;
    gdt[4].base_low = 0;
    gdt[4].base_middle = 0;
    gdt[4].access = 0xF2;      /* Present, ring 3, data, writable */
    gdt[4].granularity = 0xCF;
    gdt[4].base_high = 0;
    
    /* TSS (selector 0x28) */
    uint64_t tss_addr = (uint64_t)&tss;
    gdt[5].limit_low = sizeof(tss) - 1;
    gdt[5].base_low = tss_addr & 0xFFFF;
    gdt[5].base_middle = (tss_addr >> 16) & 0xFF;
    gdt[5].access = 0x89;      /* Present, TSS */
    gdt[5].granularity = 0x00;
    gdt[5].base_high = (tss_addr >> 24) & 0xFF;
    
    gdtr.limit = sizeof(gdt) - 1;
    gdtr.base = (uint64_t)&gdt;
    
    __asm__ volatile ("lgdt %0" : : "m"(gdtr));
    
    /* Load TSS */
    __asm__ volatile ("ltr %0" : : "r"((uint16_t)0x28));
}
