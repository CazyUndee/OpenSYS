/*
 * fs.c - OpenFS Implementation
 *
 * A simplified NTFS-style filesystem with MFT (Master File Table)
 */

#include <stdint.h>
#include <stddef.h>
#include "../include/fs.h"
#include "../include/kheap.h"

/* Filesystem state */
static fs_boot_sector_t* boot_sector = 0;
static uint8_t* mft_zone = 0;
static uint8_t* cluster_bitmap = 0;
static uint64_t current_time = 0;

/* External disk operations */
extern int disk_read(uint32_t lba, uint32_t count, void* buffer);
extern int disk_write(uint32_t lba, uint32_t count, const void* buffer);

/* MFT reserved entries */
#define MFT_MFT          0  /* $MFT */
#define MFT_MFTMIRR      1  /* $MFTMirr */
#define MFT_LOGFILE      2  /* $LogFile */
#define MFT_VOLUME       3  /* $Volume */
#define MFT_ATTRDEF      4  /* $AttrDef */
#define MFT_ROOT         5  /* Root directory */
#define MFT_BITMAP       6  /* $Bitmap */
#define MFT_BOOT         7  /* $Boot */
#define MFT_FIRST_USER   16 /* First user file */

/* Helper: get current time (placeholder) */
static uint64_t get_time(void) {
    return current_time++;
}

/* Helper: read cluster */
static int read_cluster(uint64_t cluster, void* buffer) {
    uint64_t lba = cluster * FS_SECTORS_PER_CLUSTER + boot_sector->data_start;
    return disk_read((uint32_t)lba, FS_SECTORS_PER_CLUSTER, buffer);
}

/* Helper: write cluster */
static int write_cluster(uint64_t cluster, const void* buffer) {
    uint64_t lba = cluster * FS_SECTORS_PER_CLUSTER + boot_sector->data_start;
    return disk_write((uint32_t)lba, FS_SECTORS_PER_CLUSTER, buffer);
}

/* Helper: read MFT entry */
static int read_mft_entry(uint64_t entry_num, void* buffer) {
    uint64_t cluster = boot_sector->mft_start + entry_num;
    return read_cluster(cluster, buffer);
}

/* Helper: write MFT entry */
static int write_mft_entry(uint64_t entry_num, const void* buffer) {
    uint64_t cluster = boot_sector->mft_start + entry_num;
    return write_cluster(cluster, buffer);
}

/* Allocate a cluster */
static uint64_t alloc_cluster(void) {
    for (uint64_t i = 0; i < boot_sector->total_clusters; i++) {
        uint64_t byte = i / 8;
        uint8_t bit = i % 8;
        if (!(cluster_bitmap[byte] & (1 << bit))) {
            cluster_bitmap[byte] |= (1 << bit);
            return i;
        }
    }
    return (uint64_t)-1; /* Out of space */
}

/* Free a cluster */
static void free_cluster(uint64_t cluster) {
    uint64_t byte = cluster / 8;
    uint8_t bit = cluster % 8;
    cluster_bitmap[byte] &= ~(1 << bit);
}

/* Allocate MFT entry */
static uint64_t alloc_mft_entry(void) {
    for (uint64_t i = MFT_FIRST_USER; i < boot_sector->mft_size; i++) {
        mft_header_t* entry = (mft_header_t*)(mft_zone + i * MFT_ENTRY_SIZE);
        if (!(entry->flags & MFT_FLAG_IN_USE)) {
            return i;
        }
    }
    return (uint64_t)-1;
}

/* Find attribute in MFT entry */
static void* find_attr(mft_header_t* entry, uint32_t attr_type) {
    uint8_t* ptr = (uint8_t*)entry + entry->seq_attr_offset;
    uint8_t* end = (uint8_t*)entry + entry->used_size;
    
    while (ptr < end) {
        attr_header_t* attr = (attr_header_t*)ptr;
        if (attr->type == ATTR_END || attr->type == attr_type) {
            return (attr->type == attr_type) ? attr : 0;
        }
        ptr += attr->length;
    }
    return 0;
}

