/*
 * gpt.c - GPT Partition Table Driver
 *
 * Reads and parses GPT partition tables.
 */

#include <stdint.h>
#include <stddef.h>
#include "gpt.h"
#include "disk.h"
#include "kheap.h"
#include "pmm.h"

/* Disk operations */
static disk_ops_t* disk_operations = 0;

/* GPT header and entries */
static gpt_header_t* gpt_header = 0;
static gpt_entry_t* gpt_entries = 0;
static uint32_t partition_count = 0;

/* CRC32 is optional during early development */
#define GPT_VALIDATE_CRC 0

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
    
    /* Verify signature using byte-by-byte comparison */
    if (!gpt_signature_valid((const uint8_t*)&gpt_header->signature)) {
        return -1; /* Not a GPT disk */
    }
    
    /* Verify header CRC (optional, skip during early development) */
#if GPT_VALIDATE_CRC
    uint32_t stored_crc = gpt_header->header_crc;
    gpt_header->header_crc = 0;
    uint32_t calc_crc = calc_crc32(gpt_header, gpt_header->header_size);
    gpt_header->header_crc = stored_crc;
    
    if (stored_crc != calc_crc) {
        return -1; /* CRC mismatch */
    }
#endif
    
    /* Allocate memory for entries.
     * Security: entry_count and entry_size are DISK bytes. The old code
     * computed their product in 32-bit (wrappable to a tiny allocation
     * while keeping a huge partition_count -> mass OOB reads) and never
     * enforced the GPT limits. Validate everything before allocating. */
    if (gpt_header->entry_size != 128) return -1;          /* UEFI spec size */
    if (gpt_header->entry_count == 0 ||
        gpt_header->entry_count > GPT_MAX_PARTITIONS) return -1;
    if (gpt_header->entry_array_lba == 0 ||
        gpt_header->entry_array_lba > 0xFFFFFFULL) return -1;   /* LBA28 reach */

    partition_count = gpt_header->entry_count;
    uint64_t entry_array_size = (uint64_t)partition_count * gpt_header->entry_size;
    uint32_t entry_sectors = (uint32_t)((entry_array_size + ATA_SECTOR_SIZE - 1) / ATA_SECTOR_SIZE);

    /* Reallocate when the table grows across re-inits; never reuse a
     * smaller buffer with a larger count. */
    static uint32_t allocated_entries = 0;
    if (!gpt_entries || allocated_entries < partition_count) {
        if (gpt_entries) kfree(gpt_entries);
        gpt_entries = (gpt_entry_t*)kmalloc((size_t)entry_array_size);
        if (!gpt_entries) {
            partition_count = 0;
            allocated_entries = 0;
            return -1;
        }
        allocated_entries = partition_count;
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

int gpt_list_partitions(void) {
    return (int)partition_count;
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
