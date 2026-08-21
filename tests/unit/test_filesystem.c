/*
 * test_filesystem.c - Unit Tests for Filesystem Operations
 *
 * Copyright (C) 2026 CazyUndee
 */

#include "../test_framework.h"
#include "../../include/fs.h"
#include <stdlib.h>
#include <string.h>

// Test filesystem constants
void test_filesystem_constants(void) {
    printf("Testing filesystem constants...\n");
    
    ASSERT_EQ(FS_SECTOR_SIZE, 512, "Sector size should be 512");
    ASSERT_EQ(FS_CLUSTER_SIZE, 4096, "Cluster size should be 4096");
    ASSERT_EQ(FS_MAGIC, 0x4E414C50, "Magic number should be correct");
    ASSERT_EQ(FS_VERSION, 0x00010000, "Version should be 1.0");
    ASSERT_EQ(MFT_ENTRY_SIZE, 4096, "MFT entry size should be 4096");
    ASSERT_EQ(MAX_FILENAME_LEN, 255, "Max filename length should be 255");
    
    TEST_PASS();
}

// Test boot sector structure size
void test_boot_sector_structure(void) {
    printf("Testing boot sector structure...\n");
    
    // Boot sector is 504 bytes on the host (packed, no trailing padding)
    // On the target it is padded to 512 via the disk layout
    ASSERT_EQ(sizeof(fs_boot_sector_t), 504, "Boot sector packed size should be 504");
    
    // Test structure alignment
    fs_boot_sector_t boot;
    memset(&boot, 0, sizeof(boot));
    
    boot.magic = FS_MAGIC;
    boot.version = FS_VERSION;
    boot.signature = 0xAA55;
    
    ASSERT_EQ(boot.magic, FS_MAGIC, "Magic field should be accessible");
    ASSERT_EQ(boot.version, FS_VERSION, "Version field should be accessible");
    ASSERT_EQ(boot.signature, 0xAA55, "Signature field should be accessible");
    
    TEST_PASS();
}

// Test attribute header structure
void test_attribute_header_structure(void) {
    printf("Testing attribute header structure...\n");
    
    attr_header_t attr;
    memset(&attr, 0, sizeof(attr));
    
    // Test setting and getting attribute fields
    attr.type = ATTR_DATA;
    attr.length = 100;
    attr.non_resident = 0;
    attr.flags = 0;
    
    ASSERT_EQ(attr.type, ATTR_DATA, "Attribute type should be accessible");
    ASSERT_EQ(attr.length, 100, "Attribute length should be accessible");
    ASSERT_EQ(attr.non_resident, 0, "Non-resident flag should be accessible");
    
    TEST_PASS();
}

// Test standard information attribute
void test_standard_info_attribute(void) {
    printf("Testing standard info attribute...\n");
    
    attr_std_info_t std_info;
    memset(&std_info, 0, sizeof(std_info));
    
    // Test setting timestamps and flags
    std_info.create_time = 1234567890;
    std_info.modify_time = 1234567891;
    std_info.access_time = 1234567892;
    std_info.flags = FILE_FLAG_READONLY;
    
    ASSERT_EQ(std_info.create_time, 1234567890, "Create time should be accessible");
    ASSERT_EQ(std_info.modify_time, 1234567891, "Modify time should be accessible");
    ASSERT_EQ(std_info.access_time, 1234567892, "Access time should be accessible");
    ASSERT_EQ(std_info.flags, FILE_FLAG_READONLY, "File flags should be accessible");
    
    TEST_PASS();
}

// Test file flag constants
void test_file_flags(void) {
    printf("Testing file flag constants...\n");
    
    // Test individual flags
    ASSERT_EQ(FILE_FLAG_READONLY, 0x0001, "Readonly flag should be correct");
    ASSERT_EQ(FILE_FLAG_HIDDEN, 0x0002, "Hidden flag should be correct");
    ASSERT_EQ(FILE_FLAG_SYSTEM, 0x0004, "System flag should be correct");
    ASSERT_EQ(FILE_FLAG_DIRECTORY, 0x1000, "Directory flag should be correct");
    ASSERT_EQ(FILE_FLAG_COMPRESSED, 0x2000, "Compressed flag should be correct");
    ASSERT_EQ(FILE_FLAG_ENCRYPTED, 0x4000, "Encrypted flag should be correct");
    
    // Test flag combinations
    uint32_t combined = FILE_FLAG_READONLY | FILE_FLAG_HIDDEN;
    ASSERT_EQ(combined, 0x0003, "Flag combination should work correctly");
    
    TEST_PASS();
}

// Test attribute type constants
void test_attribute_types(void) {
    printf("Testing attribute type constants...\n");
    
    ASSERT_EQ(ATTR_UNUSED, 0x00, "Unused attribute type should be correct");
    ASSERT_EQ(ATTR_STANDARD_INFO, 0x10, "Standard info attribute type should be correct");
    ASSERT_EQ(ATTR_FILENAME, 0x30, "Filename attribute type should be correct");
    ASSERT_EQ(ATTR_DATA, 0x80, "Data attribute type should be correct");
    ASSERT_EQ(ATTR_INDEX_ROOT, 0x90, "Index root attribute type should be correct");
    ASSERT_EQ(ATTR_INDEX_ALLOC, 0xA0, "Index allocation attribute type should be correct");
    ASSERT_EQ(ATTR_BITMAP, 0xB0, "Bitmap attribute type should be correct");
    ASSERT_EQ(ATTR_END, 0xFFFFFFFF, "End attribute type should be correct");
    
    TEST_PASS();
}

