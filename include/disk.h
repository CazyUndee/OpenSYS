#ifndef DISK_H
#define DISK_H

#include <stdint.h>
#include <stddef.h>

/*
 * Disk Driver - ATA/IDE Primary Master
 * 
 * Basic PIO mode disk access for QEMU and real hardware.
 */

#define ATA_SECTOR_SIZE    512

/* ATA I/O ports (Primary IDE) */
#define ATA_PRIMARY_IO     0x1F0
#define ATA_PRIMARY_CTRL   0x3F6

/* ATA Commands */
#define ATA_CMD_READ       0x20
#define ATA_CMD_READ_EXT   0x24
#define ATA_CMD_WRITE      0x30
#define ATA_CMD_WRITE_EXT  0x34
#define ATA_CMD_IDENTIFY   0xEC

/* ATA Status bits */
#define ATA_STATUS_ERR     0x01
#define ATA_STATUS_DRQ     0x08
#define ATA_STATUS_SRV     0x10
#define ATA_STATUS_DF      0x20
#define ATA_STATUS_RDY     0x40
#define ATA_STATUS_BSY     0x80

/* Disk information */
typedef struct {
    uint16_t cylinders;
    uint16_t heads;
    uint16_t sectors;
    uint32_t lba_sectors;
    uint64_t lba48_sectors;
    char     model[41];
    char     serial[21];
    char     firmware[9];
} disk_info_t;

/* Initialize disk driver */
int disk_init(void);

/* Get disk information */
const disk_info_t* disk_get_info(void);

/* Read sectors (LBA28) */
int disk_read(uint32_t lba, uint32_t count, void* buffer);

/* Write sectors (LBA28) */
int disk_write(uint32_t lba, uint32_t count, const void* buffer);

/* Read sectors (LBA48) */
int disk_read48(uint64_t lba, uint32_t count, void* buffer);

/* Write sectors (LBA48) */
int disk_write48(uint64_t lba, uint32_t count, const void* buffer);

/* Get total disk size in bytes */
uint64_t disk_get_size(void);

#endif
