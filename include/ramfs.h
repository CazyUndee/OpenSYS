/*
 * ramfs.h - RAM-based Filesystem
 */

#ifndef RAMFS_H
#define RAMFS_H

#include <stdint.h>

void ramfs_init(void);

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
