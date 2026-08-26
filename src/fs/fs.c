/*
 * fs.c - Filesystem Implementation
 *
 * A simplified NTFS-style filesystem with MFT (Master File Table)
 */

#include <stdint.h>
#include <stddef.h>
#include "fs.h"
#include "kheap.h"
#include "disk.h"
#include "volume.h"

/* Filesystem state */
static fs_boot_sector_t* boot_sector = 0;
static uint8_t* mft_zone = 0;
static uint8_t* cluster_bitmap = 0;

/* Defined below (used by fs_unlink and fs_rename). */
static void remove_index_entry(mft_header_t* dir, uint64_t child_mft);

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

/* Helper: get current time from RTC (real hardware clock) */
#include "rtc.h"
#include "timer.h"

#define SECONDS_FROM_1970_TO_2000 946684800ULL

static int rtc_time_ready = 0;

static uint64_t rtc_to_unix(const rtc_time_t* t) {
    /* Days per month (non-leap) */
    static const uint8_t mdays[12] = {31,28,31,30,31,30,31,31,30,31,30,31};

    int full_year = (int)t->year + (int)t->century * 100;
    uint64_t days = 0;

    /* Years from 1970 to start of this year */
    for (int yr = 1970; yr < full_year; yr++) {
        days += 365;
        if ((yr % 4 == 0 && yr % 100 != 0) || (yr % 400 == 0)) days++;
    }

    /* Months this year */
    for (int m = 0; m < (int)t->month - 1 && m < 12; m++) {
        days += mdays[m];
    }
    /* Leap day */
    if (t->month > 2 && ((full_year % 4 == 0 && full_year % 100 != 0) || (full_year % 400 == 0))) {
        days++;
    }

    days += (uint64_t)t->day - 1;

    return SECONDS_FROM_1970_TO_2000 + days * 86400ULL
         + (uint64_t)t->hour * 3600ULL
         + (uint64_t)t->minute * 60ULL
         + (uint64_t)t->second;
}

static uint64_t get_time(void) {
    if (!rtc_time_ready) {
        rtc_init();
        rtc_time_ready = 1;
    }
    rtc_time_t t;
    rtc_read_time(&t);
    return rtc_to_unix(&t);
}

/* Helper: read cluster */
static int read_cluster(uint64_t cluster, void* buffer) {
    uint64_t lba = volume_base_lba() + cluster * FS_SECTORS_PER_CLUSTER + boot_sector->data_start;
    return disk_read((uint32_t)lba, FS_SECTORS_PER_CLUSTER, buffer);
}

/* Helper: write cluster */
static int write_cluster(uint64_t cluster, const void* buffer) {
    uint64_t lba = volume_base_lba() + cluster * FS_SECTORS_PER_CLUSTER + boot_sector->data_start;
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
    /* Security: every offset/length here comes from DISK bytes. Clamp the
     * walk window to the MFT slot so a hostile image can never make this
     * loop read outside the entry (CWE-125 root enabler). */
    uint8_t* base = (uint8_t*)entry;
    uint32_t off = entry->seq_attr_offset;
    uint32_t used = entry->used_size;
    if (off < sizeof(mft_header_t) || off >= MFT_ENTRY_SIZE) return 0;
    if (used < sizeof(mft_header_t) || used > MFT_ENTRY_SIZE) return 0;

    uint8_t* ptr = base + off;
    uint8_t* end = base + used;

    while (ptr + sizeof(attr_header_t) <= end) {
        attr_header_t* attr = (attr_header_t*)ptr;
        if (attr->type == ATTR_END || attr->type == attr_type) {
            return (attr->type == attr_type) ? attr : 0;
        }
        /* Guard against corrupt/garbage lengths: minimum header size and
         * the whole attribute must stay inside the walked window. */
        if (attr->length < sizeof(attr_header_t)) break;
        if (ptr + attr->length > end) break;
        ptr += attr->length;
    }
    return 0;
}

/* Return a pointer to an attribute's payload struct (past the header).
 * find_attr() returns the attr_header_t; the filename struct lives right
 * after it. Callers that need the struct MUST use this helper. */
static void* find_attr_payload(mft_header_t* entry, uint32_t attr_type) {
    attr_header_t* attr = (attr_header_t*)find_attr(entry, attr_type);
    if (!attr) return 0;
    return (void*)((uint8_t*)attr + sizeof(attr_header_t));
}

/* Security helper: how many data_run_t entries actually fit inside a
 * non-resident attribute's payload. Every nr-> field is DISK bytes;
 * this is the only safe way to size a walk over runs[]. */
static uint64_t nonresident_run_count(const attr_header_t* attr,
                                      const attr_nonresident_t* nr) {
    if (nr->run_offset < sizeof(attr_header_t) + sizeof(attr_nonresident_t)) return 0;
    if (nr->run_offset >= attr->length) return 0;
    return (uint64_t)(attr->length - nr->run_offset) / sizeof(data_run_t);
}

/* Add index root attribute to directory */
static void add_index_root_attr(mft_header_t* entry) {
    /* Attributes are appended over the trailing ATTR_END marker left at
     * used_size - 8 by init_mft_entry / the previous attribute. */
    uint8_t* base = (uint8_t*)entry + entry->used_size - 8;

    index_root_t* ir = (index_root_t*)(base + sizeof(attr_header_t));
    
    ir->attr_type = ATTR_FILENAME;
    ir->collation_rule = 0;
    ir->index_alloc_size = FS_CLUSTER_SIZE;
    ir->clusters_per_index = 1;
    
    attr_header_t* attr = (attr_header_t*)base;
    attr->type = ATTR_INDEX_ROOT;
    attr->length = sizeof(attr_header_t) + sizeof(index_root_t);
    attr->non_resident = 0;
    attr->name_length = 0;
    attr->name_offset = 0;
    attr->flags = 0;
    attr->instance = entry->next_attr_id++;
    
    /* Move the end marker to just after the new attribute */
    attr_header_t* end = (attr_header_t*)(base + attr->length);
    end->type = ATTR_END;
    end->length = 8;
    entry->used_size = (uint32_t)((uint8_t*)end + 8 - (uint8_t*)entry);
}

