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

/* ---- fs mocks (functional fake backend for the fs_vfs adapter tests) ----
 * The real fs.c is disk-backed and not compiled into the host suite.
 * This in-memory fake gives the adapter real read/write/seek semantics
 * to test against. */

#include "../../include/fs.h"

#define MOCK_FS_MAX_FILES 8
#define MOCK_FS_MAX_SIZE  1024

typedef struct {
    char     name[64];
    uint8_t  data[MOCK_FS_MAX_SIZE];
    uint32_t size;
    int      used;
    int      is_dir;
} mock_fs_file_t;

static mock_fs_file_t mock_fs[MOCK_FS_MAX_FILES];

void mock_fs_reset(void) {
    memset(mock_fs, 0, sizeof(mock_fs));
}

static int mock_fs_find(const char* path) {
    if (!path) return -1;
    for (int i = 0; i < MOCK_FS_MAX_FILES; i++) {
        if (mock_fs[i].used && strcmp(mock_fs[i].name, path) == 0) return i;
    }
    return -1;
}

fs_file_t* fs_open(const char* path, int mode) {
    if (!path) return 0;
    int idx = mock_fs_find(path);
    if (idx < 0) {
        if (mode == 0) return 0;
        for (int i = 0; i < MOCK_FS_MAX_FILES; i++) {
            if (!mock_fs[i].used) {
                idx = i;
                mock_fs[i].used = 1;
                mock_fs[i].is_dir = 0;
                size_t n = strlen(path);
                if (n > 63) n = 63;
                memcpy(mock_fs[i].name, path, n);
                mock_fs[i].name[n] = 0;
                mock_fs[i].size = 0;
                break;
            }
        }
        if (idx < 0) return 0;
    }
    fs_file_t* f = (fs_file_t*)kmalloc(sizeof(fs_file_t));
    if (!f) return 0;
    f->mft_number = (uint64_t)idx;
    f->position = 0;
    f->size = mock_fs[idx].size;
    f->mode = (uint8_t)mode;
    f->flags = 0;
    return f;
}

int fs_close(fs_file_t* file) {
    if (file) kfree(file);
    return 0;
}

size_t fs_read(fs_file_t* file, void* buffer, size_t size) {
    if (!file || !buffer) return 0;
    int idx = (int)file->mft_number;
    if (idx < 0 || idx >= MOCK_FS_MAX_FILES || !mock_fs[idx].used) return 0;
    if (file->position >= mock_fs[idx].size) return 0;
    size_t avail = mock_fs[idx].size - (uint32_t)file->position;
    size_t n = size < avail ? size : avail;
    memcpy(buffer, mock_fs[idx].data + file->position, n);
    file->position += n;
    return n;
}

size_t fs_write(fs_file_t* file, const void* buffer, size_t size) {
    if (!file || !buffer) return 0;
    int idx = (int)file->mft_number;
    if (idx < 0 || idx >= MOCK_FS_MAX_FILES || !mock_fs[idx].used) return 0;
    size_t room = MOCK_FS_MAX_SIZE - (size_t)file->position;
    size_t n = size < room ? size : room;
    memcpy(mock_fs[idx].data + file->position, buffer, n);
    file->position += n;
    if (file->position > mock_fs[idx].size) mock_fs[idx].size = (uint32_t)file->position;
    file->size = mock_fs[idx].size;
    return n;
}

int fs_seek(fs_file_t* file, int64_t offset, int whence) {
    if (!file) return -1;
    int64_t base = 0;
    if (whence == 1) base = (int64_t)file->position;
    else if (whence == 2) base = (int64_t)file->size;
    else if (whence != 0) return -1;
    int64_t pos = base + offset;
    if (pos < 0) pos = 0;
    if ((uint64_t)pos > file->size) pos = (int64_t)file->size;
    file->position = (uint64_t)pos;
    return 0;
}

int fs_mkdir(const char* path) {
    if (!path) return -1;
    if (mock_fs_find(path) >= 0) return -1;
    for (int i = 0; i < MOCK_FS_MAX_FILES; i++) {
        if (!mock_fs[i].used) {
            size_t n = strlen(path);
            if (n > 63) n = 63;
            memcpy(mock_fs[i].name, path, n);
            mock_fs[i].name[n] = 0;
            mock_fs[i].used = 1;
            mock_fs[i].is_dir = 1;
            mock_fs[i].size = 0;
            return 0;
        }
    }
    return -1;
}

