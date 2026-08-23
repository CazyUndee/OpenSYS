/*
 * test_vfile.c - Unit Tests for the Virtual Filesystem Resource Layer
 *
 * Copyright (C) 2026 CazyUndee
 * SPDX-License-Identifier: AGPL-3.0
 *
 * Tests the vfile API (lookup, read, write, directory listing) using the
 * real vfile.c implementation with host-side mocks for kernel APIs.
 */

#include "../test_framework.h"
#include "../../include/vfile.h"
#include "../../include/vfs.h"
#include "../../include/ramfs.h"
#include <string.h>

/* ---- Test: vfile_init registers entries ---- */

static void test_vfile_init_registers_entries(void) {
    vfile_init();
    ASSERT(vfile_is_virtual("/") == 1, "/ should be virtual (root directory)");
    ASSERT(vfile_is_virtual("/proc/uptime") == 1, "/proc/uptime should be virtual");
    ASSERT(vfile_is_virtual("/proc/memory") == 1, "/proc/memory should be virtual");
    ASSERT(vfile_is_virtual("/proc/cpu") == 1, "/proc/cpu should be virtual");
    ASSERT(vfile_is_virtual("/proc/processes") == 1, "/proc/processes should be virtual");
    ASSERT(vfile_is_virtual("/proc/datetime") == 1, "/proc/datetime should be virtual");
    ASSERT(vfile_is_virtual("/proc/version") == 1, "/proc/version should be virtual");
    ASSERT(vfile_is_virtual("/proc/hostname") == 1, "/proc/hostname should be virtual");
    ASSERT(vfile_is_virtual("/proc/stat") == 1, "/proc/stat should be virtual");
    ASSERT(vfile_is_virtual("/proc/interrupts") == 1, "/proc/interrupts should be virtual");
    ASSERT(vfile_is_virtual("/proc/mounts") == 1, "/proc/mounts should be virtual");
    ASSERT(vfile_is_virtual("/proc/heap") == 1, "/proc/heap should be virtual");
    ASSERT(vfile_is_virtual("/proc/self/pid") == 1, "/proc/self/pid should be virtual");
    ASSERT(vfile_is_virtual("/proc/self/name") == 1, "/proc/self/name should be virtual");
    ASSERT(vfile_is_virtual("/proc/self/status") == 1, "/proc/self/status should be virtual");
    ASSERT(vfile_is_virtual("/sys/kernel/name") == 1, "/sys/kernel/name should be virtual");
    ASSERT(vfile_is_virtual("/sys/kernel/version") == 1, "/sys/kernel/version should be virtual");
    ASSERT(vfile_is_virtual("/sys/kernel/arch") == 1, "/sys/kernel/arch should be virtual");
    ASSERT(vfile_is_virtual("/sys/kernel/hostname") == 1, "/sys/kernel/hostname should be virtual");
    ASSERT(vfile_is_virtual("/sys/hardware/platform") == 1, "/sys/hardware/platform should be virtual");
    ASSERT(vfile_is_virtual("/sys/devices/pci") == 1, "/sys/devices/pci should be virtual");
    ASSERT(vfile_is_virtual("/dev/null") == 1, "/dev/null should be virtual");
    ASSERT(vfile_is_virtual("/dev/zero") == 1, "/dev/zero should be virtual");
    ASSERT(vfile_is_virtual("/dev/console") == 1, "/dev/console should be virtual");
    TEST_PASS();
}

/* ---- Test: non-virtual paths return 0 ---- */

static void test_vfile_non_virtual_paths(void) {
    vfile_init();
    ASSERT(vfile_is_virtual("/etc/passwd") == 0, "/etc/passwd should not be virtual");
    ASSERT(vfile_is_virtual("/proc") == 1, "/proc directory should be virtual");
    ASSERT(vfile_is_virtual("/proc/") == 0, "/proc/ (trailing slash) should not be virtual");
    ASSERT(vfile_is_virtual("/my_file.txt") == 0, "arbitrary path should not be virtual");
    TEST_PASS();
}

/* ---- Test: vfile_read returns content ---- */

static void test_vfile_read_uptime(void) {
    vfile_init();
    char buf[512];
    int len = vfile_read("/proc/uptime", buf, sizeof(buf));
    ASSERT(len > 0, "/proc/uptime should return data");
    buf[len] = 0;
    ASSERT(strstr(buf, "Uptime:") != NULL, "/proc/uptime should contain 'Uptime:'");
    TEST_PASS();
}

static void test_vfile_read_memory(void) {
    vfile_init();
    char buf[512];
    int len = vfile_read("/proc/memory", buf, sizeof(buf));
    ASSERT(len > 0, "/proc/memory should return data");
    buf[len] = 0;
    ASSERT(strstr(buf, "Total RAM:") != NULL, "/proc/memory should contain 'Total RAM:'");
    ASSERT(strstr(buf, "Free RAM:") != NULL, "/proc/memory should contain 'Free RAM:'");
    TEST_PASS();
}

static void test_vfile_read_version(void) {
    vfile_init();
    char buf[512];
    int len = vfile_read("/proc/version", buf, sizeof(buf));
    ASSERT(len > 0, "/proc/version should return data");
    buf[len] = 0;
    ASSERT(strstr(buf, "Plan 0") != NULL, "/proc/version should contain 'Plan 0'");
    TEST_PASS();
}

static void test_vfile_read_hostname(void) {
    vfile_init();
    char buf[512];
    int len = vfile_read("/proc/hostname", buf, sizeof(buf));
    ASSERT(len > 0, "/proc/hostname should return data");
    buf[len] = 0;
    ASSERT(strstr(buf, "plan0") != NULL, "default hostname should be 'plan0'");
    TEST_PASS();
}

static void test_vfile_read_kernel_name(void) {
    vfile_init();
    char buf[512];
    int len = vfile_read("/sys/kernel/name", buf, sizeof(buf));
    ASSERT(len > 0, "/sys/kernel/name should return data");
    buf[len] = 0;
    ASSERT(strstr(buf, "Plan 0") != NULL, "/sys/kernel/name should contain 'Plan 0'");
    TEST_PASS();
}

static void test_vfile_read_kernel_arch(void) {
    vfile_init();
    char buf[512];
    int len = vfile_read("/sys/kernel/arch", buf, sizeof(buf));
    ASSERT(len > 0, "/sys/kernel/arch should return data");
    buf[len] = 0;
    ASSERT(strstr(buf, "x86_64") != NULL, "/sys/kernel/arch should contain 'x86_64'");
    TEST_PASS();
}

static void test_vfile_read_platform(void) {
    vfile_init();
    char buf[512];
    int len = vfile_read("/sys/hardware/platform", buf, sizeof(buf));
    ASSERT(len > 0, "/sys/hardware/platform should return data");
    buf[len] = 0;
    ASSERT(strstr(buf, "x86_64") != NULL, "platform should mention x86_64");
    TEST_PASS();
}

static void test_vfile_read_null(void) {
    vfile_init();
    char buf[512];
    int len = vfile_read("/dev/null", buf, sizeof(buf));
    ASSERT(len == 0, "/dev/null should return 0 bytes");
    TEST_PASS();
}

/* ---- Test: /dev/zero returns NUL bytes ---- */

static void test_vfile_read_zero(void) {
    vfile_init();
    char buf[128];
    memset(buf, 'A', sizeof(buf));
    int len = vfile_read("/dev/zero", buf, sizeof(buf));
    ASSERT(len > 0, "/dev/zero should return data");
    for (int i = 0; i < len; i++) {
        ASSERT(buf[i] == 0, "/dev/zero should return NUL bytes");
    }
    TEST_PASS();
}

/* ---- Test: non-virtual paths return -1 ---- */

static void test_vfile_read_non_virtual(void) {
    vfile_init();
    char buf[512];
    int len = vfile_read("/not/virtual", buf, sizeof(buf));
    ASSERT(len == -1, "non-virtual path should return -1");
    TEST_PASS();
}

