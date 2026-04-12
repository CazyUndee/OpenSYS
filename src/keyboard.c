/*
 * keyboard.c - PS/2 Keyboard driver
 *
 * Reads scancodes from keyboard controller and translates to ASCII.
 * Uses scancode set 1 (most common).
 */

#include "../include/keyboard.h"
#include "../include/io.h"
#include "../include/idt.h"
#include "../include/interrupts.h"

// Simple keyboard buffer
#define KEYBOARD_BUFFER_SIZE 256
static char keyboard_buffer[KEYBOARD_BUFFER_SIZE];
static uint32_t keyboard_buffer_head = 0;
static uint32_t keyboard_buffer_tail = 0;

// Modifier state
static uint8_t shift_state = 0;
static uint8_t caps_lock = 0;

// Scancode to ASCII translation table (set 1, no shift)
const char keyboard_scancode_set1[128] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8',     /* 0-9 */
    '9', '0', '-', '=', '\b', '\t',                     /* 10-15 */
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p',   /* 16-25 */
    '[', ']', '\n', 0,                                  /* 26-28 (ctrl) */
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',   /* 29-38 */
    '\'', '`', 0,                                      /* 39-41 (left shift) */
    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', /* 42-51 */
    '/', 0, '*', 0, ' ', 0,                            /* 52-57 (right shift, *, alt, space, caps) */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,                       /* F1-F10 */
    0, 0, '7', '8', '9', '-', '4', '5', '6', '+',      /* numpad */
    '1', '2', '3', '0', '.',                            /* more numpad */
    0, 0, 0,                                            /* ? */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,                       /* F11-F20 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,                       /* ? */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0                        /* ? */
};

// Shifted versions
const char keyboard_scancode_set1_shift[128] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*',
    '(', ')', '_', '+', '\b', '\t',
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P',
    '{', '}', '\n', 0,
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',
    '"', '~', 0,
    '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>',
    '?', 0, '*', 0, ' ', 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, '7', '8', '9', '-', '4', '5', '6', '+',
    '1', '2', '3', '0', '.',
    0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/*
 * Read scancode from keyboard
 */
uint8_t keyboard_read_scan(void) {
    return inb(KEYBOARD_DATA_PORT);
}

/*
 * Check if keyboard has data
 */
int keyboard_has_data(void) {
    return inb(KEYBOARD_STATUS_PORT) & KEYBOARD_STATUS_OUTPUT;
}

/*
 * Add char to buffer
 */
static void keyboard_buffer_put(char c) {
    uint32_t next = (keyboard_buffer_head + 1) % KEYBOARD_BUFFER_SIZE;
    if (next != keyboard_buffer_tail) {
        keyboard_buffer[keyboard_buffer_head] = c;
        keyboard_buffer_head = next;
    }
}

/*
 * Get char from buffer (non-blocking)
 */
char keyboard_getchar(void) {
    if (keyboard_buffer_head == keyboard_buffer_tail) {
        return 0;  // No data
    }
    char c = keyboard_buffer[keyboard_buffer_tail];
    keyboard_buffer_tail = (keyboard_buffer_tail + 1) % KEYBOARD_BUFFER_SIZE;
    return c;
}

/*
 * Keyboard IRQ handler
 */
void keyboard_handler(struct cpu_state* regs) {
    (void)regs;  // Unused

    uint8_t scancode = keyboard_read_scan();

    // Check if key released (bit 7 set)
    if (scancode & 0x80) {
        scancode &= 0x7F;
        // Handle modifier releases
        if (scancode == 0x2A || scancode == 0x36) {  // L/R shift
            shift_state = 0;
        }
        return;
    }

    // Handle modifiers
    if (scancode == 0x2A || scancode == 0x36) {  // L/R shift
        shift_state = 1;
        return;
    }
    if (scancode == 0x3A) {  // Caps lock
        caps_lock = !caps_lock;
        return;
    }
    if (scancode == 0x1D) return;  // Ctrl (not implemented)
    if (scancode == 0x38) return;  // Alt (not implemented)

    // Get ASCII character
    char c = 0;
    if (scancode < 128) {
        if (shift_state) {
            c = keyboard_scancode_set1_shift[scancode];
        } else {
            c = keyboard_scancode_set1[scancode];
        }

        // Apply caps lock to letters
        if (caps_lock && c >= 'a' && c <= 'z') {
            c = c - 'a' + 'A';
        } else if (caps_lock && c >= 'A' && c <= 'Z') {
            c = c - 'A' + 'a';
        }
    }

    if (c) {
        keyboard_buffer_put(c);
    }
}

/*
 * Initialize keyboard
 */
void keyboard_init(void) {
    // Clear buffer
    keyboard_buffer_head = 0;
    keyboard_buffer_tail = 0;
    shift_state = 0;
    caps_lock = 0;

    // Wait for keyboard to be ready
    while (inb(KEYBOARD_STATUS_PORT) & KEYBOARD_STATUS_INPUT) {
        inb(KEYBOARD_DATA_PORT);
    }

    // Register handler for IRQ1 (keyboard)
    irq_register_handler(1, keyboard_handler);

    // Enable IRQ1
    irq_enable(1);
}