/* Initialize a new MFT entry */
static void init_mft_entry(mft_header_t* entry, uint64_t mft_num, uint16_t flags) {
    entry->magic = 0x454C4946;  /* "FILE" */
    entry->seq_attr_offset = sizeof(mft_header_t);
    entry->flags = flags | MFT_FLAG_IN_USE;
    entry->used_size = sizeof(mft_header_t);
    entry->alloc_size = MFT_ENTRY_SIZE;
    entry->base_record = 0;
    entry->next_attr_id = 0;
    entry->mft_number = (uint32_t)mft_num;
    
    /* Add end marker */
    attr_header_t* end_attr = (attr_header_t*)((uint8_t*)entry + entry->used_size);
    end_attr->type = ATTR_END;
    end_attr->length = 8;
    entry->used_size += 8;
}

/* Add filename attribute to entry */
static void add_filename_attr(mft_header_t* entry, const char* name, uint64_t parent) {
    attr_filename_t* fn = (attr_filename_t*)((uint8_t*)entry + entry->used_size + sizeof(attr_header_t));
    
    uint32_t name_len = 0;
    while (name[name_len]) name_len++;
    if (name_len > MAX_FILENAME_LEN) name_len = MAX_FILENAME_LEN;
    
    fn->parent_mft = parent;
    fn->create_time = get_time();
    fn->modify_time = fn->create_time;
    fn->access_time = fn->create_time;
    fn->alloc_size = 0;
    fn->real_size = 0;
    fn->flags = 0;
    fn->filename_length = (uint8_t)name_len;
    fn->filename_type = 0;
    
    for (uint32_t i = 0; i < name_len; i++) {
        fn->filename[i] = name[i];
    }
    
    attr_header_t* attr = (attr_header_t*)((uint8_t*)entry + entry->used_size);
    attr->type = ATTR_FILENAME;
    attr->length = sizeof(attr_header_t) + sizeof(attr_filename_t);
    attr->non_resident = 0;
    attr->name_length = 0;
    attr->name_offset = 0;
    attr->flags = 0;
    attr->instance = entry->next_attr_id++;
    
    entry->used_size += attr->length;
    
    /* Update end marker */
    attr_header_t* end = (attr_header_t*)((uint8_t*)entry + entry->used_size);
    end->type = ATTR_END;
    end->length = 8;
}

/* Format filesystem */
int fs_format(uint64_t disk_size) {
    /* Allocate boot sector */
    boot_sector = (fs_boot_sector_t*)kmalloc(FS_SECTOR_SIZE);
    if (!boot_sector) return -1;
    
    /* Setup boot sector */
    boot_sector->magic = FS_MAGIC;
    boot_sector->version = FS_VERSION;
    boot_sector->total_sectors = disk_size / FS_SECTOR_SIZE;
    boot_sector->sectors_per_cluster = FS_SECTORS_PER_CLUSTER;
    boot_sector->mft_start = 8;  /* MFT starts at cluster 8 */
    boot_sector->mft_size = 4096; /* 4096 MFT entries */
    boot_sector->mft_entry_size = MFT_ENTRY_SIZE;
    boot_sector->data_start = boot_sector->mft_start + boot_sector->mft_size;
    boot_sector->total_clusters = (boot_sector->total_sectors - boot_sector->data_start) / FS_SECTORS_PER_CLUSTER;
    boot_sector->signature = 0xAA55;
    
    /* Allocate MFT zone in memory */
    mft_zone = (uint8_t*)kmalloc(boot_sector->mft_size * MFT_ENTRY_SIZE);
    if (!mft_zone) return -1;
    
    /* Allocate cluster bitmap */
    cluster_bitmap = (uint8_t*)kmalloc((boot_sector->total_clusters + 7) / 8);
    if (!cluster_bitmap) return -1;
    
    /* Initialize all MFT entries as unused */
    for (uint64_t i = 0; i < boot_sector->mft_size; i++) {
        mft_header_t* entry = (mft_header_t*)(mft_zone + i * MFT_ENTRY_SIZE);
        entry->magic = 0;
        entry->flags = 0;
    }
    
    /* Create root directory */
    mft_header_t* root = (mft_header_t*)(mft_zone + MFT_ROOT * MFT_ENTRY_SIZE);
    init_mft_entry(root, MFT_ROOT, MFT_FLAG_DIRECTORY);
    add_filename_attr(root, "", MFT_ROOT);
    
    /* Write boot sector */
    disk_write(0, 1, boot_sector);
    
    /* Write MFT */
    for (uint64_t i = 0; i < boot_sector->mft_size; i++) {
        write_mft_entry(i, mft_zone + i * MFT_ENTRY_SIZE);
    }
    
    return 0;
}