/* ---- Test: directory listing ---- */

static int dir_entry_count;
static void count_entries(const char* name, int is_dir, uint32_t size) {
    (void)name; (void)is_dir; (void)size;
    dir_entry_count++;
}

static void test_vfile_list_root(void) {
    vfile_init();
    dir_entry_count = 0;
    int r = vfile_list("/", count_entries);
    ASSERT(r == 0, "/ should be a virtual directory");
    ASSERT(dir_entry_count >= 3, "/ should list proc, sys, dev");
    TEST_PASS();
}

static void test_vfile_list_proc(void) {
    vfile_init();
    dir_entry_count = 0;
    int r = vfile_list("/proc", count_entries);
    ASSERT(r == 0, "/proc should be a virtual directory");
    ASSERT(dir_entry_count > 0, "/proc should have entries");
    TEST_PASS();
}

static void test_vfile_list_sys(void) {
    vfile_init();
    dir_entry_count = 0;
    int r = vfile_list("/sys", count_entries);
    ASSERT(r == 0, "/sys should be a virtual directory");
    ASSERT(dir_entry_count >= 3, "/sys should have kernel, hardware, devices");
    TEST_PASS();
}

static void test_vfile_list_dev(void) {
    vfile_init();
    dir_entry_count = 0;
    int r = vfile_list("/dev", count_entries);
    ASSERT(r == 0, "/dev should be a virtual directory");
    ASSERT(dir_entry_count >= 3, "/dev should have at least null, zero, console");
    TEST_PASS();
}

static void test_vfile_list_proc_self(void) {
    vfile_init();
    dir_entry_count = 0;
    int r = vfile_list("/proc/self", count_entries);
    ASSERT(r == 0, "/proc/self should be a virtual directory");
    ASSERT(dir_entry_count >= 3, "/proc/self should have pid, name, status");
    TEST_PASS();
}

static void test_vfile_list_non_virtual(void) {
    vfile_init();
    int r = vfile_list("/not/a/dir", count_entries);
    ASSERT(r == -1, "non-virtual directory should return -1");
    TEST_PASS();
}

/* ---- Test: file-specific directory listing ---- */

static void test_vfile_list_as_file(void) {
    vfile_init();
    int r = vfile_list("/proc/uptime", count_entries);
    ASSERT(r == -1, "file path should not be listable as directory");
    TEST_PASS();
}

static void test_vfile_read_as_dir(void) {
    vfile_init();
    char buf[512];
    int len = vfile_read("/proc", buf, sizeof(buf));
    ASSERT(len == -1, "directory path should not be readable as file");
    TEST_PASS();
}

/* ---- Test: content size checks ---- */

static void test_vfile_uptime_content_nonempty(void) {
    vfile_init();
    char buf[512];
    int len = vfile_read("/proc/uptime", buf, sizeof(buf));
    ASSERT(len > 10, "uptime should have substantial content");
    TEST_PASS();
}

static void test_vfile_memory_content_nonempty(void) {
    vfile_init();
    char buf[512];
    int len = vfile_read("/proc/memory", buf, sizeof(buf));
    ASSERT(len > 20, "memory should have substantial content");
    TEST_PASS();
}

static void test_vfile_cpuid_content_nonempty(void) {
    vfile_init();
    char buf[512];
    int len = vfile_read("/proc/cpu", buf, sizeof(buf));
    ASSERT(len > 10, "cpu info should have substantial content");
    buf[len] = 0;
    ASSERT(strstr(buf, "Vendor:") != NULL, "cpu should contain 'Vendor:'");
    TEST_PASS();
}

/* ---- Test: /proc/processes content ---- */

static void test_vfile_processes_content(void) {
    vfile_init();
    char buf[1024];
    int len = vfile_read("/proc/processes", buf, sizeof(buf));
    ASSERT(len > 0, "processes should return data");
    buf[len] = 0;
    ASSERT(strstr(buf, "PID") != NULL, "processes should contain PID header");
    TEST_PASS();
}

/* ---- Test: /proc/meminfo content ---- */

static void test_vfile_meminfo_content(void) {
    vfile_init();
    char buf[512];
    int len = vfile_read("/proc/meminfo", buf, sizeof(buf));
    ASSERT(len > 0, "meminfo should return data");
    buf[len] = 0;
    ASSERT(strstr(buf, "MemTotal:") != NULL, "meminfo should contain MemTotal:");
    ASSERT(strstr(buf, "MemFree:") != NULL, "meminfo should contain MemFree:");
    TEST_PASS();
}

/* ---- Test: /proc/timer content ---- */

static void test_vfile_timer_content(void) {
    vfile_init();
    char buf[512];
    int len = vfile_read("/proc/timer", buf, sizeof(buf));
    ASSERT(len > 0, "timer should return data");
    buf[len] = 0;
    ASSERT(strstr(buf, "ticks:") != NULL, "timer should contain 'ticks:'");
    TEST_PASS();
}

/* ---- Test: /proc/stat content ---- */

static void test_vfile_stat_content(void) {
    vfile_init();
    char buf[512];
    int len = vfile_read("/proc/stat", buf, sizeof(buf));
    ASSERT(len > 0, "stat should return data");
    buf[len] = 0;
    ASSERT(strstr(buf, "cpu ") != NULL, "stat should contain 'cpu '");
    ASSERT(strstr(buf, "processes") != NULL, "stat should contain 'processes'");
    TEST_PASS();
}

/* ---- Test: /proc/interrupts content ---- */

static void test_vfile_interrupts_content(void) {
    vfile_init();
    char buf[1024];
    int len = vfile_read("/proc/interrupts", buf, sizeof(buf));
    ASSERT(len > 0, "interrupts should return data");
    buf[len] = 0;
    ASSERT(strstr(buf, "IRQ") != NULL, "interrupts should contain IRQ header");
    ASSERT(strstr(buf, "keyboard") != NULL, "interrupts should list keyboard IRQ");
    TEST_PASS();
}

/* ---- Test: /proc/mounts content ---- */

static void test_vfile_mounts_content(void) {
    /* Mirror kernel boot order: VFS first (mounts ramfs at /), then vfile */
    vfs_init();
    vfile_init();
    char buf[512];
    int len = vfile_read("/proc/mounts", buf, sizeof(buf));
    ASSERT(len > 0, "mounts should return data");
    buf[len] = 0;
    ASSERT(strstr(buf, "ramfs") != NULL, "mounts should list ramfs");
    ASSERT(strstr(buf, "vfile") != NULL, "mounts should list vfile");
    ASSERT(strstr(buf, "/proc") != NULL, "mounts should list /proc");
    ASSERT(strstr(buf, "/sys") != NULL, "mounts should list /sys");
    ASSERT(strstr(buf, "/dev") != NULL, "mounts should list /dev");
    TEST_PASS();
}

/* ---- Test: VFS mount table accessors ---- */

static void test_vfs_mount_table(void) {
    vfs_init();
    /* vfs_init mounts ramfs at / */
    ASSERT(vfs_mount_count() == 1, "vfs_init should register exactly one mount");

    char path[VFS_MAX_PATH];
    ASSERT(vfs_get_mount(0, path) == 0, "mount 0 should be readable");
    ASSERT(strcmp(path, "/") == 0, "mount 0 should be at /");
    ASSERT(vfs_get_mount(1, path) == -1, "mount 1 should be out of range");
    TEST_PASS();
}

static void test_vfs_mount_idempotent(void) {
    vfs_init();
    ASSERT(vfs_mount_count() == 1, "vfs_init should register one mount");

    /* Re-mounting the same path should not create a duplicate */
    vfs_mount("/", 0);
    vfs_mount("/", 0);
    ASSERT(vfs_mount_count() == 1, "mount count should stay 1 after re-mounting /");
    TEST_PASS();
}

