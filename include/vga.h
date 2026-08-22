/*
 * vga.h - VGA Display Interface
 *
 * Copyright (C) 2026 CazyUndee
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 * 
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef VGA_H
#define VGA_H

#include <stdint.h>
#include <stddef.h>

// Display modes
typedef enum {
    DISPLAY_MODE_TEXT = 0,
    DISPLAY_MODE_GRAPHICS = 1
} display_mode_t;

// VGA colors
typedef enum {
    COLOR_BLACK = 0,
    COLOR_BLUE = 1,
    COLOR_GREEN = 2,
    COLOR_CYAN = 3,
    COLOR_RED = 4,
    COLOR_MAGENTA = 5,
    COLOR_BROWN = 6,
    COLOR_LIGHT_GREY = 7,
    COLOR_DARK_GREY = 8,
    COLOR_LIGHT_BLUE = 9,
    COLOR_LIGHT_GREEN = 10,
    COLOR_LIGHT_CYAN = 11,
    COLOR_LIGHT_RED = 12,
    COLOR_LIGHT_MAGENTA = 13,
    COLOR_LIGHT_BROWN = 14,
    COLOR_WHITE = 15
} vga_color_t;

// Display information
typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t colors;
    display_mode_t mode;
} vga_info_t;

// Core API
void vga_init(void);
void vga_clear_screen(void);
void vga_set_color(vga_color_t fg, vga_color_t bg);
void vga_get_info(vga_info_t* info);

// Text output
void vga_print(const char* str);
void vga_print_char(char c);
void vga_println(const char* str);

// Graphics operations (future)
void vga_pixel(uint16_t x, uint16_t y, vga_color_t color);
void vga_line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, vga_color_t color);

// Terminal wrappers (defined in vga.c)
void terminal_initialize(void);
void terminal_set_color(vga_color_t fg, vga_color_t bg);
void terminal_clear(void);
void terminal_putchar(char c);
void terminal_write(const char* data, size_t size);
void terminal_writestring(const char* data);
void terminal_writestring_nl(const char* data);
void terminal_put_hex(uint64_t n);
void terminal_put_dec(uint64_t n);

/* Output capture for shell redirection. While capture is active, all
 * terminal output goes into the buffer (screen and serial stay silent).
 * terminal_capture_end() returns the number of bytes captured. */
void terminal_capture_begin(char* buf, size_t size);
size_t terminal_capture_end(void);

#endif // VGA_H
