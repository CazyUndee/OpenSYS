/*
 * test_process.c - Unit Tests for Process Management
 *
 * Copyright (C) 2026 CazyUndee
 */

#include "../test_framework.h"
#include "../../include/process.h"
#include <stdlib.h>
#include <string.h>

// Test process constants
void test_process_constants(void) {
    printf("Testing process constants...\n");
    
    ASSERT_EQ(MAX_PROCESSES, 64, "Max processes should be 64");
    ASSERT_EQ(PROCESS_NAME_LEN, 32, "Process name length should be 32");
    ASSERT_EQ(sizeof(pid_t), 8, "PID should be 8 bytes (uint64_t)");
    
    TEST_PASS();
}

// Test process state enum values
void test_process_states(void) {
    printf("Testing process states...\n");
    
    ASSERT_EQ(PROC_STATE_UNUSED, 0, "Unused state should be 0");
    ASSERT_EQ(PROC_STATE_READY, 1, "Ready state should be 1");
    ASSERT_EQ(PROC_STATE_RUNNING, 2, "Running state should be 2");
    ASSERT_EQ(PROC_STATE_BLOCKED, 3, "Blocked state should be 3");
    ASSERT_EQ(PROC_STATE_ZOMBIE, 4, "Zombie state should be 4");
    
    // Test that states are sequential
    ASSERT(PROC_STATE_READY > PROC_STATE_UNUSED, "States should be in order");
    ASSERT(PROC_STATE_RUNNING > PROC_STATE_READY, "States should be in order");
    ASSERT(PROC_STATE_BLOCKED > PROC_STATE_RUNNING, "States should be in order");
    ASSERT(PROC_STATE_ZOMBIE > PROC_STATE_BLOCKED, "States should be in order");
    
    TEST_PASS();
}

// Test CPU context structure
void test_cpu_context_structure(void) {
    printf("Testing CPU context structure...\n");
    
    cpu_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    
    // Test setting and getting register values
    ctx.rax = 0x123456789ABCDEF0;
    ctx.rbx = 0xFEDCBA9876543210;
    ctx.rip = 0x400000;
    ctx.rsp = 0x7FFFFF000;
    ctx.rflags = 0x202;
    
    ASSERT_EQ(ctx.rax, 0x123456789ABCDEF0, "RAX register should be accessible");
    ASSERT_EQ(ctx.rbx, 0xFEDCBA9876543210, "RBX register should be accessible");
    ASSERT_EQ(ctx.rip, 0x400000, "RIP register should be accessible");
    ASSERT_EQ(ctx.rsp, 0x7FFFFF000, "RSP register should be accessible");
    ASSERT_EQ(ctx.rflags, 0x202, "RFLAGS register should be accessible");
    
    // Test structure size (20 x uint64_t = 160 bytes)
    ASSERT_EQ(sizeof(cpu_context_t), 160, "CPU context should be 160 bytes");
    
    TEST_PASS();
}

// Test process control block structure
void test_process_control_block(void) {
    printf("Testing process control block...\n");
    
    process_t proc;
    memset(&proc, 0, sizeof(proc));
    
    // Test setting and getting process fields
    proc.pid = 1234;
    strncpy(proc.name, "test_process", PROCESS_NAME_LEN - 1);
    proc.state = PROC_STATE_READY;
    proc.priority = 5;
    proc.exit_code = 0;
    proc.cpu_time = 1000;
    proc.wake_time = 5000;
    
    ASSERT_EQ(proc.pid, 1234, "PID should be accessible");
    ASSERT(strcmp(proc.name, "test_process") == 0, "Process name should be accessible");
    ASSERT_EQ(proc.state, PROC_STATE_READY, "Process state should be accessible");
    ASSERT_EQ(proc.priority, 5, "Priority should be accessible");
    ASSERT_EQ(proc.exit_code, 0, "Exit code should be accessible");
    ASSERT_EQ(proc.cpu_time, 1000, "CPU time should be accessible");
    ASSERT_EQ(proc.wake_time, 5000, "Wake time should be accessible");
    
    // Test structure size
    ASSERT(sizeof(process_t) > 0, "Process structure should have size");
    ASSERT_EQ(sizeof(proc.pid), 8, "PID should be 8 bytes");
    ASSERT_EQ(sizeof(proc.state), 4, "State should be 4 bytes (enum)");
    
    TEST_PASS();
}