static void test_vfs_mount_vfile_namespaces(void) {
    vfs_init();
    vfile_init();
    /* vfs_init (/) + vfile_init (/proc, /sys, /dev) */
    ASSERT(vfs_mount_count() == 4, "vfs+vfile should register 4 mounts");

    char path[VFS_MAX_PATH];
    ASSERT(vfs_get_mount(1, path) == 0 && strcmp(path, "/proc") == 0,
           "mount 1 should be /proc");
    ASSERT(vfs_get_mount(2, path) == 0 && strcmp(path, "/sys") == 0,
           "mount 2 should be /sys");
    ASSERT(vfs_get_mount(3, path) == 0 && strcmp(path, "/dev") == 0,
           "mount 3 should be /dev");
    TEST_PASS();
}

/* ---- Test: /proc/heap content ---- */

static void test_vfile_heap_content(void) {
    vfile_init();
    char buf[1024];
    int len = vfile_read("/proc/heap", buf, sizeof(buf));
    ASSERT(len > 0, "/proc/heap should return data");
    buf[len] = 0;
    ASSERT(strstr(buf, "Heap total:") != NULL, "heap should contain 'Heap total:'");
    ASSERT(strstr(buf, "Heap used:") != NULL, "heap should contain 'Heap used:'");
    ASSERT(strstr(buf, "Heap free:") != NULL, "heap should contain 'Heap free:'");
    ASSERT(strstr(buf, "Blocks:") != NULL, "heap should contain 'Blocks:'");
    ASSERT(strstr(buf, "Integrity:") != NULL, "heap should contain 'Integrity:'");
    TEST_PASS();
}

static void test_vfile_heap_integrity_ok(void) {
    vfile_init();
    char buf[1024];
    int len = vfile_read("/proc/heap", buf, sizeof(buf));
    ASSERT(len > 0, "/proc/heap should return data");
    buf[len] = 0;
    /* The host mock for kheap_validate() reports a healthy heap */
    ASSERT(strstr(buf, "Integrity:    OK") != NULL, "heap should report Integrity: OK");
    TEST_PASS();
}

/* ---- Test: /proc/self/pid content ---- */

static void test_vfile_self_pid(void) {
    vfile_init();
    char buf[512];
    int len = vfile_read("/proc/self/pid", buf, sizeof(buf));
    ASSERT(len > 0, "/proc/self/pid should return data");
    buf[len] = 0;
    /* In host tests, process_current() returns NULL, so PID should be 0 */
    ASSERT(strstr(buf, "0") != NULL, "/proc/self/pid should contain a PID");
    TEST_PASS();
}

/* ---- Test: /proc/self/name content ---- */

static void test_vfile_self_name(void) {
    vfile_init();
    char buf[512];
    int len = vfile_read("/proc/self/name", buf, sizeof(buf));
    ASSERT(len > 0, "/proc/self/name should return data");
    buf[len] = 0;
    /* In host tests, process_current() returns NULL, so name is 'kernel' */
    ASSERT(strstr(buf, "kernel") != NULL, "/proc/self/name should contain 'kernel'");
    TEST_PASS();
}

/* ---- Test: /proc/self/status content ---- */

static void test_vfile_self_status(void) {
    vfile_init();
    char buf[1024];
    int len = vfile_read("/proc/self/status", buf, sizeof(buf));
    ASSERT(len > 0, "/proc/self/status should return data");
    buf[len] = 0;
    ASSERT(strstr(buf, "Name:") != NULL, "status should contain 'Name:'");
    ASSERT(strstr(buf, "PID:") != NULL, "status should contain 'PID:'");
    ASSERT(strstr(buf, "State:") != NULL, "status should contain 'State:'");
    TEST_PASS();
}

/* ---- Test: /sys/devices/pci content ---- */

static void test_vfile_devices_pci(void) {
    vfile_init();
    char buf[2048];
    int len = vfile_read("/sys/devices/pci", buf, sizeof(buf));
    ASSERT(len > 0, "/sys/devices/pci should return data");
    buf[len] = 0;
    ASSERT(strstr(buf, "Bus") != NULL, "pci listing should contain 'Bus' header");
    /* Mock PCI returns no devices, so just verify the header is there */
    TEST_PASS();
}

/* ================================================================
 * Write tests
 * ================================================================ */

static void test_vfile_write_null(void) {
    vfile_init();
    const char* data = "hello world";
    int written = vfile_write("/dev/null", data, strlen(data));
    ASSERT(written == (int)strlen(data), "/dev/null should accept all bytes");
    TEST_PASS();
}

static void test_vfile_null_is_writable(void) {
    vfile_init();
    ASSERT(vfile_is_writable("/dev/null") == 1, "/dev/null should be writable");
    TEST_PASS();
}

static void test_vfile_write_zero(void) {
    vfile_init();
    const char* data = "discarded data";
    int written = vfile_write("/dev/zero", data, strlen(data));
    ASSERT(written == (int)strlen(data), "/dev/zero should accept all bytes");
    TEST_PASS();
}

static void test_vfile_zero_is_writable(void) {
    vfile_init();
    ASSERT(vfile_is_writable("/dev/zero") == 1, "/dev/zero should be writable");
    TEST_PASS();
}

static void test_vfile_console_is_writable(void) {
    vfile_init();
    ASSERT(vfile_is_writable("/dev/console") == 1, "/dev/console should be writable");
    TEST_PASS();
}

static void test_vfile_write_console(void) {
    vfile_init();
    const char* data = "test output\n";
    int written = vfile_write("/dev/console", data, strlen(data));
    ASSERT(written == (int)strlen(data), "/dev/console should accept all bytes");
    TEST_PASS();
}

static void test_vfile_hostname_is_writable(void) {
    vfile_init();
    ASSERT(vfile_is_writable("/proc/hostname") == 1, "/proc/hostname should be writable");
    TEST_PASS();
}

static void test_vfile_write_hostname(void) {
    vfile_init();
    char buf[512];
    int len = vfile_read("/proc/hostname", buf, sizeof(buf));
    buf[len] = 0;
    ASSERT(strstr(buf, "plan0") != NULL, "default hostname should be 'plan0'");

    const char* new_name = "mytesthost";
    int written = vfile_write("/proc/hostname", new_name, strlen(new_name));
    ASSERT(written == (int)strlen(new_name), "write should accept hostname");

    len = vfile_read("/proc/hostname", buf, sizeof(buf));
    buf[len] = 0;
    ASSERT(strstr(buf, "mytesthost") != NULL, "hostname should be updated");

    len = vfile_read("/sys/kernel/hostname", buf, sizeof(buf));
    buf[len] = 0;
    ASSERT(strstr(buf, "mytesthost") != NULL, "/sys/kernel/hostname should mirror /proc/hostname");

    TEST_PASS();
}

static void test_vfile_sys_hostname_is_writable(void) {
    vfile_init();
    ASSERT(vfile_is_writable("/sys/kernel/hostname") == 1, "/sys/kernel/hostname should be writable");
    TEST_PASS();
}

static void test_vfile_write_sys_hostname(void) {
    vfile_init();
    const char* new_name = "sysname";
    int written = vfile_write("/sys/kernel/hostname", new_name, strlen(new_name));
    ASSERT(written == (int)strlen(new_name), "write should accept hostname");

    char buf[512];
    int len = vfile_read("/proc/hostname", buf, sizeof(buf));
    buf[len] = 0;
    ASSERT(strstr(buf, "sysname") != NULL, "/proc/hostname should reflect sys/kernel write");

    TEST_PASS();
}

static void test_vfile_readonly_rejects_write(void) {
    vfile_init();
    ASSERT(vfile_is_writable("/proc/uptime") == 0, "/proc/uptime should not be writable");
    ASSERT(vfile_is_writable("/proc/memory") == 0, "/proc/memory should not be writable");
    ASSERT(vfile_is_writable("/proc/version") == 0, "/proc/version should not be writable");
    ASSERT(vfile_is_writable("/sys/kernel/name") == 0, "/sys/kernel/name should not be writable");
    ASSERT(vfile_is_writable("/sys/kernel/arch") == 0, "/sys/kernel/arch should not be writable");
    ASSERT(vfile_is_writable("/sys/hardware/platform") == 0, "/sys/hardware/platform should not be writable");

    int written = vfile_write("/proc/uptime", "test", 4);
    ASSERT(written == -1, "write to read-only should return -1");
    TEST_PASS();
}

