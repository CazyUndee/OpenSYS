/*
 * pic.c - 8259 Programmable Interrupt Controller
 */

#include <stdint.h>

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

#define PIC_EOI   0x20

#define ICW1_INIT 0x10
#define ICW1_ICW4 0x01
#define ICW4_8086 0x01

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void pic_remap(int offset1, int offset2) {
    outb(PIC1_CMD, ICW1_INIT | ICW1_ICW4);
    outb(PIC2_CMD, ICW1_INIT | ICW1_ICW4);
    outb(PIC1_DATA, offset1);
    outb(PIC2_DATA, offset2);
    outb(PIC1_DATA, 4);
    outb(PIC2_DATA, 2);
    outb(PIC1_DATA, ICW4_8086);
    outb(PIC2_DATA, ICW4_8086);
}

void pic_mask(uint8_t mask1, uint8_t mask2) {
    outb(PIC1_DATA, mask1);
    outb(PIC2_DATA, mask2);
}

void pic_eoi(int irq) {
    outb(PIC1_CMD, PIC_EOI);
    if (irq >= 8) {
        outb(PIC2_CMD, PIC_EOI);
    }
}

void pic_init(void) {
    pic_remap(32, 40);
    pic_mask(0xFF, 0xFF);
}