int fs_truncate(fs_file_t* file, uint64_t size) {
    if (!file) return -1;
    int idx = (int)file->mft_number;
    if (idx < 0 || idx >= MOCK_FS_MAX_FILES || !mock_fs[idx].used) return -1;
    if (size >= mock_fs[idx].size) return 0;
    mock_fs[idx].size = (uint32_t)size;
    file->size = (uint64_t)size;
    if (file->position > size) file->position = size;
    return 0;
}

int fs_rename(const char* old_path, const char* new_path) {
    if (!old_path || !new_path) return -1;
    int idx = mock_fs_find(old_path);
    if (idx < 0) return -1;
    if (mock_fs_find(new_path) >= 0) return -1;
    size_t n = strlen(new_path);
    if (n > 63) n = 63;
    memcpy(mock_fs[idx].name, new_path, n);
    mock_fs[idx].name[n] = 0;
    return 0;
}

int fs_rmdir(const char* path) {
    if (!path) return -1;
    int idx = mock_fs_find(path);
    if (idx < 0) return -1;
    if (mock_fs[idx].is_dir == 0) return -1;
    /* Refuse non-empty: any other entry with this dir as prefix parent. */
    for (int i = 0; i < MOCK_FS_MAX_FILES; i++) {
        if (mock_fs[i].used && i != idx &&
            strncmp(mock_fs[i].name, path, strlen(path)) == 0 &&
            mock_fs[i].name[strlen(path)] == '/') {
            return -1;
        }
    }
    mock_fs[idx].used = 0;
    return 0;
}

int fs_unlink(const char* path) {
    int idx = mock_fs_find(path);
    if (idx < 0) return -1;
    if (mock_fs[idx].is_dir) return -1;
    mock_fs[idx].used = 0;
    return 0;
}

int fs_is_directory(const char* path) {
    if (!path) return -1;
    int idx = mock_fs_find(path);
    if (idx < 0) return -1;
    return mock_fs[idx].is_dir ? 1 : 0;
}

int fs_stat(const char* path, attr_filename_t* info) {
    if (!path || !info) return -1;
    int idx = mock_fs_find(path);
    if (idx < 0) return -1;
    memset(info, 0, sizeof(*info));
    /* Like the real kernel, report the basename (after the last '/'). */
    const char* base = strrchr(mock_fs[idx].name, '/');
    base = (base && base[1]) ? base + 1 : mock_fs[idx].name;
    size_t n = strlen(base);
    if (n > MAX_FILENAME_LEN) n = MAX_FILENAME_LEN;
    memcpy(info->filename, base, n);
    info->filename_length = (uint8_t)n;
    info->real_size = mock_fs[idx].size;
    info->parent_mft = mock_fs[idx].is_dir ? 5 : 3;
    return 0;
}

int fs_readdir(const char* path, void (*callback)(const char*, int, uint32_t)) {
    (void)path;
    if (!callback) return -1;
    for (int i = 0; i < MOCK_FS_MAX_FILES; i++) {
        if (mock_fs[i].used && !mock_fs[i].is_dir) {
            callback(mock_fs[i].name, 0, mock_fs[i].size);
        }
    }
    return 0;
}

/* ---- Disk / GPT mocks (test-configured) ----
 *
 * part.c is compiled as REAL source; its disk_* and gpt_* dependencies
 * resolve here. Tests configure the disk-ready flag, the GPT entry table,
 * and inspect the exact (lba, count) that reached the disk driver.
 */

#include "../../include/disk.h"
#include "../../include/gpt.h"

static int mock_disk_ready = 0;
static uint64_t mock_last_lba = 0;
static uint32_t mock_last_count = 0;
static int mock_last_is_write = 0;
static int mock_io_calls = 0;

/* Range of LBAs touched since the last io_reset (min start, max end).
 * Lets tests prove block I/O never escapes a bound volume. */
