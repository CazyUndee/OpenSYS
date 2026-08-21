/*
 * mock_kernel.c - Mock Kernel Function Implementations for Host-Side Tests
 *
 * Copyright (C) 2026 CazyUndee
 *
 * These stubs allow host-side test binaries to link against test code
 * that references kernel functions (kmalloc, kfree, pmm_*, memory_*).
 * The stubs return safe default values — they do NOT test kernel logic.
 * Actual kernel logic is tested at runtime via QEMU boot.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "../../include/memory.h"
#include "../../include/pmm.h"
#include "../../include/kheap.h"

/* ---- Kernel heap mocks (host malloc/free underneath) ---- */

void* kmalloc(size_t size) {
    if (size == 0) return NULL;
    return malloc(size);
}

void kfree(void* ptr) {
    free(ptr);
}

void kheap_init(uint64_t start, uint64_t size) {
    (void)start; (void)size;
}

void kheap_get_stats(kheap_stats_t* stats) {
    if (stats) memset(stats, 0, sizeof(*stats));
}

void kheap_dump(void) {}

/* Mock: heap is healthy (0 corruptions) */
int kheap_validate(void) { return 0; }

/* ---- PMM mocks ---- */

static size_t mock_total_memory = 128 * 1024 * 1024;  /* 128 MB */
static size_t mock_free_memory  = 100 * 1024 * 1024;   /* 100 MB free */

void pmm_init(uint64_t mbi_addr) { (void)mbi_addr; }
phys_addr_t pmm_alloc_page(void) { return 0; }
phys_addr_t pmm_alloc_pages(size_t count) { (void)count; return 0; }
void pmm_free_page(phys_addr_t addr) { (void)addr; }
void pmm_free_pages(phys_addr_t addr, size_t count) { (void)addr; (void)count; }
void pmm_reserve_range(phys_addr_t start, size_t length) { (void)start; (void)length; }
void pmm_free_range(phys_addr_t start, size_t length) { (void)start; (void)length; }

size_t pmm_get_total(void) { return mock_total_memory; }
size_t pmm_get_free(void) { return mock_free_memory; }
size_t pmm_get_total_pages(void) { return mock_total_memory / 4096; }
size_t pmm_get_free_pages(void) { return mock_free_memory / 4096; }

void pmm_ref_inc(phys_addr_t addr) { (void)addr; }
int pmm_ref_dec(phys_addr_t addr) { (void)addr; return 0; }
int pmm_ref_count(phys_addr_t addr) { (void)addr; return 1; }
void pmm_ref_init_range(phys_addr_t start, size_t length) { (void)start; (void)length; }
void pmm_print_map(void) {}

/* ---- Memory management mocks ---- */

void memory_get_stats(memory_stats_t* stats) {
    if (!stats) return;
    stats->total_mb = mock_total_memory / (1024 * 1024);
    stats->free_mb = mock_free_memory / (1024 * 1024);
    stats->kernel_heap_start = 0xFFFF800000000000ULL;
    stats->kernel_heap_size = 16 * 1024 * 1024;
    stats->kernel_heap_used = 0;
}

void* memory_malloc(size_t size) { return malloc(size); }
void memory_free(void* ptr) { free(ptr); }

/* ---- Timer mocks ---- */

static uint64_t mock_ticks = 0;
uint64_t timer_get_ticks(void) { return mock_ticks; }
uint64_t timer_get_ms(void) { return mock_ticks; }
void timer_init(void) {}
void timer_handler(void) {}
void timer_sleep(uint64_t ms) { (void)ms; }
void timer_sleep_busy(uint64_t ms) { (void)ms; }

/* ---- RTC mock ---- */

#include "../../include/rtc.h"
void rtc_init(void) {}
void rtc_read_time(rtc_time_t* time) {
    if (!time) return;
    time->second = 0;
    time->minute = 30;
    time->hour = 12;
    time->day = 15;
    time->month = 6;
    time->year = 26;
    time->century = 20;
}

/* ---- Process mocks ---- */

#include "../../include/process.h"
typedef struct { pid_t pid; char name[32]; process_state_t state; } mock_proc_t;
static mock_proc_t mock_procs[2] = {
    {1, "init", PROC_STATE_RUNNING},
    {2, "shell", PROC_STATE_RUNNING},
};
process_t* process_current(void) { return 0; }
process_t* process_get_by_index(int i) {
    if (i < 0 || i >= 2) return 0;
    return (process_t*)&mock_procs[i];
}
process_t* process_get(pid_t pid) {
    for (int i = 0; i < 2; i++) {
        if (mock_procs[i].pid == pid) return (process_t*)&mock_procs[i];
    }
    return 0;
}
int process_get_count(void) { return 2; }
void process_init(void) {}

/* ---- PCI mocks ---- */

#include "../../include/pci.h"

int pci_device_exists(uint8_t bus, uint8_t device, uint8_t func) {
    (void)bus; (void)device; (void)func;
    return 0;  /* no devices on host */
}

uint32_t pci_read_dword(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset) {
    (void)bus; (void)device; (void)func; (void)offset;
    return 0;
}

uint16_t pci_read_word(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset) {
    (void)bus; (void)device; (void)func; (void)offset;
    return 0;
}

uint8_t pci_read_byte(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset) {
    (void)bus; (void)device; (void)func; (void)offset;
    return 0;
}

void pci_write_dword(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset, uint32_t value) {
    (void)bus; (void)device; (void)func; (void)offset; (void)value;
}

int pci_find_device(uint8_t class_code, uint8_t subclass, uint8_t prog_if,
                    uint8_t* out_bus, uint8_t* out_device, uint8_t* out_func) {
    (void)class_code; (void)subclass; (void)prog_if;
    (void)out_bus; (void)out_device; (void)out_func;
    return -1;
}

uint16_t pci_get_io_bar(uint8_t bus, uint8_t device, uint8_t func, int bar_index) {
    (void)bus; (void)device; (void)func; (void)bar_index;
    return 0;
}

/* ---- VGA / terminal mock ---- */

void terminal_putchar(char c) { (void)c; }
void terminal_writestring(const char* s) { (void)s; }
void terminal_writestring_nl(const char* s) { (void)s; }
void terminal_put_dec(uint64_t n) { (void)n; }
void terminal_clear(void) {}
