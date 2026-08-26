/*
 * vfile.c - Virtual Filesystem Resource Layer
 *
 * Copyright (C) 2026 CazyUndee
 * SPDX-License-Identifier: AGPL-3.0
 *
 * Implements the Plan0 resource namespace content under 0/ (system,
 * hardware, dev) that is
 * generated on read (or handled on write) and consume no disk storage.
 * Each resource has a read callback that fills a caller-provided buffer,
 * optionally a write callback for writable resources, and each directory
 * has a list callback that enumerates children.
 */

#include <stdint.h>
#include <stddef.h>
#include "vfile.h"
#include "timer.h"
#include "pmm.h"
#include "rtc.h"
#include "process.h"
#include "kstring.h"
#include "vga.h"
#include "vfs.h"
#include "version.h"
#include "pci.h"
#include "kheap.h"
#include "vfs.h"

/* ---- Internal state ---- */

static vfile_entry_t entries[VFILE_MAX_ENTRIES];
static int entry_count = 0;

/* Mutable hostname (shared by 0/system/hostname and 0/system/hostname) */
static char hostname_buf[64] = "plan0";

/* Simple interrupt counters — incremented by irq_handler via
 * vfile_irq_tick().  Low overhead, no locking needed in single-core. */
#define VFILE_NUM_IRQS 16
static uint64_t irq_counts[VFILE_NUM_IRQS];

/* ---- Interrupt counter API (called from irq_handler) ---- */

void vfile_irq_tick(int irq) {
    if (irq >= 0 && irq < VFILE_NUM_IRQS) {
        irq_counts[irq]++;
    }
}

/* ---- Helper: write decimal number to buffer ---- */

static int put_dec(char* buf, uint64_t n) {
    if (n == 0) {
        buf[0] = '0';
        return 1;
    }
    char tmp[21];
    int len = 0;
    while (n > 0) {
        tmp[len++] = '0' + (char)(n % 10);
        n /= 10;
    }
    for (int i = 0; i < len; i++) {
        buf[i] = tmp[len - 1 - i];
    }
    return len;
}

/* ---- Helper: append string to buffer ---- */

static int append_str(char* buf, size_t max, const char* src) {
    int len = 0;
    while (src[len] && (size_t)(len) < max - 1) {
        buf[len] = src[len];
        len++;
    }
    return len;
}

/* ---- Helper: append decimal number to buffer ---- */

static int append_dec(char* buf, size_t max, uint64_t n) {
    char tmp[21];
    int dlen = put_dec(tmp, n);
    for (int i = 0; i < dlen && (size_t)i < max - 1; i++) {
        buf[i] = tmp[i];
    }
    return dlen;
}

/* ---- Helper: append hex number to buffer ---- */

static int append_hex(char* buf, size_t max, uint32_t n) {
    int pos = 0;
    buf[pos++] = '0';
    buf[pos++] = 'x';
    for (int i = 28; i >= 0; i -= 4) {
        int nibble = (n >> i) & 0xF;
        if (pos < (int)max)
            buf[pos++] = nibble < 10 ? (char)('0' + nibble) : (char)('a' + nibble - 10);
    }
    return pos;
}

/* ================================================================
 * Virtual file content generators (read callbacks)
 * ================================================================ */

/* 0/system/runtime/uptime - seconds since boot */
static int gen_uptime(char* buf, size_t max) {
    uint64_t ticks = timer_get_ticks();
    uint64_t seconds = ticks / 1000;
    uint64_t minutes = seconds / 60;
    uint64_t hours   = minutes / 60;

    int pos = 0;

    buf[pos++] = 'U';
    pos += append_str(buf + pos, max - (size_t)pos, "ptime: ");
    pos += append_dec(buf + pos, max - (size_t)pos, hours);
    pos += append_str(buf + pos, max - (size_t)pos, "h ");
    pos += append_dec(buf + pos, max - (size_t)pos, minutes % 60);
    pos += append_str(buf + pos, max - (size_t)pos, "m ");
    pos += append_dec(buf + pos, max - (size_t)pos, seconds % 60);
    pos += append_str(buf + pos, max - (size_t)pos, "s\n");

    buf[pos++] = 'T';
    pos += append_str(buf + pos, max - (size_t)pos, "otal seconds: ");
    pos += append_dec(buf + pos, max - (size_t)pos, seconds);
    pos += append_str(buf + pos, max - (size_t)pos, "\n");

    return pos;
}

/* 0/hardware/memory/ram - RAM stats */
static int gen_memory(char* buf, size_t max) {
    uint64_t total = pmm_get_total();
    uint64_t free  = pmm_get_free();
    uint64_t used  = total - free;

    int pos = 0;

    pos += append_str(buf + pos, max - (size_t)pos, "Total RAM: ");
    pos += append_dec(buf + pos, max - (size_t)pos, total / (1024 * 1024));
    pos += append_str(buf + pos, max - (size_t)pos, " MB\n");

    pos += append_str(buf + pos, max - (size_t)pos, "Free RAM:  ");
    pos += append_dec(buf + pos, max - (size_t)pos, free / (1024 * 1024));
    pos += append_str(buf + pos, max - (size_t)pos, " MB\n");

    pos += append_str(buf + pos, max - (size_t)pos, "Used RAM:  ");
    pos += append_dec(buf + pos, max - (size_t)pos, used / (1024 * 1024));
    pos += append_str(buf + pos, max - (size_t)pos, " MB\n");

    pos += append_str(buf + pos, max - (size_t)pos, "Free pages: ");
    pos += append_dec(buf + pos, max - (size_t)pos, pmm_get_free_pages());
    pos += append_str(buf + pos, max - (size_t)pos, "\n");

    return pos;
}

