/*
 * ps2_keyboard.h - PS/2 Keyboard Driver
 */

#ifndef PS2_KEYBOARD_H
#define PS2_KEYBOARD_H

#include <stdint.h>

/* Initialize PS/2 controller and keyboard */
int ps2_keyboard_init(void);

/* Check if key available */
int ps2_keyboard_has_key(void);

/* Get key (non-blocking, returns 0 if none) */
char ps2_keyboard_getc(void);

/* Get key (blocking) */
char ps2_keyboard_getc_block(void);

/* Handle keyboard interrupt (called from assembly) */
void ps2_keyboard_handler(void);

#endif
