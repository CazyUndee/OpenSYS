/*
 * vfs.c - Virtual Filesystem Layer
 */

#include <stdint.h>
#include <stddef.h>
#include "vfs.h"
#include "kheap.h"
#include "ramfs.h"
#include "vfile.h"
#include "process.h"
#include "kstring.h"
#include "vga.h"

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

static void fd_table_install_std(fd_table_t* table);

/* ========== Vfile VFS adapter ==========
 * Virtual resources (/proc, /sys, /dev) are registered as mounts with
 * NULL ops; find_ops() routes those paths here. The adapter bridges the
 * path-based vfile API onto the fd-based VFS interface. */

#define VFILE_VFS_OPEN_MAX 8
static char vfile_open_paths[VFILE_VFS_OPEN_MAX][VFS_MAX_PATH];
static int  vfile_open_used[VFILE_VFS_OPEN_MAX];
static char vfile_scratch[VFILE_MAX_CONTENT];

static int vfile_vfs_open(const char* path, int flags) {
    if (!path || (flags & VFS_O_CREAT)) return -1;
    if (!vfile_exists(path)) return -1;
    for (int i = 0; i < VFILE_VFS_OPEN_MAX; i++) {
        if (!vfile_open_used[i]) {
            int j = 0;
            while (path[j] && j < VFS_MAX_PATH - 1) {
                vfile_open_paths[i][j] = path[j];
                j++;
            }
            vfile_open_paths[i][j] = 0;
            vfile_open_used[i] = 1;
            return i;
        }
    }
    return -1;
}

static int vfile_vfs_close(int internal_fd) {
    if (internal_fd < 0 || internal_fd >= VFILE_VFS_OPEN_MAX) return -1;
    vfile_open_used[internal_fd] = 0;
    return 0;
}

static int vfile_vfs_read(int internal_fd, void* buf, size_t size, size_t offset) {
    if (internal_fd < 0 || internal_fd >= VFILE_VFS_OPEN_MAX || !vfile_open_used[internal_fd]) {
        return -1;
    }
    const char* path = vfile_open_paths[internal_fd];
    /* Generate the full virtual content, then copy the requested window.
     * Virtual resources are small (VFILE_MAX_CONTENT), so a static
     * scratch buffer is sufficient in this single-threaded kernel. */
    int len = vfile_read(path, vfile_scratch, sizeof(vfile_scratch));
    if (len < 0) return -1;
    if (offset >= (size_t)len) return 0;
    size_t avail = (size_t)len - offset;
    size_t n = size < avail ? size : avail;
    for (size_t i = 0; i < n; i++) {
        ((uint8_t*)buf)[i] = (uint8_t)vfile_scratch[offset + i];
    }
    return (int)n;
}

static int vfile_vfs_write(int internal_fd, const void* buf, size_t size) {
    if (internal_fd < 0 || internal_fd >= VFILE_VFS_OPEN_MAX || !vfile_open_used[internal_fd]) {
        return -1;
    }
    return vfile_write(vfile_open_paths[internal_fd], (const char*)buf, size);
}

static int vfile_vfs_size(int internal_fd) {
    if (internal_fd < 0 || internal_fd >= VFILE_VFS_OPEN_MAX || !vfile_open_used[internal_fd]) {
        return 0;
    }
    const char* path = vfile_open_paths[internal_fd];
    /* Dynamic /proc/self/fd/N forwards to another descriptor; generating
     * it to measure size would consume that descriptor (side effect).
     * Report 0 — the size is derived on read. */
    if (k_strncmp(path, "/0/system/self/fd/", k_strlen("/0/system/self/fd/")) == 0) return 0;
    int len = vfile_read(path, vfile_scratch, sizeof(vfile_scratch));
    return len < 0 ? 0 : len;
}

static vfs_ops_t vfile_vfs_ops = {
    .open  = vfile_vfs_open,
    .close = vfile_vfs_close,
    .read  = vfile_vfs_read,
    .write = vfile_vfs_write,
    .size  = vfile_vfs_size,
};