static uint64_t mock_touch_min = 0;
static uint64_t mock_touch_max = 0;

#define MOCK_GPT_MAX_PARTS 16

static gpt_entry_t mock_gpt_table[MOCK_GPT_MAX_PARTS];
static uint32_t mock_gpt_total = 0;

void mock_disk_set_ready(int ready) { mock_disk_ready = ready; }

void mock_disk_io_reset(void) {
    mock_io_calls = 0;
    mock_last_lba = 0;
    mock_last_count = 0;
    mock_last_is_write = 0;
    mock_touch_min = 0;
    mock_touch_max = 0;
}

void mock_disk_io_range(uint64_t* min_lba, uint64_t* max_end_lba) {
    if (min_lba) *min_lba = mock_touch_min;
    if (max_end_lba) *max_end_lba = mock_touch_max;
}

void mock_disk_last_io(uint64_t* lba, uint32_t* count, int* is_write, int* calls) {
    if (lba) *lba = mock_last_lba;
    if (count) *count = mock_last_count;
    if (is_write) *is_write = mock_last_is_write;
    if (calls) *calls = mock_io_calls;
}

void mock_gpt_setup(const gpt_entry_t* entries, uint32_t count) {
    memset(mock_gpt_table, 0, sizeof(mock_gpt_table));
    if (count > MOCK_GPT_MAX_PARTS) count = MOCK_GPT_MAX_PARTS;
    mock_gpt_total = count;
    for (uint32_t i = 0; i < count; i++) mock_gpt_table[i] = entries[i];
}

int disk_init(void) { return mock_disk_ready ? 0 : -1; }
int disk_is_ready(void) { return mock_disk_ready; }
uint64_t disk_get_size(void) { return mock_disk_ready ? 64ULL * 1024 * 1024 : 0; }

/* Media class control: 1 = SSD, 0 = HDD (default). */
static int mock_disk_ssd = 0;
void mock_disk_set_ssd(int ssd) { mock_disk_ssd = ssd; }
int disk_is_ssd(void) {
    if (!mock_disk_ready) return -1;
    return mock_disk_ssd;
}

int disk_read(uint32_t lba, uint32_t count, void* buffer) {
    if (!mock_disk_ready || !buffer || count == 0) return -1;
    mock_last_lba = lba;
    mock_last_count = count;
    mock_last_is_write = 0;
    mock_io_calls++;
    if (mock_io_calls == 1 || lba < mock_touch_min) mock_touch_min = lba;
    if ((uint64_t)lba + count > mock_touch_max) mock_touch_max = (uint64_t)lba + count;
    return 0;
}

int disk_write(uint32_t lba, uint32_t count, const void* buffer) {
    if (!mock_disk_ready || !buffer || count == 0) return -1;
    mock_last_lba = lba;
    mock_last_count = count;
    mock_last_is_write = 1;
    mock_io_calls++;
    if (mock_io_calls == 1 || lba < mock_touch_min) mock_touch_min = lba;
    if ((uint64_t)lba + count > mock_touch_max) mock_touch_max = (uint64_t)lba + count;
    return 0;
}

static disk_info_t mock_disk_identity = { 0 };
const disk_info_t* disk_get_info(void) {
    if (!mock_disk_ready) return 0;
    return &mock_disk_identity;
}

int gpt_init(disk_ops_t* ops) { (void)ops; return mock_gpt_total > 0 ? 0 : -1; }
int gpt_list_partitions(void) { return (int)mock_gpt_total; }

const gpt_entry_t* gpt_get_partition(uint32_t index) {
    if (index >= mock_gpt_total) return 0;
    gpt_entry_t* e = &mock_gpt_table[index];
    uint8_t zero[16] = {0};
    if (memcmp(e->type_guid, zero, 16) == 0) return 0;  /* unused slot */
    return e;
}

/* ---- VGA / terminal mock ---- */

void terminal_putchar(char c) { (void)c; }
void terminal_write(const char* s, size_t size) { (void)s; (void)size; }
void terminal_writestring(const char* s) { (void)s; }
void terminal_writestring_nl(const char* s) { (void)s; }
void terminal_put_dec(uint64_t n) { (void)n; }
void terminal_clear(void) {}
