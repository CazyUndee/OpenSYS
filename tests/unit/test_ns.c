/*
 * test_ns.c - Unit Tests for the Plan0 Unified Resource Namespace
 *
 * Copyright (C) 2026 CazyUndee
 * SPDX-License-Identifier: AGPL-3.0
 *
 * Compiles the real ns.c (resolver + shell-argument translation) against
 * mocked disk/gpt/part and PMM functions, validating: path grammar,
 * alias-to-canonical equivalence, truthful storage topology (a device
 * class exists only when detected hardware belongs to it), partition
 * volume translation, and structured errors.
 */

#include <string.h>
#include "../test_framework.h"
#include "../../include/ns.h"
#include "../../include/volume.h"
#include "../../include/gpt.h"

/* Mock control hooks (tests/mocks/mock_kernel.c) */
void mock_disk_set_ready(int ready);
void mock_gpt_setup(const void* entries, unsigned count);
void mock_disk_set_ssd(int ssd);

/* Suite-order independence: other suites leave part.c marked ready via
 * shared static state; reset it explicitly before asserting on it. */
void part_init(void);

/* Two used GPT slots on the default HDD-class device: p1 @2048-4095,
 * p2 @4096-8191. */
static void setup_two_partitions_for_ns(void) {
    gpt_entry_t t[2];
    memset(t, 0, sizeof(t));
    memcpy(t[0].type_guid, GPT_TYPE_LINUX_FS, 16);
    t[0].start_lba = 2048;
    t[0].end_lba   = 4095;
    memcpy(t[1].type_guid, GPT_TYPE_LINUX_FS, 16);
    t[1].start_lba = 4096;
    t[1].end_lba   = 8191;
    mock_disk_set_ssd(0);
    mock_disk_set_ready(1);
    mock_gpt_setup(t, 2);
    part_init();
}

/* --- grammar ------------------------------------------------------- */

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
    ASSERT(!ns_parse_valid("/legacy/path"), "leading-slash non-namespace path rejected");
    /* Bare root "0" is VALID by design (machine root); covered in classification. */
    ASSERT(!ns_parse_valid("0//double"), "empty component rejected");
    ASSERT(!ns_parse_valid("0/bad name"), "space rejected");
    ASSERT(!ns_parse_valid("0/a/../b"), "all-dot component rejected (no traversal)");
    ASSERT(ns_parse_valid("0/file.txt"), "dots inside components allowed");
    ASSERT(!ns_parse_valid("x/hss"), "non-numeric root rejected");
    TEST_PASS();
}

/* --- aliases ------------------------------------------------------- */

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
    ASSERT(ns_resolve("0/hsh", &a) == 0, "resolve via alias");
    ASSERT(ns_resolve("0/hardware/storage/hdd", &b) == 0, "resolve via canonical");

    ASSERT(a.kind == b.kind, "same kind");
    ASSERT(a.domain == b.domain, "same domain");
    ASSERT(a.part_index == b.part_index, "same part_index");
    ASSERT(a.device != 0 && b.device != 0 && strcmp(a.device, b.device) == 0, "same device");
    ASSERT(strcmp(a.canonical, "0/hardware/storage/hdd") == 0, "alias resolves to canonical spelling");
    ASSERT(strcmp(b.canonical, "0/hardware/storage/hdd") == 0, "canonical is stable");

    /* Same for an aliased partition path */
    ASSERT(ns_resolve("0/hsh/partitions/2", &a) == 0, "aliased partition resolves");
    ASSERT(ns_resolve("0/hardware/storage/hdd/partitions/2", &b) == 0, "canonical partition resolves");
    ASSERT(a.kind == NS_PARTITION && b.kind == NS_PARTITION, "both are partitions");
    ASSERT(a.part_index == 2 && b.part_index == 2, "same partition number");
    ASSERT(strcmp(a.canonical, b.canonical) == 0, "identical canonical paths");
    TEST_PASS();
}

/* --- classification ------------------------------------------------ */