static vfs_ops_t* find_ops(const char* path) {
    if (!path) return 0;

    int best = -1;
    size_t best_len = 0;

    if (path[0] != '/') {
        /* Bare name (e.g. "data.txt"): resolve against the root mount. */
        for (int i = 0; i < mount_count; i++) {
            if (mounts[i].active && mounts[i].path[0] == '/' && mounts[i].path[1] == '\0') {
                best = i;
                break;
            }
        }
    } else {
        /* Longest-prefix match over the mount table. */
        for (int i = 0; i < mount_count; i++) {
            if (!mounts[i].active) continue;
            size_t len = k_strlen(mounts[i].path);
            if (len < best_len) continue;
            if (k_strncmp(path, mounts[i].path, len) != 0) continue;
            /* Component boundary: next char must be '/' or end-of-string,
             * except for the root mount "/" which prefixes everything. */
            if (len > 1 && path[len] != '\0' && path[len] != '/') continue;
            best = i;
            best_len = len;
        }
    }

    if (best < 0) return 0;
    if (mounts[best].ops) return mounts[best].ops;
    /* Virtual namespace (/proc, /sys, /dev) — route via the vfile adapter. */
    return &vfile_vfs_ops;
}

static vfs_node_t* alloc_node(void) {
    for (int i = 0; i < MAX_VFS_NODES; i++) {
        if (node_pool[i].ref_count == 0 && node_pool[i].ops == 0) {
            /* Mark the slot as held so a second alloc cannot return the
             * same node before the caller finishes initializing it. */
            node_pool[i].ref_count = 1;
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

/* Ramfs stores flat names ("data.txt"). When mounted at "/", an absolute
 * path such as "/data.txt" must resolve to the same flat name. */
static const char* ramfs_strip_slash(const char* path) {
    while (path[0] == '/') path++;
    return path;
}

static int ramfs_vfs_open(const char* path, int flags) {
    const char* name = ramfs_strip_slash(path);
    if (name[0] == 0) return -1;  /* cannot open the root dir as a file */
    int fd;
    if (flags & VFS_O_CREAT) {
        fd = ramfs_find(name);
        if (fd < 0) {
            fd = ramfs_create(name);
        }
    } else {
        fd = ramfs_find(name);
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
    return ramfs_mkdir(ramfs_strip_slash(path));
}

static int ramfs_vfs_unlink(const char* path) {
    return ramfs_delete(ramfs_strip_slash(path));
}

static void ramfs_vfs_list(void (*callback)(const char*, int, uint32_t)) {
    ramfs_list(callback);
}

static int ramfs_vfs_size(int internal_fd) {
    return (int)ramfs_size(internal_fd);
}

static vfs_ops_t ramfs_vfs_ops = {
    .open = ramfs_vfs_open,
    .close = ramfs_vfs_close,
    .read = ramfs_vfs_read,
    .write = ramfs_vfs_write,
    .mkdir = ramfs_vfs_mkdir,
    .unlink = ramfs_vfs_unlink,
    .list = ramfs_vfs_list,
    .size = ramfs_vfs_size,
};

/* ========== Pipe VFS ops ==========
 * A pipe is a pair of fd-table entries (read end, write end) sharing a
 * fixed-size ring buffer. The node's internal_fd holds the pipe index
 * into the static pipe pool; the node type distinguishes the ends. */

#define VFS_PIPE_BUF_SIZE 4096
#define VFS_MAX_PIPES     16

typedef struct {
    uint8_t buf[VFS_PIPE_BUF_SIZE];
    size_t  head;    /* next write position */
    size_t  tail;    /* next read position */
    size_t  count;   /* bytes currently buffered */
    int     readers; /* open read ends */
    int     writers; /* open write ends */
} vfs_pipe_t;

static vfs_pipe_t pipes[VFS_MAX_PIPES];
static int pipe_in_use[VFS_MAX_PIPES];

static int pipe_alloc_index(void) {
    for (int i = 0; i < VFS_MAX_PIPES; i++) {
        if (!pipe_in_use[i]) return i;
    }
    return -1;
}

static int pipe_vfs_read(int pipe_idx, void* buf, size_t size, size_t offset) {
    (void)offset;
    if (pipe_idx < 0 || pipe_idx >= VFS_MAX_PIPES || !pipe_in_use[pipe_idx]) return -1;
    if (!buf || size == 0) return 0;

    vfs_pipe_t* p = &pipes[pipe_idx];
    if (p->count == 0) {
        /* Writers gone: EOF. Otherwise nothing to read yet. */
        return p->writers == 0 ? 0 : 0;
    }

    size_t n = size < p->count ? size : p->count;
    uint8_t* dst = (uint8_t*)buf;
    for (size_t i = 0; i < n; i++) {
        dst[i] = p->buf[(p->tail + i) % VFS_PIPE_BUF_SIZE];
    }
    p->tail = (p->tail + n) % VFS_PIPE_BUF_SIZE;
    p->count -= n;
    return (int)n;
}

static int pipe_vfs_write(int pipe_idx, const void* buf, size_t size) {
    if (pipe_idx < 0 || pipe_idx >= VFS_MAX_PIPES || !pipe_in_use[pipe_idx]) return -1;
    if (!buf || size == 0) return 0;

    vfs_pipe_t* p = &pipes[pipe_idx];
    if (p->count >= VFS_PIPE_BUF_SIZE) return 0;  /* full — no room */

    size_t n = size;
    size_t space = VFS_PIPE_BUF_SIZE - p->count;
    if (n > space) n = space;

    const uint8_t* src = (const uint8_t*)buf;
    for (size_t i = 0; i < n; i++) {
        p->buf[(p->head + i) % VFS_PIPE_BUF_SIZE] = src[i];
    }
    p->head = (p->head + n) % VFS_PIPE_BUF_SIZE;
    p->count += n;
    return (int)n;
}

static int pipe_vfs_close_read(int pipe_idx) {
    if (pipe_idx < 0 || pipe_idx >= VFS_MAX_PIPES || !pipe_in_use[pipe_idx]) return -1;
    pipes[pipe_idx].readers--;
    if (pipes[pipe_idx].readers <= 0 && pipes[pipe_idx].writers <= 0) {
        pipe_in_use[pipe_idx] = 0;
    }
    return 0;
}

static int pipe_vfs_close_write(int pipe_idx) {
    if (pipe_idx < 0 || pipe_idx >= VFS_MAX_PIPES || !pipe_in_use[pipe_idx]) return -1;
    pipes[pipe_idx].writers--;
    if (pipes[pipe_idx].readers <= 0 && pipes[pipe_idx].writers <= 0) {
        pipe_in_use[pipe_idx] = 0;
    }
    return 0;
}

static vfs_ops_t pipe_read_vfs_ops = {
    .read  = pipe_vfs_read,
    .close = pipe_vfs_close_read,
};

static vfs_ops_t pipe_write_vfs_ops = {
    .write = pipe_vfs_write,
    .close = pipe_vfs_close_write,
};

/* Create a pipe. fds[0] = read end, fds[1] = write end. Returns 0 on
 * success, -1 on failure. The pipe is created in the current fd table
 * (kernel table when no process is current). */
int vfs_pipe(int fds[2]) {
    if (!fds) return -1;

    int idx = pipe_alloc_index();
    if (idx < 0) return -1;

    vfs_pipe_t* p = &pipes[idx];
    p->head = 0;
    p->tail = 0;
    p->count = 0;
    p->readers = 1;
    p->writers = 1;
    pipe_in_use[idx] = 1;

    vfs_node_t* rnode = alloc_node();
    vfs_node_t* wnode = alloc_node();
    if (!rnode || !wnode) {
        pipe_in_use[idx] = 0;
        if (rnode) free_node(rnode);
        if (wnode) free_node(wnode);
        return -1;
    }

    rnode->type = VFS_TYPE_PIPE_READ;
    rnode->internal_fd = idx;
    rnode->ops = &pipe_read_vfs_ops;
    rnode->offset = 0;
    rnode->size = VFS_PIPE_BUF_SIZE;
    rnode->ref_count = 0;
    rnode->flags = VFS_O_RDONLY;

    wnode->type = VFS_TYPE_PIPE_WRITE;
    wnode->internal_fd = idx;
    wnode->ops = &pipe_write_vfs_ops;
    wnode->offset = 0;
    wnode->size = VFS_PIPE_BUF_SIZE;
    wnode->ref_count = 0;
    wnode->flags = VFS_O_WRONLY;

    fd_table_t* table = get_current_fd_table();

    fds[0] = fd_table_alloc(table, rnode);
    fds[1] = fd_table_alloc(table, wnode);
    if (fds[0] < 0 || fds[1] < 0) {
        if (fds[0] >= 0) fd_table_close(table, fds[0]);
        if (fds[1] >= 0) fd_table_close(table, fds[1]);
        pipe_in_use[idx] = 0;
        return -1;
    }

    return 0;
}

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
    /* The kernel context (shell) also gets the standard descriptors. */
    fd_table_install_std(&kernel_fd_table);
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
    node->size = ops->size ? (size_t)ops->size(internal_fd) : 0;
    node->ref_count = 0;
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

int vfs_dup(int fd) {
    fd_table_t* table = get_current_fd_table();
    if (!table) return -1;

    vfs_node_t* node = fd_table_get(table, fd);
    if (!node) return -1;

    /* Find the lowest free slot (classic dup semantics). The duplicated
     * entry shares the same node — and therefore the same file offset —
     * with the original, exactly like POSIX. */
    int newfd = fd_table_alloc(table, node);
    if (newfd < 0) return -1;
    return newfd;
}

int vfs_dup2(int oldfd, int newfd) {
    fd_table_t* table = get_current_fd_table();
    if (!table) return -1;

    if (oldfd < 0 || newfd < 0 || oldfd >= VFS_MAX_FDS || newfd >= VFS_MAX_FDS) {
        return -1;
    }

    /* dup2(oldfd, oldfd) is a no-op that just returns oldfd. */
    if (oldfd == newfd) {
        return newfd;
    }

    vfs_node_t* node = fd_table_get(table, oldfd);
    if (!node) return -1;

    /* If newfd is already open, close it first (POSIX). */
    if (table->fds[newfd]) {
        fd_table_close(table, newfd);
    }

    table->fds[newfd] = node;
    node->ref_count++;
    table->count++;
    return newfd;
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

/* ========== fd-table introspection ========== */

int vfs_fd_count(void) {
    fd_table_t* table = get_current_fd_table();
    if (!table) return 0;
    return table->count;
}

/* Fill out_type/out_size for an open fd. Returns 0 on success, -1 if the
 * fd is not open in the current table. */
int vfs_fd_info(int fd, int* out_type, size_t* out_size) {
    fd_table_t* table = get_current_fd_table();
    if (!table) return -1;

    vfs_node_t* node = fd_table_get(table, fd);
    if (!node) return -1;

    if (out_type) *out_type = node->type;
    if (out_size) *out_size = node->size;
    return 0;
}

/* ========== Standard fds + console device ==========
 * fds 0/1/2 are the Unix standard descriptors: stdin, stdout, stderr.
 * All are backed by a console device so that dup2/pipe/redirection can
 * target a process's standard output, exactly like POSIX. */

static int console_vfs_read(int internal_fd, void* buf, size_t size, size_t offset) {
    (void)internal_fd; (void)buf; (void)size; (void)offset;
    return 0;  /* no input source yet — clean EOF */
}

static int console_vfs_write(int internal_fd, const void* buf, size_t size) {
    (void)internal_fd;
    terminal_write((const char*)buf, size);
    return (int)size;
}

static vfs_ops_t console_vfs_ops = {
    .read  = console_vfs_read,
    .write = console_vfs_write,
};

/* Install the three standard descriptors into a fresh fd table.
 * fd 0 = stdin (read-only console), fd 1/2 = stdout/stderr (write-only
 * console). Nodes are allocated from the shared pool and owned by the
 * table (ref_count 1), so fd_table_close/destroy free them normally. */
static void fd_table_install_std(fd_table_t* table) {
    if (!table) return;
    for (int fd = 0; fd < 3; fd++) {
        vfs_node_t* node = alloc_node();
        if (!node) break;
        node->type = VFS_TYPE_DEVICE;
        node->internal_fd = fd;
        node->ops = &console_vfs_ops;
        node->offset = 0;
        node->size = 0;
        node->ref_count = 1;
        node->flags = (fd == 0) ? VFS_O_RDONLY : VFS_O_WRONLY;
        table->fds[fd] = node;
        table->count++;
    }
}

/* ========== FD table ========== */

fd_table_t* fd_table_create(void) {
    fd_table_t* table = (fd_table_t*)kmalloc(sizeof(fd_table_t));
    if (!table) return 0;
    for (int i = 0; i < VFS_MAX_FDS; i++) {
        table->fds[i] = 0;
    }
    table->count = 0;
    fd_table_install_std(table);
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
    fd_table_t* dst = (fd_table_t*)kmalloc(sizeof(fd_table_t));
    if (!dst) return 0;
    for (int i = 0; i < VFS_MAX_FDS; i++) {
        dst->fds[i] = 0;
    }
    dst->count = 0;
    for (int i = 0; i < VFS_MAX_FDS; i++) {
        if (src->fds[i]) {
            dst->fds[i] = src->fds[i];
            src->fds[i]->ref_count++;
            dst->count++;
        }
    }
    return dst;
}
