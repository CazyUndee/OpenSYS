/*
 * test_framework.h - Simple Test Framework for Plan 0
 *
 * Copyright (C) 2026 CazyUndee
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdint.h>
#include <stdio.h>
#include <setjmp.h>

typedef struct {
    const char* name;
    void (*test_func)(void);
} test_case_t;

typedef struct {
    const char* suite_name;
    test_case_t* tests;
    uint32_t test_count;
    uint32_t passed;
    uint32_t failed;
} test_suite_t;

/* jmp_buf for ASSERT macros to longjmp back to the test runner */
extern jmp_buf __test_jmp_buf;

/* Test assertion macros — use longjmp so the test runner tracks failures */
#define ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            printf("FAIL: %s - %s\n", __func__, message); \
            longjmp(__test_jmp_buf, 1); \
        } \
    } while(0)

#define ASSERT_EQ(expected, actual, message) \
    do { \
        if ((expected) != (actual)) { \
            printf("FAIL: %s - %s (expected: %llu, actual: %llu)\n", \
                   __func__, message, (unsigned long long)(expected), \
                   (unsigned long long)(actual)); \
            longjmp(__test_jmp_buf, 1); \
        } \
    } while(0)

#define ASSERT_NE(expected, actual, message) \
    do { \
        if ((expected) == (actual)) { \
            printf("FAIL: %s - %s (values should not be equal: %llu)\n", \
                   __func__, message, (unsigned long long)(expected)); \
            longjmp(__test_jmp_buf, 1); \
        } \
    } while(0)

#define ASSERT_NULL(ptr, message) \
    do { \
        if ((ptr) != NULL) { \
            printf("FAIL: %s - %s (expected NULL, got %p)\n", \
                   __func__, message, (void*)(ptr)); \
            longjmp(__test_jmp_buf, 1); \
        } \
    } while(0)

#define ASSERT_NOT_NULL(ptr, message) \
    do { \
        if ((ptr) == NULL) { \
            printf("FAIL: %s - %s (expected non-NULL)\n", \
                   __func__, message); \
            longjmp(__test_jmp_buf, 1); \
        } \
    } while(0)

#define TEST_PASS() \
    do { \
        printf("PASS: %s\n", __func__); \
        return; \
    } while(0)

/* Test framework functions */
void test_suite_init(test_suite_t* suite, const char* name);
void test_suite_add_test(test_suite_t* suite, const char* name, void (*test_func)(void));
void test_suite_run(test_suite_t* suite);
void test_suite_print_summary(test_suite_t* suite);

#endif // TEST_FRAMEWORK_H
