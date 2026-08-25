/*
 * test_kernel_integration.c - Integration Tests for Kernel Components
 *
 * Copyright (C) 2026 CazyUndee
 */

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "../test_framework.h"
#include "../../include/memory.h"
#include "../../include/process.h"
#include "../../include/fs.h"

// Test memory and process integration
void test_memory_process_integration(void) {
    printf("Testing memory and process integration...\n");
    
    // Test that process structures can be allocated from memory
    process_t* proc = malloc(sizeof(process_t));
    ASSERT_NOT_NULL(proc, "Should be able to allocate process structure");
    
    if (proc) {
        memset(proc, 0, sizeof(process_t));
        proc->pid = 1001;
        proc->state = PROC_STATE_READY;
        strcpy(proc->name, "test_proc");
        
        ASSERT_EQ(proc->pid, 1001, "Process PID should be set correctly");
        ASSERT_EQ(proc->state, PROC_STATE_READY, "Process state should be set correctly");
        ASSERT(strcmp(proc->name, "test_proc") == 0, "Process name should be set correctly");
        
        free(proc);
    }
    
    TEST_PASS();
}

// Test filesystem and memory integration
void test_filesystem_memory_integration(void) {
    printf("Testing filesystem and memory integration...\n");
    
    // Test that filesystem structures can be allocated
    fs_boot_sector_t* boot = malloc(sizeof(fs_boot_sector_t));
    ASSERT_NOT_NULL(boot, "Should be able to allocate boot sector");
    
    if (boot) {
        memset(boot, 0, sizeof(fs_boot_sector_t));
        boot->magic = FS_MAGIC;
        boot->version = FS_VERSION;
        boot->signature = 0xAA55;
        
        ASSERT_EQ(boot->magic, FS_MAGIC, "Boot sector magic should be set");
        ASSERT_EQ(boot->version, FS_VERSION, "Boot sector version should be set");
        ASSERT_EQ(boot->signature, 0xAA55, "Boot sector signature should be set");
        
        free(boot);
    }
    
    TEST_PASS();
}

// Test multiple component allocation
void test_multiple_component_allocation(void) {
    printf("Testing multiple component allocation...\n");
    
    // Allocate multiple structures of different types
    process_t* processes[5];
    fs_boot_sector_t* boot_sectors[3];
    void* generic_blocks[10];
    
    // Allocate processes
    for (int i = 0; i < 5; i++) {
        processes[i] = malloc(sizeof(process_t));
        ASSERT_NOT_NULL(processes[i], "Process allocation should succeed");
        
        if (processes[i]) {
            memset(processes[i], 0, sizeof(process_t));
            processes[i]->pid = 2000 + i;
            processes[i]->state = PROC_STATE_READY;
        }
    }
    
    // Allocate boot sectors
    for (int i = 0; i < 3; i++) {
        boot_sectors[i] = malloc(sizeof(fs_boot_sector_t));
        ASSERT_NOT_NULL(boot_sectors[i], "Boot sector allocation should succeed");
        
        if (boot_sectors[i]) {
            memset(boot_sectors[i], 0, sizeof(fs_boot_sector_t));
            boot_sectors[i]->magic = FS_MAGIC;
            boot_sectors[i]->total_sectors = 1000000;
        }
    }
    
    // Allocate generic memory blocks
    for (int i = 0; i < 10; i++) {
        size_t size = (i + 1) * 100;
        generic_blocks[i] = malloc(size);
        ASSERT_NOT_NULL(generic_blocks[i], "Generic allocation should succeed");
        
        if (generic_blocks[i]) {
            memset(generic_blocks[i], 0xFF, size);
        }
    }
    
    // Verify data integrity
    for (int i = 0; i < 5; i++) {
        if (processes[i]) {
            ASSERT_EQ(processes[i]->pid, (pid_t)(2000 + i), "Process PID should be preserved");
            ASSERT_EQ(processes[i]->state, PROC_STATE_READY, "Process state should be preserved");
        }
    }
    
    for (int i = 0; i < 3; i++) {
        if (boot_sectors[i]) {
            ASSERT_EQ(boot_sectors[i]->magic, FS_MAGIC, "Boot sector magic should be preserved");
            ASSERT_EQ(boot_sectors[i]->total_sectors, 1000000, "Boot sector size should be preserved");
        }
    }
    
    // Clean up
    for (int i = 0; i < 5; i++) {
        free(processes[i]);
    }
    for (int i = 0; i < 3; i++) {
        free(boot_sectors[i]);
    }
    for (int i = 0; i < 10; i++) {
        free(generic_blocks[i]);
    }
    
    TEST_PASS();
}

