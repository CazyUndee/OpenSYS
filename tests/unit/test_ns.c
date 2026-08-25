/*
 * test_ns.c - Unit Tests for the Plan0 Unified Resource Namespace
 *
 * Copyright (C) 2026 CazyUndee
 * SPDX-License-Identifier: AGPL-3.0
 *
 * Compiles the real ns.c (resolver) against mocked disk/gpt/part and PMM
 * functions, validating: path grammar, alias-to-canonical equivalence,
 * resource classification, partition index rules, and structured errors.
 */

#include <string.h>
#include "../test_framework.h"
#include "../../include/ns.h"

/* Mock control hooks (tests/mocks/mock_kernel.c) */
void mock_disk_set_ready(int ready);
void mock_gpt_setup(const void* entries, unsigned count);

/* Suite-order independence: other suites leave part.c marked ready via
 * shared static state; reset it explicitly before asserting on it. */
void part_init(void);

/* --- helpers ------------------------------------------------------- */

static void test_grammar_valid_paths(void) {
    ASSERT(ns_parse_valid("0/hmr"), "short alias path is valid");
    ASSERT(ns_parse_valid("0/hardware/storage/ssd"), "full canonical path is valid");
    ASSERT(ns_parse_valid("0/hardware/storage/ssd/partitions/1"), "partition path is valid");
    ASSERT(ns_parse_valid("0/HSS"), "uppercase input accepted (normalized)");
    ASSERT(ns_parse_valid("0/user/documents/readme.txt"), "user store paths are valid");
    ASSERT(ns_parse_valid("9/main/index.html"), "future numeric domain parses");
    ASSERT(ns_parse_valid("0/trailing/"), "trailing slash allowed");
    TEST_PASS();
}

static void test_grammar_rejects(void) {
    ASSERT(!ns_parse_valid(""), "empty path rejected");
    ASSERT(!ns_parse_valid("/proc/uptime"), "leading-slash Unix path rejected");
    /* Bare root "0" is VALID by design (machine root); covered in classification. */
    ASSERT(!ns_parse_valid("0//double"), "empty component rejected");
    ASSERT(!ns_parse_valid("0/bad name"), "space rejected");
    ASSERT(!ns_parse_valid("0/a/../b"), "all-dot component rejected (no traversal)");
    ASSERT(ns_parse_valid("0/file.txt"), "dots inside components allowed");
    ASSERT(!ns_parse_valid("x/hss"), "non-numeric root rejected");
    TEST_PASS();
}

static void test_alias_expansion(void) {
    char canon[NS_MAX_PATH];

    ASSERT(ns_canonical("0/hss", canon, sizeof(canon)) == 0, "expand hss");
    ASSERT(strcmp(canon, "0/hardware/storage/ssd") == 0, "hss -> hardware/storage/ssd");

    ASSERT(ns_canonical("0/HMR", canon, sizeof(canon)) == 0, "uppercase alias expands");
    ASSERT(strcmp(canon, "0/hardware/memory/ram") == 0, "hmr -> hardware/memory/ram");

    /* Alias covers the whole subtree at component boundaries */
    ASSERT(ns_canonical("0/hss/partitions/1", canon, sizeof(canon)) == 0, "subtree alias expands");
    ASSERT(strcmp(canon, "0/hardware/storage/ssd/partitions/1") == 0,
           "alias expansion covers partitions subtree");

    /* Component boundary: a longer component sharing the prefix must NOT expand */
    ASSERT(ns_canonical("0/hsss", canon, sizeof(canon)) == 0, "hsss parses");
    ASSERT(strcmp(canon, "0/hsss") == 0, "hsss is not hijacked by the hss alias");

    /* Canonical input is its own canonical form */
    ASSERT(ns_canonical("0/hardware/storage/hdd", canon, sizeof(canon)) == 0, "canonical passes through");
    ASSERT(strcmp(canon, "0/hardware/storage/hdd") == 0, "no alias leaves path unchanged");
    TEST_PASS();
}

static void test_alias_equivalence(void) {
    /* Alias and canonical must resolve to the IDENTICAL resource. */
    ns_resource_t a, b;
    ASSERT(ns_resolve("0/hss", &a) == 0, "resolve via alias");
    ASSERT(ns_resolve("0/hardware/storage/ssd", &b) == 0, "resolve via canonical");

    ASSERT(a.kind == b.kind, "same kind");
    ASSERT(a.domain == b.domain, "same domain");
    ASSERT(a.part_index == b.part_index, "same part_index");
    ASSERT(a.device != 0 && b.device != 0 && strcmp(a.device, b.device) == 0, "same device");
    ASSERT(strcmp(a.canonical, "0/hardware/storage/ssd") == 0, "alias resolves to canonical spelling");
    ASSERT(strcmp(b.canonical, "0/hardware/storage/ssd") == 0, "canonical is stable");

    /* Same for an aliased partition path */
    ASSERT(ns_resolve("0/hsh/partitions/2", &a) == 0, "aliased partition resolves");
    ASSERT(ns_resolve("0/hardware/storage/hdd/partitions/2", &b) == 0, "canonical partition resolves");
    ASSERT(a.kind == NS_PARTITION && b.kind == NS_PARTITION, "both are partitions");
    ASSERT(a.part_index == 2 && b.part_index == 2, "same partition number");
    ASSERT(strcmp(a.canonical, b.canonical) == 0, "identical canonical paths");
    TEST_PASS();
}