/* 0/hardware/cpu — CPU identification via CPUID */
static int gen_cpu(char* buf, size_t max) {
    uint32_t eax, ebx, ecx, edx;
    int pos = 0;

    __asm__ volatile("cpuid"
                     : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(0));
    pos += append_str(buf + pos, max - (size_t)pos, "Vendor:     ");
    char vendor[13];
    vendor[0] = (char)(ebx & 0xFF);
    vendor[1] = (char)((ebx >> 8) & 0xFF);
    vendor[2] = (char)((ebx >> 16) & 0xFF);
    vendor[3] = (char)((ebx >> 24) & 0xFF);
    vendor[4] = (char)(edx & 0xFF);
    vendor[5] = (char)((edx >> 8) & 0xFF);
    vendor[6] = (char)((edx >> 16) & 0xFF);
    vendor[7] = (char)((edx >> 24) & 0xFF);
    vendor[8] = (char)(ecx & 0xFF);
    vendor[9] = (char)((ecx >> 8) & 0xFF);
    vendor[10] = (char)((ecx >> 16) & 0xFF);
    vendor[11] = (char)((ecx >> 24) & 0xFF);
    vendor[12] = 0;
    pos += append_str(buf + pos, max - (size_t)pos, vendor);
    pos += append_str(buf + pos, max - (size_t)pos, "\n");

    char brand[49];
    uint32_t* b = (uint32_t*)brand;
    __asm__ volatile("cpuid" : "=a"(b[0]), "=b"(b[1]), "=c"(b[2]), "=d"(b[3]) : "a"(0x80000002));
    __asm__ volatile("cpuid" : "=a"(b[4]), "=b"(b[5]), "=c"(b[6]), "=d"(b[7]) : "a"(0x80000003));
    __asm__ volatile("cpuid" : "=a"(b[8]), "=b"(b[9]), "=c"(b[10]), "=d"(b[11]) : "a"(0x80000004));
    brand[48] = 0;

    pos += append_str(buf + pos, max - (size_t)pos, "Brand:      ");
    pos += append_str(buf + pos, max - (size_t)pos, brand);
    pos += append_str(buf + pos, max - (size_t)pos, "\n");

    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
    pos += append_str(buf + pos, max - (size_t)pos, "Features:   0x");
    for (int i = 28; i >= 0; i -= 4) {
        int nibble = (edx >> i) & 0xF;
        buf[pos++] = nibble < 10 ? (char)('0' + nibble) : (char)('a' + nibble - 10);
    }
    buf[pos++] = '\n';

    return pos;
}

/* 0/system/processes — process table */
static int gen_processes(char* buf, size_t max) {
    int pos = 0;

    pos += append_str(buf + pos, max - (size_t)pos, "PID  Name            State\n");
    pos += append_str(buf + pos, max - (size_t)pos, "---  --------------  -------\n");

    for (int i = 0; i < 64; i++) {
        process_t* p = process_get_by_index(i);
        if (!p || p->state == PROC_STATE_UNUSED) continue;

        pos += append_dec(buf + pos, max - (size_t)pos, p->pid);
        pos += append_str(buf + pos, max - (size_t)pos, "  ");

        int namelen = k_strlen(p->name);
        pos += append_str(buf + pos, max - (size_t)pos, p->name);
        for (int j = namelen; j < 14 && (size_t)pos < max - 1; j++) {
            buf[pos++] = ' ';
        }
        pos += append_str(buf + pos, max - (size_t)pos,
                          p->state == PROC_STATE_RUNNING ? "running" : "other");
        pos += append_str(buf + pos, max - (size_t)pos, "\n");
    }

    return pos;
}

/* 0/system/time — current date and time */
static int gen_datetime(char* buf, size_t max) {
    rtc_time_t t;
    rtc_read_time(&t);

    int pos = 0;

    pos += append_str(buf + pos, max - (size_t)pos, "Date: ");
    pos += append_dec(buf + pos, max - (size_t)pos, t.century);
    pos += append_dec(buf + pos, max - (size_t)pos, t.year);
    buf[pos++] = '-';
    if (t.month < 10) buf[pos++] = '0';
    pos += append_dec(buf + pos, max - (size_t)pos, t.month);
    buf[pos++] = '-';
    if (t.day < 10) buf[pos++] = '0';
    pos += append_dec(buf + pos, max - (size_t)pos, t.day);
    pos += append_str(buf + pos, max - (size_t)pos, "\n");

    pos += append_str(buf + pos, max - (size_t)pos, "Time: ");
    if (t.hour < 10) buf[pos++] = '0';
    pos += append_dec(buf + pos, max - (size_t)pos, t.hour);
    buf[pos++] = ':';
    if (t.minute < 10) buf[pos++] = '0';
    pos += append_dec(buf + pos, max - (size_t)pos, t.minute);
    buf[pos++] = ':';
    if (t.second < 10) buf[pos++] = '0';
    pos += append_dec(buf + pos, max - (size_t)pos, t.second);
    pos += append_str(buf + pos, max - (size_t)pos, "\n");

    return pos;
}

