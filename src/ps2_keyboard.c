/*
 * ps2_keyboard.c - PS/2 Keyboard Driver
 *
 * Uses i8042 controller on ports 0x60/0x64
 * IRQ1 handler for keyboard interrupts
 */

#include <stdint.h>

#define PS2_DATA    0x60
#define PS2_STATUS  0x64
#define PS2_CMD     0x64

/* Status register bits */
#define PS2_STATUS_OUTPUT_FULL  0x01
#define PS2_STATUS_INPUT_FULL   0x02

/* Keyboard commands */
#define PS2_KB_ENABLE_SCANNING  0xF4
#define PS2_KB_DISABLE_SCANNING 0xF5
#define PS2_KB_SET_SCANCODE     0xF0
#define PS2_KB_IDENTIFY         0xF2
#define PS2_KB_RESET            0xFF

/* Controller commands */
#define PS2_CMD_READ_CONFIG     0x20
#define PS2_CMD_WRITE_CONFIG    0x60
#define PS2_CMD_DISABLE_PORT2   0xA7
#define PS2_CMD_ENABLE_PORT2    0xA8
#define PS2_CMD_DISABLE_PORT1   0xAD
#define PS2_CMD_ENABLE_PORT1    0xAE
#define PS2_CMD_SELF_TEST       0xAA
#define PS2_CMD_PORT1_TEST      0xAB
#define PS2_CMD_PORT2_TEST      0xA9

/* Config byte bits */
#define PS2_CONFIG_PORT1_IRQ    0x01
#define PS2_CONFIG_PORT2_IRQ    0x02
#define PS2_CONFIG_PORT1_CLOCK  0x10
#define PS2_CONFIG_PORT2_CLOCK  0x20
#define PS2_CONFIG_TRANSLATION  0x40

/* Key buffer */
#define KEY_BUFFER_SIZE 64
static char key_buffer[KEY_BUFFER_SIZE];
static volatile int key_head = 0;
static volatile int key_tail = 0;

/* Modifier state */
static volatile uint8_t modifiers = 0;
#define MOD_LSHIFT 0x01
#define MOD_RSHIFT 0x02
#define MOD_LCTRL  0x04
#define MOD_RCTRL  0x08
#define MOD_LALT   0x10
#define MOD_RALT   0x20
#define MOD_CAPS   0x40

/* LEDs */
static uint8_t led_state = 0;
#define LED_SCROLL 0x01
#define LED_NUM    0x02
#define LED_CAPS   0x04

/* I/O ports */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* Wait for controller ready */
static int ps2_wait_read(void) {
    for (int i = 0; i < 10000; i++) {
        if (inb(PS2_STATUS) & PS2_STATUS_OUTPUT_FULL)
            return 1;
    }
    return 0;
}

static int ps2_wait_write(void) {
    for (int i = 0; i < 10000; i++) {
        if (!(inb(PS2_STATUS) & PS2_STATUS_INPUT_FULL))
            return 1;
    }
    return 0;
}

/* Send command to controller */
static int ps2_controller_cmd(uint8_t cmd) {
    if (!ps2_wait_write()) return -1;
    outb(PS2_CMD, cmd);
    return 0;
}

/* Read from controller */
static int ps2_read(void) {
    if (!ps2_wait_read()) return -1;
    return inb(PS2_DATA);
}

/* Write to controller */
static int ps2_write(uint8_t data) {
    if (!ps2_wait_write()) return -1;
    outb(PS2_DATA, data);
    return 0;
}

/* Send command to keyboard with ACK check */
static int kb_command(uint8_t cmd) {
    for (int retry = 0; retry < 3; retry++) {
        if (ps2_write(cmd) < 0) return -1;
        int resp = ps2_read();
        if (resp == 0xFA) return 0;  /* ACK */
        if (resp == 0xFE) continue;  /* Resend */
        return -1;
    }
    return -1;
}

