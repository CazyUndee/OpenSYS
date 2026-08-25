/*
 * gdt64.c - Global Descriptor Table for 64-bit
 */

#include <stdint.h>
#include "tss.h"

struct gdt_entry {
uint16_t limit_low;
uint16_t base_low;
uint8_t base_middle;
uint8_t access;
uint8_t granularity;
uint8_t base_high;
} __attribute__((packed));

struct gdt_ptr {
uint16_t limit;
uint64_t base;
} __attribute__((packed));

static struct gdt_entry gdt[8];
static struct gdt_ptr gdtr;

void gdt64_init(void) {
/* `tss` is a pointer to the heap-allocated TSS.  Use the TSS itself,
 * not the address of the pointer variable. */
uint64_t tss_addr = tss ? (uint64_t)tss : 0;
uint32_t tss_limit = tss_size() - 1;

gdt[0].limit_low = 0; gdt[0].base_low = 0; gdt[0].base_middle = 0;
gdt[0].access = 0; gdt[0].granularity = 0; gdt[0].base_high = 0;

gdt[1].limit_low = 0; gdt[1].base_low = 0; gdt[1].base_middle = 0;
gdt[1].access = 0x9A; gdt[1].granularity = 0xAF; gdt[1].base_high = 0;

gdt[2].limit_low = 0; gdt[2].base_low = 0; gdt[2].base_middle = 0;
gdt[2].access = 0x92; gdt[2].granularity = 0xCF; gdt[2].base_high = 0;

gdt[3].limit_low = 0; gdt[3].base_low = 0; gdt[3].base_middle = 0;
gdt[3].access = 0xFA; gdt[3].granularity = 0xAF; gdt[3].base_high = 0;

gdt[4].limit_low = 0; gdt[4].base_low = 0; gdt[4].base_middle = 0;
gdt[4].access = 0xF2; gdt[4].granularity = 0xCF; gdt[4].base_high = 0;

gdt[5].limit_low = tss_limit & 0xFFFF;
gdt[5].base_low = tss_addr & 0xFFFF;
gdt[5].base_middle = (tss_addr >> 16) & 0xFF;
gdt[5].access = 0x89;
gdt[5].granularity = 0x00;
gdt[5].base_high = (tss_addr >> 24) & 0xFF;

gdt[6].limit_low = (tss_addr >> 32) & 0xFFFF;   /* base[47:32] */
gdt[6].base_low = (tss_addr >> 48) & 0xFFFF;    /* base[63:48] — upper half of the high dword */
gdt[6].base_middle = 0;
gdt[6].access = 0;
gdt[6].granularity = 0;
gdt[6].base_high = 0;

gdt[7].limit_low = 0; gdt[7].base_low = 0; gdt[7].base_middle = 0;
gdt[7].access = 0; gdt[7].granularity = 0; gdt[7].base_high = 0;

gdtr.limit = sizeof(gdt) - 1;
gdtr.base = (uint64_t)&gdt;

__asm__ volatile ("lgdt %0" : : "m"(gdtr));
__asm__ volatile ("ltr %0" : : "r"((uint16_t)0x28));
}