/* 0/system/version — OS version (uses version.h) */
static int gen_version(char* buf, size_t max) {
    int pos = 0;
    pos += append_str(buf + pos, max - (size_t)pos, PLAN0_FULL_NAME "\n");
    return pos;
}

/* 0/system/hostname — system hostname (read/write) */
static int gen_hostname(char* buf, size_t max) {
    int pos = 0;
    pos += append_str(buf + pos, max - (size_t)pos, hostname_buf);
    buf[pos++] = '\n';
    return pos;
}

static int write_hostname(const char* data, size_t len) {
    int n = (int)len;
    while (n > 0 && (data[n-1] == '\n' || data[n-1] == '\r')) n--;
    if (n >= (int)sizeof(hostname_buf)) n = (int)sizeof(hostname_buf) - 1;
    for (int i = 0; i < n; i++) hostname_buf[i] = data[i];
    hostname_buf[n] = 0;
    return n;
}

/* 0/system/stat — CPU statistics */
static int gen_stat(char* buf, size_t max) {
    uint64_t ticks = timer_get_ticks();
    int pos = 0;

    pos += append_str(buf + pos, max - (size_t)pos, "cpu ");
    pos += append_dec(buf + pos, max - (size_t)pos, 0);
    buf[pos++] = ' ';
    pos += append_dec(buf + pos, max - (size_t)pos, 0);
    buf[pos++] = ' ';
    pos += append_dec(buf + pos, max - (size_t)pos, 0);
    buf[pos++] = ' ';
    pos += append_dec(buf + pos, max - (size_t)pos, ticks);
    buf[pos++] = ' ';
    pos += append_dec(buf + pos, max - (size_t)pos, 0);
    buf[pos++] = ' ';
    pos += append_dec(buf + pos, max - (size_t)pos, 0);
    buf[pos++] = ' ';
    pos += append_dec(buf + pos, max - (size_t)pos, 0);
    buf[pos++] = ' ';
    pos += append_dec(buf + pos, max - (size_t)pos, 0);
    pos += append_str(buf + pos, max - (size_t)pos, "\n");

    pos += append_str(buf + pos, max - (size_t)pos, "btime 0\n");
    pos += append_str(buf + pos, max - (size_t)pos, "processes ");
    {
        int count = 0;
        for (int i = 0; i < 64; i++) {
            process_t* p = process_get_by_index(i);
            if (p && p->state != PROC_STATE_UNUSED) count++;
        }
        pos += append_dec(buf + pos, max - (size_t)pos, count);
    }
    pos += append_str(buf + pos, max - (size_t)pos, "\n");

    return pos;
}

/* 0/system/heap — kernel heap statistics and integrity status */
static int gen_heap(char* buf, size_t max) {
    kheap_stats_t st;
    kheap_get_stats(&st);
    int corruptions = kheap_validate();

    int pos = 0;

    pos += append_str(buf + pos, max - (size_t)pos, "Heap total:   ");
    pos += append_dec(buf + pos, max - (size_t)pos, st.total_size);
    pos += append_str(buf + pos, max - (size_t)pos, " bytes\n");

    pos += append_str(buf + pos, max - (size_t)pos, "Heap used:    ");
    pos += append_dec(buf + pos, max - (size_t)pos, st.used_size);
    pos += append_str(buf + pos, max - (size_t)pos, " bytes\n");

    pos += append_str(buf + pos, max - (size_t)pos, "Heap free:    ");
    pos += append_dec(buf + pos, max - (size_t)pos, st.total_size - st.used_size);
    pos += append_str(buf + pos, max - (size_t)pos, " bytes\n");

    pos += append_str(buf + pos, max - (size_t)pos, "Blocks:       ");
    pos += append_dec(buf + pos, max - (size_t)pos, st.block_count);
    pos += append_str(buf + pos, max - (size_t)pos, "\n");

    pos += append_str(buf + pos, max - (size_t)pos, "Allocated:    ");
    pos += append_dec(buf + pos, max - (size_t)pos, st.allocated_blocks);
    pos += append_str(buf + pos, max - (size_t)pos, " blocks\n");

    pos += append_str(buf + pos, max - (size_t)pos, "Free blocks:  ");
    pos += append_dec(buf + pos, max - (size_t)pos, st.free_blocks);
    pos += append_str(buf + pos, max - (size_t)pos, "\n");

    pos += append_str(buf + pos, max - (size_t)pos, "Largest free: ");
    pos += append_dec(buf + pos, max - (size_t)pos, st.largest_free);
    pos += append_str(buf + pos, max - (size_t)pos, " bytes\n");

    pos += append_str(buf + pos, max - (size_t)pos, "Integrity:    ");
    if (corruptions == 0) {
        pos += append_str(buf + pos, max - (size_t)pos, "OK\n");
    } else {
        pos += append_str(buf + pos, max - (size_t)pos, "CORRUPT (") ;
        pos += append_dec(buf + pos, max - (size_t)pos, (uint64_t)corruptions);
        pos += append_str(buf + pos, max - (size_t)pos, " issues)\n");
    }

    return pos;
}

/* 0/system/interrupts — per-IRQ counts */
static const char* irq_names[] = {
    "timer", "keyboard", "cascade", "serial2", "serial1",
    "parallel2", "floppy", "parallel1", "rtc", "free",
    "free", "free", "ps2_mouse", "fpu", "ide_primary", "ide_secondary"
};

