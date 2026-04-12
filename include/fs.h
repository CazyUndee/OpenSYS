#ifndef FS_H
#define FS_H

#include <stdint.h>
#include <stddef.h>

/*
 * OpenFS - A simplified NTFS-style filesystem
 * 
 * Structure:
 *   - Boot sector (sector 0)
 *   - MFT Zone (Master File Table)
 *   - Data Zone (file contents)
 * 
 * MFT contains all metadata as "attributes":
 *   - File name
 *   - Data (small files stored directly in MFT)
 *   - Index root (directory contents)
 *   - Attribute list (for fragmented files)
 */

#define FS_SECTOR_SIZE      512
#define FS_CLUSTER_SIZE     4096
#define FS_SECTORS_PER_CLUSTER  (FS_CLUSTER_SIZE / FS_SECTOR_SIZE)

#define FS_MAGIC            0x4F50464E  /* "OPFN" - OpenFS Native */
#define FS_VERSION          0x00010000  /* v1.0 */

#define MFT_ENTRY_SIZE      4096
#define MFT_ENTRIES_PER_CLUSTER  1

#define MAX_FILENAME_LEN    255
#define MAX_ATTRS_PER_ENTRY 16

/* Attribute types */
#define ATTR_UNUSED         0x00
#define ATTR_STANDARD_INFO  0x10
#define ATTR_FILENAME       0x30
#define ATTR_DATA           0x80
#define ATTR_INDEX_ROOT     0x90
#define ATTR_INDEX_ALLOC    0xA0
#define ATTR_BITMAP         0xB0
#define ATTR_END            0xFFFFFFFF

/* File flags */
#define FILE_FLAG_READONLY  0x0001
#define FILE_FLAG_HIDDEN    0x0002
#define FILE_FLAG_SYSTEM    0x0004
#define FILE_FLAG_DIRECTORY 0x1000
#define FILE_FLAG_COMPRESSED 0x2000
#define FILE_FLAG_ENCRYPTED  0x4000

/* MFT entry flags */
#define MFT_FLAG_IN_USE     0x0001
#define MFT_FLAG_DIRECTORY  0x0002

/* Boot sector */
typedef struct {
    uint32_t magic;              /* FS_MAGIC */
    uint32_t version;            /* FS_VERSION */
    uint64_t total_sectors;
    uint32_t sectors_per_cluster;
    uint32_t mft_start;          /* First cluster of MFT */
    uint32_t mft_size;           /* Number of MFT entries */
    uint32_t mft_entry_size;     /* Bytes per MFT entry */
    uint32_t data_start;         /* First cluster of data area */
    uint64_t total_clusters;
    uint8_t  reserved[458];
    uint16_t signature;          /* 0xAA55 */
} __attribute__((packed)) fs_boot_sector_t;

/* Attribute header */
typedef struct {
    uint32_t type;               /* Attribute type */
    uint32_t length;             /* Total length including header */
    uint8_t  non_resident;       /* 0 = resident, 1 = non-resident */
    uint8_t  name_length;        /* 0 = no name */
    uint16_t name_offset;
    uint16_t flags;              /* Compressed, encrypted, etc */
    uint16_t instance;
    /* Resident content follows immediately */
} __attribute__((packed)) attr_header_t;

/* Standard information attribute */
typedef struct {
    uint64_t create_time;        /* Unix timestamp */
    uint64_t modify_time;
    uint64_t access_time;
    uint32_t flags;              /* FILE_FLAG_* */
    uint32_t max_versions;
    uint32_t version;
    uint32_t class_id;
    uint32_t owner_id;
    uint32_t security_id;
} __attribute__((packed)) attr_std_info_t;