// Test process name handling
void test_process_name_handling(void) {
    printf("Testing process name handling...\n");
    
    process_t proc;
    memset(&proc, 0, sizeof(proc));
    
    // Test normal name
    strcpy(proc.name, "init");
    ASSERT(strcmp(proc.name, "init") == 0, "Normal name should work");
    
    // Test maximum length name
    char long_name[PROCESS_NAME_LEN];
    memset(long_name, 'A', PROCESS_NAME_LEN - 1);
    long_name[PROCESS_NAME_LEN - 1] = '\0';
    
    strncpy(proc.name, long_name, PROCESS_NAME_LEN - 1);
    proc.name[PROCESS_NAME_LEN - 1] = '\0';
    ASSERT(strcmp(proc.name, long_name) == 0, "Max length name should work");
    
    // Test name truncation
    char too_long_name[PROCESS_NAME_LEN + 10];
    memset(too_long_name, 'B', sizeof(too_long_name) - 1);
    too_long_name[sizeof(too_long_name) - 1] = '\0';
    
    strncpy(proc.name, too_long_name, PROCESS_NAME_LEN - 1);
    proc.name[PROCESS_NAME_LEN - 1] = '\0';
    ASSERT(strlen(proc.name) <= PROCESS_NAME_LEN - 1, "Name should be truncated");
    
    TEST_PASS();
}

// Test process state transitions
void test_process_state_transitions(void) {
    printf("Testing process state transitions...\n");
    
    process_t proc;
    memset(&proc, 0, sizeof(proc));
    
    // Test initial state
    proc.state = PROC_STATE_UNUSED;
    ASSERT_EQ(proc.state, PROC_STATE_UNUSED, "Initial state should be UNUSED");
    
    // Test state transitions
    proc.state = PROC_STATE_READY;
    ASSERT_EQ(proc.state, PROC_STATE_READY, "Should transition to READY");
    
    proc.state = PROC_STATE_RUNNING;
    ASSERT_EQ(proc.state, PROC_STATE_RUNNING, "Should transition to RUNNING");
    
    proc.state = PROC_STATE_BLOCKED;
    ASSERT_EQ(proc.state, PROC_STATE_BLOCKED, "Should transition to BLOCKED");
    
    proc.state = PROC_STATE_ZOMBIE;
    ASSERT_EQ(proc.state, PROC_STATE_ZOMBIE, "Should transition to ZOMBIE");
    
    proc.state = PROC_STATE_UNUSED;
    ASSERT_EQ(proc.state, PROC_STATE_UNUSED, "Should transition back to UNUSED");
    
    TEST_PASS();
}

// Test process priority handling
void test_process_priority(void) {
    printf("Testing process priority...\n");
    
    process_t proc;
    memset(&proc, 0, sizeof(proc));
    
    // Test different priority values
    proc.priority = 0;  // Lowest priority
    ASSERT_EQ(proc.priority, 0, "Priority 0 should be valid");
    
    proc.priority = 10; // Medium priority
    ASSERT_EQ(proc.priority, 10, "Priority 10 should be valid");
    
    proc.priority = 20; // High priority
    ASSERT_EQ(proc.priority, 20, "Priority 20 should be valid");
    
    proc.priority = -1; // Negative priority (might be invalid)
    ASSERT_EQ(proc.priority, -1, "Negative priority should be stored");
    
    TEST_PASS();
}

