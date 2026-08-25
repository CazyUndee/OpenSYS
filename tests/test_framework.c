/*
 * test_framework.c - Simple Test Framework Implementation
 *
 * Copyright (C) 2026 CazyUndee
 */

#include "test_framework.h"
#include <stdlib.h>
#include <setjmp.h>
#include <signal.h>

/* jmp_buf for ASSERT macros to jump back to the test runner */
jmp_buf __test_jmp_buf;

void test_suite_init(test_suite_t* suite, const char* name) {
    suite->suite_name = name;
    suite->tests = NULL;
    suite->test_count = 0;
    suite->passed = 0;
    suite->failed = 0;
}

void test_suite_add_test(test_suite_t* suite, const char* name, void (*test_func)(void)) {
    suite->tests = realloc(suite->tests, (suite->test_count + 1) * sizeof(test_case_t));
    suite->tests[suite->test_count].name = name;
    suite->tests[suite->test_count].test_func = test_func;
    suite->test_count++;
}

void test_suite_run(test_suite_t* suite) {
    printf("\n=== Running Test Suite: %s ===\n", suite->suite_name);
    
    for (uint32_t i = 0; i < suite->test_count; i++) {
        printf("Running: %s\n", suite->tests[i].name);
        
        if (setjmp(__test_jmp_buf) == 0) {
            /* Normal execution — run the test.
             * If an ASSERT fails, it calls longjmp which lands here with value != 0 */
            suite->tests[i].test_func();
            suite->passed++;
        } else {
            /* Returned via longjmp from a failed ASSERT */
            suite->failed++;
        }
    }
}

void test_suite_print_summary(test_suite_t* suite) {
    printf("\n=== Test Suite Summary: %s ===\n", suite->suite_name);
    printf("Total: %u, Passed: %u, Failed: %u\n", 
           suite->test_count, suite->passed, suite->failed);
    
    if (suite->failed == 0) {
        printf("✓ All tests passed!\n");
    } else {
        printf("✗ %u tests failed!\n", suite->failed);
    }
}