static void test_classification(void) {
    ns_resource_t r;

    ASSERT(ns_resolve("0", &r) == 0 && r.kind == NS_ROOT, "bare root is machine root");
    ASSERT(ns_resolve("0/hardware", &r) == 0 && r.kind == NS_HARDWARE_DIR, "hardware is structural dir");
    ASSERT(ns_resolve("0/hardware/storage", &r) == 0 && r.kind == NS_HARDWARE_DIR, "storage is structural dir");
    ASSERT(ns_resolve("0/hardware/memory/ram", &r) == 0 && r.kind == NS_MEMORY_RAM, "ram classifies as memory");
    ASSERT(ns_resolve("0/hardware/cpu", &r) == 0 && r.kind == NS_CPU, "cpu classifies");

    /* Detected device class: fixture disk reports HDD. */
    ASSERT(ns_resolve("0/hardware/storage/hdd", &r) == 0 && r.kind == NS_STORAGE_DEVICE,
           "attached device class resolves");
    ASSERT(r.device && strcmp(r.device, "hdd") == 0, "device name recorded");

    /* Topology honesty: classes with no attached hardware are UNKNOWN. */
    ASSERT(ns_resolve("0/hardware/storage/ssd", &r) == -2, "absent ssd class unknown");
    ASSERT(ns_resolve("0/hardware/storage/ssd/partitions/1", &r) == -2,
           "partitions under absent class unknown");

    ASSERT(ns_resolve("0/hardware/storage/hdd/partitions", &r) == 0 &&
           r.kind == NS_PARTITIONS_DIR, "partitions dir classifies");
    ASSERT(ns_resolve("0/user/docs", &r) == 0 && r.kind == NS_USER_STORE, "user store classifies");

    /* A partition resource covers its whole volume-relative tree. */
    ASSERT(ns_resolve("0/hardware/storage/hdd/partitions/3", &r) == 0 &&
           r.kind == NS_PARTITION && r.part_index == 3, "partition index parsed");
    ASSERT(ns_resolve("0/hardware/storage/hdd/partitions/3/deep/file.txt", &r) == 0 &&
           r.kind == NS_PARTITION && r.part_index == 3,
           "content below a partition resolves as that partition");
    TEST_PASS();
}

/* --- errors -------------------------------------------------------- */

static void test_errors(void) {
    ns_resource_t r;

    ASSERT(ns_resolve("/0/dev/null", &r) < 0, "Unix-style path is a parse error");
    ASSERT(ns_resolve("0/nothing/here", &r) == -2, "unknown branch -> unresolved (-2)");
    ASSERT(ns_resolve("0/hardware/storage/tape", &r) == -2, "unknown storage type unresolved");
    ASSERT(ns_resolve("0/hardware/storage/ssd/partitions/0", &r) == -2,
           "partition 0 invalid (1-based)");
    ASSERT(ns_resolve("0/hardware/storage/hdd/partitions/abc", &r) == -2,
           "non-numeric partition invalid");
    ASSERT(ns_resolve("0/hardware/memory/swap", &r) == -2, "undefined memory node unresolved");
    ASSERT(ns_describe("garbage", 0, 0) < 0, "describe rejects garbage grammar");
    TEST_PASS();
}

static void test_partition_requires_ready_backend(void) {
    /* Reset backend state: no disk, empty GPT table -> part_init fails
     * and clears its readiness flag. With no hardware attached there is
     * NO hdd/ssd class at all — the namespace reports unknown instead of
     * pretending a device exists. */
    mock_disk_set_ready(0);
    mock_gpt_setup(0, 0);
    part_init();
    ns_resource_t r;
    ASSERT(ns_resolve("0/hardware/storage/hdd", &r) == -2,
           "no hardware -> no storage class (topology honesty)");
    ASSERT(ns_resolve("0/hardware/storage/ssd", &r) == -2,
           "no hardware -> no ssd class either");
    ASSERT(ns_describe("0/hardware/storage/hdd/partitions/1", 0, 0) == -2,
           "describe reports unknown, never crashes");
    TEST_PASS();
}

/* --- shell argument translation ------------------------------------ */

static void test_fs_path_passthrough(void) {
    char out[NS_MAX_PATH];
    ASSERT(ns_to_fs_path("/legacy/file.txt", out, sizeof(out)) == NS_FS_NOT_NS,
           "legacy absolute path passes through");
    ASSERT(ns_to_fs_path("notes.txt", out, sizeof(out)) == NS_FS_NOT_NS,
           "bare filename passes through");
    ASSERT(ns_to_fs_path("1file", out, sizeof(out)) == NS_FS_NOT_NS,
           "digit-leading name without slash passes through");
    ASSERT(ns_to_fs_path("", out, sizeof(out)) == NS_FS_NOT_NS,
           "empty argument passes through");
    TEST_PASS();
}