static int gen_interrupts(char* buf, size_t max) {
    int pos = 0;

    pos += append_str(buf + pos, max - (size_t)pos, "IRQ  Count  Name\n");
    pos += append_str(buf + pos, max - (size_t)pos, "---  -----  ----\n");

    for (int i = 0; i < VFILE_NUM_IRQS; i++) {
        if (i < 10) buf[pos++] = ' ';
        pos += append_dec(buf + pos, max - (size_t)pos, i);
        pos += append_str(buf + pos, max - (size_t)pos, "  ");
        pos += append_dec(buf + pos, max - (size_t)pos, irq_counts[i]);
        pos += append_str(buf + pos, max - (size_t)pos, "  ");
        pos += append_str(buf + pos, max - (size_t)pos, irq_names[i]);
        pos += append_str(buf + pos, max - (size_t)pos, "\n");
    }

    return pos;
}

/* 0/system/mounts — filesystem mount table (from the live VFS mount table) */
static int gen_mounts(char* buf, size_t max) {
    int pos = 0;

    pos += append_str(buf + pos, max - (size_t)pos, "Filesystem    Mount    Type\n");
    pos += append_str(buf + pos, max - (size_t)pos, "------------  -------  ----\n");

    /* Real mounts registered in the VFS mount table */
    int mcount = vfs_mount_count();
    if (mcount <= 0) {
        pos += append_str(buf + pos, max - (size_t)pos, "(none)\n");
    }
    for (int i = 0; i < mcount; i++) {
        char mpath[VFS_MAX_PATH];
        if (vfs_get_mount(i, mpath) < 0) continue;

        if (k_strcmp(mpath, "/") == 0) {
            pos += append_str(buf + pos, max - (size_t)pos, "ramfs         /        ramfs\n");
        } else {
            pos += append_str(buf + pos, max - (size_t)pos, "vfile         ");
            int plen = k_strlen(mpath);
            for (int j = 0; j < plen && (size_t)pos < max - 1; j++) {
                buf[pos++] = mpath[j];
            }
            while (plen < 7) { buf[pos++] = ' '; plen++; }
            pos += append_str(buf + pos, max - (size_t)pos, "  virtual\n");
        }
    }

    return pos;
}

/* ================================================================
 * 0/system/self/ — per-process information (current process)
 * ================================================================ */

static int gen_self_pid(char* buf, size_t max) {
    process_t* p = process_current();
    int pos = 0;
    if (p) {
        pos += append_dec(buf + pos, max - (size_t)pos, p->pid);
    } else {
        pos += append_str(buf + pos, max - (size_t)pos, "0");
    }
    buf[pos++] = '\n';
    return pos;
}

static int gen_self_name(char* buf, size_t max) {
    process_t* p = process_current();
    int pos = 0;
    if (p) {
        pos += append_str(buf + pos, max - (size_t)pos, p->name);
    } else {
        pos += append_str(buf + pos, max - (size_t)pos, "kernel");
    }
    buf[pos++] = '\n';
    return pos;
}

static int gen_self_status(char* buf, size_t max) {
    process_t* p = process_current();
    int pos = 0;

    pos += append_str(buf + pos, max - (size_t)pos, "Name:    ");
    if (p) {
        pos += append_str(buf + pos, max - (size_t)pos, p->name);
    } else {
        pos += append_str(buf + pos, max - (size_t)pos, "kernel");
    }
    buf[pos++] = '\n';

    pos += append_str(buf + pos, max - (size_t)pos, "PID:     ");
    if (p) {
        pos += append_dec(buf + pos, max - (size_t)pos, p->pid);
    } else {
        buf[pos++] = '0';
    }
    buf[pos++] = '\n';

    pos += append_str(buf + pos, max - (size_t)pos, "State:   ");
    if (p) {
        switch (p->state) {
            case PROC_STATE_RUNNING: pos += append_str(buf + pos, max - (size_t)pos, "running"); break;
            case PROC_STATE_READY:   pos += append_str(buf + pos, max - (size_t)pos, "ready"); break;
            case PROC_STATE_BLOCKED: pos += append_str(buf + pos, max - (size_t)pos, "blocked"); break;
            case PROC_STATE_ZOMBIE:  pos += append_str(buf + pos, max - (size_t)pos, "zombie"); break;
            default:                 pos += append_str(buf + pos, max - (size_t)pos, "unused"); break;
        }
    } else {
        pos += append_str(buf + pos, max - (size_t)pos, "kernel");
    }
    buf[pos++] = '\n';

    pos += append_str(buf + pos, max - (size_t)pos, "Priority:");
    if (p) {
        buf[pos++] = ' ';
        pos += append_dec(buf + pos, max - (size_t)pos, p->priority);
    } else {
        pos += append_str(buf + pos, max - (size_t)pos, " 0");
    }
    buf[pos++] = '\n';

    pos += append_str(buf + pos, max - (size_t)pos, "CPU time:");
    if (p) {
        buf[pos++] = ' ';
        pos += append_dec(buf + pos, max - (size_t)pos, p->cpu_time);
    } else {
        pos += append_str(buf + pos, max - (size_t)pos, " 0");
    }
    pos += append_str(buf + pos, max - (size_t)pos, " ms\n");

    return pos;
}

static void list_proc_self(vfile_dir_emit_fn emit) {
    emit("pid", 0, 0);
    emit("name", 0, 0);
    emit("status", 0, 0);
    emit("fd", 1, 0);
    emit("fdinfo", 0, 0);
}

