/*
 * input.c - Unified Input System
 *
 * Wraps PS/2 and USB HID keyboards behind the input.h API.
 * Shell and kernel code call only input.h / keyboard_* functions.
 */

#include <stdint.h>
#include "input.h"
#include "ps2_keyboard.h"
#include "hid_keyboard.h"
#include "usb.h"
#include "kstring.h"

/* Track which backend is active */
static int ps2_active = 0;
static int hid_active = 0;

void input_init(void) {
    ps2_active = (ps2_keyboard_init() == 0);

    /* Try USB HID keyboard if PS/2 failed or as fallback */
    if (usb_init() == 0) {
        usb_enumerate();
        if (hid_keyboard_init() == 0) {
            hid_active = 1;
        }
    }
}

int input_has_event(void) {
    if (hid_active && hid_keyboard_has_key()) return 1;
    if (ps2_active && ps2_keyboard_has_key()) return 1;
    return 0;
}

input_event_t input_get_event(void) {
    input_event_t ev = {0};
    ev.device_type = INPUT_DEVICE_PS2_KEYBOARD;
    ev.event_type = INPUT_EVENT_KEY_PRESS;

    if (hid_active && hid_keyboard_has_key()) {
        ev.device_type = INPUT_DEVICE_USB_HID;
        ev.key_code   = hid_keyboard_getc();
    } else if (ps2_active && ps2_keyboard_has_key()) {
        ev.key_code = ps2_keyboard_getc();
    }

    return ev;
}

void input_flush_events(void) {
    while (ps2_active && ps2_keyboard_has_key()) {
        ps2_keyboard_getc();
    }
    while (hid_active && hid_keyboard_has_key()) {
        hid_keyboard_getc();
    }
}

int input_register_device(input_device_type_t type, void (*callback)(input_event_t*)) {
    (void)type;
    (void)callback;
    /* Future: dispatch events to registered callbacks */
    return -1;
}

int input_unregister_device(input_device_type_t type) {
    (void)type;
    return -1;
}

int keyboard_init(void) {
    return input_init(), 0;
}

int keyboard_has_key(void) {
    return input_has_event();
}

char keyboard_getc(void) {
    /* Non-blocking: try USB first (already polled by hid_keyboard_getc which blocks,
     * so we use hid_keyboard_has_key as a guard and ps2 fallback). */
    if (hid_active && hid_keyboard_has_key()) {
        return hid_keyboard_getc();
    }
    if (ps2_active && ps2_keyboard_has_key()) {
        return ps2_keyboard_getc();
    }
    return 0;
}

int mouse_init(void) {
    return -1;
}

int mouse_has_event(void) {
    return 0;
}

input_event_t mouse_get_event(void) {
    input_event_t ev = {0};
    ev.device_type = INPUT_DEVICE_MOUSE;
    ev.event_type  = INPUT_EVENT_MOUSE_MOVE;
    return ev;
}
