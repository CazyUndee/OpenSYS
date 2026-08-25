/*
 * serial.c - Serial Port Driver (COM1)
 *
 * Provides output to COM1 (0x3F8) for QEMU -serial stdio.
 * Used for debug logging and test output.
 */

#include <stdint.h>
#include "serial.h"
#include "io.h"

#define COM1 0x3F8

#define SERIAL_DATA 0
#define SERIAL_IER  1
#define SERIAL_FCR  2
#define SERIAL_LCR  3
#define SERIAL_MCR  4
#define SERIAL_LSR  5

#define LSR_TX_EMPTY 0x20

static int serial_is_ready(void) {
    return inb(COM1 + SERIAL_LSR) & LSR_TX_EMPTY;
}

void serial_init(void) {
    outb(COM1 + SERIAL_IER, 0x00);
    outb(COM1 + SERIAL_LCR, 0x80);
    outb(COM1 + SERIAL_DATA, 0x01);
outb(COM1 + SERIAL_IER, 0x00); // Divisor high byte = 0
 outb(COM1 + SERIAL_LCR, 0x03);
    outb(COM1 + SERIAL_FCR, 0xC7);
    outb(COM1 + SERIAL_MCR, 0x0B);
    outb(COM1 + SERIAL_IER, 0x01);
}

void serial_putchar(char c) {
    while (!serial_is_ready());
    outb(COM1 + SERIAL_DATA, (uint8_t)c);
}

void serial_write(const char* data, size_t size) {
    for (size_t i = 0; i < size; i++) {
        if (data[i] == '\n') serial_putchar('\r');
        serial_putchar(data[i]);
    }
}

void serial_writestring(const char* data) {
    while (*data) {
        if (*data == '\n') serial_putchar('\r');
        serial_putchar(*data++);
    }
}

int serial_received(void) {
    return inb(COM1 + SERIAL_LSR) & 0x01;
}

char serial_getchar(void) {
    while (!serial_received());
    return (char)inb(COM1 + SERIAL_DATA);
}