/* ================================================================
 * 0/system/self/fd — open file descriptors of the current context
 * ================================================================ */

static const char* fd_type_name(int type) {
    switch (type) {
        case VFS_TYPE_FILE:       return "file";
        case VFS_TYPE_DIR:        return "dir";
        case VFS_TYPE_DEVICE:     return "device";
        case VFS_TYPE_PIPE_READ:  return "pipe-r";
        case VFS_TYPE_PIPE_WRITE: return "pipe-w";
        default:                  return "unknown";
    }
}

static void list_self_fd(vfile_dir_emit_fn emit) {
    int count = vfs_fd_count();
    for (int fd = 0; fd < VFS_MAX_FDS; fd++) {
        int type = 0;
        if (vfs_fd_info(fd, &type, 0) < 0) continue;
        (void)count;
        char name[16];
        int n = 0;
        if (fd == 0) { name[n++] = '0'; }
        else {
            int tmp = fd;
            char rev[8];
            int rl = 0;
            while (tmp > 0) { rev[rl++] = (char)('0' + tmp % 10); tmp /= 10; }
            for (int i = rl - 1; i >= 0; i--) name[n++] = rev[i];
        }
        name[n] = 0;
        emit(name, 0, 0);
    }
}

/* 0/system/self/fdinfo — table of open fds (number, type, size) */
static int gen_self_fdinfo(char* buf, size_t max) {
    int pos = 0;

    pos += append_str(buf + pos, max - (size_t)pos, "fd  Type       Size\n");
    pos += append_str(buf + pos, max - (size_t)pos, "--  ---------  ----\n");

    for (int fd = 0; fd < VFS_MAX_FDS; fd++) {
        int type = 0;
        size_t size = 0;
        if (vfs_fd_info(fd, &type, &size) < 0) continue;

        if (fd < 10) buf[pos++] = ' ';
        pos += append_dec(buf + pos, max - (size_t)pos, (uint64_t)fd);
        pos += append_str(buf + pos, max - (size_t)pos, "  ");
        pos += append_str(buf + pos, max - (size_t)pos, fd_type_name(type));
        for (int j = k_strlen(fd_type_name(type)); j < 9 && (size_t)pos < max - 1; j++) {
            buf[pos++] = ' ';
        }
        pos += append_str(buf + pos, max - (size_t)pos, "  ");
        pos += append_dec(buf + pos, max - (size_t)pos, (uint64_t)size);
        buf[pos++] = '\n';
    }

    return pos;
}

/* 0/system/self/fd/N — reads through the open descriptor N */
static int gen_self_fd_path(char* buf, size_t max, const char* path) {
    /* Path is 0/system/self/fd/<number>.
     * Security: clamp the parsed descriptor number to the fd-table range
     * (unbounded accumulation used to overflow int), and refuse chains
     * (fd/N resolving to another self/fd path) which would recurse and
     * corrupt the shared scratch buffer. */
    const char* num = path + k_strlen("/0/system/self/fd/");
    long fd = 0;
    int digits = 0;
    while (*num >= '0' && *num <= '9') {
        fd = fd * 10 + (*num - '0');
        if (fd > 9 * VFS_MAX_FDS) return -1;   /* beyond any valid fd */
        num++;
        digits++;
    }
    if (!digits || *num != 0) return -1;
    if (fd >= VFS_MAX_FDS) return -1;

    /* Security: an fd can hold a node keyed as another self/fd path
     * (they are openable through the adapter), so reading one could
     * recurse without limit. Cap the chain depth instead. */
    static int self_fd_depth = 0;
    if (self_fd_depth >= 2) return -1;
    self_fd_depth++;
    int r = vfs_read((int)fd, buf, max);
    self_fd_depth--;
    return r;
}

/* ================================================================
 * /sys/devices/ — PCI hardware enumeration
 * ================================================================ */