/* Filename attribute */
typedef struct {
    uint64_t parent_mft;         /* MFT entry of parent directory */
    uint64_t create_time;
    uint64_t modify_time;
    uint64_t access_time;
    uint64_t alloc_size;         /* Alated size */
    uint64_t real_size;          /* Actual size */
    uint32_t flags;
    uint32_t reparse;
    uint8_t  filename_length;
    uint8_t  filename_type;      /* 0 = ASCII, 1 = UTF-8 */
    char     filename[MAX_FILENAME_LEN];
} __attribute__((packed)) attr_filename_t;

/* Non-resident attribute header extension */
typedef struct {
    uint64_t start_vcn;          /* Starting VCN */
    uint64_t last_vcn;           /* Last VCN */
    uint16_t run_offset;         /* Offset to data runs */
    uint16_t comp_unit_size;     /* Compression unit size */
    uint32_t reserved;
    uint64_t allocated_size;
    uint64_t real_size;
    uint64_t initialized_size;
} __attribute__((packed)) attr_nonresident_t;

/* Data run (cluster mapping) */
typedef struct {
    uint64_t start_cluster;      /* Starting cluster (0 = sparse) */
    uint64_t length;             /* Number of clusters */
} __attribute__((packed)) data_run_t;

/* MFT entry header */
typedef struct {
    uint32_t magic;              /* "FILE" = 0x454C4946 */
    uint16_t seq_attr_offset;    /* Offset to first attribute */
    uint16_t flags;              /* MFT_FLAG_* */
    uint32_t used_size;          /* Used bytes in entry */
    uint32_t alloc_size;         /* Allocated bytes */
    uint64_t base_record;        /* 0 = base, else points to base */
    uint16_t next_attr_id;
    uint16_t padding;
    uint32_t mft_number;         /* Entry number */
} __attribute__((packed)) mft_header_t;

/* Directory index entry */
typedef struct {
    uint64_t mft_ref;            /* MFT entry of file */
    uint16_t length;             /* Entry length */
    uint16_t attr_type;          /* Type of indexed attribute (usually filename) */
    uint16_t flags;              /* 0x01 = last entry */
    uint16_t reserved;
    attr_filename_t filename;    /* Indexed data */
} __attribute__((packed)) index_entry_t;

/* Index root (for small directories) */
typedef struct {
    uint32_t attr_type;          /* Type being indexed */
    uint32_t collation_rule;
    uint32_t index_alloc_size;   /* Bytes per index block */
    uint8_t  clusters_per_index;
    uint8_t  reserved[3];
    /* index_entry_t entries follow */
} __attribute__((packed)) index_root_t;

/* File handle */
typedef struct {
    uint64_t mft_number;
    uint64_t position;
    uint64_t size;
    uint8_t  mode;               /* 0=read, 1=write, 2=append */
    uint8_t  flags;
} fs_file_t;

/* Directory handle */
typedef struct {
    uint64_t mft_number;
    uint64_t position;           /* Current entry position */
    uint32_t entry_count;
    uint32_t flags;
} fs_dir_t;

/* Initialize filesystem on disk */
int fs_format(uint64_t disk_size);

/* Mount filesystem */
int fs_mount(void);

/* File operations */
fs_file_t* fs_open(const char* path, int mode);
int fs_close(fs_file_t* file);
size_t fs_read(fs_file_t* file, void* buffer, size_t size);
size_t fs_write(fs_file_t* file, const void* buffer, size_t size);
int fs_seek(fs_file_t* file, int64_t offset, int whence);
int fs_truncate(fs_file_t* file, uint64_t size);

/* Directory operations */
fs_dir_t* fs_opendir(const char* path);
int fs_closedir(fs_dir_t* dir);
int fs_readdir(fs_dir_t* dir, char* name, uint64_t* mft_ref);
int fs_mkdir(const char* path);
int fs_rmdir(const char* path);

/* File management */
int fs_unlink(const char* path);
int fs_rename(const char* old_path, const char* new_path);
int fs_stat(const char* path, attr_filename_t* info);

/* Utility */
uint64_t fs_get_free_space(void);
uint64_t fs_get_total_space(void);
void fs_dump_info(void);

#endif