// Test process creation simulation
void test_process_creation_simulation(void) {
    printf("Testing process creation simulation...\n");
    
    // Simulate process table
    process_t* process_table[MAX_PROCESSES];
    memset(process_table, 0, sizeof(process_table));
    
    // Simulate creating multiple processes
    for (int i = 0; i < 10; i++) {
        process_table[i] = malloc(sizeof(process_t));
        ASSERT_NOT_NULL(process_table[i], "Process creation should succeed");
        
        if (process_table[i]) {
            memset(process_table[i], 0, sizeof(process_t));
            process_table[i]->pid = 3000 + i;
            process_table[i]->state = PROC_STATE_READY;
            process_table[i]->priority = i % 10;
            snprintf(process_table[i]->name, PROCESS_NAME_LEN, "proc_%d", i);
        }
    }
    
    // Verify process table contents
    for (int i = 0; i < 10; i++) {
        if (process_table[i]) {
            ASSERT_EQ(process_table[i]->pid, (pid_t)(3000 + i), "PID should match expected");
            ASSERT_EQ(process_table[i]->state, PROC_STATE_READY, "State should be READY");
            ASSERT_EQ(process_table[i]->priority, i % 10, "Priority should match expected");
            
            char expected_name[PROCESS_NAME_LEN];
            snprintf(expected_name, PROCESS_NAME_LEN, "proc_%d", i);
            ASSERT(strcmp(process_table[i]->name, expected_name) == 0, "Name should match expected");
        }
    }
    
    // Simulate process cleanup
    for (int i = 0; i < 10; i++) {
        if (process_table[i]) {
            process_table[i]->state = PROC_STATE_ZOMBIE;
            ASSERT_EQ(process_table[i]->state, PROC_STATE_ZOMBIE, "Process should become zombie");
            
            free(process_table[i]);
            process_table[i] = NULL;
        }
    }
    
    TEST_PASS();
}

// Test filesystem metadata simulation
void test_filesystem_metadata_simulation(void) {
    printf("Testing filesystem metadata simulation...\n");
    
    // Simulate MFT entries
    typedef struct {
        uint32_t mft_number;
        char filename[MAX_FILENAME_LEN];
        uint32_t flags;
        uint64_t size;
        uint64_t create_time;
    } mock_mft_entry_t;
    
    mock_mft_entry_t* mft_entries[5];
    
    // Create mock MFT entries
    const char* filenames[] = {"boot.bin", "kernel.bin", "init.exe", "config.txt", "readme.md"};
    const uint32_t flags[] = {FILE_FLAG_SYSTEM, FILE_FLAG_SYSTEM, 0, 0, 0};
    const uint64_t sizes[] = {512, 1024*1024, 4096, 256, 1024};
    
    for (int i = 0; i < 5; i++) {
        mft_entries[i] = malloc(sizeof(mock_mft_entry_t));
        ASSERT_NOT_NULL(mft_entries[i], "MFT entry allocation should succeed");
        
        if (mft_entries[i]) {
            mft_entries[i]->mft_number = i;
            strncpy(mft_entries[i]->filename, filenames[i], MAX_FILENAME_LEN - 1);
            mft_entries[i]->filename[MAX_FILENAME_LEN - 1] = '\0';
            mft_entries[i]->flags = flags[i];
            mft_entries[i]->size = sizes[i];
            mft_entries[i]->create_time = 1234567890 + i;
        }
    }
    
    // Verify MFT entries
    for (int i = 0; i < 5; i++) {
        if (mft_entries[i]) {
            ASSERT_EQ(mft_entries[i]->mft_number, (uint32_t)i, "MFT number should match");
            ASSERT(strcmp(mft_entries[i]->filename, filenames[i]) == 0, "Filename should match");
            ASSERT_EQ(mft_entries[i]->flags, flags[i], "Flags should match");
            ASSERT_EQ(mft_entries[i]->size, sizes[i], "Size should match");
            ASSERT_EQ(mft_entries[i]->create_time, (uint64_t)(1234567890 + i), "Create time should match");
        }
    }
    
    // Clean up
    for (int i = 0; i < 5; i++) {
        free(mft_entries[i]);
    }
    
    TEST_PASS();
}