static void test_vfile_write_non_virtual(void) {
    vfile_init();
    int written = vfile_write("/not/virtual", "test", 4);
    ASSERT(written == -1, "write to non-virtual path should return -1");
    TEST_PASS();
}

static void test_vfile_hostname_strips_newline(void) {
    vfile_init();
    const char* data = "newhost\n";
    int written = vfile_write("/proc/hostname", data, strlen(data));
    ASSERT(written == 7, "write should strip trailing newline and return 7");

    char buf[512];
    int len = vfile_read("/proc/hostname", buf, sizeof(buf));
    buf[len] = 0;
    ASSERT(strstr(buf, "newhost") != NULL, "hostname should be 'newhost'");
    ASSERT(strstr(buf, "newhost\n\n") == NULL, "should not have double newline");
    TEST_PASS();
}

/* ---- Test: VFS fd-table round trip (kernel context) ---- */

static void test_vfs_fd_open_read_close(void) {
    vfs_init();
    ramfs_init();

    /* Seed a file directly in ramfs (flat namespace, no leading slash) */
    int rfd = ramfs_create("data.txt");
    ASSERT(rfd >= 0, "ramfs create should succeed");
    const char* payload = "fd-payload";
    ramfs_write(rfd, payload, (uint32_t)strlen(payload));

    /* Open through VFS — gets a real fd-table entry */
    int fd = vfs_open("data.txt", VFS_O_RDONLY);
    ASSERT(fd >= 0, "vfs_open should return an fd");

    /* Read through the fd */
    char buf[64];
    memset(buf, 0, sizeof(buf));
    int n = vfs_read(fd, buf, sizeof(buf));
    ASSERT(n == (int)strlen(payload), "vfs_read should return payload length");
    ASSERT(strcmp(buf, payload) == 0, "vfs_read data should match payload");

    /* Sequential read should now return 0 (offset advanced past EOF) */
    int n2 = vfs_read(fd, buf, sizeof(buf));
    ASSERT(n2 == 0, "second read at EOF should return 0");

    /* Close and verify the fd is released */
    ASSERT(vfs_close(fd) == 0, "vfs_close should succeed");
    ASSERT(vfs_read(fd, buf, sizeof(buf)) == -1, "read after close should fail");
    TEST_PASS();
}

static void test_vfs_fd_write_roundtrip(void) {
    vfs_init();
    ramfs_init();

    /* Create a file via VFS with WRONLY, write, close */
    int fd = vfs_open("out.txt", VFS_O_CREAT | VFS_O_WRONLY);
    ASSERT(fd >= 0, "vfs_open with O_CREAT should succeed");

    const char* msg = "written via fd";
    int w = vfs_write(fd, msg, (int)strlen(msg));
    ASSERT(w == (int)strlen(msg), "vfs_write should return byte count");
    ASSERT(vfs_close(fd) == 0, "vfs_close should succeed");

    /* Re-open read-only and verify the data persisted */
    int rfd = vfs_open("out.txt", VFS_O_RDONLY);
    ASSERT(rfd >= 0, "reopen should succeed");
    char buf[64];
    memset(buf, 0, sizeof(buf));
    int n = vfs_read(rfd, buf, sizeof(buf));
    ASSERT(n == (int)strlen(msg), "read should return written bytes");
    ASSERT(strcmp(buf, msg) == 0, "data should round-trip through the fd");
    vfs_close(rfd);
    TEST_PASS();
}

static void test_vfs_fd_unlink(void) {
    vfs_init();
    ramfs_init();
    int fd = vfs_open("gone.txt", VFS_O_CREAT | VFS_O_WRONLY);
    ASSERT(fd >= 0, "create should succeed");
    vfs_close(fd);

    ASSERT(vfs_unlink("gone.txt") == 0, "vfs_unlink should succeed");
    int rfd = vfs_open("gone.txt", VFS_O_RDONLY);
    ASSERT(rfd < 0, "open after unlink should fail");
    TEST_PASS();
}

/* ---- Test: /proc/self/fd introspection ---- */

static void test_vfile_fdinfo_lists_open_fd(void) {
    vfs_init();
    ramfs_init();

    /* Open a file so the kernel fd table has an entry */
    int rfd = ramfs_create("fdx.txt");
    ASSERT(rfd >= 0, "ramfs create should succeed");
    ramfs_write(rfd, "abc", 3);
    int fd = vfs_open("fdx.txt", VFS_O_RDONLY);
    ASSERT(fd >= 0, "vfs_open should succeed");

    char buf[1024];
    int len = vfile_read("/proc/self/fdinfo", buf, sizeof(buf));
    ASSERT(len > 0, "fdinfo should return data");
    buf[len] = 0;
    ASSERT(strstr(buf, "fd  Type") != NULL, "fdinfo should have a header");
    ASSERT(strstr(buf, "file") != NULL, "fdinfo should list the open file fd");

    vfs_close(fd);
    TEST_PASS();
}

static void test_vfile_fd_dir_lists_fd(void) {
    vfs_init();
    ramfs_init();
    int rfd = ramfs_create("fdy.txt");
    ramfs_write(rfd, "xyz", 3);
    int fd = vfs_open("fdy.txt", VFS_O_RDONLY);
    ASSERT(fd >= 0, "vfs_open should succeed");

    dir_entry_count = 0;
    int r = vfile_list("/proc/self/fd", count_entries);
    ASSERT(r == 0, "/proc/self/fd should be a virtual directory");
    ASSERT(dir_entry_count >= 1, "fd dir should list at least the open fd");

    vfs_close(fd);
    TEST_PASS();
}

static void test_vfile_fd_read_through_descriptor(void) {
    vfs_init();
    ramfs_init();
    int rfd = ramfs_create("fdz.txt");
    ramfs_write(rfd, "through-fd", 10);
    int fd = vfs_open("fdz.txt", VFS_O_RDONLY);
    ASSERT(fd >= 0, "vfs_open should succeed");

    char path[32];
    snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);
    char buf[64];
    memset(buf, 0, sizeof(buf));
    int len = vfile_read(path, buf, sizeof(buf));
    ASSERT(len > 0, "read through /proc/self/fd/N should return data");
    ASSERT(strcmp(buf, "through-fd") == 0, "data should read through the fd");

    vfs_close(fd);
    TEST_PASS();
}

static void test_vfile_fd_pipe_visible_in_fdinfo(void) {
    vfs_init();
    int fds[2];
    ASSERT(vfs_pipe(fds) == 0, "vfs_pipe should succeed");

    char buf[1024];
    int len = vfile_read("/proc/self/fdinfo", buf, sizeof(buf));
    ASSERT(len > 0, "fdinfo should return data");
    buf[len] = 0;
    ASSERT(strstr(buf, "pipe-r") != NULL, "fdinfo should list the pipe read end");
    ASSERT(strstr(buf, "pipe-w") != NULL, "fdinfo should list the pipe write end");

    vfs_close(fds[0]);
    vfs_close(fds[1]);
    TEST_PASS();
}

static void test_vfile_fd_bad_path(void) {
    vfs_init();
    char buf[32];
    ASSERT(vfile_read("/proc/self/fd/notanumber", buf, sizeof(buf)) == -1,
           "non-numeric fd path should fail");
    ASSERT(vfile_read("/proc/self/fd/", buf, sizeof(buf)) == -1,
           "empty fd number should fail");
    ASSERT(vfile_read("/proc/self/fd/99", buf, sizeof(buf)) == -1,
           "unopened fd should fail");
    TEST_PASS();
}

/* ---- Test: VFS pipes ---- */

