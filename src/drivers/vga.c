/*
 * vga.c - VGA Display Driver
 * 
 * Copyright (C) 2026 CazyUndee
 * SPDX-License-Identifier: AGPL-3.0
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

#include "vga.h"
#include "serial.h"
#include <stddef.h>
#include <stdint.h>

#define VGA_BUFFER 0xB8000
#define VGA_WIDTH 80
#define VGA_HEIGHT 25

static uint16_t* vga_buffer = (uint16_t*)VGA_BUFFER;
static size_t cursor_row = 0;
static size_t cursor_col = 0;
static uint8_t terminal_color = 0x07;  // Light grey on black

static inline uint16_t vga_entry(unsigned char c, uint8_t color) {
    return (uint16_t)c | (uint16_t)color << 8;
}

// Initialize VGA system
void vga_init(void) {
    terminal_writestring("[VGA] Initializing Display System...\n");
    
    // Initialize terminal
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    vga_clear_screen();
    
    terminal_writestring("  VGA text mode 80x25\n");
    terminal_writestring("  16 colors available\n");
    
    terminal_writestring("[VGA] Display System Ready!\n");
}

// Set text color
void vga_set_color(vga_color_t fg, vga_color_t bg) {
    terminal_color = fg | bg << 4;
}

// Clear screen
void vga_clear_screen(void) {
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            vga_buffer[y * VGA_WIDTH + x] = vga_entry(' ', terminal_color);
        }
    }
    cursor_row = 0;
    cursor_col = 0;
}

static void terminal_scroll(void) {
    for (size_t y = 0; y < VGA_HEIGHT - 1; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            vga_buffer[y * VGA_WIDTH + x] = vga_buffer[(y + 1) * VGA_WIDTH + x];
        }
    }
    for (size_t x = 0; x < VGA_WIDTH; x++) {
        vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = vga_entry(' ', terminal_color);
    }
}

/* Redirect terminal output into a buffer (used by shell redirection:
 * `cmd > file` runs the command with output captured instead of shown).
 * While active, all vga output funnels into the buffer; the screen and
 * serial are silent. Returns the number of bytes captured on end. */
static char* capture_buf = 0;
static size_t capture_size = 0;
static size_t capture_len = 0;

void terminal_capture_begin(char* buf, size_t size) {
    capture_buf = buf;
    capture_size = size;
    capture_len = 0;
    if (buf && size) buf[0] = 0;
}

size_t terminal_capture_end(void) {
    size_t n = capture_len;
    capture_buf = 0;
    capture_size = 0;
    capture_len = 0;
    return n;
}

void vga_putchar(char c) {
    if (capture_buf) {
        if (capture_len < capture_size - 1) {
            capture_buf[capture_len++] = c;
            capture_buf[capture_len] = 0;
        }
        return;
    }

    serial_putchar(c);

    if (c == '\n') {
        cursor_col = 0;
        cursor_row++;
    } else if (c == '\r') {
        cursor_col = 0;
    } else if (c == '\t') {
        cursor_col = (cursor_col + 8) & ~(8 - 1);
    } else if (c >= ' ' && c <= '~') {
        vga_buffer[cursor_row * VGA_WIDTH + cursor_col] = vga_entry(c, terminal_color);
        cursor_col++;
    } else if (c == '\b') {
        if (cursor_col > 0) {
            cursor_col--;
            vga_buffer[cursor_row * VGA_WIDTH + cursor_col] = vga_entry(' ', terminal_color);
        }
    }

    if (cursor_col >= VGA_WIDTH) {
        cursor_col = 0;
        cursor_row++;
    }

    if (cursor_row >= VGA_HEIGHT) {
        terminal_scroll();
        cursor_row = VGA_HEIGHT - 1;
    }
}

void vga_write(const char* data, size_t size) {
    for (size_t i = 0; i < size; i++) {
        vga_putchar(data[i]);
    }
}

void vga_writestring(const char* data) {
    size_t len = 0;
    while (data[len]) len++;
    vga_write(data, len);
}

void vga_writestring_nl(const char* data) {
    vga_writestring(data);
    vga_putchar('\n');
}

void vga_put_hex(uint64_t n) {
    const char hex[] = "0123456789ABCDEF";
    char buf[19];
    buf[0] = '0'; buf[1] = 'x'; buf[18] = 0;
    for (int i = 17; i >= 2; i--) { buf[i] = hex[n & 0xF]; n >>= 4; }
    vga_writestring(buf);
}

void vga_put_dec(uint64_t n) {
    char buf[24];
    int i = 23;
    buf[i] = 0;
    if (n == 0) { vga_putchar('0'); return; }
    while (n > 0) { buf[--i] = '0' + (n % 10); n /= 10; }
    vga_writestring(&buf[i]);
}

// Legacy compatibility functions
void terminal_initialize(void) {
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    vga_clear_screen();
}

void terminal_set_color(vga_color_t fg, vga_color_t bg) {
    vga_set_color(fg, bg);
}

void terminal_clear(void) {
    vga_clear_screen();
}

void terminal_putchar(char c) {
    vga_putchar(c);
}

void terminal_write(const char* data, size_t size) {
    vga_write(data, size);
}

void terminal_writestring(const char* data) {
    vga_writestring(data);
}

void terminal_writestring_nl(const char* data) {
    vga_writestring_nl(data);
}

void terminal_put_hex(uint64_t n) {
    vga_put_hex(n);
}

void terminal_put_dec(uint64_t n) {
    vga_put_dec(n);
}

// Get display information
void vga_get_info(vga_info_t* info) {
    if (!info) return;
    
    info->width = 80;
    info->height = 25;
    info->colors = 16;
    info->mode = 0;  // Text mode
}