// Test MFT entry flags
void test_mft_flags(void) {
    printf("Testing MFT entry flags...\n");
    
    ASSERT_EQ(MFT_FLAG_IN_USE, 0x0001, "In-use flag should be correct");
    ASSERT_EQ(MFT_FLAG_DIRECTORY, 0x0002, "Directory flag should be correct");
    
    // Test flag combinations
    uint16_t combined = MFT_FLAG_IN_USE | MFT_FLAG_DIRECTORY;
    ASSERT_EQ(combined, 0x0003, "MFT flag combination should work correctly");
    
    TEST_PASS();
}

// Test filesystem calculations
void test_filesystem_calculations(void) {
    printf("Testing filesystem calculations...\n");
    
    // Test sector/cluster relationships
    ASSERT_EQ(FS_SECTORS_PER_CLUSTER, FS_CLUSTER_SIZE / FS_SECTOR_SIZE, 
              "Sectors per cluster calculation should be correct");
    ASSERT_EQ(FS_SECTORS_PER_CLUSTER, 8, "Should have 8 sectors per cluster");
    
    // Test MFT entries per cluster
    ASSERT_EQ(MFT_ENTRIES_PER_CLUSTER, 1, "Should have 1 MFT entry per cluster");
    
    // Test maximum attributes per entry
    ASSERT_EQ(MAX_ATTRS_PER_ENTRY, 16, "Should support 16 attributes per entry");
    
    TEST_PASS();
}

// Test structure packing and alignment
void test_structure_packing(void) {
    printf("Testing structure packing...\n");
    
    // Test that structures are properly packed (no unexpected padding)
    ASSERT_EQ(sizeof(attr_header_t), 16, "Attribute header should be 16 bytes");
    ASSERT_EQ(sizeof(attr_std_info_t), 48, "Standard info attribute should be 48 bytes");
    
    // Boot sector packed size (504 bytes on host, padded to 512 on disk)
    ASSERT_EQ(sizeof(fs_boot_sector_t), 504, 
              "Boot sector packed size should be 504 bytes");
    
    TEST_PASS();
}

// Test filename length validation
void test_filename_validation(void) {
    printf("Testing filename validation...\n");
    
    // Test maximum filename length
    ASSERT(MAX_FILENAME_LEN > 0, "Max filename length should be positive");
    ASSERT_EQ(MAX_FILENAME_LEN, 255, "Max filename length should be 255");
    
    // Test that we can create a filename of maximum length
    char long_filename[MAX_FILENAME_LEN + 1];
    memset(long_filename, 'A', MAX_FILENAME_LEN);
    long_filename[MAX_FILENAME_LEN] = '\0';
    
    ASSERT_EQ(strlen(long_filename), MAX_FILENAME_LEN, 
              "Should be able to create max length filename");
    
    TEST_PASS();
}

// Test filesystem stats structure layout
void test_fs_stats_structure(void) {
    printf("Testing filesystem stats structure...\n");

    fs_stats_t stats;
    memset(&stats, 0, sizeof(stats));

    ASSERT(sizeof(stats.total_bytes) == 8, "total_bytes should be 8 bytes");
    ASSERT(sizeof(stats.free_bytes) == 8, "free_bytes should be 8 bytes");
    ASSERT(sizeof(stats.used_bytes) == 8, "used_bytes should be 8 bytes");
    ASSERT(sizeof(stats.file_count) == 4, "file_count should be 4 bytes");
    ASSERT(sizeof(stats.dir_count) == 4, "dir_count should be 4 bytes");

    TEST_PASS();
}

// Test filesystem magic number validation
void test_magic_number_validation(void) {
    printf("Testing magic number validation...\n");
    
    // Test magic number as bytes
    uint8_t magic_bytes[4] = {0x50, 0x4C, 0x41, 0x4E}; // "PLAN"
    uint32_t magic_value = *(uint32_t*)magic_bytes;
    
    ASSERT_EQ(magic_value, FS_MAGIC, "Magic number should match 'PLAN'");
    
    // Test that magic number is reasonable (not zero or common patterns)
    ASSERT(FS_MAGIC != 0x00000000, "Magic number should not be zero");
    ASSERT(FS_MAGIC != 0xFFFFFFFF, "Magic number should not be all ones");
    
    TEST_PASS();
}

// Create test suite
test_suite_t* create_filesystem_test_suite(void) {
    static test_suite_t suite;
    test_suite_init(&suite, "Filesystem Tests");
    
    test_suite_add_test(&suite, "filesystem_constants", test_filesystem_constants);
    test_suite_add_test(&suite, "boot_sector_structure", test_boot_sector_structure);
    test_suite_add_test(&suite, "attribute_header_structure", test_attribute_header_structure);
    test_suite_add_test(&suite, "standard_info_attribute", test_standard_info_attribute);
    test_suite_add_test(&suite, "file_flags", test_file_flags);
    test_suite_add_test(&suite, "attribute_types", test_attribute_types);
    test_suite_add_test(&suite, "mft_flags", test_mft_flags);
    test_suite_add_test(&suite, "filesystem_calculations", test_filesystem_calculations);
    test_suite_add_test(&suite, "structure_packing", test_structure_packing);
    test_suite_add_test(&suite, "filename_validation", test_filename_validation);
    test_suite_add_test(&suite, "magic_number_validation", test_magic_number_validation);
    test_suite_add_test(&suite, "fs_stats_structure", test_fs_stats_structure);
    
    return &suite;
}
