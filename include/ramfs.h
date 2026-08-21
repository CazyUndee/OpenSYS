/*
 * ramfs.h - RAM-based Filesystem
 */

#ifndef RAMFS_H
#define RAMFS_H

#include <stdint.h>

/* Logical capacity of the ramfs filesystem (1 MB of file data). */
#define RAMFS_TOTAL_CAPACITY (1024u * 1024u)

typedef struct {
    uint64_t total_capacity;   /* logical capacity in bytes (RAMFS_TOTAL_CAPACITY) */
    uint64_t used_bytes;       /* sum of file data sizes */
    uint64_t free_bytes;       /* total_capacity - used_bytes */
    uint64_t allocated_bytes;  /* actual heap bytes held by file buffers */
    uint32_t file_count;       /* number of in-use file/dir entries */
    uint32_t dir_count;        /* number of directories */
} ramfs_stats_t;

void ramfs_init(void);

void ramfs_get_stats(ramfs_stats_t* stats);

int ramfs_create(const char* name);
int ramfs_mkdir(const char* name);
int ramfs_find(const char* name);
int ramfs_delete(const char* name);

int ramfs_write(int fd, const void* data, uint32_t size);
int ramfs_read(int fd, void* data, uint32_t size, uint32_t offset);
uint32_t ramfs_size(int fd);
int ramfs_is_dir(int fd);
const char* ramfs_name(int fd);

void ramfs_list(void (*callback)(const char* name, int is_dir, uint32_t size));
int ramfs_get_file_count(void);

#endif
