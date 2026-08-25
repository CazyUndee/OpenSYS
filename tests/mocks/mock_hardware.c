/*
 * mock_hardware.c - Mock Hardware Drivers Implementation
 *
 * Copyright (C) 2026 CazyUndee
 */

#include "mock_hardware.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Mock disk implementation
mock_disk_t* mock_disk_create(size_t size, size_t sector_size) {
    mock_disk_t* disk = malloc(sizeof(mock_disk_t));
    if (!disk) return NULL;
    
    disk->data = malloc(size);
    if (!disk->data) {
        free(disk);
        return NULL;
    }
    
    disk->size = size;
    disk->sector_size = sector_size;
    disk->sector_count = size / sector_size;
    disk->read_count = 0;
    disk->write_count = 0;
    disk->error_count = 0;
    
    // Initialize with zeros
    memset(disk->data, 0, size);
    
    return disk;
}

void mock_disk_destroy(mock_disk_t* disk) {
    if (disk) {
        free(disk->data);
        free(disk);
    }
}

int mock_disk_read(mock_disk_t* disk, uint32_t lba, uint32_t count, void* buffer) {
    if (!disk || !buffer) return -1;
    
    disk->read_count++;
    
    // Check bounds
    if (lba + count > disk->sector_count) {
        disk->error_count++;
        return -1;
    }
    
    // Simulate random errors if requested
    if (disk->error_count > 0 && (rand() % 100) < disk->error_count) {
        return -1;
    }
    
    // Copy data
    size_t offset = lba * disk->sector_size;
    size_t size = count * disk->sector_size;
    memcpy(buffer, disk->data + offset, size);
    
    return 0;
}

int mock_disk_write(mock_disk_t* disk, uint32_t lba, uint32_t count, const void* buffer) {
    if (!disk || !buffer) return -1;
    
    disk->write_count++;
    
    // Check bounds
    if (lba + count > disk->sector_count) {
        disk->error_count++;
        return -1;
    }
    
    // Simulate random errors if requested
    if (disk->error_count > 0 && (rand() % 100) < disk->error_count) {
        return -1;
    }
    
    // Copy data
    size_t offset = lba * disk->sector_size;
    size_t size = count * disk->sector_size;
    memcpy(disk->data + offset, buffer, size);
    
    return 0;
}

void mock_disk_inject_error(mock_disk_t* disk, int error_rate) {
    if (disk) {
        disk->error_count = error_rate;
    }
}

// Mock VGA implementation
mock_vga_t* mock_vga_create(size_t width, size_t height) {
    mock_vga_t* vga = malloc(sizeof(mock_vga_t));
    if (!vga) return NULL;
    
    vga->framebuffer = malloc(width * height * 2); // 2 bytes per character
    if (!vga->framebuffer) {
        free(vga);
        return NULL;
    }
    
    vga->width = width;
    vga->height = height;
    vga->cursor_x = 0;
    vga->cursor_y = 0;
    vga->puts_count = 0;
    vga->clear_count = 0;
    
    // Clear framebuffer
    memset(vga->framebuffer, 0, width * height * 2);
    
    return vga;
}

void mock_vga_destroy(mock_vga_t* vga) {
    if (vga) {
        free(vga->framebuffer);
        free(vga);
    }
}

void mock_vga_puts(mock_vga_t* vga, const char* str) {
    if (!vga || !str) return;
    
    vga->puts_count++;
    
    while (*str && vga->cursor_y < vga->height) {
        if (*str == '\n') {
            vga->cursor_x = 0;
            vga->cursor_y++;
        } else if (*str == '\r') {
            vga->cursor_x = 0;
        } else {
            size_t offset = (vga->cursor_y * vga->width + vga->cursor_x) * 2;
            vga->framebuffer[offset] = *str;
            vga->framebuffer[offset + 1] = 0x07; // White on black
            
            vga->cursor_x++;
            if (vga->cursor_x >= vga->width) {
                vga->cursor_x = 0;
                vga->cursor_y++;
            }
        }
        str++;
    }
}

void mock_vga_clear(mock_vga_t* vga) {
    if (!vga) return;
    
    vga->clear_count++;
    vga->cursor_x = 0;
    vga->cursor_y = 0;
    memset(vga->framebuffer, 0, vga->width * vga->height * 2);
}

void mock_vga_set_cursor(mock_vga_t* vga, size_t x, size_t y) {
    if (!vga) return;
    
    if (x < vga->width && y < vga->height) {
        vga->cursor_x = x;
        vga->cursor_y = y;
    }
}

// Mock timer implementation
static void (*timer_callback)(void) = NULL;

mock_timer_t* mock_timer_create(uint64_t tick_interval) {
    mock_timer_t* timer = malloc(sizeof(mock_timer_t));
    if (!timer) return NULL;
    
    timer->current_time = 0;
    timer->tick_interval = tick_interval;
    timer->tick_count = 0;
    timer->callback_count = 0;
    
    return timer;
}

void mock_timer_destroy(mock_timer_t* timer) {
    if (timer) {
        free(timer);
    }
}

void mock_timer_tick(mock_timer_t* timer) {
    if (!timer) return;
    
    timer->current_time += timer->tick_interval;
    timer->tick_count++;
    
    if (timer_callback) {
        timer_callback();
        timer->callback_count++;
    }
}

