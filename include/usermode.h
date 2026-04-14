/*
 * usermode.h - User Mode Transition
 */

#ifndef USERMODE_H
#define USERMODE_H

#include <stdint.h>

/* User segment selectors (RPL=3) */
#define USER_CS 0x1B
#define USER_DS 0x23

/* Jump to user mode */
void jump_to_user(uint64_t entry, uint64_t stack);

/* Setup user stack */
void setup_user_stack(uint64_t stack_top, uint64_t stack_size);

#endif