static int gen_devices(char* buf, size_t max) {
    int pos = 0;

    pos += append_str(buf + pos, max - (size_t)pos, "Bus  Dev  Func  Vendor    Device    Class  Sub\n");
    pos += append_str(buf + pos, max - (size_t)pos, "---  ---  ----  --------  --------  -----  ---\n");

    for (int bus = 0; bus < 256; bus++) {
        for (int dev = 0; dev < 32; dev++) {
            if (!pci_device_exists((uint8_t)bus, (uint8_t)dev, 0)) continue;

            int funcs = 1;
            uint8_t header_type = pci_read_byte((uint8_t)bus, (uint8_t)dev, 0, PCI_HEADER_TYPE);
            if (header_type & 0x80) funcs = 8;  /* multi-function */

            for (int func = 0; func < funcs; func++) {
                if (!pci_device_exists((uint8_t)bus, (uint8_t)dev, (uint8_t)func)) continue;

                uint16_t vendor = pci_read_word((uint8_t)bus, (uint8_t)dev, (uint8_t)func, PCI_VENDOR_ID);
                uint16_t device = pci_read_word((uint8_t)bus, (uint8_t)dev, (uint8_t)func, PCI_DEVICE_ID);
                uint8_t  class_code = pci_read_byte((uint8_t)bus, (uint8_t)dev, (uint8_t)func, PCI_CLASS);
                uint8_t  subclass = pci_read_byte((uint8_t)bus, (uint8_t)dev, (uint8_t)func, PCI_SUBCLASS);

                if (vendor == 0xFFFF) continue;

                /* Bus */
                if (bus < 10) buf[pos++] = ' ';
                if (bus < 100) buf[pos++] = ' ';
                pos += append_dec(buf + pos, max - (size_t)pos, bus);
                pos += append_str(buf + pos, max - (size_t)pos, "  ");

                /* Dev */
                if (dev < 10) buf[pos++] = ' ';
                pos += append_dec(buf + pos, max - (size_t)pos, dev);
                pos += append_str(buf + pos, max - (size_t)pos, "  ");

                /* Func */
                pos += append_dec(buf + pos, max - (size_t)pos, func);
                if (func < 10) { buf[pos++] = ' '; buf[pos++] = ' '; buf[pos++] = ' '; }
                else { buf[pos++] = ' '; buf[pos++] = ' '; }
                pos += append_str(buf + pos, max - (size_t)pos, "  ");

                /* Vendor */
                pos += append_hex(buf + pos, max - (size_t)pos, vendor);
                pos += append_str(buf + pos, max - (size_t)pos, "  ");

                /* Device */
                pos += append_hex(buf + pos, max - (size_t)pos, device);
                pos += append_str(buf + pos, max - (size_t)pos, "  ");

                /* Class */
                pos += append_hex(buf + pos, max - (size_t)pos, class_code);
                pos += append_str(buf + pos, max - (size_t)pos, "  ");

                /* Subclass */
                pos += append_hex(buf + pos, max - (size_t)pos, subclass);
                buf[pos++] = '\n';
            }
        }
    }

    return pos;
}

/* ================================================================
 * 0/system/kernel/version (uses version.h)
 * ================================================================ */

static int gen_kernel_version(char* buf, size_t max) {
    int pos = 0;
    pos += append_str(buf + pos, max - (size_t)pos, PLAN0_VERSION "\n");
    return pos;
}

/* 0/system/kernel/name (uses version.h) */
static int gen_kernel_name(char* buf, size_t max) {
    int pos = 0;
    pos += append_str(buf + pos, max - (size_t)pos, PLAN0_NAME "\n");
    return pos;
}

/* 0/system/kernel/arch */
static int gen_kernel_arch(char* buf, size_t max) {
    int pos = 0;
    pos += append_str(buf + pos, max - (size_t)pos, PLAN0_ARCH "\n");
    return pos;
}

/* 0/system/hostname — alias for 0/system/hostname (read/write) */
/* 0/hardware/cpu-id — raw CPUID leaf 0 */
static int gen_cpuid(char* buf, size_t max) {
    uint32_t eax, ebx, ecx, edx;
    int pos = 0;

    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0));
    pos += append_str(buf + pos, max - (size_t)pos, "CPUID leaf 0:\n");
    pos += append_str(buf + pos, max - (size_t)pos, "  EAX = 0x");
    for (int i = 28; i >= 0; i -= 4) { int n = (eax >> i) & 0xF; buf[pos++] = n < 10 ? (char)('0'+n) : (char)('a'+n-10); }
    buf[pos++] = '\n';
    pos += append_str(buf + pos, max - (size_t)pos, "  EBX = 0x");
    for (int i = 28; i >= 0; i -= 4) { int n = (ebx >> i) & 0xF; buf[pos++] = n < 10 ? (char)('0'+n) : (char)('a'+n-10); }
    buf[pos++] = '\n';
    pos += append_str(buf + pos, max - (size_t)pos, "  ECX = 0x");
    for (int i = 28; i >= 0; i -= 4) { int n = (ecx >> i) & 0xF; buf[pos++] = n < 10 ? (char)('0'+n) : (char)('a'+n-10); }
    buf[pos++] = '\n';
    pos += append_str(buf + pos, max - (size_t)pos, "  EDX = 0x");
    for (int i = 28; i >= 0; i -= 4) { int n = (edx >> i) & 0xF; buf[pos++] = n < 10 ? (char)('0'+n) : (char)('a'+n-10); }
    buf[pos++] = '\n';
    return pos;
}

/* 0/hardware/memory/info — detailed memory map */
static int gen_meminfo(char* buf, size_t max) {
    int pos = 0;
    pos += append_str(buf + pos, max - (size_t)pos, "MemTotal:  ");
    pos += append_dec(buf + pos, max - (size_t)pos, pmm_get_total());
    pos += append_str(buf + pos, max - (size_t)pos, " bytes\n");
    pos += append_str(buf + pos, max - (size_t)pos, "MemFree:   ");
    pos += append_dec(buf + pos, max - (size_t)pos, pmm_get_free());
    pos += append_str(buf + pos, max - (size_t)pos, " bytes\n");
    pos += append_str(buf + pos, max - (size_t)pos, "PagesFree: ");
    pos += append_dec(buf + pos, max - (size_t)pos, pmm_get_free_pages());
    pos += append_str(buf + pos, max - (size_t)pos, "\n");
    pos += append_str(buf + pos, max - (size_t)pos, "PageSize:  ");
    pos += append_dec(buf + pos, max - (size_t)pos, 4096);
    pos += append_str(buf + pos, max - (size_t)pos, "\n");
    return pos;
}