// Test memory pressure simulation
void test_memory_pressure_simulation(void) {
    printf("Testing memory pressure simulation...\n");
    
    void* allocations[100];
    size_t allocation_sizes[100];
    int successful_allocations = 0;
    
    // Try to allocate memory with varying sizes
    for (int i = 0; i < 100; i++) {
        size_t size = (i + 1) * 1024; // 1KB to 100KB
        allocations[i] = malloc(size);
        allocation_sizes[i] = size;
        
        if (allocations[i]) {
            successful_allocations++;
            // Write some data to verify memory is usable
            memset(allocations[i], 0xAA, size);
        }
    }
    
    printf("Successfully allocated %d out of 100 blocks\n", successful_allocations);
    ASSERT(successful_allocations > 0, "At least some allocations should succeed");
    
    // Verify data integrity for successful allocations
    for (int i = 0; i < 100; i++) {
        if (allocations[i]) {
            uint8_t* ptr = (uint8_t*)allocations[i];
            for (size_t j = 0; j < allocation_sizes[i]; j++) {
                if (ptr[j] != 0xAA) {
                    // Data corruption detected
                    ASSERT(false, "Memory data should be preserved");
                    break;
                }
            }
        }
    }
    
    // Clean up
    for (int i = 0; i < 100; i++) {
        if (allocations[i]) {
            free(allocations[i]);
        }
    }
    
    TEST_PASS();
}

// Test component lifecycle simulation
void test_component_lifecycle_simulation(void) {
    printf("Testing component lifecycle simulation...\n");
    
    // Simulate kernel startup sequence
    printf("  Simulating kernel startup...\n");
    
    // 1. Initialize memory management
    memory_stats_t mem_stats;
    memset(&mem_stats, 0, sizeof(mem_stats));
    memory_get_stats(&mem_stats);
    printf("  Memory stats obtained\n");
    
    // 2. Create initial processes
    process_t* init_process = malloc(sizeof(process_t));
    ASSERT_NOT_NULL(init_process, "Init process should be created");
    
    if (init_process) {
        memset(init_process, 0, sizeof(process_t));
        init_process->pid = 1;
        strcpy(init_process->name, "init");
        init_process->state = PROC_STATE_RUNNING;
        printf("  Init process created (PID: %llu)\n", (unsigned long long)init_process->pid);
    }
    
    // 3. Initialize filesystem structures
    fs_boot_sector_t* boot_sector = malloc(sizeof(fs_boot_sector_t));
    ASSERT_NOT_NULL(boot_sector, "Boot sector should be allocated");
    
    if (boot_sector) {
        memset(boot_sector, 0, sizeof(fs_boot_sector_t));
        boot_sector->magic = FS_MAGIC;
        boot_sector->version = FS_VERSION;
        printf("  Filesystem boot sector initialized\n");
    }
    
    // 4. Simulate normal operation
    printf("  Simulating normal operation...\n");
    for (int i = 0; i < 5; i++) {
        process_t* user_process = malloc(sizeof(process_t));
        if (user_process) {
            memset(user_process, 0, sizeof(process_t));
            user_process->pid = 100 + i;
            snprintf(user_process->name, PROCESS_NAME_LEN, "user_%d", i);
            user_process->state = PROC_STATE_READY;
            
            // Simulate process execution
            user_process->state = PROC_STATE_RUNNING;
            user_process->cpu_time += 100;
            user_process->state = PROC_STATE_ZOMBIE;
            
            free(user_process);
        }
    }
    
    // 5. Simulate shutdown
    printf("  Simulating shutdown...\n");
    if (init_process) {
        init_process->state = PROC_STATE_ZOMBIE;
        free(init_process);
        printf("  Init process terminated\n");
    }
    
    if (boot_sector) {
        free(boot_sector);
        printf("  Filesystem structures cleaned up\n");
    }
    
    TEST_PASS();
}

// Create test suite
test_suite_t* create_kernel_integration_test_suite(void) {
    static test_suite_t suite;
    test_suite_init(&suite, "Kernel Integration Tests");
    
    test_suite_add_test(&suite, "memory_process_integration", test_memory_process_integration);
    test_suite_add_test(&suite, "filesystem_memory_integration", test_filesystem_memory_integration);
    test_suite_add_test(&suite, "multiple_component_allocation", test_multiple_component_allocation);
    test_suite_add_test(&suite, "process_creation_simulation", test_process_creation_simulation);
    test_suite_add_test(&suite, "filesystem_metadata_simulation", test_filesystem_metadata_simulation);
    test_suite_add_test(&suite, "memory_pressure_simulation", test_memory_pressure_simulation);
    test_suite_add_test(&suite, "component_lifecycle_simulation", test_component_lifecycle_simulation);
    
    return &suite;
}
