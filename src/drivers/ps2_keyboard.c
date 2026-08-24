/*
 * ps2_keyboard.c - PS/2 Keyboard Driver
 *
 * Handles PS/2 keyboard input using IRQ1.
 * Provides init, polling, and interrupt-driven key input.
 *
 * Copyright (C) 2026 CazyUndee
 * SPDX-License-Identifier: AGPL-3.0
 */

#include <stdint.h>
#include "ps2_keyboard.h"
#include "io.h"

/* PS/2 Controller I/O ports */
#define PS2_DATA_PORT    0x60
#define PS2_STATUS_PORT  0x64
#define PS2_COMMAND_PORT 0x64

/* PS/2 Controller commands */
#define PS2_CMD_READ_CONFIG   0x20
#define PS2_CMD_WRITE_CONFIG  0x60
#define PS2_CMD_TEST_PORT1    0xAB
#define PS2_CMD_RESET         0xFF

/* PS/2 Controller status bits */
#define PS2_STATUS_OUTPUT_FULL  0x01
#define PS2_STATUS_INPUT_FULL   0x02

/* PS/2 Controller configuration byte bits */
#define PS2_CONFIG_PORT1_IRQ    0x01
#define PS2_CONFIG_PORT2_IRQ    0x02
#define PS2_CONFIG_TRANSLATION  0x40

/* Key buffer size */
#define KEY_BUFFER_SIZE 256
#define SCAN_CODE_COUNT 128

/* Scan code set 1 to ASCII mapping (unshifted) */
static const char sc1_to_ascii[SCAN_CODE_COUNT] = {
/* 0x00 */ 0,  27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
/* 0x0E */ '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
/* 0x1C */ 0,  'a','s','d','f','g','h','j','k','l',';','\'','`',
/* 0x29 */ 0,  '\\','z','x','c','v','b','n','m',',','.','/',
/* 0x36 */ 0,  '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, /* numpad */
/* 0x43 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x43-0x52 */
/* 0x53 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x53-0x62 */
/* 0x63 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x63-0x72 */
/* 0x73 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0   /* 0x73-0x7F */
};