static void test_fs_path_partition_translation(void) {
    setup_two_partitions_for_ns();               /* default HDD-class device */
    ASSERT(volume_use_partition(1) == 0, "mount partition 1");

    char out[NS_MAX_PATH];
    ASSERT(ns_to_fs_path("0/hsh/partitions/1/a.txt", out, sizeof(out)) == NS_FS_OK,
           "aliased volume path translates");
    ASSERT(strcmp(out, "/a.txt") == 0, "volume-relative file path");

    ASSERT(ns_to_fs_path("0/HARDWARE/Storage/HDD/partitions/1/docs/x.txt",
                         out, sizeof(out)) == NS_FS_OK,
           "canonical uppercase path translates (normalized)");
    ASSERT(strcmp(out, "/docs/x.txt") == 0, "nested remainder preserved");

    ASSERT(ns_to_fs_path("0/hsh/partitions/1/", out, sizeof(out)) == NS_FS_OK,
           "trailing slash tolerated on volume root");
    ASSERT(strcmp(out, "/") == 0, "bare volume maps to fs root");

    /* Wrong / unmounted volumes */
    ASSERT(ns_to_fs_path("0/hsh/partitions/2/a.txt", out, sizeof(out)) == NS_FS_EVOLUME,
           "unmounted second volume rejected");

    volume_use_whole_disk();
    ASSERT(ns_to_fs_path("0/hsh/partitions/1/a.txt", out, sizeof(out)) == NS_FS_EVOLUME,
           "no mounted volume rejected");
    TEST_PASS();
}

static void test_fs_path_user_store(void) {
    setup_two_partitions_for_ns();
    volume_use_partition(1);

    char out[NS_MAX_PATH];
    ASSERT(ns_to_fs_path("0/user/readme.txt", out, sizeof(out)) == NS_FS_OK,
           "user store path translates");
    ASSERT(strcmp(out, "/readme.txt") == 0, "user root binds the active volume");

    ASSERT(ns_to_fs_path("0/user/docs/nested.txt", out, sizeof(out)) == NS_FS_OK,
           "nested user path translates");
    ASSERT(strcmp(out, "/docs/nested.txt") == 0, "user nesting preserved");

    ASSERT(ns_to_fs_path("0/user", out, sizeof(out)) == NS_FS_OK, "bare user root ok");
    ASSERT(strcmp(out, "/") == 0, "bare user root maps to fs root");
    TEST_PASS();
}

static void test_fs_path_errors(void) {
    setup_two_partitions_for_ns();
    volume_use_partition(1);

    char out[NS_MAX_PATH];
    /* Namespace content resources translate onto the /0 VFS prefix. */
    ASSERT(ns_to_fs_path("0/system/version", out, sizeof(out)) == NS_FS_OK,
           "system node translates");
    ASSERT(strcmp(out, "/0/system/version") == 0, "system node maps under /0");
    ASSERT(ns_to_fs_path("0/dev/null", out, sizeof(out)) == NS_FS_OK,
           "dev shim translates");
    ASSERT(strcmp(out, "/0/dev/null") == 0, "dev shim maps under /0");
    ASSERT(ns_to_fs_path("0/hardware/memory/ram", out, sizeof(out)) == NS_FS_OK,
           "memory ram translates");
    ASSERT(strcmp(out, "/0/hardware/memory/ram") == 0, "ram maps under /0");

    /* Security regression: alias expansion GROWS paths (0/hmr -> long
     * canonical). A too-small caller buffer must be refused cleanly,
     * never truncated mid-component or silently passed through. */
    char small[8];
    ASSERT(ns_to_fs_path("0/hmr", small, sizeof(small)) == NS_FS_EPARSE,
           "capacity overflow is a clean parse error");

    ASSERT(ns_to_fs_path("garbage", out, sizeof(out)) == NS_FS_NOT_NS,
           "non-namespace input passes through (not a parse error)");
    ASSERT(ns_to_fs_path("0//x", out, sizeof(out)) == NS_FS_EPARSE,
           "empty component is a parse error");
    ASSERT(ns_to_fs_path("0/nothing/here", out, sizeof(out)) == NS_FS_EUNKNOWN,
           "unknown resource errors");
    ASSERT(ns_to_fs_path("0/hardware/storage/hdd", out, sizeof(out)) == NS_FS_EKIND,
           "structural device node has no filesystem");
    ASSERT(ns_to_fs_path("0/hardware/storage/hdd/partitions", out, sizeof(out)) == NS_FS_EKIND,
           "partition-table listing has no filesystem");
    ASSERT(ns_to_fs_path("0/hardware/storage/ssd/partitions/1/x", out, sizeof(out)) == NS_FS_EUNKNOWN,
           "volume on absent device class is unknown");
    ASSERT(ns_to_fs_path("0/hsh/partitions/9/x", out, sizeof(out)) == NS_FS_EVOLUME,
           "absent partition on mounted device is a volume error");
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
    test_suite_add_test(&suite, "fs_path_passthrough", test_fs_path_passthrough);
    test_suite_add_test(&suite, "fs_path_partition_translation", test_fs_path_partition_translation);
    test_suite_add_test(&suite, "fs_path_user_store", test_fs_path_user_store);
    test_suite_add_test(&suite, "fs_path_errors", test_fs_path_errors);

    return &suite;
}
