/*
 * vfile.h - Virtual Filesystem Resource Layer
 *
 * Copyright (C) 2026 CazyUndee
 * SPDX-License-Identifier: AGPL-3.0
 *
 * If a system resource can reasonably be represented as a file,
 * expose it through the filesystem. Virtual files are generated on
 * read (or handled on write) and consume no disk storage.
 *
 * Directory layout:
 *   /proc/uptime       — seconds since boot
 *   /proc/memory       — RAM total/free
 *   /proc/cpu          — CPU vendor and feature flags
 *   /proc/processes    — active process table
 *   /proc/datetime     — current date and time
 *   /proc/version      — OS version string
 *   /proc/hostname     — system hostname (read/write)
 *   /proc/stat         — CPU statistics
 *   /proc/interrupts   — interrupt counts
 *   /sys/kernel/name   — kernel name
 *   /sys/kernel/version — kernel version
 *   /sys/kernel/arch   — architecture
 *   /sys/kernel/hostname — hostname (alias)
 *   /dev/null          — discard any write (read returns 0 bytes)
 *   /dev/zero          — read returns zeros (write discards)
 *   /dev/console       — console output (write goes to terminal)
 */

#ifndef VFILE_H
#define VFILE_H

#include <stdint.h>
#include <stddef.h>

#define VFILE_MAX_ENTRIES 48
#define VFILE_MAX_PATH    128
#define VFILE_MAX_CONTENT 2048
#define VFILE_MAX_DIR_ENTRIES 16
#define VFILE_MAX_DIR_NAME    32

/* Callback: generate content for a virtual file.
 * buf: caller-provided buffer (VFILE_MAX_CONTENT bytes)
 * Returns: number of bytes written to buf, or -1 on error. */
typedef int (*vfile_read_fn)(char* buf, size_t max_len);

/* Callback: handle a write to a virtual file.
 * data: bytes written, len: number of bytes
 * Returns: number of bytes accepted, or -1 on error. */
typedef int (*vfile_write_fn)(const char* data, size_t len);

/* Callback: list virtual directory entries.
 * Each call to emit(name, is_dir, size) reports one entry. */
typedef void (*vfile_dir_emit_fn)(const char* name, int is_dir, uint32_t size);
typedef void (*vfile_list_fn)(vfile_dir_emit_fn emit);

/* A single virtual file entry. */
typedef struct vfile_entry {
    char path[VFILE_MAX_PATH];   /* e.g. "/proc/uptime" */
    int  is_dir;                 /* 1 = directory, 0 = file */
    vfile_read_fn  read_fn;      /* for files: generate content */
    vfile_write_fn write_fn;     /* for files: handle writes (NULL = read-only) */
    vfile_list_fn  list_fn;      /* for dirs: list children */
    uint32_t size;               /* cached size for directory entries */
} vfile_entry_t;

/* Initialize the virtual file system — registers all built-in resources.
 * Must be called before shell_run(). */
void vfile_init(void);

/* Check if path is a virtual file and read its content.
 * Returns bytes read into buf, or -1 if not a virtual file. */
int vfile_read(const char* path, char* buf, size_t max_len);

/* Check if path is a virtual file and write data to it.
 * Returns bytes written, or -1 if not a virtual file or not writable. */
int vfile_write(const char* path, const char* data, size_t len);

/* Check if path is a virtual directory and list its entries.
 * Returns 0 on success, or -1 if not a virtual directory. */
int vfile_list(const char* path, void (*callback)(const char*, int, uint32_t));

/* Check if a path starts with a known virtual filesystem prefix.
 * Returns 1 if virtual, 0 otherwise. */
int vfile_is_virtual(const char* path);

/* Check if a path resolves to a virtual *file* (not a directory),
 * including dynamic paths such as /proc/self/fd/<N>.
 * Returns 1 if it can be opened as a file, 0 otherwise. */
int vfile_exists(const char* path);

/* Check if a virtual file supports writing.
 * Returns 1 if writable, 0 if read-only or not virtual. */
int vfile_is_writable(const char* path);

/* Increment the counter for the given IRQ line (0-15).
 * Called from irq_handler to feed /proc/interrupts. */
void vfile_irq_tick(int irq);

#endif /* VFILE_H */
