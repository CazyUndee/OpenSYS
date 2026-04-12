/*
 * hid_keyboard.c - USB HID Keyboard Driver
 *
 * Handles USB HID keyboard input using interrupt transfers.
 */

#include <stdint.h>
#include "../include/hid_keyboard.h"
#include "../include/usb.h"
#include "../include/kheap.h"

/* Key buffer */
#define KEY_BUFFER_SIZE 64
static char key_buffer[KEY_BUFFER_SIZE];
static int key_head = 0;
static int key_tail = 0;

/* Current modifiers */
static uint8_t current_modifiers = 0;

/* LED state */
uint8_t keyboard_leds = 0;

/* Previously pressed keys (for detection) */
static uint8_t prev_keys[6] = {0};

/* US keyboard layout (unshifted) */
const char hid_keyboard_us[256] = {
    0, 0, 0, 0, 'a', 'b', 'c', 'd', 'e', 'f',
    'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p',
    'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
    '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
    '\n', 27, '\b', '\t', ' ', '-', '=', '[', ']', '\\',
    '#', ';', '\'', '`', ',', '.', '/', 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0
};

/* US keyboard layout (shifted) */
const char hid_keyboard_us_shift[256] = {
    0, 0, 0, 0, 'A', 'B', 'C', 'D', 'E', 'F',
    'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
    'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
    '!', '@', '#', '$', '%', '^', '&', '*', '(', ')',
    '\n', 27, '\b', '\t', ' ', '_', '+', '{', '}', '|',
    '~', ':', '"', '~', '<', '>', '?', 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0
};

/* USB device */
static usb_device_t* keyboard_dev = 0;

/* Convert HID code to ASCII */
char hid_to_ascii(uint8_t hid_code, uint8_t modifiers) {
    if (hid_code >= 256) return 0;
    
    if (modifiers & (HID_MOD_LSHIFT | HID_MOD_RSHIFT)) {
        return hid_keyboard_us_shift[hid_code];
    }
    return hid_keyboard_us[hid_code];
}

/* Add key to buffer */
static void buffer_put(char c) {
    int next = (key_head + 1) % KEY_BUFFER_SIZE;
    if (next != key_tail) {
        key_buffer[key_head] = c;
        key_head = next;
    }
}

/* Initialize HID keyboard */
int hid_keyboard_init(void) {
    /* Find USB keyboard */
    keyboard_dev = usb_find_device(0x03);  /* HID class */
    
    if (!keyboard_dev) {
        return -1;
    }
    
    key_head = 0;
    key_tail = 0;
    current_modifiers = 0;
    
    return 0;
}

/* Poll keyboard for input */
int hid_keyboard_poll(void) {
    if (!keyboard_dev) return -1;
    
    hid_keyboard_report_t report;
    
    /* Read interrupt endpoint */
    if (usb_interrupt_transfer(keyboard_dev, 
        USB_ENDPOINT_IN | keyboard_dev->endpoint_in,
        &report, sizeof(report)) == sizeof(report)) {
        
        /* Update modifiers */
        current_modifiers = report.modifiers;
        
        /* Check for new keys */
        for (int i = 0; i < 6; i++) {
            if (report.keys[i] == 0) continue;
            
            /* Check if this key was already pressed */
            int is_new = 1;
            for (int j = 0; j < 6; j++) {
                if (report.keys[i] == prev_keys[j]) {
                    is_new = 0;
                    break;
                }
            }
            
            if (is_new) {
                char c = hid_to_ascii(report.keys[i], current_modifiers);
                if (c) {
                    buffer_put(c);
                }
            }
        }
        
        /* Save current keys */
        for (int i = 0; i < 6; i++) {
            prev_keys[i] = report.keys[i];
        }
        
        return 1;
    }
    
    return 0;
}

/* Check if key available */
int hid_keyboard_has_key(void) {
    return key_head != key_tail;
}

/* Get key (blocking) */
char hid_keyboard_getc(void) {
    while (!hid_keyboard_has_key()) {
        hid_keyboard_poll();
    }
    
    char c = key_buffer[key_tail];
    key_tail = (key_tail + 1) % KEY_BUFFER_SIZE;
    return c;
}

/* Get modifiers */
uint8_t hid_keyboard_get_modifiers(void) {
    return current_modifiers;
}

/* Set LEDs (requires SET_REPORT control transfer) */
void hid_keyboard_set_leds(uint8_t leds) {
    keyboard_leds = leds;
    /* Would send SET_REPORT to keyboard */
}