// Test process timing
void test_process_timing(void) {
    printf("Testing process timing...\n");
    
    process_t proc;
    memset(&proc, 0, sizeof(proc));
    
    // Test CPU time tracking
    proc.cpu_time = 0;
    ASSERT_EQ(proc.cpu_time, 0, "Initial CPU time should be 0");
    
    proc.cpu_time = 1000; // 1000 time units
    ASSERT_EQ(proc.cpu_time, 1000, "CPU time should be trackable");
    
    proc.cpu_time += 500; // Add more time
    ASSERT_EQ(proc.cpu_time, 1500, "CPU time should accumulate");
    
    // Test wake time
    proc.wake_time = 0;
    ASSERT_EQ(proc.wake_time, 0, "Initial wake time should be 0");
    
    proc.wake_time = 10000; // Wake at time 10000
    ASSERT_EQ(proc.wake_time, 10000, "Wake time should be settable");
    
    TEST_PASS();
}

// Test process linked list structure
void test_process_linked_list(void) {
    printf("Testing process linked list structure...\n");
    
    process_t proc1, proc2, proc3;
    memset(&proc1, 0, sizeof(proc1));
    memset(&proc2, 0, sizeof(proc2));
    memset(&proc3, 0, sizeof(proc3));
    
    // Set up PIDs for identification
    proc1.pid = 1;
    proc2.pid = 2;
    proc3.pid = 3;
    
    // Test linking processes
    proc1.next = &proc2;
    proc2.next = &proc3;
    proc3.next = NULL;
    
    proc2.prev = &proc1;
    proc3.prev = &proc2;
    proc1.prev = NULL;
    
    // Test forward traversal
    ASSERT_EQ(proc1.next, &proc2, "Forward link should work");
    ASSERT_EQ(proc2.next, &proc3, "Forward link should work");
    ASSERT_EQ(proc3.next, NULL, "Last process should point to NULL");
    
    // Test backward traversal
    ASSERT_EQ(proc3.prev, &proc2, "Backward link should work");
    ASSERT_EQ(proc2.prev, &proc1, "Backward link should work");
    ASSERT_EQ(proc1.prev, NULL, "First process should point to NULL");
    
    // Test circularity check
    ASSERT(proc1.next->pid == 2, "Next process PID should be correct");
    ASSERT(proc2.prev->pid == 1, "Previous process PID should be correct");
    
    TEST_PASS();
}

// Test process function pointer type
void test_process_function_pointer(void) {
    printf("Testing process function pointer type...\n");
    
    // Test that process_entry_t is a function pointer
    ASSERT(sizeof(process_entry_t) == sizeof(void*), "Function pointer should be pointer-sized");
    
    // Test with a dummy function
    void dummy_process(void* arg) {
        (void)arg; // unused parameter
    }
    
    process_entry_t entry_func = dummy_process;
    ASSERT(entry_func != NULL, "Function pointer should be assignable");
    ASSERT(entry_func == dummy_process, "Function pointer should match original");
    
    TEST_PASS();
}

// Create test suite
test_suite_t* create_process_test_suite(void) {
    static test_suite_t suite;
    test_suite_init(&suite, "Process Management Tests");
    
    test_suite_add_test(&suite, "process_constants", test_process_constants);
    test_suite_add_test(&suite, "process_states", test_process_states);
    test_suite_add_test(&suite, "cpu_context_structure", test_cpu_context_structure);
    test_suite_add_test(&suite, "process_control_block", test_process_control_block);
    test_suite_add_test(&suite, "process_name_handling", test_process_name_handling);
    test_suite_add_test(&suite, "process_state_transitions", test_process_state_transitions);
    test_suite_add_test(&suite, "process_priority", test_process_priority);
    test_suite_add_test(&suite, "process_timing", test_process_timing);
    test_suite_add_test(&suite, "process_linked_list", test_process_linked_list);
    test_suite_add_test(&suite, "process_function_pointer", test_process_function_pointer);
    
    return &suite;
}