/* Add directory entry to parent's index */
static void add_dir_entry(mft_header_t* parent_dir, uint64_t child_mft, const char* name) {
    /* Find index root in parent */
    attr_header_t* index_attr = find_attr(parent_dir, ATTR_INDEX_ROOT);
    if (!index_attr) return;
    
    index_root_t* ir = (index_root_t*)((uint8_t*)index_attr + sizeof(attr_header_t));
    
    /* Walk existing index entries to find the insertion point. The walk
     * is bounded by index_attr->length CLAMPED to the MFT slot (both are
     * disk-controlled), so a hostile image can never push the insert
     * pointer outside the entry. `insert` tracks where the new entry
     * goes: for an empty index it is the first entry position. */
    index_entry_t* last = (index_entry_t*)((uint8_t*)ir + sizeof(index_root_t));
    uint8_t* slot_end = (uint8_t*)parent_dir + MFT_ENTRY_SIZE;
    uint8_t* walk_end = (uint8_t*)index_attr + index_attr->length;
    if (walk_end > slot_end) walk_end = slot_end;
    if ((uint8_t*)last > walk_end) return;   /* corrupt index offset */
    uint8_t* insert = (uint8_t*)last;

    while ((uint8_t*)last + sizeof(index_entry_t) <= walk_end &&
           last->length >= sizeof(index_entry_t)) {
        if (last->flags & 0x01) {
            /* Already indexed? */
            if (last->mft_ref == child_mft) return;
            /* Demote the previous last entry, then append after it */
            last->flags &= (uint16_t)~0x01;
            insert = (uint8_t*)last + last->length;
            break;
        }
        insert = (uint8_t*)last + last->length;
        last = (index_entry_t*)insert;
    }

    /* The new entry (+ trailing end marker) must fit inside the MFT slot. */
    if (insert + sizeof(index_entry_t) + sizeof(attr_header_t) > slot_end) {
        return;   /* index full or corrupt - refuse rather than overflow */
    }

    /* New entry goes at the current end of the index (this is where the
     * trailing end-of-index marker sat; it gets overwritten and re-added) */
    index_entry_t* idx_entry = (index_entry_t*)insert;
    
    idx_entry->mft_ref = child_mft;
    idx_entry->length = sizeof(index_entry_t);
    idx_entry->attr_type = ATTR_FILENAME;
    idx_entry->flags = 0x01; /* Last entry */
    idx_entry->reserved = 0;
    
    /* Copy filename */
    attr_filename_t* fn = &idx_entry->filename;
    fn->parent_mft = parent_dir->mft_number;
    fn->create_time = get_time();
    fn->modify_time = fn->create_time;
    fn->access_time = fn->create_time;
    fn->alloc_size = 0;
    fn->real_size = 0;
    fn->flags = 0;
    fn->reparse = 0;
    
    int name_len = 0;
    while (name[name_len] && name_len < MAX_FILENAME_LEN) name_len++;
    fn->filename_length = name_len;
    fn->filename_type = 0;
    
    for (int i = 0; i < name_len; i++) {
        fn->filename[i] = name[i];
    }
    
    /* Extend the index attribute to cover the new entry, staying inside
     * the MFT slot (the guard above already verified the fit). */
    index_attr->length += sizeof(index_entry_t);
    
    /* Add a fresh end-of-attributes marker right after the new entry */
    attr_header_t* end = (attr_header_t*)((uint8_t*)idx_entry + sizeof(index_entry_t));
    end->type = ATTR_END;
    end->length = 8;
    parent_dir->used_size = (uint32_t)((uint8_t*)end + 8 - (uint8_t*)parent_dir);
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
    /* Attributes are appended over the trailing ATTR_END marker left at
     * used_size - 8 by init_mft_entry. */
    uint8_t* base = (uint8_t*)entry + entry->used_size - 8;

    attr_filename_t* fn = (attr_filename_t*)(base + sizeof(attr_header_t));
    
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
    
    attr_header_t* attr = (attr_header_t*)base;
    attr->type = ATTR_FILENAME;
    attr->length = sizeof(attr_header_t) + sizeof(attr_filename_t);
    attr->non_resident = 0;
    attr->name_length = 0;
    attr->name_offset = 0;
    attr->flags = 0;
    attr->instance = entry->next_attr_id++;
    
    /* Move the end marker to just after the new attribute */
    attr_header_t* end = (attr_header_t*)(base + attr->length);
    end->type = ATTR_END;
    end->length = 8;
    entry->used_size = (uint32_t)((uint8_t*)end + 8 - (uint8_t*)entry);
}

/* Format filesystem */
int fs_format(uint64_t disk_size) {
    /* A bound volume caps the filesystem size — the fs must never span
     * past the partition into neighboring volumes. */
    if (volume_sectors() > 0) {
        uint64_t volume_bytes = volume_sectors() * FS_SECTOR_SIZE;
        if (disk_size > volume_bytes) disk_size = volume_bytes;
    }

    /* Allocate boot sector */
    boot_sector = (fs_boot_sector_t*)kmalloc(FS_SECTOR_SIZE);
    if (!boot_sector) return -1;
    
    /* Setup boot sector */
    boot_sector->magic = FS_MAGIC;
    boot_sector->version = FS_VERSION;
    boot_sector->total_sectors = disk_size / FS_SECTOR_SIZE;
    boot_sector->sectors_per_cluster = FS_SECTORS_PER_CLUSTER;
    boot_sector->mft_start = 8;  /* MFT starts at cluster 8 */
    boot_sector->mft_size = 128; /* 128 MFT entries (512KB) - enough for the UI state graph; keeps format/mount fast */
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
    add_index_root_attr(root);
    
    /* Write to disk only if disk is present */
    if (disk_is_ready()) {
        /* Write boot sector at the volume base (partition start when a
         * partition volume is bound; LBA 0 on a whole disk) */
        disk_write((uint32_t)volume_base_lba(), 1, boot_sector);

        /* Write MFT */
        for (uint64_t i = 0; i < boot_sector->mft_size; i++) {
            write_mft_entry(i, mft_zone + i * MFT_ENTRY_SIZE);
        }
    }
    
    return 0;
}