/* 0/system/timer — high-resolution tick count */
static int gen_timer(char* buf, size_t max) {
    int pos = 0;
    pos += append_str(buf + pos, max - (size_t)pos, "ticks: ");
    pos += append_dec(buf + pos, max - (size_t)pos, timer_get_ticks());
    pos += append_str(buf + pos, max - (size_t)pos, "\nms:     ");
    pos += append_dec(buf + pos, max - (size_t)pos, timer_get_ms());
    pos += append_str(buf + pos, max - (size_t)pos, "\n");
    return pos;
}

/* 0/hardware/platform — hardware platform info */
static int gen_platform(char* buf, size_t max) {
    int pos = 0;
    pos += append_str(buf + pos, max - (size_t)pos, "Platform:   PC/AT compatible\n");
    pos += append_str(buf + pos, max - (size_t)pos, "Arch:       x86_64\n");
    pos += append_str(buf + pos, max - (size_t)pos, "Mode:       64-bit long mode\n");
    pos += append_str(buf + pos, max - (size_t)pos, "Paging:     4-level (PML4)\n");
    return pos;
}

/* ================================================================
 * Directory listing callbacks
 * ================================================================ */

/* Generic helper: list entries under a given prefix */
static void list_vfile_dir(const char* prefix, vfile_dir_emit_fn emit) {
    int prefix_len = k_strlen(prefix);
    for (int i = 0; i < entry_count; i++) {
        if (entries[i].is_dir) continue;
        if (k_strncmp(entries[i].path, prefix, prefix_len) == 0) {
            const char* name = entries[i].path + prefix_len;
            if (*name == 0) continue;
            int has_slash = 0;
            for (int j = 0; name[j]; j++) {
                if (name[j] == '/') { has_slash = 1; break; }
            }
            if (!has_slash) {
                emit(name, 0, 0);
            }
        }
    }
}

/* / — root directory: lists top-level virtual directories */
static void list_root(vfile_dir_emit_fn emit) {
    emit("0", 1, 0);
}

/* 0/ - namespace root */
static void list_ns_root(vfile_dir_emit_fn emit) {
    emit("system", 1, 0);
    emit("hardware", 1, 0);
    emit("dev", 1, 0);
}

static void list_system(vfile_dir_emit_fn emit) {
    list_vfile_dir("/0/system/", emit);
    /* directories are not picked up by the file-prefix scan */
    emit("kernel", 1, 0);
    emit("runtime", 1, 0);
    emit("self", 1, 0);
}

static void list_sys_kernel(vfile_dir_emit_fn emit) {
    list_vfile_dir("/0/system/kernel/", emit);
}

static void list_sys_runtime(vfile_dir_emit_fn emit) {
    list_vfile_dir("/0/system/runtime/", emit);
}

static void list_hardware(vfile_dir_emit_fn emit) {
    list_vfile_dir("/0/hardware/", emit);
    /* memory has files below it; emit the directory explicitly */
    emit("memory", 1, 0);
}

static void list_hw_memory(vfile_dir_emit_fn emit) {
    list_vfile_dir("/0/hardware/memory/", emit);
}

static void list_dev(vfile_dir_emit_fn emit) {
    emit("null", 0, 0);
    emit("zero", 0, 0);
    emit("console", 0, 0);
}

/* ================================================================
 * 0/dev/null — discard any write, read returns 0 bytes
 * ================================================================ */

static int gen_null(char* buf, size_t max) {
    (void)buf; (void)max;
    return 0;
}

static int write_null(const char* data, size_t len) {
    (void)data;
    return (int)len;
}

/* ================================================================
 * 0/dev/zero — read returns NUL bytes, write discards
 * ================================================================ */

static int gen_zero(char* buf, size_t max) {
    int n = (int)max - 1;
    for (int i = 0; i < n; i++) buf[i] = 0;
    return n;
}

static int write_zero(const char* data, size_t len) {
    (void)data;
    return (int)len;
}

/* ================================================================
 * 0/dev/console — write goes to terminal (VGA + serial)
 * ================================================================ */

static int gen_console(char* buf, size_t max) {
    int pos = 0;
    pos += append_str(buf + pos, max - (size_t)pos, PLAN0_NAME " console\n");
    pos += append_str(buf + pos, max - (size_t)pos, "Write to this file to display text.\n");
    return pos;
}

static int write_console(const char* data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        terminal_putchar(data[i]);
    }
    return (int)len;
}

/* ================================================================
 * Registration
 * ================================================================ */

static void reg_file(const char* path, vfile_read_fn fn) {
    if (entry_count >= VFILE_MAX_ENTRIES) return;
    vfile_entry_t* e = &entries[entry_count++];
    k_strcpy(e->path, path);
    e->is_dir = 0;
    e->read_fn = fn;
    e->write_fn = 0;
    e->list_fn = 0;
    e->size = 0;
}

static void reg_file_rw(const char* path, vfile_read_fn read_fn, vfile_write_fn write_fn) {
    if (entry_count >= VFILE_MAX_ENTRIES) return;
    vfile_entry_t* e = &entries[entry_count++];
    k_strcpy(e->path, path);
    e->is_dir = 0;
    e->read_fn = read_fn;
    e->write_fn = write_fn;
    e->list_fn = 0;
    e->size = 0;
}

static void reg_dir(const char* path, vfile_list_fn fn) {
    if (entry_count >= VFILE_MAX_ENTRIES) return;
    vfile_entry_t* e = &entries[entry_count++];
    k_strcpy(e->path, path);
    e->is_dir = 1;
    e->read_fn = 0;
    e->write_fn = 0;
    e->list_fn = fn;
    e->size = 0;
}