/* Mount filesystem */
int fs_mount(void) {
    boot_sector = (fs_boot_sector_t*)kmalloc(FS_SECTOR_SIZE);
    if (!boot_sector) return -1;
    
    if (disk_read(0, 1, boot_sector) < 0) return -1;
    if (boot_sector->magic != FS_MAGIC) return -1;
    
    /* Load MFT */
    mft_zone = (uint8_t*)kmalloc(boot_sector->mft_size * MFT_ENTRY_SIZE);
    if (!mft_zone) return -1;
    
    for (uint64_t i = 0; i < boot_sector->mft_size; i++) {
        read_mft_entry(i, mft_zone + i * MFT_ENTRY_SIZE);
    }
    
    /* Load bitmap */
    cluster_bitmap = (uint8_t*)kmalloc((boot_sector->total_clusters + 7) / 8);
    if (!cluster_bitmap) return -1;
    
    return 0;
}

/* Find file by path */
static uint64_t find_file(const char* path) {
    if (!path || path[0] != '/') return (uint64_t)-1;
    
    uint64_t current = MFT_ROOT;
    const char* ptr = path + 1;
    
    while (*ptr) {
        /* Extract next component */
        char component[MAX_FILENAME_LEN];
        int len = 0;
        while (*ptr && *ptr != '/' && len < MAX_FILENAME_LEN - 1) {
            component[len++] = *ptr++;
        }
        component[len] = 0;
        if (*ptr == '/') ptr++;
        if (len == 0) continue;
        
        /* Search in current directory */
        mft_header_t* entry = (mft_header_t*)(mft_zone + current * MFT_ENTRY_SIZE);
        attr_filename_t* fn = find_attr(entry, ATTR_FILENAME);
        if (!fn) return (uint64_t)-1;
        
        /* Scan directory entries (simplified - linear search) */
        int found = 0;
        for (uint64_t i = MFT_FIRST_USER; i < boot_sector->mft_size; i++) {
            mft_header_t* child = (mft_header_t*)(mft_zone + i * MFT_ENTRY_SIZE);
            if (!(child->flags & MFT_FLAG_IN_USE)) continue;
            
            attr_filename_t* child_fn = find_attr(child, ATTR_FILENAME);
            if (!child_fn) continue;
            
            if (child_fn->parent_mft == current) {
                /* Compare names */
                int match = 1;
                for (int j = 0; j < len && j < child_fn->filename_length; j++) {
                    if (component[j] != child_fn->filename[j]) {
                        match = 0;
                        break;
                    }
                }
                if (match && len == child_fn->filename_length) {
                    current = i;
                    found = 1;
                    break;
                }
            }
        }
        
        if (!found) return (uint64_t)-1;
    }
    
    return current;
}

/* Open file */
fs_file_t* fs_open(const char* path, int mode) {
    uint64_t mft_num = find_file(path);
    
    if (mft_num == (uint64_t)-1 && mode == 1) {
        /* Create new file */
        mft_num = alloc_mft_entry();
        if (mft_num == (uint64_t)-1) return 0;
        
        /* Find parent directory */
        const char* last_slash = path;
        for (const char* p = path; *p; p++) {
            if (*p == '/') last_slash = p;
        }
        
        char parent_path[256];
        int parent_len = (int)(last_slash - path);
        if (parent_len == 0) {
            parent_path[0] = '/';
            parent_path[1] = 0;
        } else {
            for (int i = 0; i < parent_len; i++) {
                parent_path[i] = path[i];
            }
            parent_path[parent_len] = 0;
        }
        
        uint64_t parent_mft = find_file(parent_path);
        if (parent_mft == (uint64_t)-1) return 0;
        
        /* Initialize file entry */
        mft_header_t* entry = (mft_header_t*)(mft_zone + mft_num * MFT_ENTRY_SIZE);
        init_mft_entry(entry, mft_num, 0);
        
        const char* filename = last_slash + 1;
        add_filename_attr(entry, filename, parent_mft);
        
        write_mft_entry(mft_num, entry);
    } else if (mft_num == (uint64_t)-1) {
        return 0;
    }
    
    fs_file_t* file = (fs_file_t*)kmalloc(sizeof(fs_file_t));
    if (!file) return 0;
    
    file->mft_number = mft_num;
    file->position = 0;
    file->mode = (uint8_t)mode;
    
    mft_header_t* entry = (mft_header_t*)(mft_zone + mft_num * MFT_ENTRY_SIZE);
    attr_filename_t* fn = find_attr(entry, ATTR_FILENAME);
    if (fn) {
        file->size = fn->real_size;
    } else {
        file->size = 0;
    }
    
    return file;
}