/* Scan code set 1 to ASCII mapping (shifted) */
static const char sc1_to_ascii_shift[SCAN_CODE_COUNT] = {
/* 0x00 */ 0,  27, '!','@','#','$','%','^','&','*','(',')','_','+', '\b',
/* 0x0E */ '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
/* 0x1C */ 0,  'A','S','D','F','G','H','J','K','L',':','"','~',
/* 0x29 */ 0,  '|','Z','X','C','V','B','N','M','<','>','?',
/* 0x36 */ 0,  '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, /* numpad */
/* 0x43 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* 0x53 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* 0x63 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* 0x73 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/* Key buffer (ring buffer) */
static char key_buffer[KEY_BUFFER_SIZE];
static volatile int key_head = 0;
static volatile int key_tail = 0;

/* Modifier state */
static int shift_pressed = 0;
static int ext_pending = 0;  /* an 0xE0 prefix arrived; next make is an extended key */
static int ctrl_pressed = 0;
static int alt_pressed = 0;
static int caps_lock = 0;

/* Initialized flag */
static int ps2_initialized = 0;

/* ---- I/O helpers ---- */

static inline void outb_ps2(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb_ps2(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* Wait for PS/2 controller output buffer full */
static int ps2_wait_output(void) {
    for (int i = 0; i < 100000; i++) {
        if (inb_ps2(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) {
            return 0;
        }
    }
    return -1;
}

/* Wait for PS/2 controller input buffer empty */
static int ps2_wait_input(void) {
    for (int i = 0; i < 100000; i++) {
        if (!(inb_ps2(PS2_STATUS_PORT) & PS2_STATUS_INPUT_FULL)) {
            return 0;
        }
    }
    return -1;
}

/* Write a command to the PS/2 controller */
static void ps2_write_command(uint8_t cmd) {
    ps2_wait_input();
    outb_ps2(PS2_COMMAND_PORT, cmd);
}

/* Write data to the PS/2 data port (after controller command) */
static void ps2_write_data(uint8_t data) {
    ps2_wait_input();
    outb_ps2(PS2_DATA_PORT, data);
}

/* Read from PS/2 data port */
static uint8_t ps2_read_data(void) {
    ps2_wait_output();
    return inb_ps2(PS2_DATA_PORT);
}

/* Add character to ring buffer */
static void buffer_put(char c) {
    int next = (key_head + 1) % KEY_BUFFER_SIZE;
    if (next != key_tail) {
        key_buffer[key_head] = c;
        key_head = next;
    }
}

/* ---- Public API ---- */

int ps2_keyboard_init(void) {
    /* Flush the output buffer */
    while (inb_ps2(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) {
        inb_ps2(PS2_DATA_PORT);
    }

    /* Disable ports and IRQs during init */
    ps2_write_command(0xAD);  /* Disable port 1 */
    ps2_write_command(0xA7);  /* Disable port 2 */

    /* Flush again */
    while (inb_ps2(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) {
        inb_ps2(PS2_DATA_PORT);
    }

    /* Read configuration byte */
    ps2_write_command(PS2_CMD_READ_CONFIG);
    uint8_t config = ps2_read_data();

    /* Disable IRQs and translation for now */
    config &= ~(PS2_CONFIG_PORT1_IRQ | PS2_CONFIG_PORT2_IRQ);
    config &= ~PS2_CONFIG_TRANSLATION;

    /* Write back config */
    ps2_write_command(PS2_CMD_WRITE_CONFIG);
    ps2_write_data(config);

    /* Controller self-test */
    ps2_write_command(0xAA);
    (void)ps2_read_data();  /* 0x55 = pass */

    /* Enable port 1 */
    ps2_write_command(0xAE);

    /* Port 1 interface test */
    ps2_write_command(PS2_CMD_TEST_PORT1);
    uint8_t port_test = ps2_read_data();
    if (port_test != 0x00) {
        return -1;
    }

    /* Reset keyboard */
    ps2_write_data(PS2_CMD_RESET);
    for (int i = 0; i < 100000; i++) {
        if (inb_ps2(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) {
            uint8_t resp = inb_ps2(PS2_DATA_PORT);
            if (resp == 0xFA) break;
            if (resp == 0xFE) ps2_write_data(PS2_CMD_RESET);
        }
    }

    /* Wait for keyboard to initialize */
    for (volatile int i = 0; i < 100000; i++);

    /* Re-read config and enable IRQ + scancode-set translation.
     * Translation must stay ON: keyboards (and QEMU's emulation) send
     * scancode set 2 by default; the controller converts it to set 1,
     * which the scan tables below use. With translation off, set-2
     * codes (and the 0xF0 break prefix) get misread as set-1 makes. */
    ps2_write_command(PS2_CMD_READ_CONFIG);
    config = ps2_read_data();
    config |= PS2_CONFIG_PORT1_IRQ;
    config &= ~PS2_CONFIG_PORT2_IRQ;
    config |= PS2_CONFIG_TRANSLATION;
    ps2_write_command(PS2_CMD_WRITE_CONFIG);
    ps2_write_data(config);

    /* Flush any stale bytes */
    while (inb_ps2(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) {
        inb_ps2(PS2_DATA_PORT);
    }

    key_head = 0;
    key_tail = 0;
    shift_pressed = 0;
    ctrl_pressed = 0;
    alt_pressed = 0;
    caps_lock = 0;

    ps2_initialized = 1;
    return 0;
}

int ps2_keyboard_has_key(void) {
    return key_head != key_tail;
}

char ps2_keyboard_getc(void) {
    if (!ps2_initialized) return 0;
    if (key_head == key_tail) return 0;

    char c = key_buffer[key_tail];
    key_tail = (key_tail + 1) % KEY_BUFFER_SIZE;
    return c;
}

char ps2_keyboard_getc_block(void) {
    while (!ps2_keyboard_has_key()) {
        __asm__ volatile ("hlt");
    }
    return ps2_keyboard_getc();
}

/* Handle keyboard interrupt (IRQ1) - called from interrupts.asm */
void ps2_keyboard_handler(void) {
    /* Guard against spurious IRQs: only read the data port when the
     * controller actually has a byte (reading an empty queue can return
     * stale data on some emulators). */
    if (!(inb_ps2(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL)) {
        return;
    }

    uint8_t scancode = inb_ps2(PS2_DATA_PORT);

    /* Extended (0xE0-prefixed) keys: arrows reach the shell as control
     * codes (0x01 up, 0x02 down); other extended keys are ignored. */
    if (scancode == 0xE0) {
        ext_pending = 1;
        return;
    }

    /* Ignore key release (bit 7 set for scan code set 1) */
    if (scancode & 0x80) {
        if (ext_pending) { ext_pending = 0; return; }
        uint8_t released = scancode & 0x7F;
        if (released == 0x2A || released == 0x36) shift_pressed = 0;
        if (released == 0x1D) ctrl_pressed = 0;
        if (released == 0x38) alt_pressed = 0;
        return;
    }

    /* Extended key make code (after an 0xE0 prefix). */
    if (ext_pending) {
        ext_pending = 0;
        if (scancode == 0x48) buffer_put('\x01');  /* Up arrow */
        else if (scancode == 0x50) buffer_put('\x02');  /* Down arrow */
        return;
    }

    /* Track modifier presses */
    if (scancode == 0x2A || scancode == 0x36) { shift_pressed = 1; return; }
    if (scancode == 0x1D) { ctrl_pressed = 1; return; }
    if (scancode == 0x38) { alt_pressed = 1; return; }
    if (scancode == 0x3A) { caps_lock = !caps_lock; return; }

    if (scancode < SCAN_CODE_COUNT) {
        char c = shift_pressed ? sc1_to_ascii_shift[scancode]
                               : sc1_to_ascii[scancode];

        /* Apply caps lock to letters */
        if (caps_lock && c >= 'a' && c <= 'z') c = c - 'a' + 'A';
        else if (caps_lock && c >= 'A' && c <= 'Z') c = c - 'A' + 'a';

        /* Ctrl+C */
        if (ctrl_pressed && (c == 'c' || c == 'C')) {
            buffer_put('\x03');
            return;
        }

        if (c) buffer_put(c);
    }
}
