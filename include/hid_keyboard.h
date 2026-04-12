/*
 * hid_keyboard.h - USB HID Keyboard Driver
 *
 * USB keyboards use the HID (Human Interface Device) class.
 * Reports contain key codes and modifier states.
 */

#ifndef HID_KEYBOARD_H
#define HID_KEYBOARD_H

#include <stdint.h>

/* HID Keyboard modifiers */
#define HID_MOD_LCTRL     0x01
#define HID_MOD_LSHIFT    0x02
#define HID_MOD_LALT      0x04
#define HID_MOD_LGUI      0x08
#define HID_MOD_RCTRL     0x10
#define HID_MOD_RSHIFT    0x20
#define HID_MOD_RALT      0x40
#define HID_MOD_RGUI      0x80

/* HID keyboard report (8 bytes) */
typedef struct {
    uint8_t modifiers;    /* Modifier keys (Ctrl, Shift, Alt, GUI) */
    uint8_t reserved;     /* Always 0 */
    uint8_t keys[6];      /* Up to 6 keys pressed simultaneously */
} __attribute__((packed)) hid_keyboard_report_t;

/* LED states */
#define HID_LED_NUMLOCK    0x01
#define HID_LED_CAPSLOCK   0x02
#define HID_LED_SCROLLLOCK 0x04

/* Keyboard LED state */
extern uint8_t keyboard_leds;

/* Initialize USB HID keyboard */
int hid_keyboard_init(void);

/* Read key from keyboard (blocking) */
char hid_keyboard_getc(void);

/* Check if key available */
int hid_keyboard_has_key(void);

/* Set LED state */
void hid_keyboard_set_leds(uint8_t leds);

/* Poll keyboard (non-blocking) */
int hid_keyboard_poll(void);

/* Get modifier state */
uint8_t hid_keyboard_get_modifiers(void);

/* Convert HID code to ASCII */
char hid_to_ascii(uint8_t hid_code, uint8_t modifiers);

/* US keyboard layout */
extern const char hid_keyboard_us[];
extern const char hid_keyboard_us_shift[];

#endif