uint64_t mock_timer_get_time(mock_timer_t* timer) {
    return timer ? timer->current_time : 0;
}

void mock_timer_set_callback(mock_timer_t* timer, void (*callback)(void)) {
    (void)timer;
    timer_callback = callback;
}

// Mock PIC implementation
mock_pic_t* mock_pic_create(void) {
    mock_pic_t* pic = malloc(sizeof(mock_pic_t));
    if (!pic) return NULL;
    
    pic->irq_mask = 0xFF; // All IRQs masked initially
    pic->irq_pending = 0;
    pic->irq_service = 0;
    pic->eoi_count = 0;
    pic->mask_count = 0;
    
    return pic;
}

void mock_pic_destroy(mock_pic_t* pic) {
    if (pic) {
        free(pic);
    }
}

void mock_pic_mask_irq(mock_pic_t* pic, uint8_t irq) {
    if (!pic || irq > 7) return;
    
    pic->irq_mask |= (1 << irq);
    pic->mask_count++;
}

void mock_pic_unmask_irq(mock_pic_t* pic, uint8_t irq) {
    if (!pic || irq > 7) return;
    
    pic->irq_mask &= ~(1 << irq);
    pic->mask_count++;
}

void mock_pic_send_eoi(mock_pic_t* pic) {
    if (!pic) return;
    
    pic->eoi_count++;
    pic->irq_service = 0;
}

uint8_t mock_pic_get_irq(mock_pic_t* pic) {
    if (!pic) return 0xFF;
    
    uint8_t pending = pic->irq_pending & ~pic->irq_mask;
    if (pending) {
        for (int i = 0; i < 8; i++) {
            if (pending & (1 << i)) {
                pic->irq_service = (1 << i);
                return i;
            }
        }
    }
    
    return 0xFF; // No pending IRQ
}

// Mock serial implementation
mock_serial_t* mock_serial_create(size_t buffer_size) {
    mock_serial_t* serial = malloc(sizeof(mock_serial_t));
    if (!serial) return NULL;
    
    serial->buffer = malloc(buffer_size);
    if (!serial->buffer) {
        free(serial);
        return NULL;
    }
    
    serial->buffer_size = buffer_size;
    serial->read_pos = 0;
    serial->write_pos = 0;
    serial->read_count = 0;
    serial->write_count = 0;
    
    memset(serial->buffer, 0, buffer_size);
    
    return serial;
}

void mock_serial_destroy(mock_serial_t* serial) {
    if (serial) {
        free(serial->buffer);
        free(serial);
    }
}

int mock_serial_read(mock_serial_t* serial, void* buffer, size_t size) {
    if (!serial || !buffer) return -1;
    
    serial->read_count++;
    
    size_t available = serial->write_pos - serial->read_pos;
    if (available > serial->buffer_size) {
        available = serial->buffer_size;
    }
    
    size_t to_read = size < available ? size : available;
    memcpy(buffer, serial->buffer + serial->read_pos, to_read);
    serial->read_pos += to_read;
    
    return to_read;
}

int mock_serial_write(mock_serial_t* serial, const void* buffer, size_t size) {
    if (!serial || !buffer) return -1;
    
    serial->write_count++;
    
    size_t available = serial->buffer_size - serial->write_pos;
    size_t to_write = size < available ? size : available;
    memcpy(serial->buffer + serial->write_pos, buffer, to_write);
    serial->write_pos += to_write;
    
    return to_write;
}

void mock_serial_inject_data(mock_serial_t* serial, const void* data, size_t size) {
    if (!serial || !data) return;
    
    size_t available = serial->buffer_size - serial->write_pos;
    size_t to_write = size < available ? size : available;
    memcpy(serial->buffer + serial->write_pos, data, to_write);
    serial->write_pos += to_write;
}

// Mock RTC implementation
mock_rtc_t* mock_rtc_create(uint8_t seconds, uint8_t minutes, uint8_t hours,
                           uint8_t day, uint8_t month, uint16_t year) {
    mock_rtc_t* rtc = malloc(sizeof(mock_rtc_t));
    if (!rtc) return NULL;
    
    rtc->seconds = seconds;
    rtc->minutes = minutes;
    rtc->hours = hours;
    rtc->day = day;
    rtc->month = month;
    rtc->year = year;
    rtc->read_count = 0;
    
    return rtc;
}

void mock_rtc_destroy(mock_rtc_t* rtc) {
    if (rtc) {
        free(rtc);
    }
}

void mock_rtc_get_time(mock_rtc_t* rtc, uint8_t* seconds, uint8_t* minutes, 
                       uint8_t* hours, uint8_t* day, uint8_t* month, uint16_t* year) {
    if (!rtc) return;
    
    rtc->read_count++;
    
    if (seconds) *seconds = rtc->seconds;
    if (minutes) *minutes = rtc->minutes;
    if (hours) *hours = rtc->hours;
    if (day) *day = rtc->day;
    if (month) *month = rtc->month;
    if (year) *year = rtc->year;
}

void mock_rtc_set_time(mock_rtc_t* rtc, uint8_t seconds, uint8_t minutes, 
                       uint8_t hours, uint8_t day, uint8_t month, uint16_t year) {
    if (!rtc) return;
    
    rtc->seconds = seconds;
    rtc->minutes = minutes;
    rtc->hours = hours;
    rtc->day = day;
    rtc->month = month;
    rtc->year = year;
}