static void test_classification(void) {
    ns_resource_t r;

    ASSERT(ns_resolve("0", &r) == 0 && r.kind == NS_ROOT, "bare root is machine root");
    ASSERT(ns_resolve("0/hardware", &r) == 0 && r.kind == NS_HARDWARE_DIR, "hardware is structural dir");
    ASSERT(ns_resolve("0/hardware/storage", &r) == 0 && r.kind == NS_HARDWARE_DIR, "storage is structural dir");
    ASSERT(ns_resolve("0/hardware/memory/ram", &r) == 0 && r.kind == NS_MEMORY_RAM, "ram classifies as memory");
    ASSERT(ns_resolve("0/hardware/cpu", &r) == 0 && r.kind == NS_CPU, "cpu classifies");
    ASSERT(ns_resolve("0/hardware/storage/hdd", &r) == 0 && r.kind == NS_STORAGE_DEVICE, "hdd is a device");
    ASSERT(r.device && strcmp(r.device, "hdd") == 0, "device name recorded");
    ASSERT(ns_resolve("0/hardware/storage/hdd/partitions", &r) == 0 &&
           r.kind == NS_PARTITIONS_DIR, "partitions dir classifies");
    ASSERT(ns_resolve("0/user/docs", &r) == 0 && r.kind == NS_USER_STORE, "user store classifies");

    ASSERT(ns_resolve("0/hardware/storage/ssd/partitions/3", &r) == 0 &&
           r.kind == NS_PARTITION && r.part_index == 3, "partition index parsed");
    TEST_PASS();
}

static void test_errors(void) {
    ns_resource_t r;

    ASSERT(ns_resolve("/dev/null", &r) < 0, "Unix-style path is a parse error");
    ASSERT(ns_resolve("0/nothing/here", &r) == -2, "unknown branch -> unresolved (-2)");
    ASSERT(ns_resolve("0/hardware/storage/tape", &r) == -2, "unknown storage type unresolved");
    ASSERT(ns_resolve("0/hardware/storage/ssd/partitions/0", &r) == -2,
           "partition 0 invalid (1-based)");
    ASSERT(ns_resolve("0/hardware/storage/ssd/partitions/abc", &r) == -2,
           "non-numeric partition invalid");
    ASSERT(ns_resolve("0/hardware/storage/ssd/partitions/99/deep", &r) == -2,
           "depth below a partition unresolved");
    ASSERT(ns_resolve("0/hardware/memory/swap", &r) == -2, "undefined memory node unresolved");
    ASSERT(ns_describe("garbage", 0, 0) < 0, "describe rejects garbage grammar");
    TEST_PASS();
}

static void test_partition_requires_ready_backend(void) {
    /* Reset backend state: no disk, empty GPT table -> part_init fails
     * and clears its readiness flag. Resolution still classifies; the
     * description reports the unavailable backend instead of crashing. */
    mock_disk_set_ready(0);
    mock_gpt_setup(0, 0);
    part_init();
    char text[512];
    ASSERT(ns_describe("0/hardware/storage/ssd/partitions/1", text, sizeof(text)) == 0,
           "describe works without hardware");
    ASSERT(strstr(text, "unavailable") != 0 || strstr(text, "none attached") != 0,
           "backend unavailability reported");
    TEST_PASS();
}

test_suite_t* create_ns_test_suite(void) {
    static test_suite_t suite;
    test_suite_init(&suite, "Namespace Resolver");

    test_suite_add_test(&suite, "grammar_valid_paths", test_grammar_valid_paths);
    test_suite_add_test(&suite, "grammar_rejects", test_grammar_rejects);
    test_suite_add_test(&suite, "alias_expansion", test_alias_expansion);
    test_suite_add_test(&suite, "alias_equivalence", test_alias_equivalence);
    test_suite_add_test(&suite, "classification", test_classification);
    test_suite_add_test(&suite, "errors", test_errors);
    test_suite_add_test(&suite, "partition_requires_ready_backend",
                        test_partition_requires_ready_backend);

    return &suite;
}