/* US keyboard layout (set 1 scancodes) */
static const char scancode_to_ascii[128] = {
    0,   0,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t','q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,   'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,   '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, '7', '8', '9', '-', '4', '5', '6', '+', '1',
    '2', '3', '0', '.', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const char scancode_to_ascii_shift[128] = {
    0,   0,  '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t','Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,   'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,   '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, '7', '8', '9', '-', '4', '5', '6', '+', '1',
    '2', '3', '0', '.', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/* Add key to buffer */
static void buffer_put(char c) {
    int next = (key_head + 1) % KEY_BUFFER_SIZE;
    if (next != key_tail) {
        key_buffer[key_head] = c;
        key_head = next;
    }
}

/* Handle keyboard interrupt (called from assembly) */
void ps2_keyboard_handler(void) {
    uint8_t scancode = inb(PS2_DATA);
    
    /* Key release (bit 7 set) */
    int release = scancode & 0x80;
    uint8_t code = scancode & 0x7F;
    
    /* Handle modifiers */
    switch (code) {
        case 0x2A: /* Left shift */
            if (release) modifiers &= ~MOD_LSHIFT;
            else modifiers |= MOD_LSHIFT;
            return;
        case 0x36: /* Right shift */
            if (release) modifiers &= ~MOD_RSHIFT;
            else modifiers |= MOD_RSHIFT;
            return;
        case 0x1D: /* Ctrl */
            if (release) modifiers &= ~MOD_LCTRL;
            else modifiers |= MOD_LCTRL;
            return;
        case 0x38: /* Alt */
            if (release) modifiers &= ~MOD_LALT;
            else modifiers |= MOD_LALT;
            return;
    }
    
    /* Only process key press */
    if (release) return;
    
    /* Convert to ASCII */
    char c;
    if (modifiers & (MOD_LSHIFT | MOD_RSHIFT)) {
        c = scancode_to_ascii_shift[code];
    } else {
        c = scancode_to_ascii[code];
    }
    
    if (c) {
        buffer_put(c);
    }
}

/* Check if key available */
int ps2_keyboard_has_key(void) {
    return key_head != key_tail;
}

/* Get key (non-blocking) */
char ps2_keyboard_getc(void) {
    if (key_head == key_tail) return 0;
    char c = key_buffer[key_tail];
    key_tail = (key_tail + 1) % KEY_BUFFER_SIZE;
    return c;
}

/* Get key (blocking) */
char ps2_keyboard_getc_block(void) {
    while (!ps2_keyboard_has_key());
    return ps2_keyboard_getc();
}

/* Initialize PS/2 controller and keyboard */
int ps2_keyboard_init(void) {
    uint8_t config;
    
    /* Disable devices */
    ps2_controller_cmd(PS2_CMD_DISABLE_PORT1);
    ps2_controller_cmd(PS2_CMD_DISABLE_PORT2);
    
    /* Flush output buffer */
    inb(PS2_DATA);
    
    /* Read config */
    ps2_controller_cmd(PS2_CMD_READ_CONFIG);
    config = ps2_read();
    
    /* Disable IRQs and translation */
    config &= ~(PS2_CONFIG_PORT1_IRQ | PS2_CONFIG_PORT2_IRQ | PS2_CONFIG_TRANSLATION);
    ps2_controller_cmd(PS2_CMD_WRITE_CONFIG);
    ps2_write(config);
    
    /* Self test */
    ps2_controller_cmd(PS2_CMD_SELF_TEST);
    if (ps2_read() != 0x55) {
        return -1;  /* Controller failed */
    }
    
    /* Test first port */
    ps2_controller_cmd(PS2_CMD_PORT1_TEST);
    if (ps2_read() != 0x00) {
        return -1;  /* Keyboard failed */
    }
    
    /* Enable first port */
    ps2_controller_cmd(PS2_CMD_ENABLE_PORT1);
    
    /* Enable IRQ1 */
    ps2_controller_cmd(PS2_CMD_READ_CONFIG);
    config = ps2_read();
    config |= PS2_CONFIG_PORT1_IRQ;
    ps2_controller_cmd(PS2_CMD_WRITE_CONFIG);
    ps2_write(config);
    
    /* Reset keyboard */
    if (kb_command(PS2_KB_RESET) < 0) {
        return -1;
    }
    ps2_read();  /* BAT completion code */
    
    /* Set scancode set 1 */
    kb_command(PS2_KB_SET_SCANCODE);
    kb_command(0x01);
    
    /* Enable scanning */
    if (kb_command(PS2_KB_ENABLE_SCANNING) < 0) {
        return -1;
    }
    
    key_head = 0;
    key_tail = 0;
    
    return 0;
}
