#ifndef FS_VFS_H
#define FS_VFS_H

/*
 * fs_vfs.h - NTFS-style fs backend adapter for the VFS fd layer
 *
 * Exposes the disk-backed fs.c filesystem through the unified VFS mount
 * table so real files are reachable via the file-descriptor layer.
 */

#include "vfs.h"

extern vfs_ops_t fs_vfs_ops;

#endif
