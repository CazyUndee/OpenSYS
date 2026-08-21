/*
 * vfs.c - Virtual Filesystem Layer
 */

#include <stdint.h>
#include <stddef.h>
#include "vfs.h"
#include "kheap.h"
#include "ramfs.h"
#include "process.h"
#include "kstring.h"

#define MAX_VFS_NODES 128

static vfs_mount_t mounts[VFS_MAX_MOUNTS];
static int mount_count = 0;

static vfs_node_t node_pool[MAX_VFS_NODES];

/* Fallback fd table used when no process context exists (kernel context,
 * host tests). User processes get their own per-process table instead. */
static fd_table_t kernel_fd_table;

static fd_table_t* get_current_fd_table(void) {
    process_t* proc = process_current();
    if (proc && proc->fd_table) return proc->fd_table;
    return &kernel_fd_table;
}

static vfs_ops_t* find_ops(const char* path) {
    (void)path;
    if (mount_count > 0 && mounts[0].active) {
        return mounts[0].ops;
    }
    return 0;
}

static vfs_node_t* alloc_node(void) {
    for (int i = 0; i < MAX_VFS_NODES; i++) {
        if (node_pool[i].ref_count == 0 && node_pool[i].ops == 0) {
            return &node_pool[i];
        }
    }
    return 0;
}

static void free_node(vfs_node_t* node) {
    node->ref_count = 0;
    node->ops = 0;
    node->internal_fd = -1;
    node->type = 0;
    node->offset = 0;
    node->size = 0;
    node->flags = 0;
}

/* ========== Ramfs VFS ops ========== */

static int ramfs_vfs_open(const char* path, int flags) {
    int fd;
    if (flags & VFS_O_CREAT) {
        fd = ramfs_find(path);
        if (fd < 0) {
            fd = ramfs_create(path);
        }
    } else {
        fd = ramfs_find(path);
    }
    return fd;
}

static int ramfs_vfs_close(int internal_fd) {
    (void)internal_fd;
    return 0;
}

static int ramfs_vfs_read(int internal_fd, void* buf, size_t size, size_t offset) {
    return ramfs_read(internal_fd, buf, (uint32_t)size, (uint32_t)offset);
}

static int ramfs_vfs_write(int internal_fd, const void* buf, size_t size) {
    return ramfs_write(internal_fd, buf, (uint32_t)size);
}

static int ramfs_vfs_mkdir(const char* path) {
    return ramfs_mkdir(path);
}

static int ramfs_vfs_unlink(const char* path) {
    return ramfs_delete(path);
}

static void ramfs_vfs_list(void (*callback)(const char*, int, uint32_t)) {
    ramfs_list(callback);
}

static vfs_ops_t ramfs_vfs_ops = {
    .open = ramfs_vfs_open,
    .close = ramfs_vfs_close,
    .read = ramfs_vfs_read,
    .write = ramfs_vfs_write,
    .mkdir = ramfs_vfs_mkdir,
    .unlink = ramfs_vfs_unlink,
    .list = ramfs_vfs_list,
};

/* ========== VFS core ========== */

void vfs_init(void) {
    for (int i = 0; i < MAX_VFS_NODES; i++) {
        node_pool[i].ref_count = 0;
        node_pool[i].ops = 0;
        node_pool[i].internal_fd = -1;
    }
    for (int i = 0; i < VFS_MAX_FDS; i++) {
        kernel_fd_table.fds[i] = 0;
    }
    kernel_fd_table.count = 0;
    mount_count = 0;
    vfs_mount("/", &ramfs_vfs_ops);
}

void vfs_mount(const char* path, vfs_ops_t* ops) {
    if (!path) return;

    /* Idempotent: no duplicate mountpoints */
    for (int m = 0; m < mount_count; m++) {
        if (mounts[m].active && k_strcmp(mounts[m].path, path) == 0) {
            mounts[m].ops = ops;  /* allow re-registering ops */
            return;
        }
    }

    if (mount_count >= VFS_MAX_MOUNTS) return;
    int i = 0;
    while (path[i] && i < VFS_MAX_PATH - 1) {
        mounts[mount_count].path[i] = path[i];
        i++;
    }
    mounts[mount_count].path[i] = 0;
    mounts[mount_count].ops = ops;
    mounts[mount_count].active = 1;
    mount_count++;
}

int vfs_open(const char* path, int flags) {
    vfs_ops_t* ops = find_ops(path);
    if (!ops || !ops->open) return -1;

    int internal_fd = ops->open(path, flags);
    if (internal_fd < 0) return -1;

    vfs_node_t* node = alloc_node();
    if (!node) {
        if (ops->close) ops->close(internal_fd);
        return -1;
    }

    node->type = VFS_TYPE_FILE;
    node->internal_fd = internal_fd;
    node->ops = ops;
    node->offset = 0;
    node->size = ramfs_size(internal_fd);
    node->ref_count = 1;
    node->flags = flags & 3;

    process_t* proc = process_current();
    fd_table_t* table = 0;
    if (proc && proc->fd_table) {
        table = proc->fd_table;
    } else if (proc) {
        /* First open from this process — create its fd table */
        table = fd_table_create();
        if (!table) {
            free_node(node);
            return -1;
        }
        proc->fd_table = table;
    } else {
        /* Kernel context — use the shared kernel fd table */
        table = &kernel_fd_table;
    }

    int fd = fd_table_alloc(table, node);
    if (fd < 0) {
        free_node(node);
        return -1;
    }

    return fd;
}

