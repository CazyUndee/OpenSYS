/*
 * volume.h - Storage volume binding for the filesystem
 *
 * Copyright (C) 2026 CazyUndee
 * SPDX-License-Identifier: AGPL-3.0
 *
 * docs/NAMESPACE.md: a filesystem mounts INSIDE a partition volume,
 * never across a whole device. The volume layer translates "which
 * partition" into a base LBA + size that fs.c offsets all block I/O
 * with. Legacy whole-disk behavior is base 0 / unbounded.
 */

#ifndef VOLUME_H
#define VOLUME_H

#include <stdint.h>

/* Bind the fs to GPT partition n (1-based, dense over used entries).
 * Returns 0 on success, -1 if the partition does not exist. */
int volume_use_partition(int partition_num);

/* Restore legacy whole-disk behavior (base LBA 0, unbounded). */
void volume_use_whole_disk(void);

/* First LBA of the bound volume (0 in legacy mode). */
uint64_t volume_base_lba(void);

/* Size of the bound volume in sectors (0 = whole disk). */
uint64_t volume_sectors(void);

#endif /* VOLUME_H */
