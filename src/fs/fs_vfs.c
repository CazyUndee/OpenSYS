/*
 * fs_vfs.c - NTFS-style fs backend adapter for the VFS fd layer
 *
 * Copyright (C) 2026 CazyUndee
 * SPDX-License-Identifier: AGPL-3.0
 *
 * The disk-backed fs.c filesystem is the shell's "real" filesystem.
 * This adapter exposes it through the unified VFS mount table, so files
 * like /notes.txt are reachable through the same file-descriptor layer
 * as virtual resources (/proc, /sys, /dev) and ramfs.
 */

#include <stdint.h>
#include <stddef.h>
#include "fs.h"
#include "vfs.h"

/* The VFS open/read/write/close ops are internal-fd based. Map a small
 * table of fs_file_t* handles to internal fds. */
#define FS_VFS_OPEN_MAX 8

static fs_file_t* fs_vfs_handles[FS_VFS_OPEN_MAX];
static int fs_vfs_handle_used[FS_VFS_OPEN_MAX];

static int fs_vfs_open(const char* path, int flags) {
    if (!path) return -1;

    /* Map VFS open flags onto fs modes: 0=read, 1=write/create. */
    int mode;
    int access = flags & 3;  /* VFS_O_RDONLY/WRONLY/RDWR */
    if (access == VFS_O_WRONLY || access == VFS_O_RDWR || (flags & VFS_O_CREAT)) {
        mode = 1;
    } else {
        mode = 0;
    }

    fs_file_t* f = fs_open(path, mode);
    if (!f) return -1;

    /* VFS_O_TRUNC: shrink the file to zero so a shorter write cannot
     * leave stale tail bytes from a previous larger content. */
    if (flags & VFS_O_TRUNC) {
        fs_truncate(f, 0);
        fs_seek(f, 0, 0);
    }

    for (int i = 0; i < FS_VFS_OPEN_MAX; i++) {
        if (!fs_vfs_handle_used[i]) {
            fs_vfs_handles[i] = f;
            fs_vfs_handle_used[i] = 1;
            return i;
        }
    }
    fs_close(f);
    return -1;
}

static int fs_vfs_close(int internal_fd) {
    if (internal_fd < 0 || internal_fd >= FS_VFS_OPEN_MAX || !fs_vfs_handle_used[internal_fd]) {
        return -1;
    }
    fs_close(fs_vfs_handles[internal_fd]);
    fs_vfs_handle_used[internal_fd] = 0;
    return 0;
}

static int fs_vfs_read(int internal_fd, void* buf, size_t size, size_t offset) {
    if (internal_fd < 0 || internal_fd >= FS_VFS_OPEN_MAX || !fs_vfs_handle_used[internal_fd]) {
        return -1;
    }
    fs_file_t* f = fs_vfs_handles[internal_fd];
    /* Read access is enforced by the VFS node flags (vfs_read rejects
     * O_WRONLY nodes); fs_read itself is mode-agnostic, so a handle
     * opened O_RDWR must be readable. */
    if (fs_seek(f, (int64_t)offset, 0) < 0) return -1;
    return (int)fs_read(f, buf, size);
}

static int fs_vfs_write(int internal_fd, const void* buf, size_t size) {
    if (internal_fd < 0 || internal_fd >= FS_VFS_OPEN_MAX || !fs_vfs_handle_used[internal_fd]) {
        return -1;
    }
    fs_file_t* f = fs_vfs_handles[internal_fd];
    /* Write access is enforced by the VFS node flags (vfs_write rejects
     * O_RDONLY nodes); fs_write is mode-agnostic. */
    return (int)fs_write(f, buf, size);
}

static int fs_vfs_size(int internal_fd) {
    if (internal_fd < 0 || internal_fd >= FS_VFS_OPEN_MAX || !fs_vfs_handle_used[internal_fd]) {
        return 0;
    }
    return (int)fs_vfs_handles[internal_fd]->size;
}

static int fs_vfs_mkdir(const char* path) {
    return fs_mkdir(path);
}

static int fs_vfs_unlink(const char* path) {
    return fs_unlink(path);
}

static void fs_vfs_list(void (*callback)(const char*, int, uint32_t)) {
    fs_readdir("/", callback);
}

vfs_ops_t fs_vfs_ops = {
    .open  = fs_vfs_open,
    .close = fs_vfs_close,
    .read  = fs_vfs_read,
    .write = fs_vfs_write,
    .mkdir = fs_vfs_mkdir,
    .unlink = fs_vfs_unlink,
    .list  = fs_vfs_list,
    .size  = fs_vfs_size,
};