/* Mount filesystem */
int fs_mount(void) {
    boot_sector = (fs_boot_sector_t*)kmalloc(FS_SECTOR_SIZE);
    if (!boot_sector) return -1;

    if (disk_read((uint32_t)volume_base_lba(), 1, boot_sector) < 0) return -1;
    if (boot_sector->magic != FS_MAGIC) return -1;

    /* Security: every field below is DISK bytes. A hostile boot sector
     * used to wrap mft_size*MFT_ENTRY_SIZE in 32-bit math (kmalloc(0) ->
     * immediate heap smash) or allocate absurd bitmaps. Enforce sane
     * geometry before trusting anything. */
    if (boot_sector->mft_size == 0 || boot_sector->mft_size > 4096) return -1;
    if ((uint64_t)boot_sector->mft_size * MFT_ENTRY_SIZE > 64ULL * 1024 * 1024) return -1;
    if (boot_sector->total_clusters == 0 || boot_sector->total_clusters > 4ULL * 1024 * 1024) return -1;
    if (boot_sector->data_start >= boot_sector->total_sectors &&
        boot_sector->total_sectors != 0) return -1;

    /* Load MFT (64-bit sizing; zeroed so garbage never parses as entries) */
    uint64_t mft_bytes = (uint64_t)boot_sector->mft_size * MFT_ENTRY_SIZE;
    mft_zone = (uint8_t*)kmalloc((size_t)mft_bytes);
    if (!mft_zone) return -1;
    for (uint64_t z = 0; z < mft_bytes; z++) mft_zone[z] = 0;

    for (uint64_t i = 0; i < boot_sector->mft_size; i++) {
        read_mft_entry(i, mft_zone + i * MFT_ENTRY_SIZE);
    }

    /* Load bitmap */
    cluster_bitmap = (uint8_t*)kmalloc((size_t)((boot_sector->total_clusters + 7) / 8));
    if (!cluster_bitmap) return -1;
    for (uint64_t z = 0; z < (boot_sector->total_clusters + 7) / 8; z++) cluster_bitmap[z] = 0;

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
        attr_filename_t* fn = (attr_filename_t*)find_attr_payload(entry, ATTR_FILENAME);
        if (!fn) return (uint64_t)-1;
        
        /* Scan directory entries (simplified - linear search) */
        int found = 0;
        for (uint64_t i = MFT_FIRST_USER; i < boot_sector->mft_size; i++) {
            mft_header_t* child = (mft_header_t*)(mft_zone + i * MFT_ENTRY_SIZE);
            if (!(child->flags & MFT_FLAG_IN_USE)) continue;
            
            attr_filename_t* child_fn = (attr_filename_t*)find_attr_payload(child, ATTR_FILENAME);
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
    
    if (mft_num == (uint64_t)-1 && mode != 0) {
        /* Create new file (mode 1 = write, mode 2 = append) */
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
        
        /* Add entry to parent directory's index */
        mft_header_t* parent = (mft_header_t*)(mft_zone + parent_mft * MFT_ENTRY_SIZE);
        add_dir_entry(parent, mft_num, filename);
        
        /* Write both entries */
        write_mft_entry(mft_num, entry);
        write_mft_entry(parent_mft, parent);
    } else if (mft_num == (uint64_t)-1) {
        return 0;
    }

    /* Permission check: an existing read-only file cannot be opened for
     * writing, appending, or truncation (mode 1 = write, mode 2 = append). */
    if (mode != 0) {
        mft_header_t* existing = (mft_header_t*)(mft_zone + mft_num * MFT_ENTRY_SIZE);
        if (existing->flags & MFT_FLAG_READONLY) {
            return 0;
        }
    }
    
    fs_file_t* file = (fs_file_t*)kmalloc(sizeof(fs_file_t));
    if (!file) return 0;
    
    file->mft_number = mft_num;
    file->position = 0;
    file->mode = (uint8_t)mode;
    
    mft_header_t* entry = (mft_header_t*)(mft_zone + mft_num * MFT_ENTRY_SIZE);
    attr_filename_t* fn = (attr_filename_t*)find_attr_payload(entry, ATTR_FILENAME);
    if (fn) {
        file->size = fn->real_size;
    } else {
        file->size = 0;
    }
    
    /* Reset position for new files */
    file->position = 0;
    
    return file;
}

/* Close file */
int fs_close(fs_file_t* file) {
    if (file) {
        kfree(file);
    }
    return 0;
}

/* Truncate a file to `size` bytes. Shrinking frees the data clusters
 * beyond the new size (non-resident) or shrinks the resident data
 * attribute; the filename real_size is updated to match. Growing beyond
 * the current size is a no-op (writes extend naturally). Returns 0 on
 * success, -1 on bad arguments. */
int fs_truncate(fs_file_t* file, uint64_t size) {
    if (!file) return -1;
    if (size >= file->size) return 0;  /* nothing to shrink */

    mft_header_t* entry = (mft_header_t*)(mft_zone + file->mft_number * MFT_ENTRY_SIZE);
    attr_header_t* data_attr = find_attr(entry, ATTR_DATA);

    if (data_attr && data_attr->non_resident) {
        attr_nonresident_t* nr = (attr_nonresident_t*)((uint8_t*)data_attr + sizeof(attr_header_t));
        data_run_t* runs = (data_run_t*)((uint8_t*)data_attr + nr->run_offset);
        /* Security: run count from DISC-controlled vcn fields, bounded by
         * what the attribute payload can actually hold; absurd single-run
         * lengths are skipped rather than iterated (DoS/OOB guard). */
        uint64_t num_runs = nonresident_run_count(data_attr, nr);
        if (nr->last_vcn >= nr->start_vcn &&
            num_runs > nr->last_vcn - nr->start_vcn + 1) {
            num_runs = nr->last_vcn - nr->start_vcn + 1;
        }
        uint64_t keep_clusters = (size + FS_CLUSTER_SIZE - 1) / FS_CLUSTER_SIZE;
        uint64_t seen = 0;

        for (uint64_t i = 0; i < num_runs; i++) {
            if (runs[i].length > boot_sector->total_clusters) break;
            for (uint64_t j = 0; j < runs[i].length; j++) {
                if (seen >= keep_clusters) {
                    uint64_t cluster = runs[i].start_cluster + j;
                    uint64_t byte = cluster / 8;
                    uint64_t bit = cluster % 8;
                    if (byte < (boot_sector->total_clusters + 7) / 8) {
                        cluster_bitmap[byte] &= ~(1 << bit);
                    }
                }
                seen++;
            }
        }

        nr->real_size = size;
        nr->initialized_size = size;
    } else if (data_attr) {
        /* Resident: shrink the attribute payload length. */
        data_attr->length = sizeof(attr_header_t) + (uint32_t)size;
    }

    /* Update the filename real_size so reopen reports the new size. */
    attr_filename_t* fn = (attr_filename_t*)find_attr_payload(entry, ATTR_FILENAME);
    if (fn) {
        fn->real_size = size;
        fn->modify_time = get_time();
    }

    file->size = size;
    if (file->position > size) file->position = size;
    write_mft_entry(file->mft_number, entry);
    return 0;
}

/* Reposition the read/write offset within a file. whence is the usual
 * 0=SET, 1=CUR, 2=END; the resulting position is clamped to [0, size].
 * Returns 0 on success, -1 on bad arguments. */
int fs_seek(fs_file_t* file, int64_t offset, int whence) {
    if (!file) return -1;

    int64_t base;
    switch (whence) {
        case 0:  /* SEEK_SET */
            base = 0;
            break;
        case 1:  /* SEEK_CUR */
            base = (int64_t)file->position;
            break;
        case 2:  /* SEEK_END */
            base = (int64_t)file->size;
            break;
        default:
            return -1;
    }

    int64_t pos = base + offset;
    if (pos < 0) pos = 0;
    if ((uint64_t)pos > file->size) pos = (int64_t)file->size;
    file->position = (uint64_t)pos;
    return 0;
}

/* Read from file */
size_t fs_read(fs_file_t* file, void* buffer, size_t size) {
    if (!file || !buffer || size == 0) return 0;
    
    mft_header_t* entry = (mft_header_t*)(mft_zone + file->mft_number * MFT_ENTRY_SIZE);
    attr_header_t* data_attr = find_attr(entry, ATTR_DATA);
    
    if (!data_attr) return 0;
    
    if (data_attr->non_resident) {
        /* Non-resident: read from data clusters.
         * Security: nr-> fields are DISK bytes. Bound the run walk by the
         * attribute's own length so garbage allocated_size/real_size can
         * never walk runs[] past the MFT slot or drive arbitrary LBAs. */
        attr_nonresident_t* nr = (attr_nonresident_t*)((uint8_t*)data_attr + sizeof(attr_header_t));
        data_run_t* runs = (data_run_t*)((uint8_t*)data_attr + nr->run_offset);

        uint64_t clusters_available = nr->allocated_size / FS_CLUSTER_SIZE;
        uint64_t max_runs = nonresident_run_count(data_attr, nr);
        if (clusters_available > max_runs) clusters_available = max_runs;

        uint64_t start_cluster = file->position / FS_CLUSTER_SIZE;
        uint64_t offset_in_cluster = file->position % FS_CLUSTER_SIZE;

        size_t bytes_read = 0;
        uint8_t* buf = (uint8_t*)buffer;

        for (uint64_t i = start_cluster; i < clusters_available && bytes_read < size; i++) {
            uint64_t cluster = runs[i].start_cluster;
            uint64_t to_read = FS_CLUSTER_SIZE - offset_in_cluster;

            if (i == clusters_available - 1 && nr->real_size > file->position) {
                /* Last cluster - don't read beyond file size */
                uint64_t file_remaining = nr->real_size - file->position;
                if (to_read > file_remaining) {
                    to_read = file_remaining;
                }
            }
            
            if (to_read > size - bytes_read) {
                to_read = size - bytes_read;
            }
            
            /* Read cluster */
            uint8_t cluster_buf[FS_CLUSTER_SIZE];
            read_cluster(cluster, cluster_buf);
            
            /* Copy relevant portion */
            for (uint64_t j = 0; j < to_read; j++) {
                buf[bytes_read + j] = cluster_buf[offset_in_cluster + j];
            }
            
            bytes_read += to_read;
            offset_in_cluster = 0; /* Only for first cluster */
        }
        
        file->position += bytes_read;
        return bytes_read;
        
    } else {
        /* Resident: read data directly from MFT entry.
         * Security: length is DISK bytes - an underflow here once turned
         * into a huge data_size and leaked adjacent heap via `cat`. */
        if (data_attr->length < sizeof(attr_header_t)) return 0;
        uint32_t data_size = data_attr->length - sizeof(attr_header_t);
        /* The payload cannot extend past the MFT slot. */
        uint32_t attr_off = (uint32_t)((uint8_t*)data_attr - (uint8_t*)entry);
        if (attr_off + sizeof(attr_header_t) + data_size > MFT_ENTRY_SIZE) {
            data_size = MFT_ENTRY_SIZE - attr_off - sizeof(attr_header_t);
        }
        if (file->position >= data_size) return 0;

        uint8_t* data = (uint8_t*)data_attr + sizeof(attr_header_t);

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
}

/* Write to file */
size_t fs_write(fs_file_t* file, const void* buffer, size_t size) {
    if (!file || !buffer || size == 0) return 0;
    
    /* Append mode: write at the end of the existing data */
    if (file->mode == 2) {
        file->position = file->size;
    }
    
    mft_header_t* entry = (mft_header_t*)(mft_zone + file->mft_number * MFT_ENTRY_SIZE);
    
    /* Find or create data attribute */
    attr_header_t* data_attr = find_attr(entry, ATTR_DATA);
    if (!data_attr) {
        /* Determine if file should be non-resident */
        size_t available_space = MFT_ENTRY_SIZE - entry->used_size - sizeof(attr_header_t) - 8;
        if (size > available_space) {
            /* Create non-resident data attribute (over the trailing ATTR_END) */
            data_attr = (attr_header_t*)((uint8_t*)entry + entry->used_size - 8);
            data_attr->type = ATTR_DATA;
            data_attr->non_resident = 1;
            data_attr->length = sizeof(attr_header_t) + sizeof(attr_nonresident_t);
            
            /* Initialize non-resident header */
            attr_nonresident_t* nr = (attr_nonresident_t*)((uint8_t*)data_attr + sizeof(attr_header_t));
            nr->start_vcn = 0;
            nr->last_vcn = 0;
            nr->run_offset = sizeof(attr_header_t) + sizeof(attr_nonresident_t);
            nr->comp_unit_size = 0;
            nr->reserved = 0;
            nr->allocated_size = 0;
            nr->real_size = 0;
            nr->initialized_size = 0;
            
            /* End marker initially right after the non-resident header; the
             * cluster-allocation loop below extends the attribute over the
             * data runs and re-places the marker. */
            attr_header_t* end = (attr_header_t*)((uint8_t*)data_attr + data_attr->length);
            end->type = ATTR_END;
            end->length = 8;
            entry->used_size = (uint32_t)((uint8_t*)end + 8 - (uint8_t*)entry);
        } else {
            /* Create resident data attribute (over the trailing ATTR_END) */
            data_attr = (attr_header_t*)((uint8_t*)entry + entry->used_size - 8);
            data_attr->type = ATTR_DATA;
            data_attr->non_resident = 0;
            data_attr->length = sizeof(attr_header_t);
            
            /* End marker moves to just after the attribute */
            attr_header_t* end = (attr_header_t*)((uint8_t*)data_attr + sizeof(attr_header_t));
            end->type = ATTR_END;
            end->length = 8;
            entry->used_size = (uint32_t)((uint8_t*)end + 8 - (uint8_t*)entry);
        }
    }
    
    if (data_attr->non_resident) {
        /* Non-resident: allocate clusters and write data */
        attr_nonresident_t* nr = (attr_nonresident_t*)((uint8_t*)data_attr + sizeof(attr_header_t));
        uint64_t clusters_needed = (file->position + size + FS_CLUSTER_SIZE - 1) / FS_CLUSTER_SIZE;
        
        /* Allocate data runs */
        data_run_t* runs = (data_run_t*)((uint8_t*)data_attr + nr->run_offset);
        uint64_t clusters_allocated = 0;

        /* Security: existing-run count and the run array capacity come
         * from the attribute's DISK-controlled length; a hostile value
         * used to let runs[huge] be written past the MFT slot. */
        uint64_t max_runs = nonresident_run_count(data_attr, nr);
        if (nr->allocated_size > 0) {
            clusters_allocated = nr->allocated_size / FS_CLUSTER_SIZE;
            if (clusters_allocated > max_runs) {
                clusters_allocated = max_runs;
                nr->allocated_size = clusters_allocated * FS_CLUSTER_SIZE;
            }
        }

        /* Allocate additional clusters if needed (never beyond capacity) */
        while (clusters_allocated < clusters_needed && clusters_allocated < max_runs) {
            uint64_t new_cluster = alloc_cluster();
            if (new_cluster == (uint64_t)-1) {
                /* Out of space */
                break;
            }
            
            /* Add to data runs */
            runs[clusters_allocated].start_cluster = new_cluster;
            runs[clusters_allocated].length = 1;
            clusters_allocated++;
        }
        
        /* Write data to clusters */
        const uint8_t* buf = (const uint8_t*)buffer;
        size_t bytes_written = 0;
        
        for (uint64_t i = 0; i < clusters_needed && bytes_written < size; i++) {
            uint64_t cluster = runs[i].start_cluster;
            uint64_t offset_in_cluster = 0;
            
            if (i == file->position / FS_CLUSTER_SIZE) {
                offset_in_cluster = file->position % FS_CLUSTER_SIZE;
            }
            
            uint64_t to_write = FS_CLUSTER_SIZE - offset_in_cluster;
            if (to_write > size - bytes_written) {
                to_write = size - bytes_written;
            }
            
            /* Read cluster, modify, write back */
            uint8_t cluster_buf[FS_CLUSTER_SIZE];
            if (offset_in_cluster > 0 || to_write < FS_CLUSTER_SIZE) {
                read_cluster(cluster, cluster_buf);
            }
            
            for (uint64_t j = 0; j < to_write; j++) {
                cluster_buf[offset_in_cluster + j] = buf[bytes_written + j];
            }
            
            write_cluster(cluster, cluster_buf);
            bytes_written += to_write;
        }
        
        /* Size the attribute to include the data runs and re-place the
         * end-of-attributes marker right after them */
        data_attr->length = nr->run_offset + (uint32_t)(clusters_allocated * sizeof(data_run_t));
        attr_header_t* end = (attr_header_t*)((uint8_t*)data_attr + data_attr->length);
        end->type = ATTR_END;
        end->length = 8;
        entry->used_size = (uint32_t)((uint8_t*)end + 8 - (uint8_t*)entry);

        /* Update non-resident header */
        nr->allocated_size = clusters_allocated * FS_CLUSTER_SIZE;
        nr->real_size = file->position + bytes_written;
        nr->initialized_size = nr->real_size;
        nr->last_vcn = clusters_allocated - 1;
        
        file->position += bytes_written;
        file->size = file->position;  /* keep in-memory size in sync */
        
        /* Update file size */
        attr_filename_t* fn = (attr_filename_t*)find_attr_payload(entry, ATTR_FILENAME);
        if (fn) {
            fn->real_size = file->position;
            fn->modify_time = get_time();
        }
        
        write_mft_entry(file->mft_number, entry);
        return bytes_written;
        
    } else {
        /* Resident: write data directly in MFT entry */
        uint8_t* data = (uint8_t*)data_attr + sizeof(attr_header_t);
        const uint8_t* buf = (const uint8_t*)buffer;
        
        /* Check if we have space */
        if (file->position + size > MFT_ENTRY_SIZE - entry->used_size) {
            /* --- Convert resident to non-resident --- */
            /* Security: length is DISK-controlled; clamp the saved
             * resident payload to both the attribute and the slot so a
             * hostile length cannot smash this stack buffer. */
            uint64_t existing_size = (data_attr->length > sizeof(attr_header_t))
                                   ? (uint64_t)(data_attr->length - sizeof(attr_header_t))
                                   : 0;
            uint64_t attr_off = (uint64_t)((uint8_t*)data_attr - (uint8_t*)entry);
            if (attr_off + sizeof(attr_header_t) + existing_size > MFT_ENTRY_SIZE) {
                existing_size = MFT_ENTRY_SIZE - attr_off - sizeof(attr_header_t);
            }

            /* Save existing resident data before modifying the attribute */
            uint8_t existing_data[MFT_ENTRY_SIZE];
            for (uint64_t i = 0; i < existing_size; i++) {
                existing_data[i] = data[i];
            }
            
            /* Calculate total data size after write */
            uint64_t write_end = file->position + size;
            uint64_t total_end = (write_end > existing_size) ? write_end : existing_size;
            uint64_t clusters_needed = (total_end + FS_CLUSTER_SIZE - 1) / FS_CLUSTER_SIZE;
            if (clusters_needed == 0) clusters_needed = 1;
            
            /* Convert attribute to non-resident */
            data_attr->non_resident = 1;
            uint32_t new_attr_len = sizeof(attr_header_t) + sizeof(attr_nonresident_t) 
                                  + (uint32_t)(clusters_needed * sizeof(data_run_t));
            data_attr->length = new_attr_len;
            
            /* Initialize non-resident header */
            attr_nonresident_t* nr = (attr_nonresident_t*)((uint8_t*)data_attr + sizeof(attr_header_t));
            nr->start_vcn = 0;
            nr->last_vcn = clusters_needed - 1;
            nr->run_offset = sizeof(attr_header_t) + sizeof(attr_nonresident_t);
            nr->comp_unit_size = 0;
            nr->reserved = 0;
            nr->allocated_size = 0;
            nr->real_size = 0;
            nr->initialized_size = 0;
            
            /* Allocate data runs */
            data_run_t* runs = (data_run_t*)((uint8_t*)data_attr + nr->run_offset);
            uint64_t clusters_allocated = 0;
            for (uint64_t i = 0; i < clusters_needed; i++) {
                uint64_t new_cluster = alloc_cluster();
                if (new_cluster == (uint64_t)-1) break;
                runs[clusters_allocated].start_cluster = new_cluster;
                runs[clusters_allocated].length = 1;
                clusters_allocated++;
            }
            
            if (clusters_allocated == 0) return 0;
            
            /* Write existing resident data to clusters */
            for (uint64_t i = 0; i < clusters_allocated; i++) {
                uint8_t cluster_buf[FS_CLUSTER_SIZE];
                for (size_t z = 0; z < FS_CLUSTER_SIZE; z++) cluster_buf[z] = 0;
                
                uint64_t copy_start = i * FS_CLUSTER_SIZE;
                if (copy_start < existing_size) {
                    uint64_t copy_size = FS_CLUSTER_SIZE;
                    if (copy_start + copy_size > existing_size) {
                        copy_size = existing_size - copy_start;
                    }
                    for (uint64_t j = 0; j < copy_size; j++) {
                        cluster_buf[j] = existing_data[copy_start + j];
                    }
                }
                write_cluster(runs[i].start_cluster, cluster_buf);
            }
            
            /* Overlay new data from buffer onto clusters */
            const uint8_t* buf = (const uint8_t*)buffer;
            size_t bytes_written = 0;
            while (bytes_written < size) {
                uint64_t cluster_idx = (file->position + bytes_written) / FS_CLUSTER_SIZE;
                if (cluster_idx >= clusters_allocated) break;
                
                uint64_t cluster = runs[cluster_idx].start_cluster;
                uint64_t offset_in_cluster = (file->position + bytes_written) % FS_CLUSTER_SIZE;
                
                uint64_t to_write = FS_CLUSTER_SIZE - offset_in_cluster;
                if (to_write > size - bytes_written) {
                    to_write = size - bytes_written;
                }
                
                /* Read-modify-write this cluster */
                uint8_t cluster_buf[FS_CLUSTER_SIZE];
                read_cluster(cluster, cluster_buf);
                for (uint64_t j = 0; j < to_write; j++) {
                    cluster_buf[offset_in_cluster + j] = buf[bytes_written + j];
                }
                write_cluster(cluster, cluster_buf);
                
                bytes_written += to_write;
            }
            
            /* Update non-resident header */
            nr->allocated_size = clusters_allocated * FS_CLUSTER_SIZE;
            nr->real_size = (file->position + bytes_written > existing_size) 
                          ? (file->position + bytes_written) : existing_size;
            nr->initialized_size = nr->real_size;
            
            file->position += bytes_written;
            file->size = file->position;  /* keep in-memory size in sync */
            
            /* Update file size */
            attr_filename_t* fn = (attr_filename_t*)find_attr_payload(entry, ATTR_FILENAME);
            if (fn) {
                fn->real_size = file->position;
                fn->modify_time = get_time();
            }
            
            /* Move ATTR_END to right after the new non-resident attribute */
            attr_header_t* end = (attr_header_t*)((uint8_t*)data_attr + new_attr_len);
            end->type = ATTR_END;
            end->length = 8;
            entry->used_size = (uint32_t)((uint8_t*)end + 8 - (uint8_t*)entry);
            
            write_mft_entry(file->mft_number, entry);
            return bytes_written;
        }
        
        for (size_t i = 0; i < size; i++) {
            data[file->position + i] = buf[i];
        }
        
        file->position += size;
        file->size = file->position;  /* keep in-memory size in sync */
        data_attr->length = sizeof(attr_header_t) + file->position;
        
        /* Update file size */
        attr_filename_t* fn = (attr_filename_t*)find_attr_payload(entry, ATTR_FILENAME);
        if (fn) {
            fn->real_size = file->position;
            fn->modify_time = get_time();
        }
        
        write_mft_entry(file->mft_number, entry);
        return size;
    }
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

/* Walk the MFT and cluster bitmap to produce usage statistics. */
int fs_get_stats(fs_stats_t* stats) {
    if (!stats || !boot_sector || !mft_zone) return -1;

    uint64_t free_clusters = 0;
    for (uint64_t i = 0; i < boot_sector->total_clusters; i++) {
        uint64_t byte = i / 8;
        uint8_t bit = i % 8;
        if (!(cluster_bitmap[byte] & (1 << bit))) {
            free_clusters++;
        }
    }

    uint64_t total = boot_sector->total_clusters * FS_CLUSTER_SIZE;
    uint64_t freeb = free_clusters * FS_CLUSTER_SIZE;

    uint32_t files = 0;
    uint32_t dirs = 0;
    for (uint64_t i = MFT_FIRST_USER; i < boot_sector->mft_size; i++) {
        mft_header_t* entry = (mft_header_t*)(mft_zone + i * MFT_ENTRY_SIZE);
        if (!(entry->flags & MFT_FLAG_IN_USE)) continue;
        if (entry->flags & MFT_FLAG_DIRECTORY) {
            dirs++;
        } else {
            files++;
        }
    }

    stats->total_bytes = total;
    stats->free_bytes = freeb;
    stats->used_bytes = total - freeb;
    stats->file_count = files;
    stats->dir_count = dirs;
    return 0;
}

/* Create directory */
int fs_mkdir(const char* path) {
    if (!path || !path[0]) return -1;
    
    uint64_t mft_num = alloc_mft_entry();
    if (mft_num == (uint64_t)-1) return -1;
    
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
    if (parent_mft == (uint64_t)-1) return -1;
    
    /* Initialize directory entry */
    mft_header_t* entry = (mft_header_t*)(mft_zone + mft_num * MFT_ENTRY_SIZE);
    init_mft_entry(entry, mft_num, MFT_FLAG_DIRECTORY);
    
    const char* filename = last_slash + 1;
    add_filename_attr(entry, filename, parent_mft);
    add_index_root_attr(entry);
    
    /* Add entry to parent directory's index */
    mft_header_t* parent = (mft_header_t*)(mft_zone + parent_mft * MFT_ENTRY_SIZE);
    add_dir_entry(parent, mft_num, filename);
    
    /* Write both entries */
    if (write_mft_entry(mft_num, entry) < 0) return -1;
    return write_mft_entry(parent_mft, parent) < 0 ? -1 : 0;
}

/* Remove an empty directory: refuses non-empty directories (a child
 * would be orphaned — its parent_mft would dangle), removes the
 * directory's own entry from its parent's index, then clears the MFT
 * entry. Returns 0 on success, -1 on failure. */
int fs_rmdir(const char* path) {
    if (!path || !path[0]) return -1;

    uint64_t mft_num = find_file(path);
    if (mft_num == (uint64_t)-1) return -1;

    mft_header_t* entry = (mft_header_t*)(mft_zone + mft_num * MFT_ENTRY_SIZE);
    if (!(entry->flags & MFT_FLAG_DIRECTORY)) return -1;  /* not a directory */
    if (entry->flags & MFT_FLAG_READONLY) return -1;

    /* Refuse if any file or directory lists this as its parent. */
    for (uint64_t i = MFT_FIRST_USER; i < boot_sector->mft_size; i++) {
        mft_header_t* child = (mft_header_t*)(mft_zone + i * MFT_ENTRY_SIZE);
        if (!(child->flags & MFT_FLAG_IN_USE)) continue;
        attr_filename_t* cfn = (attr_filename_t*)find_attr_payload(child, ATTR_FILENAME);
        if (cfn && cfn->parent_mft == mft_num) return -1;  /* non-empty */
    }

    /* Remove our index entry from the parent directory. */
    attr_filename_t* fn = (attr_filename_t*)find_attr_payload(entry, ATTR_FILENAME);
    if (fn && fn->parent_mft != (uint64_t)-1 && fn->parent_mft < boot_sector->mft_size) {
        mft_header_t* parent = (mft_header_t*)(mft_zone + fn->parent_mft * MFT_ENTRY_SIZE);
        remove_index_entry(parent, mft_num);
        write_mft_entry(fn->parent_mft, parent);
    }

    /* Clear MFT entry */
    entry->magic = 0;
    entry->flags = 0;
    entry->used_size = 0;
    return write_mft_entry(mft_num, entry) < 0 ? -1 : 0;
}

/* Delete file/directory */
int fs_unlink(const char* path) {
    if (!path || !path[0]) return -1;
    
    uint64_t mft_num = find_file(path);
    if (mft_num == (uint64_t)-1) return -1;
    
    mft_header_t* entry = (mft_header_t*)(mft_zone + mft_num * MFT_ENTRY_SIZE);

    /* Read-only files cannot be deleted. */
    if (entry->flags & MFT_FLAG_READONLY) return -1;

    /* Directories must go through fs_rmdir (empty-check + index move). */
    if (entry->flags & MFT_FLAG_DIRECTORY) return -1;
    
    /* Free data clusters if non-resident */
    attr_header_t* data_attr = find_attr(entry, ATTR_DATA);
    if (data_attr && data_attr->non_resident) {
        attr_nonresident_t* nr = (attr_nonresident_t*)((uint8_t*)data_attr + sizeof(attr_header_t));
        data_run_t* runs = (data_run_t*)((uint8_t*)data_attr + nr->run_offset);
        /* Security: same run-count bounding as fs_truncate. */
        uint64_t num_runs = nonresident_run_count(data_attr, nr);
        if (nr->last_vcn >= nr->start_vcn &&
            num_runs > nr->last_vcn - nr->start_vcn + 1) {
            num_runs = nr->last_vcn - nr->start_vcn + 1;
        }

        for (uint64_t i = 0; i < num_runs; i++) {
            if (runs[i].length > boot_sector->total_clusters) break;
            for (uint64_t j = 0; j < runs[i].length; j++) {
                uint64_t cluster = runs[i].start_cluster + j;
                uint64_t byte = cluster / 8;
                uint64_t bit = cluster % 8;
                if (byte < (boot_sector->total_clusters + 7) / 8) {
                    cluster_bitmap[byte] &= ~(1 << bit);
                }
            }
        }
    }
    
    /* Remove this file's entry from its parent's directory index. If
     * left behind, a later file reusing this MFT slot would be listed
     * under the old name too (phantom entries). */
    attr_filename_t* fn = (attr_filename_t*)find_attr_payload(entry, ATTR_FILENAME);
    if (fn) {
        uint64_t parent_mft = fn->parent_mft;
        if (parent_mft != (uint64_t)-1 && parent_mft < boot_sector->mft_size) {
            mft_header_t* parent = (mft_header_t*)(mft_zone + parent_mft * MFT_ENTRY_SIZE);
            remove_index_entry(parent, mft_num);
            write_mft_entry(parent_mft, parent);
        }
    }

    /* Clear MFT entry */
    entry->magic = 0;
    entry->flags = 0;
    entry->used_size = 0;
    
    return write_mft_entry(mft_num, entry) < 0 ? -1 : 0;
}

/* Find the index entry for child_mft in a directory's index root. */
static index_entry_t* find_index_entry(mft_header_t* dir, uint64_t child_mft) {
    attr_header_t* index_attr = find_attr(dir, ATTR_INDEX_ROOT);
    if (!index_attr) return 0;

    index_root_t* ir = (index_root_t*)((uint8_t*)index_attr + sizeof(attr_header_t));
    index_entry_t* e = (index_entry_t*)((uint8_t*)ir + sizeof(index_root_t));
    uint8_t* end = (uint8_t*)index_attr + index_attr->length;

    while ((uint8_t*)e + sizeof(index_entry_t) <= end) {
        if (e->mft_ref == child_mft) return e;
        if (e->flags & 0x01) break;
        if (e->length < sizeof(index_entry_t)) break;
        e = (index_entry_t*)((uint8_t*)e + e->length);
    }
    return 0;
}

/* Remove the index entry for child_mft from a directory's index root,
 * compacting the entries that followed it. */
static void remove_index_entry(mft_header_t* dir, uint64_t child_mft) {
    attr_header_t* index_attr = find_attr(dir, ATTR_INDEX_ROOT);
    if (!index_attr) return;

    index_root_t* ir = (index_root_t*)((uint8_t*)index_attr + sizeof(attr_header_t));
    index_entry_t* e = (index_entry_t*)((uint8_t*)ir + sizeof(index_root_t));
    uint8_t* end = (uint8_t*)index_attr + index_attr->length;
    uint8_t* slot_end = (uint8_t*)dir + MFT_ENTRY_SIZE;
    if (end > slot_end) end = slot_end;   /* hostile length clamp */
    index_entry_t* prev = 0;

    while ((uint8_t*)e + sizeof(index_entry_t) <= end) {
        if (e->mft_ref == child_mft) {
            /* Security: e->length is DISK bytes. A bad length must never
             * drive the compaction copy (a negative ptrdiff once wrapped
             * to ~4GB of OOB write here). If the length cannot be
             * trusted, truncate the index at this entry instead. */
            if (e->length < sizeof(index_entry_t) ||
                (uint8_t*)e + e->length > end) {
                index_attr->length = (uint32_t)((uint8_t*)e - (uint8_t*)index_attr);
                if (prev) prev->flags |= 0x01;
                return;
            }
            uint32_t entry_len = e->length;
            uint8_t* next = (uint8_t*)e + entry_len;
            uint32_t trailing = (uint32_t)(end - next);   /* provably >= 0 */
            for (uint32_t i = 0; i < trailing; i++) {
                ((uint8_t*)e)[i] = next[i];
            }
            index_attr->length -= entry_len;
            /* The entry before the removed one (if any) becomes the last. */
            if (prev) prev->flags |= 0x01;
            return;
        }
        if (e->flags & 0x01) return;
        if (e->length < sizeof(index_entry_t)) return;
        prev = e;
        e = (index_entry_t*)((uint8_t*)e + e->length);
    }
}

/* Rename (or move) a file: updates the child's filename attribute and
 * moves its directory index entry between parents. Unlike copy+delete,
 * the MFT flags (e.g. read-only) and data clusters are preserved. */
int fs_rename(const char* old_path, const char* new_path) {
    if (!old_path || !new_path || old_path[0] != '/' || new_path[0] != '/') return -1;

    uint64_t mft_num = find_file(old_path);
    if (mft_num == (uint64_t)-1) return -1;

    /* Split new_path into parent directory + basename. */
    const char* last_slash = new_path;
    for (const char* p = new_path; *p; p++) {
        if (*p == '/') last_slash = p;
    }
    const char* new_name = last_slash + 1;
    if (!*new_name) return -1;

    char parent_path[256];
    int parent_len = (int)(last_slash - new_path);
    if (parent_len == 0) {
        parent_path[0] = '/';
        parent_path[1] = 0;
    } else {
        for (int i = 0; i < parent_len && i < 254; i++) {
            parent_path[i] = new_path[i];
        }
        parent_path[parent_len] = 0;
    }

    uint64_t new_parent = find_file(parent_path);
    if (new_parent == (uint64_t)-1) return -1;

    /* Destination already exists? Refuse (like mv refusing overwrite of
     * a directory, but simple for now). */
    if (find_file(new_path) != (uint64_t)-1) return -1;

    mft_header_t* entry = (mft_header_t*)(mft_zone + mft_num * MFT_ENTRY_SIZE);
    attr_filename_t* fn = (attr_filename_t*)find_attr_payload(entry, ATTR_FILENAME);
    if (!fn) return -1;

    uint64_t old_parent = fn->parent_mft;
    int name_len = 0;
    while (new_name[name_len] && name_len < MAX_FILENAME_LEN) name_len++;

    /* Update the child's own filename attribute. */
    fn->filename_length = (uint8_t)name_len;
    for (int i = 0; i < name_len; i++) fn->filename[i] = new_name[i];
    fn->parent_mft = new_parent;
    fn->modify_time = get_time();
    write_mft_entry(mft_num, entry);

    /* Update the directory index. */
    if (old_parent == new_parent) {
        /* Same directory: rewrite the existing index entry's filename. */
        mft_header_t* parent = (mft_header_t*)(mft_zone + old_parent * MFT_ENTRY_SIZE);
        index_entry_t* ie = find_index_entry(parent, mft_num);
        if (ie) {
            attr_filename_t* ifn = &ie->filename;
            ifn->filename_length = (uint8_t)name_len;
            for (int i = 0; i < name_len; i++) ifn->filename[i] = new_name[i];
            write_mft_entry(old_parent, parent);
        }
    } else {
        /* Cross-directory: remove from the old parent, add to the new. */
        mft_header_t* oldp = (mft_header_t*)(mft_zone + old_parent * MFT_ENTRY_SIZE);
        remove_index_entry(oldp, mft_num);
        write_mft_entry(old_parent, oldp);

        mft_header_t* newp = (mft_header_t*)(mft_zone + new_parent * MFT_ENTRY_SIZE);
        add_dir_entry(newp, mft_num, new_name);
        write_mft_entry(new_parent, newp);
    }

    return 0;
}

/* Permissions: mark a file read-only (ro=1) or writable (ro=0). The
 * flag lives in the MFT entry flags, so it survives remounts. */
int fs_set_readonly(const char* path, int ro) {
    if (!path || !path[0]) return -1;

    uint64_t mft_num = find_file(path);
    if (mft_num == (uint64_t)-1) return -1;

    mft_header_t* entry = (mft_header_t*)(mft_zone + mft_num * MFT_ENTRY_SIZE);
    if (ro) {
        entry->flags |= MFT_FLAG_READONLY;
    } else {
        entry->flags &= (uint16_t)~MFT_FLAG_READONLY;
    }

    return write_mft_entry(mft_num, entry) < 0 ? -1 : 0;
}

int fs_is_readonly(const char* path) {
    if (!path || !path[0]) return -1;

    uint64_t mft_num = find_file(path);
    if (mft_num == (uint64_t)-1) return -1;

    mft_header_t* entry = (mft_header_t*)(mft_zone + mft_num * MFT_ENTRY_SIZE);
    return (entry->flags & MFT_FLAG_READONLY) ? 1 : 0;
}

/* Report whether the path resolves to a directory (1), a file (0),
 * or does not exist (-1). Companion to fs_is_readonly. */
int fs_is_directory(const char* path) {
    if (!path || !path[0]) return -1;

    uint64_t mft_num = find_file(path);
    if (mft_num == (uint64_t)-1) return -1;

    mft_header_t* entry = (mft_header_t*)(mft_zone + mft_num * MFT_ENTRY_SIZE);
    return (entry->flags & MFT_FLAG_DIRECTORY) ? 1 : 0;
}

/* Stat a path: fills `info` with the file's ATTR_FILENAME payload (name,
 * sizes, timestamps, parent). Returns 0 on success, -1 if not found. */
int fs_stat(const char* path, attr_filename_t* info) {
    if (!path || !path[0] || !info) return -1;

    uint64_t mft_num = find_file(path);
    if (mft_num == (uint64_t)-1) return -1;

    mft_header_t* entry = (mft_header_t*)(mft_zone + mft_num * MFT_ENTRY_SIZE);
    attr_filename_t* fn = (attr_filename_t*)find_attr_payload(entry, ATTR_FILENAME);
    if (!fn) return -1;

    *info = *fn;
    return 0;
}

/* List directory contents using index */
int fs_readdir(const char* path, void (*callback)(const char* name, int is_dir, uint32_t size)) {
    if (!path || !callback) return -1;
    
    uint64_t dir_mft = find_file(path);
    if (dir_mft == (uint64_t)-1) return -1;
    
    mft_header_t* dir_entry = (mft_header_t*)(mft_zone + dir_mft * MFT_ENTRY_SIZE);
    attr_header_t* index_attr = find_attr(dir_entry, ATTR_INDEX_ROOT);
    
    if (!index_attr) {
        /* Fallback to linear search for compatibility */
        int count = 0;
        for (uint64_t i = MFT_FIRST_USER; i < boot_sector->mft_size; i++) {
            mft_header_t* entry = (mft_header_t*)(mft_zone + i * MFT_ENTRY_SIZE);
            if (!(entry->flags & MFT_FLAG_IN_USE)) continue;
            
            attr_filename_t* fn = (attr_filename_t*)find_attr_payload(entry, ATTR_FILENAME);
            if (!fn) continue;
            
            if (fn->parent_mft == dir_mft) {
                char name[MAX_FILENAME_LEN + 1];
                int len = fn->filename_length;
                for (int j = 0; j < len; j++) {
                    name[j] = fn->filename[j];
                }
                name[len] = 0;
                
                callback(name, entry->flags & MFT_FLAG_DIRECTORY, fn->real_size);
                count++;
            }
        }
        return count;
    }
    
    /* Use index for fast directory listing */
    index_root_t* ir = (index_root_t*)((uint8_t*)index_attr + sizeof(attr_header_t));
    index_entry_t* entries = (index_entry_t*)((uint8_t*)ir + sizeof(index_root_t));
    
    int count = 0;
    uint8_t* ptr = (uint8_t*)entries;
    uint8_t* end = (uint8_t*)index_attr + index_attr->length;
    uint8_t* slot_end = (uint8_t*)dir_entry + MFT_ENTRY_SIZE;
    if (end > slot_end) end = slot_end;   /* hostile length clamp */

    while (ptr + sizeof(index_entry_t) <= end) {
        index_entry_t* entry = (index_entry_t*)ptr;

        /* A zero/garbage length must not loop forever or walk past end. */
        if (entry->length < sizeof(index_entry_t)) break;
        
        /* Check for end marker */
        if (entry->flags & 0x01) {
            /* Last entry - process it and break */
            if (entry->mft_ref >= MFT_FIRST_USER && entry->mft_ref < boot_sector->mft_size) {
                mft_header_t* file_entry = (mft_header_t*)(mft_zone + entry->mft_ref * MFT_ENTRY_SIZE);
                if (file_entry->flags & MFT_FLAG_IN_USE) {
                    char name[MAX_FILENAME_LEN + 1];
                    int len = entry->filename.filename_length;
                    for (int j = 0; j < len; j++) {
                        name[j] = entry->filename.filename[j];
                    }
                    name[len] = 0;
                    
                    /* Report the file's real size from its own MFT entry */
                    attr_filename_t* ffn = (attr_filename_t*)find_attr_payload(file_entry, ATTR_FILENAME);
                    callback(name, file_entry->flags & MFT_FLAG_DIRECTORY,
                             ffn ? ffn->real_size : entry->filename.real_size);
                    count++;
                }
            }
            break;
        }
        
        /* Process regular entry */
        if (entry->mft_ref >= MFT_FIRST_USER && entry->mft_ref < boot_sector->mft_size) {
            mft_header_t* file_entry = (mft_header_t*)(mft_zone + entry->mft_ref * MFT_ENTRY_SIZE);
            if (file_entry->flags & MFT_FLAG_IN_USE) {
                char name[MAX_FILENAME_LEN + 1];
                int len = entry->filename.filename_length;
                for (int j = 0; j < len; j++) {
                    name[j] = entry->filename.filename[j];
                }
                name[len] = 0;
                
                /* Report the file's real size from its own MFT entry */
                attr_filename_t* ffn = (attr_filename_t*)find_attr_payload(file_entry, ATTR_FILENAME);
                callback(name, file_entry->flags & MFT_FLAG_DIRECTORY,
                         ffn ? ffn->real_size : entry->filename.real_size);
                count++;
            }
        }
        
        ptr += entry->length;
    }

    return count;
}