static void test_vfs_pipe_roundtrip(void) {
    vfs_init();
    int fds[2];
    ASSERT(vfs_pipe(fds) == 0, "vfs_pipe should succeed");
    ASSERT(fds[0] >= 0 && fds[1] >= 0, "pipe fds should be valid");
    ASSERT(fds[0] != fds[1], "read and write ends should differ");

    const char* msg = "pipe-data";
    int w = vfs_write(fds[1], msg, (int)strlen(msg));
    ASSERT(w == (int)strlen(msg), "pipe write should return byte count");

    char buf[64];
    memset(buf, 0, sizeof(buf));
    int n = vfs_read(fds[0], buf, sizeof(buf));
    ASSERT(n == (int)strlen(msg), "pipe read should return byte count");
    ASSERT(strcmp(buf, msg) == 0, "pipe data should round-trip");
    TEST_PASS();
}

static void test_vfs_pipe_empty_read(void) {
    vfs_init();
    int fds[2];
    ASSERT(vfs_pipe(fds) == 0, "vfs_pipe should succeed");

    char buf[16];
    int n = vfs_read(fds[0], buf, sizeof(buf));
    ASSERT(n == 0, "read from empty pipe should return 0");
    TEST_PASS();
}

static void test_vfs_pipe_sequential_reads(void) {
    vfs_init();
    int fds[2];
    ASSERT(vfs_pipe(fds) == 0, "vfs_pipe should succeed");

    vfs_write(fds[1], "abc", 3);
    vfs_write(fds[1], "def", 3);

    char buf[8];
    memset(buf, 0, sizeof(buf));
    int n1 = vfs_read(fds[0], buf, 3);
    ASSERT(n1 == 3 && strcmp(buf, "abc") == 0, "first read should return 'abc'");

    memset(buf, 0, sizeof(buf));
    int n2 = vfs_read(fds[0], buf, 3);
    ASSERT(n2 == 3 && strcmp(buf, "def") == 0, "second read should return 'def'");

    memset(buf, 0, sizeof(buf));
    int n3 = vfs_read(fds[0], buf, 3);
    ASSERT(n3 == 0, "third read on drained pipe should return 0");
    TEST_PASS();
}

static void test_vfs_pipe_wrong_end_rejected(void) {
    vfs_init();
    int fds[2];
    ASSERT(vfs_pipe(fds) == 0, "vfs_pipe should succeed");

    /* Reading the write end or writing the read end must fail */
    char buf[16];
    ASSERT(vfs_read(fds[1], buf, sizeof(buf)) == -1, "read on write end should fail");
    ASSERT(vfs_write(fds[0], "x", 1) == -1, "write on read end should fail");
    TEST_PASS();
}

static void test_vfs_pipe_close_frees_pipe(void) {
    vfs_init();
    int fds[2];
    ASSERT(vfs_pipe(fds) == 0, "vfs_pipe should succeed");

    ASSERT(vfs_close(fds[0]) == 0, "close read end should succeed");
    ASSERT(vfs_close(fds[1]) == 0, "close write end should succeed");

    /* Both ends closed: reads/writes on the old fds fail */
    char buf[8];
    ASSERT(vfs_read(fds[0], buf, sizeof(buf)) == -1, "read after close should fail");
    ASSERT(vfs_write(fds[1], "x", 1) == -1, "write after close should fail");

    /* A new pipe should still work (pool slot recycled) */
    int fds2[2];
    ASSERT(vfs_pipe(fds2) == 0, "second vfs_pipe should succeed");
    vfs_write(fds2[1], "hi", 2);
    char b2[8];
    memset(b2, 0, sizeof(b2));
    int n = vfs_read(fds2[0], b2, sizeof(b2));
    ASSERT(n == 2 && strcmp(b2, "hi") == 0, "new pipe should work after old closed");
    vfs_close(fds2[0]);
    vfs_close(fds2[1]);
    TEST_PASS();
}

/* ---- Test: VFS dup / dup2 ---- */

static void test_vfs_dup_shared_offset(void) {
    vfs_init();
    const char* name = "dup_shared.txt";
    int fd = vfs_open(name, VFS_O_CREAT | VFS_O_RDWR);
    ASSERT(fd >= 0, "open/create should succeed");

    const char* payload = "hello dup";
    ASSERT(vfs_write(fd, payload, (int)strlen(payload)) == (int)strlen(payload),
           "write should return byte count");

    vfs_seek(fd, VFS_SEEK_SET, 0);
    int dupfd = vfs_dup(fd);
    ASSERT(dupfd >= 0, "dup should succeed");
    ASSERT(dupfd != fd, "dup should return a different fd");

    /* The duplicate shares the node, so reading it advances the
     * offset for the original too. */
    char buf[32];
    memset(buf, 0, sizeof(buf));
    int n = vfs_read(dupfd, buf, sizeof(buf));
    ASSERT(n == (int)strlen(payload), "read via dup should return payload");
    ASSERT(strcmp(buf, payload) == 0, "dup read should match payload");

    /* Original offset moved in step — reading it returns 0 (EOF). */
    memset(buf, 0, sizeof(buf));
    ASSERT(vfs_read(fd, buf, sizeof(buf)) == 0, "original offset should have advanced");

    /* Close the original; the duplicate keeps the node alive. */
    ASSERT(vfs_close(fd) == 0, "close original should succeed");
    memset(buf, 0, sizeof(buf));
    ASSERT(vfs_seek(dupfd, VFS_SEEK_SET, 0) == 0, "seek on dup should still work");
    int n2 = vfs_read(dupfd, buf, sizeof(buf));
    ASSERT(n2 == (int)strlen(payload), "dup should survive original close");
    ASSERT(strcmp(buf, payload) == 0, "dup content should persist");

    vfs_close(dupfd);
    vfs_unlink(name);
    TEST_PASS();
}

static void test_vfs_dup2_replaces_target(void) {
    vfs_init();
    const char* name_a = "dup2_a.txt";
    const char* name_b = "dup2_b.txt";
    int fd_a = vfs_open(name_a, VFS_O_CREAT | VFS_O_RDWR);
    int fd_b = vfs_open(name_b, VFS_O_CREAT | VFS_O_RDWR);
    ASSERT(fd_a >= 0 && fd_b >= 0, "both opens should succeed");

    /* dup2(fd_a, fd_b) replaces fd_b's node with fd_a's. */
    ASSERT(vfs_dup2(fd_a, fd_b) == fd_b, "dup2 should return newfd");

    char buf[32];
    memset(buf, 0, sizeof(buf));
    vfs_write(fd_a, "AAA", 3);
    vfs_seek(fd_b, VFS_SEEK_SET, 0);
    int n = vfs_read(fd_b, buf, sizeof(buf));
    ASSERT(n == 3 && strcmp(buf, "AAA") == 0, "fd_b should now read fd_a's file");

    vfs_close(fd_a);
    vfs_close(fd_b);
    vfs_unlink(name_a);
    vfs_unlink(name_b);
    TEST_PASS();
}

static void test_vfs_dup2_same_fd_noop(void) {
    vfs_init();
    const char* name = "dup2_same.txt";
    int fd = vfs_open(name, VFS_O_CREAT | VFS_O_RDWR);
    ASSERT(fd >= 0, "open should succeed");

    /* dup2(fd, fd) is a no-op returning fd. */
    ASSERT(vfs_dup2(fd, fd) == fd, "dup2 same fd should return it");

    /* Still only one reference — closing once is enough. */
    ASSERT(vfs_close(fd) == 0, "close should succeed");
    ASSERT(vfs_fd_info(fd, 0, 0) == -1, "fd should be closed");
    vfs_unlink(name);
    TEST_PASS();
}

static void test_vfs_dup_invalid_fd(void) {
    vfs_init();
    ASSERT(vfs_dup(99) == -1, "dup of unopened fd should fail");
    ASSERT(vfs_dup(-1) == -1, "dup of negative fd should fail");
    ASSERT(vfs_dup2(1, 99) == -1, "dup2 to out-of-range newfd should fail");
    ASSERT(vfs_dup2(99, 1) == -1, "dup2 of unopened oldfd should fail");
    TEST_PASS();
}

