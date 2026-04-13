/*
 * ramfs.c - Simple RAM-based Filesystem
 *
 * In-memory filesystem for testing. No disk required.
 */

#include <stdint.h>
#include <stddef.h>
#include "../include/kheap.h"

#define MAX_FILES 64
#define MAX_FILENAME 32
#define MAX_FILESIZE 4096

typedef struct {
    char name[MAX_FILENAME];
    uint8_t* data;
    uint32_t size;
    uint32_t capacity;
    uint8_t is_directory;
    uint8_t in_use;
} ramfile_t;

static ramfile_t files[MAX_FILES];
static int fs_initialized = 0;

void ramfs_init(void) {
    for (int i = 0; i < MAX_FILES; i++) {
        files[i].in_use = 0;
        files[i].data = 0;
        files[i].size = 0;
        files[i].capacity = 0;
        files[i].is_directory = 0;
    }
    
    /* Create root directory */
    files[0].in_use = 1;
    files[0].is_directory = 1;
    files[0].name[0] = '/';
    files[0].name[1] = 0;
    files[0].size = 0;
    
    fs_initialized = 1;
}

int ramfs_create(const char* name) {
    if (!fs_initialized) return -1;
    
    /* Find free slot */
    int slot = -1;
    for (int i = 1; i < MAX_FILES; i++) {
        if (!files[i].in_use) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return -1;
    
    /* Copy name */
    int len = 0;
    while (name[len] && len < MAX_FILENAME - 1) {
        files[slot].name[len] = name[len];
        len++;
    }
    files[slot].name[len] = 0;
    
    /* Allocate initial buffer */
    files[slot].data = (uint8_t*)kmalloc(256);
    if (!files[slot].data) return -1;
    
    files[slot].capacity = 256;
    files[slot].size = 0;
    files[slot].is_directory = 0;
    files[slot].in_use = 1;
    
    return slot;
}

int ramfs_mkdir(const char* name) {
    if (!fs_initialized) return -1;
    
    int slot = -1;
    for (int i = 1; i < MAX_FILES; i++) {
        if (!files[i].in_use) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return -1;
    
    int len = 0;
    while (name[len] && len < MAX_FILENAME - 1) {
        files[slot].name[len] = name[len];
        len++;
    }
    files[slot].name[len] = 0;
    
    files[slot].is_directory = 1;
    files[slot].in_use = 1;
    files[slot].size = 0;
    files[slot].data = 0;
    
    return 0;
}

int ramfs_find(const char* name) {
    if (!fs_initialized) return -1;
    
    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].in_use) {
            int match = 1;
            int j = 0;
            while (name[j] && files[i].name[j]) {
                if (name[j] != files[i].name[j]) {
                    match = 0;
                    break;
                }
                j++;
            }
            if (match && name[j] == files[i].name[j]) {
                return i;
            }
        }
    }
    return -1;
}

int ramfs_delete(const char* name) {
    int idx = ramfs_find(name);
    if (idx < 0) return -1;
    
    if (files[idx].data) {
        kfree(files[idx].data);
    }
    files[idx].in_use = 0;
    return 0;
}

int ramfs_write(int fd, const void* data, uint32_t size) {
    if (fd < 0 || fd >= MAX_FILES || !files[fd].in_use) return -1;
    
    /* Grow buffer if needed */
    while (files[fd].size + size > files[fd].capacity) {
        uint32_t new_cap = files[fd].capacity * 2;
        uint8_t* new_data = (uint8_t*)kmalloc(new_cap);
        if (!new_data) return -1;
        
        /* Copy old data */
        for (uint32_t i = 0; i < files[fd].size; i++) {
            new_data[i] = files[fd].data[i];
        }
        
        if (files[fd].data) kfree(files[fd].data);
        files[fd].data = new_data;
        files[fd].capacity = new_cap;
    }
    
    /* Write data */
    const uint8_t* src = (const uint8_t*)data;
    for (uint32_t i = 0; i < size; i++) {
        files[fd].data[files[fd].size + i] = src[i];
    }
    files[fd].size += size;
    
    return size;
}

int ramfs_read(int fd, void* data, uint32_t size, uint32_t offset) {
    if (fd < 0 || fd >= MAX_FILES || !files[fd].in_use) return -1;
    
    if (offset >= files[fd].size) return 0;
    
    uint32_t to_read = size;
    if (offset + to_read > files[fd].size) {
        to_read = files[fd].size - offset;
    }
    
    uint8_t* dst = (uint8_t*)data;
    for (uint32_t i = 0; i < to_read; i++) {
        dst[i] = files[fd].data[offset + i];
    }
    
    return to_read;
}

uint32_t ramfs_size(int fd) {
    if (fd < 0 || fd >= MAX_FILES || !files[fd].in_use) return 0;
    return files[fd].size;
}

int ramfs_is_dir(int fd) {
    if (fd < 0 || fd >= MAX_FILES) return 0;
    return files[fd].is_directory;
}

const char* ramfs_name(int fd) {
    if (fd < 0 || fd >= MAX_FILES) return 0;
    return files[fd].name;
}

void ramfs_list(void (*callback)(const char* name, int is_dir, uint32_t size)) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].in_use) {
            callback(files[i].name, files[i].is_directory, files[i].size);
        }
    }
}

int ramfs_get_file_count(void) {
    int count = 0;
    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].in_use) count++;
    }
    return count;
}
