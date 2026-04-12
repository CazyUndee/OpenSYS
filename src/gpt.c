/*
 * gpt.c - GPT Partition Table Driver
 *
 * Reads and parses GPT partition tables.
 */

#include <stdint.h>
#include <stddef.h>
#include "../include/gpt.h"
#include "../include/disk.h"
#include "../include/kheap.h"
#include "../include/pmm.h"

/* Disk operations */
static disk_ops_t* disk_operations = 0;

/* GPT header and entries */
static gpt_header_t* gpt_header = 0;
static gpt_entry_t* gpt_entries = 0;
static uint32_t partition_count = 0;

/* CRC32 lookup table */
static uint32_t crc32_table[256];
static int crc32_initialized = 0;

/*
 * Initialize CRC32 table
 */
static void init_crc32(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
        crc32_table[i] = crc;
    }
    crc32_initialized = 1;
}

/*
 * Calculate CRC32
 */
static uint32_t calc_crc32(const void* data, size_t len) {
    if (!crc32_initialized) init_crc32();
    
    uint32_t crc = 0xFFFFFFFF;
    const uint8_t* p = (const uint8_t*)data;
    
    for (size_t i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }
    
    return ~crc;
}

/*
 * Check if two GUIDs are equal
 */
static int guid_equal(const uint8_t* a, const uint8_t* b) {
    for (int i = 0; i < 16; i++) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

/*
 * Initialize GPT driver
 */
int gpt_init(disk_ops_t* ops) {
    disk_operations = ops;
    return gpt_read_header();
}

/*
 * Read and parse GPT header
 */
int gpt_read_header(void) {
    /* Allocate memory for header */
    if (!gpt_header) {
        gpt_header = (gpt_header_t*)kmalloc(ATA_SECTOR_SIZE);
        if (!gpt_header) return -1;
    }
    
    /* Read GPT header (LBA 1) */
    if (disk_read(GPT_HEADER_LBA, 1, gpt_header) < 0) {
        return -1;
    }
    
    /* Verify signature */
    if (gpt_header->signature != GPT_SIGNATURE) {
        return -1;  /* Not a GPT disk */
    }
    
    /* Verify header CRC */
    uint32_t stored_crc = gpt_header->header_crc;
    gpt_header->header_crc = 0;
    uint32_t calc_crc = calc_crc32(gpt_header, gpt_header->header_size);
    
    if (stored_crc != calc_crc) {
        return -1;  /* CRC mismatch */
    }
    
    /* Restore CRC */
    gpt_header->header_crc = stored_crc;
    
    /* Allocate memory for entries */
    partition_count = gpt_header->entry_count;
    uint32_t entry_array_size = partition_count * gpt_header->entry_size;
    uint32_t entry_sectors = (entry_array_size + ATA_SECTOR_SIZE - 1) / ATA_SECTOR_SIZE;
    
    if (!gpt_entries) {
        gpt_entries = (gpt_entry_t*)kmalloc(entry_sectors * ATA_SECTOR_SIZE);
        if (!gpt_entries) return -1;
    }
    
    /* Read entry array */
    uint64_t entry_lba = gpt_header->entry_array_lba;
    if (disk_read((uint32_t)entry_lba, entry_sectors, gpt_entries) < 0) {
        return -1;
    }
    
    return 0;
}

/*
 * Get partition count
 */
uint32_t gpt_get_partition_count(void) {
    return partition_count;
}

/*
 * Get partition entry
 */
const gpt_entry_t* gpt_get_partition(uint32_t index) {
    if (index >= partition_count) return 0;
    
    /* Check if partition is used */
    gpt_entry_t* entry = &gpt_entries[index];
    if (guid_equal(entry->type_guid, GPT_TYPE_UNUSED)) {
        return 0;  /* Empty entry */
    }
    
    return entry;
}

/*
 * Find partition by type GUID
 */
int gpt_find_by_type(const uint8_t type_guid[16]) {
    for (uint32_t i = 0; i < partition_count; i++) {
        gpt_entry_t* entry = &gpt_entries[i];
        if (guid_equal(entry->type_guid, type_guid)) {
            return (int)i;
        }
    }
    return -1;  /* Not found */
}

/*
 * Get partition size in bytes
 */
uint64_t gpt_get_partition_size(uint32_t index) {
    const gpt_entry_t* entry = gpt_get_partition(index);
    if (!entry) return 0;
    
    return (entry->end_lba - entry->start_lba + 1) * ATA_SECTOR_SIZE;
}

/*
 * Read from partition
 */
int gpt_read_partition(uint32_t index, uint64_t offset, uint32_t size, void* buffer) {
    const gpt_entry_t* entry = gpt_get_partition(index);
    if (!entry) return -1;
    
    /* Convert offset to LBA */
    uint64_t start_lba = entry->start_lba + (offset / ATA_SECTOR_SIZE);
    uint32_t offset_in_sector = offset % ATA_SECTOR_SIZE;
    uint32_t sectors = (size + offset_in_sector + ATA_SECTOR_SIZE - 1) / ATA_SECTOR_SIZE;
    
    /* Temporary buffer for sector-aligned read */
    void* temp = kmalloc(sectors * ATA_SECTOR_SIZE);
    if (!temp) return -1;
    
    /* Read sectors */
    if (disk_read((uint32_t)start_lba, sectors, temp) < 0) {
        kfree(temp);
        return -1;
    }
    
    /* Copy to buffer */
    for (uint32_t i = 0; i < size; i++) {
        ((uint8_t*)buffer)[i] = ((uint8_t*)temp)[offset_in_sector + i];
    }
    
    kfree(temp);
    return 0;
}

/*
 * Print GPT info
 */
void gpt_print_info(void) {
    if (!gpt_header) {
        /* This would print "No GPT header loaded" */
        return;
    }
    
    /* Print disk GUID, partition count, etc. */
    /* Implementation depends on kernel printf */
}
