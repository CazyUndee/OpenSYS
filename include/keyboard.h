#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>
#include "interrupts.h"

// Keyboard controller ports
#define KEYBOARD_DATA_PORT      0x60
#define KEYBOARD_STATUS_PORT    0x64
#define KEYBOARD_COMMAND_PORT   0x64

// Status register bits
#define KEYBOARD_STATUS_OUTPUT  0x01
#define KEYBOARD_STATUS_INPUT   0x02
#define KEYBOARD_STATUS_SYSTEM  0x04
#define KEYBOARD_STATUS_COMMAND 0x08
#define KEYBOARD_STATUS_LOCKED  0x10
#define KEYBOARD_STATUS_AUX     0x20
#define KEYBOARD_STATUS_TIMEOUT 0x40
#define KEYBOARD_STATUS_PARITY  0x80

// Commands
#define KEYBOARD_CMD_LED        0xED
#define KEYBOARD_CMD_ACK        0xFA
#define KEYBOARD_CMD_RESEND     0xFE

// Scancode set 1 (US QWERTY)
extern const char keyboard_scancode_set1[128];
extern const char keyboard_scancode_set1_shift[128];

void keyboard_init(void);
uint8_t keyboard_read_scan(void);
char keyboard_getchar(void);
int keyboard_has_data(void);
void keyboard_handler(struct cpu_state* regs);

#endif
