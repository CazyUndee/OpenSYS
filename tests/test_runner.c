/*
 * test_runner.c - Main Test Runner for Plan 0
 *
 * Copyright (C) 2026 CazyUndee
 */

#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// External test suite declarations
extern test_suite_t* create_memory_test_suite(void);
extern test_suite_t* create_filesystem_test_suite(void);
extern test_suite_t* create_process_test_suite(void);
extern test_suite_t* create_nl_parser_test_suite(void);
extern test_suite_t* create_kernel_integration_test_suite(void);
extern test_suite_t* create_vfile_test_suite(void);
extern test_suite_t* create_ramfs_test_suite(void);
extern test_suite_t* create_part_test_suite(void);
extern test_suite_t* create_ns_test_suite(void);
extern test_suite_t* create_fs_volume_test_suite(void);

// Test suite registry
typedef struct {
    const char* name;
    test_suite_t* (*create_suite)(void);
    int enabled;
} test_suite_info_t;

static test_suite_info_t test_suites[] = {
    {"Memory Management", create_memory_test_suite, 1},
    {"Filesystem", create_filesystem_test_suite, 1},
    {"Process Management", create_process_test_suite, 1},
    {"Natural Language Parser", create_nl_parser_test_suite, 1},
    {"Kernel Integration", create_kernel_integration_test_suite, 1},
    {"Virtual Filesystem", create_vfile_test_suite, 1},
    {"RAM Filesystem", create_ramfs_test_suite, 1},
    {"Partition Manager", create_part_test_suite, 1},
    {"Namespace Resolver", create_ns_test_suite, 1},
    {"FS Volume Binding", create_fs_volume_test_suite, 1},
};

static const int num_test_suites = sizeof(test_suites) / sizeof(test_suite_info_t);

// Print usage information
void print_usage(const char* program_name) {
    printf("Usage: %s [options]\n", program_name);
    printf("Options:\n");
    printf("  -h, --help          Show this help message\n");
    printf("  -l, --list          List available test suites\n");
    printf("  -s, --suite <name>  Run specific test suite\n");
    printf("  -v, --verbose       Verbose output\n");
    printf("  -q, --quiet         Quiet output (show only summary)\n");
    printf("\nAvailable test suites:\n");
    for (int i = 0; i < num_test_suites; i++) {
        printf("  %-20s %s\n", test_suites[i].name, 
               test_suites[i].enabled ? "(enabled)" : "(disabled)");
    }
}

// List available test suites
void list_test_suites(void) {
    printf("Available test suites:\n");
    for (int i = 0; i < num_test_suites; i++) {
        printf("  %-20s %s\n", test_suites[i].name,
               test_suites[i].enabled ? "(enabled)" : "(disabled)");
    }
}

// Find test suite by name
test_suite_info_t* find_test_suite(const char* name) {
    for (int i = 0; i < num_test_suites; i++) {
        if (strcmp(test_suites[i].name, name) == 0) {
            return &test_suites[i];
        }
    }
    return NULL;
}

// Run all enabled test suites
int run_all_tests(int verbose) {
    int total_passed = 0;
    int total_failed = 0;
    int suites_run = 0;
    
    printf("\n");
    printf("========================================\n");
    printf("Plan 0 Test Runner\n");
    printf("========================================\n");
    
    for (int i = 0; i < num_test_suites; i++) {
        if (!test_suites[i].enabled) {
            continue;
        }
        
        test_suite_t* suite = test_suites[i].create_suite();
        if (suite) {
            test_suite_run(suite);
            total_passed += suite->passed;
            total_failed += suite->failed;
            suites_run++;
            
            if (verbose) {
                test_suite_print_summary(suite);
            }
        }
    }
    
    printf("\n========================================\n");
    printf("Overall Test Results\n");
    printf("========================================\n");
    printf("Test suites run: %d\n", suites_run);
    printf("Total tests: %d\n", total_passed + total_failed);
    printf("Passed: %d\n", total_passed);
    printf("Failed: %d\n", total_failed);
    
    if (total_failed == 0) {
        printf("✓ All tests passed!\n");
        return 0;
    } else {
        printf("✗ %d tests failed!\n", total_failed);
        return 1;
    }
}

// Run specific test suite
int run_test_suite(const char* name, int verbose) {
    test_suite_info_t* suite_info = find_test_suite(name);
    if (!suite_info) {
        printf("Error: Test suite '%s' not found\n", name);
        return 1;
    }
    
    if (!suite_info->enabled) {
        printf("Error: Test suite '%s' is disabled\n", name);
        return 1;
    }
    
printf("\n");
printf("========================================\n");
printf("Plan 0 Test Runner\n");
printf("========================================\n");

test_suite_t* suite = suite_info->create_suite();
    if (suite) {
        test_suite_run(suite);
        
        if (verbose) {
            test_suite_print_summary(suite);
        }
        
        printf("\n========================================\n");
        printf("Test Suite Results\n");
        printf("========================================\n");
        printf("Test suite: %s\n", suite->suite_name);
        printf("Total tests: %d\n", suite->passed + suite->failed);
        printf("Passed: %d\n", suite->passed);
        printf("Failed: %d\n", suite->failed);
        
        if (suite->failed == 0) {
            printf("✓ All tests passed!\n");
            return 0;
        } else {
            printf("✗ %d tests failed!\n", suite->failed);
            return 1;
        }
    }
    
    return 1;
}

int main(int argc, char* argv[]) {
    int verbose = 1;
    const char* specific_suite = NULL;
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--list") == 0) {
            list_test_suites();
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "--quiet") == 0) {
            verbose = 0;
        } else if ((strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--suite") == 0) && i + 1 < argc) {
            specific_suite = argv[++i];
        } else {
            printf("Error: Unknown option '%s'\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }
    
    // Run tests
    if (specific_suite) {
        return run_test_suite(specific_suite, verbose);
    } else {
        return run_all_tests(verbose);
    }
}