int vfs_close(int fd) {
    fd_table_t* table = get_current_fd_table();
    if (!table) return -1;

    return fd_table_close(table, fd);
}

int vfs_read(int fd, void* buf, size_t size) {
    fd_table_t* table = get_current_fd_table();
    if (!table) return -1;

    vfs_node_t* node = fd_table_get(table, fd);
    if (!node || !node->ops || !node->ops->read) return -1;
    if ((node->flags & 3) == VFS_O_WRONLY) return -1;

    int result = node->ops->read(node->internal_fd, buf, size, node->offset);
    if (result > 0) {
        node->offset += result;
    }
    return result;
}

int vfs_write(int fd, const void* buf, size_t size) {
    fd_table_t* table = get_current_fd_table();
    if (!table) return -1;

    vfs_node_t* node = fd_table_get(table, fd);
    if (!node || !node->ops || !node->ops->write) return -1;
    if ((node->flags & 3) == VFS_O_RDONLY) return -1;

    int result = node->ops->write(node->internal_fd, buf, size);
    if (result > 0) {
        node->offset += result;
        if (node->offset > node->size) node->size = node->offset;
    }
    return result;
}

int vfs_seek(int fd, int whence, int offset) {
    fd_table_t* table = get_current_fd_table();
    if (!table) return -1;

    vfs_node_t* node = fd_table_get(table, fd);
    if (!node) return -1;

    switch (whence) {
    case VFS_SEEK_SET:
        node->offset = offset;
        break;
    case VFS_SEEK_CUR:
        node->offset += offset;
        break;
    case VFS_SEEK_END:
        node->offset = node->size + offset;
        break;
    default:
        return -1;
    }

    return (int)node->offset;
}

int vfs_mkdir(const char* path) {
    vfs_ops_t* ops = find_ops(path);
    if (!ops || !ops->mkdir) return -1;
    return ops->mkdir(path);
}

int vfs_unlink(const char* path) {
    vfs_ops_t* ops = find_ops(path);
    if (!ops || !ops->unlink) return -1;
    return ops->unlink(path);
}

void vfs_list(void (*callback)(const char*, int, uint32_t)) {
    if (mount_count > 0 && mounts[0].active && mounts[0].ops->list) {
        mounts[0].ops->list(callback);
    }
}

/* ========== Mount table accessors ========== */

int vfs_mount_count(void) {
    int count = 0;
    for (int i = 0; i < mount_count; i++) {
        if (mounts[i].active) count++;
    }
    return count;
}

/* Copy the i-th active mount's mountpoint into out_path (VFS_MAX_PATH bytes).
 * Returns 0 on success, -1 if i is out of range. */
int vfs_get_mount(int index, char* out_path) {
    if (!out_path) return -1;
    int count = 0;
    for (int i = 0; i < mount_count; i++) {
        if (!mounts[i].active) continue;
        if (count == index) {
            int j = 0;
            while (mounts[i].path[j] && j < VFS_MAX_PATH - 1) {
                out_path[j] = mounts[i].path[j];
                j++;
            }
            out_path[j] = 0;
            return 0;
        }
        count++;
    }
    return -1;
}

/* ========== FD table ========== */

fd_table_t* fd_table_create(void) {
    fd_table_t* table = (fd_table_t*)kmalloc(sizeof(fd_table_t));
    if (!table) return 0;
    for (int i = 0; i < VFS_MAX_FDS; i++) {
        table->fds[i] = 0;
    }
    table->count = 0;
    return table;
}

void fd_table_destroy(fd_table_t* table) {
    if (!table) return;
    for (int i = 0; i < VFS_MAX_FDS; i++) {
        if (table->fds[i]) {
            table->fds[i]->ref_count--;
            if (table->fds[i]->ref_count <= 0) {
                if (table->fds[i]->ops && table->fds[i]->ops->close) {
                    table->fds[i]->ops->close(table->fds[i]->internal_fd);
                }
                free_node(table->fds[i]);
            }
            table->fds[i] = 0;
        }
    }
    kfree(table);
}

int fd_table_alloc(fd_table_t* table, vfs_node_t* node) {
    if (!table) return -1;
    for (int i = 0; i < VFS_MAX_FDS; i++) {
        if (!table->fds[i]) {
            table->fds[i] = node;
            node->ref_count++;
            table->count++;
            return i;
        }
    }
    return -1;
}

vfs_node_t* fd_table_get(fd_table_t* table, int fd) {
    if (!table || fd < 0 || fd >= VFS_MAX_FDS) return 0;
    return table->fds[fd];
}

int fd_table_close(fd_table_t* table, int fd) {
    if (!table || fd < 0 || fd >= VFS_MAX_FDS) return -1;
    vfs_node_t* node = table->fds[fd];
    if (!node) return -1;

    node->ref_count--;
    if (node->ref_count <= 0) {
        if (node->ops && node->ops->close) {
            node->ops->close(node->internal_fd);
        }
        free_node(node);
    }

    table->fds[fd] = 0;
    table->count--;
    return 0;
}

fd_table_t* fd_table_clone(fd_table_t* src) {
    fd_table_t* dst = fd_table_create();
    if (!dst) return 0;
    for (int i = 0; i < VFS_MAX_FDS; i++) {
        if (src->fds[i]) {
            dst->fds[i] = src->fds[i];
            src->fds[i]->ref_count++;
        }
    }
    dst->count = src->count;
    return dst;
}