/* ---- Test: path-aware mount dispatch (unified VFS mount) ---- */

static void test_vfs_mount_path_dispatch(void) {
    vfs_init();
    vfile_init();
    ramfs_init();

    /* A bare name resolves to the root ramfs mount. */
    int rfd = ramfs_create("plain.txt");
    ASSERT(rfd >= 0, "ramfs create should succeed");
    ramfs_write(rfd, "abc", 3);
    int fd = vfs_open("plain.txt", VFS_O_RDONLY);
    ASSERT(fd >= 0, "bare name should route to ramfs");
    char buf[16];
    ASSERT(vfs_read(fd, buf, 16) == 3, "ramfs read via bare name should work");
    vfs_close(fd);

    /* An absolute path under / routes to the root ramfs mount too. */
    int fd2 = vfs_open("/plain.txt", VFS_O_RDONLY);
    ASSERT(fd2 >= 0, "absolute path under / should route to ramfs");
    vfs_close(fd2);

    /* A path under /proc routes to the vfile adapter. */
    int vfd = vfs_open("/proc/uptime", VFS_O_RDONLY);
    ASSERT(vfd >= 0, "/proc/uptime should open through the vfile adapter");
    vfs_close(vfd);

    /* /proc itself is a directory — not openable as a file. */
    ASSERT(vfs_open("/proc", VFS_O_RDONLY) == -1, "/proc (dir) should not open as a file");
    TEST_PASS();
}

static void test_vfs_open_virtual_file(void) {
    vfs_init();
    vfile_init();

    int fd = vfs_open("/proc/uptime", VFS_O_RDONLY);
    ASSERT(fd >= 0, "vfs_open(/proc/uptime) should succeed");

    char buf[256];
    memset(buf, 0, sizeof(buf));
    int n = vfs_read(fd, buf, sizeof(buf) - 1);
    ASSERT(n > 0, "vfs_read should return virtual content");
    buf[n] = 0;
    ASSERT(strstr(buf, "Uptime:") != NULL, "content should be the uptime resource");

    /* Virtual content is regenerated per read; size should be non-zero. */
    size_t sz = 0;
    ASSERT(vfs_fd_info(fd, 0, &sz) == 0, "fd_info should succeed");
    ASSERT(sz > 0, "virtual file should report a size");

    ASSERT(vfs_close(fd) == 0, "vfs_close should succeed");
    ASSERT(vfs_read(fd, buf, 16) == -1, "read after close should fail");
    TEST_PASS();
}

static void test_vfs_open_virtual_offset_read(void) {
    vfs_init();
    vfile_init();

    int fd = vfs_open("/proc/uptime", VFS_O_RDONLY);
    ASSERT(fd >= 0, "vfs_open should succeed");

    /* Read in two halves; the second half continues from the offset. */
    char full[256];
    memset(full, 0, sizeof(full));
    int n1 = vfs_read(fd, full, 8);
    ASSERT(n1 == 8, "first chunk should be 8 bytes");
    int n2 = vfs_read(fd, full + 8, sizeof(full) - 8);
    ASSERT(n2 > 0, "second chunk should continue the stream");
    full[8 + n2] = 0;
    ASSERT(strstr(full, "Uptime:") != NULL, "concatenated chunks should be the resource");

    vfs_close(fd);
    TEST_PASS();
}

static void test_vfs_write_virtual_file(void) {
    vfs_init();
    vfile_init();

    /* /proc/hostname is writable — write through the VFS fd layer. */
    int fd = vfs_open("/proc/hostname", VFS_O_WRONLY);
    ASSERT(fd >= 0, "vfs_open(/proc/hostname, O_WRONLY) should succeed");
    int w = vfs_write(fd, "vfs-node", 8);
    ASSERT(w == 8, "vfs_write should accept 8 bytes");
    vfs_close(fd);

    /* Read back through the direct vfile API to confirm the write landed. */
    char buf[64];
    memset(buf, 0, sizeof(buf));
    int n = vfile_read("/proc/hostname", buf, sizeof(buf) - 1);
    ASSERT(n > 0, "hostname should be readable");
    buf[n] = 0;
    ASSERT(strstr(buf, "vfs-node") != NULL, "hostname should reflect the VFS write");

    /* Restore the default so other tests are unaffected. */
    int rfd = vfs_open("/proc/hostname", VFS_O_WRONLY);
    ASSERT(rfd >= 0, "reopen for restore");
    vfs_write(rfd, "plan0", 5);
    vfs_close(rfd);
    TEST_PASS();
}

static void test_vfs_read_only_virtual_denied(void) {
    vfs_init();
    vfile_init();

    /* Open a read-only virtual file for writing: the adapter rejects the
     * write through vfile_write (no write callback registered). */
    int fd = vfs_open("/proc/uptime", VFS_O_WRONLY);
    ASSERT(fd >= 0, "vfs_open with O_WRONLY should return a slot");
    int w = vfs_write(fd, "x", 1);
    ASSERT(w < 0, "write to read-only virtual file should fail");
    vfs_close(fd);
    TEST_PASS();
}

static void test_vfs_open_virtual_missing(void) {
    vfs_init();
    vfile_init();

    ASSERT(vfs_open("/proc/definitely-not-real", VFS_O_RDONLY) == -1,
           "unknown virtual file should not open");
    ASSERT(vfs_open("/sys/definitely-not-real", VFS_O_RDONLY) == -1,
           "unknown /sys file should not open");
    ASSERT(vfs_open("/dev/definitely-not-real", VFS_O_RDONLY) == -1,
           "unknown /dev file should not open");
    /* Creation is not allowed in virtual namespaces. */
    ASSERT(vfs_open("/proc/newfile", VFS_O_CREAT | VFS_O_RDWR) == -1,
           "O_CREAT in /proc should fail");
    TEST_PASS();
}

static void test_vfs_open_virtual_dynamic_fd(void) {
    vfs_init();
    vfile_init();
    ramfs_init();

    /* Open a ramfs file so fd 0 is populated, then read it through the
     * dynamic virtual path /proc/self/fd/0 via the VFS adapter. */
    int rfd = ramfs_create("dyn.txt");
    ASSERT(rfd >= 0, "ramfs create should succeed");
    const char* payload = "dynamic-fd-payload";
    ramfs_write(rfd, payload, (uint32_t)strlen(payload));

    int fd = vfs_open("dyn.txt", VFS_O_RDONLY);
    ASSERT(fd >= 0, "vfs_open should succeed");

    /* The ramfs file is the first free fd after the standard 0/1/2. */
    ASSERT(fd >= 3, "std fds 0/1/2 should precede the file fd");
    char dynpath[32];
    snprintf(dynpath, sizeof(dynpath), "/proc/self/fd/%d", fd);
    int vfd = vfs_open(dynpath, VFS_O_RDONLY);
    ASSERT(vfd >= 0, "dynamic /proc/self/fd/N should open via the adapter");
    char buf[64];
    memset(buf, 0, sizeof(buf));
    int n = vfs_read(vfd, buf, sizeof(buf) - 1);
    ASSERT(n == (int)strlen(payload), "read through dynamic path should match payload");
    ASSERT(strcmp(buf, payload) == 0, "dynamic read should be the payload");
    vfs_close(vfd);
    vfs_close(fd);
    TEST_PASS();
}

/* ---- fs VFS adapter tests (functional fs mock backend) ---- */

/* Defined in tests/mocks/mock_kernel.c — resets the fake fs store. */
extern void mock_fs_reset(void);

extern vfs_ops_t fs_vfs_ops;