void vfile_init(void) {
    entry_count = 0;

    for (int i = 0; i < VFILE_NUM_IRQS; i++) irq_counts[i] = 0;

    /* Register the namespace resource tree in the live VFS mount table so
     * /0-system resources are reachable through the fd layer. The vfile
     * layer has no backing ops of its own — entries are resolved by the
     * vfile registry — so the mount ops pointer is NULL (path-only entry;
     * find_ops routes NULL-op mounts to the vfile adapter). */
    vfs_mount("/0", 0);

    /* Directories */
    reg_dir("/",             list_root);
    reg_dir("/0",            list_ns_root);
    reg_dir("/0/system",     list_system);
    reg_dir("/0/system/kernel",  list_sys_kernel);
    reg_dir("/0/system/runtime", list_sys_runtime);
    reg_dir("/0/system/self",    list_proc_self);
    reg_dir("/0/system/self/fd", list_self_fd);
    reg_dir("/0/hardware",       list_hardware);
    reg_dir("/0/hardware/memory", list_hw_memory);
    reg_dir("/0/dev",        list_dev);

    /* 0/system files (read-only) */
    reg_file("/0/system/processes",  gen_processes);
    reg_file("/0/system/time",       gen_datetime);
    reg_file("/0/system/version",    gen_version);
    reg_file("/0/system/timer",      gen_timer);
    reg_file("/0/system/stat",       gen_stat);
    reg_file("/0/system/interrupts", gen_interrupts);
    reg_file("/0/system/mounts",     gen_mounts);
    reg_file("/0/system/heap",       gen_heap);

    /* 0/system files (read/write) */
    reg_file_rw("/0/system/hostname", gen_hostname, write_hostname);

    /* 0/system/kernel */
    reg_file("/0/system/kernel/name",    gen_kernel_name);
    reg_file("/0/system/kernel/version", gen_kernel_version);
    reg_file("/0/system/kernel/arch",    gen_kernel_arch);

    /* 0/system/runtime */
    reg_file("/0/system/runtime/uptime", gen_uptime);

    /* 0/system/self */
    reg_file("/0/system/self/pid",    gen_self_pid);
    reg_file("/0/system/self/name",   gen_self_name);
    reg_file("/0/system/self/status", gen_self_status);
    reg_file("/0/system/self/fdinfo", gen_self_fdinfo);

    /* 0/hardware content (read-only introspection) */
    reg_file("/0/hardware/memory/ram",  gen_memory);
    reg_file("/0/hardware/memory/info", gen_meminfo);
    reg_file("/0/hardware/cpu",         gen_cpu);
    reg_file("/0/hardware/cpu-id",      gen_cpuid);
    reg_file("/0/hardware/platform",    gen_platform);
    reg_file("/0/hardware/pci",         gen_devices);

    /* 0/dev shims (read/write) */
    reg_file_rw("/0/dev/null",    gen_null,    write_null);
    reg_file_rw("/0/dev/zero",    gen_zero,    write_zero);
    reg_file_rw("/0/dev/console", gen_console, write_console);
}

/* ================================================================
 * Public API
 * ================================================================ */

int vfile_is_virtual(const char* path) {
    for (int i = 0; i < entry_count; i++) {
        if (k_strcmp(entries[i].path, path) == 0) return 1;
    }
    return 0;
}

int vfile_exists(const char* path) {
    /* Registered virtual files (not directories). */
    for (int i = 0; i < entry_count; i++) {
        if (!entries[i].is_dir && k_strcmp(entries[i].path, path) == 0) return 1;
    }
    /* Dynamic virtual files: 0/system/self/fd/<N> reads through descriptor N. */
    if (k_strncmp(path, "/0/system/self/fd/", k_strlen("/0/system/self/fd/")) == 0) return 1;
    return 0;
}

int vfile_read(const char* path, char* buf, size_t max_len) {
    for (int i = 0; i < entry_count; i++) {
        if (!entries[i].is_dir && k_strcmp(entries[i].path, path) == 0) {
            if (entries[i].read_fn) {
                return entries[i].read_fn(buf, max_len);
            }
            return -1;
        }
    }

    /* Dynamic virtual files: 0/system/self/fd/<N> reads through the open
     * descriptor N of the current context. */
    if (k_strncmp(path, "/0/system/self/fd/", k_strlen("/0/system/self/fd/")) == 0) {
        return gen_self_fd_path(buf, max_len, path);
    }

    return -1;
}

int vfile_write(const char* path, const char* data, size_t len) {
    for (int i = 0; i < entry_count; i++) {
        if (!entries[i].is_dir && k_strcmp(entries[i].path, path) == 0) {
            if (entries[i].write_fn) {
                return entries[i].write_fn(data, len);
            }
            return -1;
        }
    }
    return -1;
}

int vfile_list(const char* path, void (*callback)(const char*, int, uint32_t)) {
    for (int i = 0; i < entry_count; i++) {
        if (entries[i].is_dir && k_strcmp(entries[i].path, path) == 0) {
            if (entries[i].list_fn) {
                entries[i].list_fn(callback);
                return 0;
            }
            return -1;
        }
    }
    return -1;
}

int vfile_is_writable(const char* path) {
    for (int i = 0; i < entry_count; i++) {
        if (!entries[i].is_dir && k_strcmp(entries[i].path, path) == 0) {
            return entries[i].write_fn != 0;
        }
    }
    return 0;
}
