/*
 * vfs.h - Virtual Filesystem Layer
 */

#ifndef VFS_H
#define VFS_H

#include <stdint.h>
#include <stddef.h>

#define VFS_MAX_FDS 32
#define VFS_MAX_MOUNTS 8
#define VFS_MAX_PATH 256

#define VFS_TYPE_FILE 1
#define VFS_TYPE_DIR 2
#define VFS_TYPE_DEVICE 3

#define VFS_O_RDONLY 0
#define VFS_O_WRONLY 1
#define VFS_O_RDWR 2
#define VFS_O_CREAT 4
#define VFS_O_TRUNC 8

#define VFS_SEEK_SET 0
#define VFS_SEEK_CUR 1
#define VFS_SEEK_END 2

typedef struct vfs_node vfs_node_t;
typedef struct vfs_ops vfs_ops_t;
typedef struct vfs_mount vfs_mount_t;
typedef struct fd_table fd_table_t;

struct vfs_ops {
    int (*open)(const char* path, int flags);
    int (*close)(int internal_fd);
    int (*read)(int internal_fd, void* buf, size_t size, size_t offset);
    int (*write)(int internal_fd, const void* buf, size_t size);
    int (*mkdir)(const char* path);
    int (*unlink)(const char* path);
    void (*list)(void (*callback)(const char*, int, uint32_t));
};

struct vfs_mount {
    char path[VFS_MAX_PATH];
    vfs_ops_t* ops;
    int active;
};

struct vfs_node {
    int type;
    int internal_fd;
    vfs_ops_t* ops;
    size_t offset;
    size_t size;
    int ref_count;
    int flags;
};

struct fd_table {
    vfs_node_t* fds[VFS_MAX_FDS];
    int count;
};

void vfs_init(void);

int vfs_open(const char* path, int flags);
int vfs_close(int fd);
int vfs_read(int fd, void* buf, size_t size);
int vfs_write(int fd, const void* buf, size_t size);
int vfs_seek(int fd, int whence, int offset);
int vfs_mkdir(const char* path);
int vfs_unlink(const char* path);
void vfs_list(void (*callback)(const char*, int, uint32_t));

void vfs_mount(const char* path, vfs_ops_t* ops);

/* Mount table accessors */
int vfs_mount_count(void);
int vfs_get_mount(int index, char* out_path);

fd_table_t* fd_table_create(void);
void fd_table_destroy(fd_table_t* table);
int fd_table_alloc(fd_table_t* table, vfs_node_t* node);
vfs_node_t* fd_table_get(fd_table_t* table, int fd);
int fd_table_close(fd_table_t* table, int fd);
fd_table_t* fd_table_clone(fd_table_t* src);

#endif
