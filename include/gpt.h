#ifndef GPT_H
#define GPT_H

#include <stdint.h>
#include <stddef.h>

/*
 * GPT (GUID Partition Table) Driver
 * 
 * GPT is the modern replacement for MBR partition tables.
 * It uses 64-bit LBA addressing and supports up to 128 partitions.
 * 
 * Structure:
 *   LBA 0:      Protective MBR (for MBR compatibility)
 *   LBA 1:      GPT Header
 *   LBA 2-33:   GPT Entry Array (128 entries × 128 bytes = 16KB = 32 sectors)
 *   LBA -33:    Backup GPT Entry Array
 *   LBA -1:     Backup GPT Header
 */

#define GPT_LBA_SIZE           512
#define GPT_HEADER_LBA         1
#define GPT_ENTRIES_PER_SECTOR 4
#define GPT_MAX_PARTITIONS     128

/* GPT Header signature "EFI PART" */
#define GPT_SIGNATURE          0x5452415020494645ULL

/* Well-known partition type GUIDs */
static const uint8_t GPT_TYPE_UNUSED[16] = {0};
static const uint8_t GPT_TYPE_EFI_SYSTEM[16] = {
    0xC1, 0x2A, 0x73, 0x28, 0x81, 0x1C, 0x10, 0x46,
    0xB0, 0x86, 0x4B, 0x54, 0x79, 0x98, 0x4D, 0x83
};
static const uint8_t GPT_TYPE_LINUX_FS[16] = {
    0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47,
    0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4
};
static const uint8_t GPT_TYPE_LINUX_SWAP[16] = {
    0x61, 0x57, 0xC6, 0x06, 0x81, 0x1C, 0x10, 0x46,
    0xB0, 0x86, 0x4B, 0x54, 0x79, 0x98, 0x4D, 0x83
};
static const uint8_t GPT_TYPE_NTFS[16] = {
    0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44,
    0x87, 0xC0, 0x4B, 0x6E, 0x77, 0x06, 0x74, 0xA2
};
static const uint8_t GPT_TYPE_FAT32[16] = {
    0xE3, 0xAF, 0x2E, 0xEB, 0x5D, 0xF3, 0x46, 0x47,
    0x9D, 0x14, 0xA5, 0x43, 0x51, 0x5C, 0xC8, 0xB0
};

/* GPT Partition Entry */
typedef struct {
    uint8_t  type_guid[16];         /* Partition type GUID */
    uint8_t  partition_guid[16];    /* Unique partition GUID */
    uint64_t start_lba;             /* Starting LBA */
    uint64_t end_lba;               /* Ending LBA (inclusive) */
    uint64_t attributes;            /* Partition attributes */
    char     name[72];              /* Partition name (UTF-16LE) */
} __attribute__((packed)) gpt_entry_t;

/* GPT Header */
typedef struct {
    uint64_t signature;             /* "EFI PART" */
    uint32_t revision;              /* GPT revision (1.0 = 0x00010000) */
    uint32_t header_size;           /* Header size in bytes */
    uint32_t header_crc;            /* CRC32 of header */
    uint32_t reserved;              /* Reserved, must be 0 */
    uint64_t current_lba;           /* LBA of this header */
    uint64_t backup_lba;            /* LBA of backup header */
    uint64_t first_usable_lba;      /* First usable LBA for partitions */
    uint64_t last_usable_lba;       /* Last usable LBA for partitions */
    uint8_t  disk_guid[16];         /* Disk GUID */
    uint64_t entry_array_lba;       /* Starting LBA of entry array */
    uint32_t entry_count;           /* Number of partition entries */
    uint32_t entry_size;            /* Size of each entry (usually 128) */
    uint32_t entry_array_crc;       /* CRC32 of entry array */
    uint8_t  reserved2[420];        /* Reserved */
} __attribute__((packed)) gpt_header_t;

/* Disk operations (to be implemented by disk driver) */
typedef struct disk_ops {
    int (*read)(uint64_t lba, uint32_t count, void* buffer);
    int (*write)(uint64_t lba, uint32_t count, const void* buffer);
} disk_ops_t;

/* Initialize GPT driver with disk operations */
int gpt_init(disk_ops_t* ops);

/* Read GPT header */
int gpt_read_header(void);

/* Get partition count */
uint32_t gpt_get_partition_count(void);

/* Get partition entry */
const gpt_entry_t* gpt_get_partition(uint32_t index);

/* Find partition by type GUID */
int gpt_find_by_type(const uint8_t type_guid[16]);

/* Get partition size in bytes */
uint64_t gpt_get_partition_size(uint32_t index);

/* Read from partition */
int gpt_read_partition(uint32_t index, uint64_t offset, uint32_t size, void* buffer);

/* Print GPT info (debug) */
void gpt_print_info(void);

#endif