static void test_fs_vfs_adapter_roundtrip(void) {
    vfs_init();
    mock_fs_reset();
    /* Mount the real-fs backend at the root. */
    vfs_mount("/", &fs_vfs_ops);

    /* Create + write through the fd layer. */
    int fd = vfs_open("/notes.txt", VFS_O_CREAT | VFS_O_WRONLY);
    ASSERT(fd >= 0, "create+write open should succeed on fs backend");
    const char* payload = "hello real fs";
    int w = vfs_write(fd, payload, (size_t)strlen(payload));
    ASSERT(w == (int)strlen(payload), "write should store the full payload");
    vfs_close(fd);

    /* Reopen read-only and verify content. */
    int rfd = vfs_open("/notes.txt", VFS_O_RDONLY);
    ASSERT(rfd >= 0, "read open should succeed on fs backend");
    char buf[64];
    memset(buf, 0, sizeof(buf));
    int n = vfs_read(rfd, buf, sizeof(buf) - 1);
    ASSERT(n == (int)strlen(payload), "read should return the stored length");
    ASSERT(strcmp(buf, payload) == 0, "read content should match what was written");
    vfs_close(rfd);

    TEST_PASS();
}

static void test_fs_vfs_adapter_offset_read(void) {
    vfs_init();
    mock_fs_reset();
    vfs_mount("/", &fs_vfs_ops);

    int fd = vfs_open("/data.txt", VFS_O_CREAT | VFS_O_WRONLY);
    ASSERT(fd >= 0, "create open should succeed");
    const char* payload = "0123456789abcdef";
    vfs_write(fd, payload, (size_t)strlen(payload));
    vfs_close(fd);

    /* The adapter seeks to the requested offset before each read. */
    int rfd = vfs_open("/data.txt", VFS_O_RDONLY);
    ASSERT(rfd >= 0, "read open should succeed");
    vfs_seek(rfd, VFS_SEEK_SET, 5);
    char buf[16];
    memset(buf, 0, sizeof(buf));
    int n = vfs_read(rfd, buf, sizeof(buf) - 1);
    ASSERT(n == 11, "read from offset 5 should return 11 bytes");
    ASSERT(strcmp(buf, "56789abcdef") == 0, "offset read should start at position 5");
    vfs_close(rfd);

    TEST_PASS();
}

static void test_fs_vfs_adapter_unlink(void) {
    vfs_init();
    mock_fs_reset();
    vfs_mount("/", &fs_vfs_ops);

    int fd = vfs_open("/temp.txt", VFS_O_CREAT | VFS_O_WRONLY);
    ASSERT(fd >= 0, "create open should succeed");
    vfs_write(fd, "x", 1);
    vfs_close(fd);

    ASSERT(vfs_unlink("/temp.txt") == 0, "unlink should succeed through fs adapter");
    ASSERT(vfs_open("/temp.txt", VFS_O_RDONLY) == -1, "deleted file should not reopen");
    TEST_PASS();
}

static void test_fs_vfs_mount_precedence(void) {
    vfs_init();
    vfile_init();
    mock_fs_reset();
    vfs_mount("/", &fs_vfs_ops);

    /* Longest-prefix: /proc routes to vfile, everything else to fs. */
    int vfd = vfs_open("/proc/uptime", VFS_O_RDONLY);
    ASSERT(vfd >= 0, "/proc/uptime should still route to the vfile adapter");
    vfs_close(vfd);

    int fd = vfs_open("/real.txt", VFS_O_CREAT | VFS_O_WRONLY);
    ASSERT(fd >= 0, "root path should route to the fs adapter");
    vfs_close(fd);
    TEST_PASS();
}

/* ---- Standard fd (stdin/stdout/stderr) tests ---- */

static void test_std_fds_installed(void) {
    vfs_init();
    ASSERT(vfs_fd_count() >= 3, "kernel fd table should have at least the 3 std fds");

    int type = 0;
    size_t size = 0;
    ASSERT(vfs_fd_info(0, &type, &size) == 0, "fd 0 (stdin) should exist");
    ASSERT(type == VFS_TYPE_DEVICE, "stdin should be a device");
    ASSERT(vfs_fd_info(1, &type, &size) == 0, "fd 1 (stdout) should exist");
    ASSERT(type == VFS_TYPE_DEVICE, "stdout should be a device");
    ASSERT(vfs_fd_info(2, &type, &size) == 0, "fd 2 (stderr) should exist");
    ASSERT(type == VFS_TYPE_DEVICE, "stderr should be a device");
    TEST_PASS();
}

static void test_std_stdout_writes_to_console(void) {
    vfs_init();
    /* Writing to fd 1 must succeed (console device swallows it). */
    int w = vfs_write(1, "hi", 2);
    ASSERT(w == 2, "write to stdout should report the byte count");
    /* Reading from stdin (fd 0) returns clean EOF, not an error. */
    char buf[8];
    ASSERT(vfs_read(0, buf, sizeof(buf)) == 0, "stdin read should be EOF");
    /* Writing to stdin (fd 0) is denied (read-only). */
    ASSERT(vfs_write(0, "x", 1) == -1, "write to stdin should be denied");
    TEST_PASS();
}

static void test_std_dup2_redirects_stdout(void) {
    vfs_init();
    int fds[2];
    ASSERT(vfs_pipe(fds) == 0, "pipe should succeed");

    /* Point fd 1 (stdout) at the pipe write end. */
    ASSERT(vfs_dup2(fds[1], 1) == 1, "dup2 pipe-write onto stdout");

    /* Write through fd 1 — must land in the pipe, not the console. */
    const char* msg = "via-stdout";
    ASSERT(vfs_write(1, msg, (size_t)strlen(msg)) == (int)strlen(msg),
           "write to redirected stdout should succeed");

    char buf[32];
    memset(buf, 0, sizeof(buf));
    int n = vfs_read(fds[0], buf, sizeof(buf) - 1);
    ASSERT(n == (int)strlen(msg), "pipe should receive the stdout bytes");
    ASSERT(strcmp(buf, msg) == 0, "redirected stdout content should round-trip");

    /* Close the pipe ends (stdout at fd 1 is left pointing at the freed
     * pipe write end — fine for this test; each test re-runs vfs_init). */
    vfs_close(fds[0]);
    vfs_close(fds[1]);
    TEST_PASS();
}

static void test_std_fds_survive_process_table(void) {
    vfs_init();
    fd_table_t* table = fd_table_create();
    ASSERT(table != 0, "fd_table_create should succeed");
    ASSERT(table->count >= 3, "new process table should have std fds");
    ASSERT(table->fds[0] && table->fds[1] && table->fds[2], "std slots populated");
    ASSERT(table->fds[0]->type == VFS_TYPE_DEVICE, "fd 0 is a device");
    fd_table_destroy(table);
    TEST_PASS();
}

/* ---- Create test suite ---- */