/* Close file */
int fs_close(fs_file_t* file) {
    if (file) {
        kfree(file);
    }
    return 0;
}

/* Read from file */
size_t fs_read(fs_file_t* file, void* buffer, size_t size) {
    if (!file || !buffer || size == 0) return 0;
    
    mft_header_t* entry = (mft_header_t*)(mft_zone + file->mft_number * MFT_ENTRY_SIZE);
    attr_header_t* data_attr = find_attr(entry, ATTR_DATA);
    
    if (!data_attr) return 0;
    
    /* For now, only handle resident data (small files) */
    uint8_t* data = (uint8_t*)data_attr + sizeof(attr_header_t);
    uint32_t data_size = data_attr->length - sizeof(attr_header_t);
    
    size_t to_read = size;
    if (file->position + to_read > data_size) {
        to_read = data_size - file->position;
    }
    
    uint8_t* buf = (uint8_t*)buffer;
    for (size_t i = 0; i < to_read; i++) {
        buf[i] = data[file->position + i];
    }
    
    file->position += to_read;
    return to_read;
}

/* Write to file */
size_t fs_write(fs_file_t* file, const void* buffer, size_t size) {
    if (!file || !buffer || size == 0) return 0;
    
    mft_header_t* entry = (mft_header_t*)(mft_zone + file->mft_number * MFT_ENTRY_SIZE);
    
    /* Find or create data attribute */
    attr_header_t* data_attr = find_attr(entry, ATTR_DATA);
    if (!data_attr) {
        /* Create data attribute */
        data_attr = (attr_header_t*)((uint8_t*)entry + entry->used_size);
        data_attr->type = ATTR_DATA;
        data_attr->non_resident = 0;
        data_attr->length = sizeof(attr_header_t);
        entry->used_size += sizeof(attr_header_t);
        
        /* Add end marker */
        attr_header_t* end = (attr_header_t*)((uint8_t*)entry + entry->used_size);
        end->type = ATTR_END;
        end->length = 8;
        entry->used_size += 8;
    }
    
    /* Write data (resident only for now) */
    uint8_t* data = (uint8_t*)data_attr + sizeof(attr_header_t);
    const uint8_t* buf = (const uint8_t*)buffer;
    
    for (size_t i = 0; i < size; i++) {
        data[file->position + i] = buf[i];
    }
    
    file->position += size;
    data_attr->length = sizeof(attr_header_t) + file->position;
    
    /* Update file size */
    attr_filename_t* fn = find_attr(entry, ATTR_FILENAME);
    if (fn) {
        fn->real_size = file->position;
        fn->modify_time = get_time();
    }
    
    write_mft_entry(file->mft_number, entry);
    
    return size;
}

/* Get free space */
uint64_t fs_get_free_space(void) {
    uint64_t free_clusters = 0;
    for (uint64_t i = 0; i < boot_sector->total_clusters; i++) {
        uint64_t byte = i / 8;
        uint8_t bit = i % 8;
        if (!(cluster_bitmap[byte] & (1 << bit))) {
            free_clusters++;
        }
    }
    return free_clusters * FS_CLUSTER_SIZE;
}

/* Get total space */
uint64_t fs_get_total_space(void) {
    return boot_sector->total_clusters * FS_CLUSTER_SIZE;
}
