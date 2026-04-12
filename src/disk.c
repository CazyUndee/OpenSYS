/*
 * disk.c - ATA/IDE Disk Driver
 *
 * Basic PIO mode driver for IDE primary master.
 */

#include <stdint.h>
#include "../include/disk.h"
#include "../include/io.h"

/* ATA Ports */
#define ATA_DATA        (ATA_PRIMARY_IO + 0)   /* 0x1F0 */
#define ATA_ERROR       (ATA_PRIMARY_IO + 1)   /* 0x1F1 */
#define ATA_FEATURES    (ATA_PRIMARY_IO + 1)   /* 0x1F1 */
#define ATA_COUNT       (ATA_PRIMARY_IO + 2)   /* 0x1F2 */
#define ATA_LBA_LOW     (ATA_PRIMARY_IO + 3)   /* 0x1F3 */
#define ATA_LBA_MID     (ATA_PRIMARY_IO + 4)   /* 0x1F4 */
#define ATA_LBA_HIGH    (ATA_PRIMARY_IO + 5)   /* 0x1F5 */
#define ATA_DRIVE       (ATA_PRIMARY_IO + 6)   /* 0x1F6 */
#define ATA_STATUS      (ATA_PRIMARY_IO + 7)   /* 0x1F7 */
#define ATA_COMMAND     (ATA_PRIMARY_IO + 7)   /* 0x1F7 */
#define ATA_CONTROL     (ATA_PRIMARY_CTRL)     /* 0x3F6 */

/* Drive selection */
#define ATA_DRIVE_MASTER 0xE0
#define ATA_DRIVE_SLAVE  0xF0

/* Disk info */
static disk_info_t disk_info;
static int disk_initialized = 0;

/*
 * Wait for disk ready (not BSY)
 */
static int wait_ready(void) {
    int timeout = 100000;
    while (timeout--) {
        uint8_t status = inb(ATA_STATUS);
        if (!(status & ATA_STATUS_BSY)) {
            return 0;
        }
    }
    return -1;  /* Timeout */
}

/*
 * Wait for data ready (DRQ set, BSY clear)
 */
static int wait_data(void) {
    int timeout = 100000;
    while (timeout--) {
        uint8_t status = inb(ATA_STATUS);
        if (!(status & ATA_STATUS_BSY) && (status & ATA_STATUS_DRQ)) {
            return 0;
        }
    }
    return -1;  /* Timeout */
}

/*
 * Send reset to controller
 */
static void ata_reset(void) {
    outb(ATA_CONTROL, 0x04);  /* Assert SRST */
    io_wait();
    outb(ATA_CONTROL, 0x00);  /* Deassert SRST */
    io_wait();
    wait_ready();
}

/*
 * Read 256 words from disk data port
 */
static void read_data(uint16_t* buffer) {
    for (int i = 0; i < 256; i++) {
        buffer[i] = inw(ATA_DATA);
    }
}

/*
 * Write 256 words to disk data port
 */
static void write_data(const uint16_t* buffer) {
    for (int i = 0; i < 256; i++) {
        outw(ATA_DATA, buffer[i]);
    }
}

/*
 * Initialize disk driver
 */
int disk_init(void) {
    /* Reset controller */
    ata_reset();
    
    /* Select master drive */
    outb(ATA_DRIVE, ATA_DRIVE_MASTER);
    io_wait();
    
    /* Send IDENTIFY command */
    outb(ATA_COMMAND, ATA_CMD_IDENTIFY);
    io_wait();
    
    /* Check if drive exists */
    uint8_t status = inb(ATA_STATUS);
    if (status == 0) {
        return -1;  /* No drive */
    }
    
    /* Wait for completion */
    if (wait_ready() < 0) {
        return -1;
    }
    
    /* Check for error */
    if (status & ATA_STATUS_ERR) {
        return -1;
    }
    
    /* Read IDENTIFY data */
    uint16_t identify[256];
    read_data(identify);
    
    /* Parse disk info */
    /* LBA sectors (words 60-61) */
    disk_info.lba_sectors = ((uint32_t)identify[61] << 16) | identify[60];
    
    /* LBA48 sectors (words 100-103) */
    disk_info.lba48_sectors = ((uint64_t)identify[103] << 48) |
                              ((uint64_t)identify[102] << 32) |
                              ((uint64_t)identify[101] << 16) |
                              identify[100];
    
    /* Model string (words 27-46, byte-swapped) */
    for (int i = 0; i < 20; i++) {
        disk_info.model[i * 2] = identify[27 + i] >> 8;
        disk_info.model[i * 2 + 1] = identify[27 + i] & 0xFF;
    }
    disk_info.model[40] = 0;
    
    /* Serial (words 10-19, byte-swapped) */
    for (int i = 0; i < 10; i++) {
        disk_info.serial[i * 2] = identify[10 + i] >> 8;
        disk_info.serial[i * 2 + 1] = identify[10 + i] & 0xFF;
    }
    disk_info.serial[20] = 0;
    
    disk_initialized = 1;
    return 0;
}

/*
 * Get disk info
 */
const disk_info_t* disk_get_info(void) {
    return &disk_info;
}

/*
 * Read sectors using LBA28
 */
int disk_read(uint32_t lba, uint32_t count, void* buffer) {
    if (count == 0 || count > 256) return -1;
    if (lba > 0x0FFFFFFF) return -1;
    
    uint16_t* buf = (uint16_t*)buffer;
    
    /* Wait for ready */
    if (wait_ready() < 0) return -1;
    
    /* Setup for LBA28 read */
    outb(ATA_DRIVE, ATA_DRIVE_MASTER | ((lba >> 24) & 0x0F));
    outb(ATA_COUNT, count == 256 ? 0 : count);
    outb(ATA_LBA_LOW, lba & 0xFF);
    outb(ATA_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_LBA_HIGH, (lba >> 16) & 0xFF);
    
    /* Send READ command */
    outb(ATA_COMMAND, ATA_CMD_READ);
    
    /* Read each sector */
    for (uint32_t i = 0; i < count; i++) {
        if (wait_data() < 0) return -1;
        read_data(buf);
        buf += 256;
    }
    
    return 0;
}

/*
 * Write sectors using LBA28
 */
int disk_write(uint32_t lba, uint32_t count, const void* buffer) {
    if (count == 0 || count > 256) return -1;
    if (lba > 0x0FFFFFFF) return -1;
    
    const uint16_t* buf = (const uint16_t*)buffer;
    
    if (wait_ready() < 0) return -1;
    
    outb(ATA_DRIVE, ATA_DRIVE_MASTER | ((lba >> 24) & 0x0F));
    outb(ATA_COUNT, count == 256 ? 0 : count);
    outb(ATA_LBA_LOW, lba & 0xFF);
    outb(ATA_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_LBA_HIGH, (lba >> 16) & 0xFF);
    
    outb(ATA_COMMAND, ATA_CMD_WRITE);
    
    for (uint32_t i = 0; i < count; i++) {
        if (wait_ready() < 0) return -1;
        write_data(buf);
        buf += 256;
    }
    
    return 0;
}

/*
 * Get total disk size
 */
uint64_t disk_get_size(void) {
    if (disk_info.lba48_sectors) {
        return disk_info.lba48_sectors * ATA_SECTOR_SIZE;
    }
    return disk_info.lba_sectors * ATA_SECTOR_SIZE;
}