test_suite_t* create_vfile_test_suite(void) {
    static test_suite_t suite;
    test_suite_init(&suite, "Virtual Filesystem");

    /* Registration and path tests */
    test_suite_add_test(&suite, "vfile_init_registers_entries", test_vfile_init_registers_entries);
    test_suite_add_test(&suite, "vfile_non_virtual_paths", test_vfile_non_virtual_paths);

    /* Read tests */
    test_suite_add_test(&suite, "vfile_read_uptime", test_vfile_read_uptime);
    test_suite_add_test(&suite, "vfile_read_memory", test_vfile_read_memory);
    test_suite_add_test(&suite, "vfile_read_version", test_vfile_read_version);
    test_suite_add_test(&suite, "vfile_read_hostname", test_vfile_read_hostname);
    test_suite_add_test(&suite, "vfile_read_kernel_name", test_vfile_read_kernel_name);
    test_suite_add_test(&suite, "vfile_read_kernel_arch", test_vfile_read_kernel_arch);
    test_suite_add_test(&suite, "vfile_read_platform", test_vfile_read_platform);
    test_suite_add_test(&suite, "vfile_read_null", test_vfile_read_null);
    test_suite_add_test(&suite, "vfile_read_zero", test_vfile_read_zero);
    test_suite_add_test(&suite, "vfile_read_non_virtual", test_vfile_read_non_virtual);

    /* Content tests */
    test_suite_add_test(&suite, "vfile_uptime_content_nonempty", test_vfile_uptime_content_nonempty);
    test_suite_add_test(&suite, "vfile_memory_content_nonempty", test_vfile_memory_content_nonempty);
    test_suite_add_test(&suite, "vfile_cpuid_content_nonempty", test_vfile_cpuid_content_nonempty);
    test_suite_add_test(&suite, "vfile_processes_content", test_vfile_processes_content);
    test_suite_add_test(&suite, "vfile_meminfo_content", test_vfile_meminfo_content);
    test_suite_add_test(&suite, "vfile_timer_content", test_vfile_timer_content);
    test_suite_add_test(&suite, "vfile_stat_content", test_vfile_stat_content);
    test_suite_add_test(&suite, "vfile_interrupts_content", test_vfile_interrupts_content);
    test_suite_add_test(&suite, "vfile_mounts_content", test_vfile_mounts_content);
    test_suite_add_test(&suite, "vfile_heap_content", test_vfile_heap_content);
    test_suite_add_test(&suite, "vfile_heap_integrity_ok", test_vfile_heap_integrity_ok);

    /* VFS mount table tests */
    test_suite_add_test(&suite, "vfs_mount_table", test_vfs_mount_table);
    test_suite_add_test(&suite, "vfs_mount_idempotent", test_vfs_mount_idempotent);
    test_suite_add_test(&suite, "vfs_mount_vfile_namespaces", test_vfs_mount_vfile_namespaces);

    /* VFS fd-table round-trip tests */
    test_suite_add_test(&suite, "vfs_fd_open_read_close", test_vfs_fd_open_read_close);
    test_suite_add_test(&suite, "vfs_fd_write_roundtrip", test_vfs_fd_write_roundtrip);
    test_suite_add_test(&suite, "vfs_fd_unlink", test_vfs_fd_unlink);

    /* /proc/self/fd introspection tests */
    test_suite_add_test(&suite, "vfile_fdinfo_lists_open_fd", test_vfile_fdinfo_lists_open_fd);
    test_suite_add_test(&suite, "vfile_fd_dir_lists_fd", test_vfile_fd_dir_lists_fd);
    test_suite_add_test(&suite, "vfile_fd_read_through_descriptor", test_vfile_fd_read_through_descriptor);
    test_suite_add_test(&suite, "vfile_fd_pipe_visible_in_fdinfo", test_vfile_fd_pipe_visible_in_fdinfo);
    test_suite_add_test(&suite, "vfile_fd_bad_path", test_vfile_fd_bad_path);

    /* VFS pipe tests */
    test_suite_add_test(&suite, "vfs_pipe_roundtrip", test_vfs_pipe_roundtrip);
    test_suite_add_test(&suite, "vfs_pipe_empty_read", test_vfs_pipe_empty_read);
    test_suite_add_test(&suite, "vfs_pipe_sequential_reads", test_vfs_pipe_sequential_reads);
    test_suite_add_test(&suite, "vfs_pipe_wrong_end_rejected", test_vfs_pipe_wrong_end_rejected);
    test_suite_add_test(&suite, "vfs_pipe_close_frees_pipe", test_vfs_pipe_close_frees_pipe);

    /* VFS dup/dup2 tests */
    test_suite_add_test(&suite, "vfs_dup_shared_offset", test_vfs_dup_shared_offset);
    test_suite_add_test(&suite, "vfs_dup2_replaces_target", test_vfs_dup2_replaces_target);
    test_suite_add_test(&suite, "vfs_dup2_same_fd_noop", test_vfs_dup2_same_fd_noop);
    test_suite_add_test(&suite, "vfs_dup_invalid_fd", test_vfs_dup_invalid_fd);

    /* Unified VFS mount tests (path-aware dispatch + vfile adapter) */
    test_suite_add_test(&suite, "vfs_mount_path_dispatch", test_vfs_mount_path_dispatch);
    test_suite_add_test(&suite, "vfs_open_virtual_file", test_vfs_open_virtual_file);
    test_suite_add_test(&suite, "vfs_open_virtual_offset_read", test_vfs_open_virtual_offset_read);
    test_suite_add_test(&suite, "vfs_write_virtual_file", test_vfs_write_virtual_file);
    test_suite_add_test(&suite, "vfs_read_only_virtual_denied", test_vfs_read_only_virtual_denied);
    test_suite_add_test(&suite, "vfs_open_virtual_missing", test_vfs_open_virtual_missing);
    test_suite_add_test(&suite, "vfs_open_virtual_dynamic_fd", test_vfs_open_virtual_dynamic_fd);

    /* fs backend adapter tests (functional fs mock) */
    test_suite_add_test(&suite, "fs_vfs_adapter_roundtrip", test_fs_vfs_adapter_roundtrip);
    test_suite_add_test(&suite, "fs_vfs_adapter_offset_read", test_fs_vfs_adapter_offset_read);
    test_suite_add_test(&suite, "fs_vfs_adapter_unlink", test_fs_vfs_adapter_unlink);
    test_suite_add_test(&suite, "fs_vfs_mount_precedence", test_fs_vfs_mount_precedence);

    /* Standard fd (stdio) tests */
    test_suite_add_test(&suite, "std_fds_installed", test_std_fds_installed);
    test_suite_add_test(&suite, "std_stdout_writes_to_console", test_std_stdout_writes_to_console);
    test_suite_add_test(&suite, "std_dup2_redirects_stdout", test_std_dup2_redirects_stdout);
    test_suite_add_test(&suite, "std_fds_survive_process_table", test_std_fds_survive_process_table);

    /* /proc/self tests */
    test_suite_add_test(&suite, "vfile_self_pid", test_vfile_self_pid);
    test_suite_add_test(&suite, "vfile_self_name", test_vfile_self_name);
    test_suite_add_test(&suite, "vfile_self_status", test_vfile_self_status);

    /* /sys/devices tests */
    test_suite_add_test(&suite, "vfile_devices_pci", test_vfile_devices_pci);

    /* Directory listing tests */
    test_suite_add_test(&suite, "vfile_list_root", test_vfile_list_root);
    test_suite_add_test(&suite, "vfile_list_proc", test_vfile_list_proc);
    test_suite_add_test(&suite, "vfile_list_sys", test_vfile_list_sys);
    test_suite_add_test(&suite, "vfile_list_dev", test_vfile_list_dev);
    test_suite_add_test(&suite, "vfile_list_proc_self", test_vfile_list_proc_self);
    test_suite_add_test(&suite, "vfile_list_non_virtual", test_vfile_list_non_virtual);
    test_suite_add_test(&suite, "vfile_list_as_file", test_vfile_list_as_file);
    test_suite_add_test(&suite, "vfile_read_as_dir", test_vfile_read_as_dir);

    /* Write tests */
    test_suite_add_test(&suite, "vfile_write_null", test_vfile_write_null);
    test_suite_add_test(&suite, "vfile_null_is_writable", test_vfile_null_is_writable);
    test_suite_add_test(&suite, "vfile_write_zero", test_vfile_write_zero);
    test_suite_add_test(&suite, "vfile_zero_is_writable", test_vfile_zero_is_writable);
    test_suite_add_test(&suite, "vfile_console_is_writable", test_vfile_console_is_writable);
    test_suite_add_test(&suite, "vfile_write_console", test_vfile_write_console);
    test_suite_add_test(&suite, "vfile_hostname_is_writable", test_vfile_hostname_is_writable);
    test_suite_add_test(&suite, "vfile_write_hostname", test_vfile_write_hostname);
    test_suite_add_test(&suite, "vfile_sys_hostname_is_writable", test_vfile_sys_hostname_is_writable);
    test_suite_add_test(&suite, "vfile_write_sys_hostname", test_vfile_write_sys_hostname);
    test_suite_add_test(&suite, "vfile_readonly_rejects_write", test_vfile_readonly_rejects_write);
    test_suite_add_test(&suite, "vfile_write_non_virtual", test_vfile_write_non_virtual);
    test_suite_add_test(&suite, "vfile_hostname_strips_newline", test_vfile_hostname_strips_newline);

    return &suite;
}
