/*
 * ns.h - Plan0 Unified Resource Namespace
 *
 * Copyright (C) 2026 CazyUndee
 * SPDX-License-Identifier: AGPL-3.0
 *
 * Authoritative specification: docs/NAMESPACE.md
 *
 * One namespace addresses every resource: local files, devices,
 * partitions, memory, and (future) network domains. Paths start with a
 * single decimal digit - the resource domain root (`0/` = this machine).
 * Resolution is table-driven; backends register capability, never path
 * strings. Short aliases are pure renames that expand to canonical paths
 * BEFORE classification, so both spellings resolve to one resource.
 */

#ifndef NS_H
#define NS_H

#include <stddef.h>

#define NS_MAX_PATH   256
#define NS_MAX_DEPTH  16

/* Resource kinds. The descriptor is what backends bind to; adding a
 * resource kind means adding a table row, not touching resolvers. */
typedef enum {
    NS_RESOURCE_NONE = 0,
    NS_ROOT,             /* 0                                 - machine root    */
    NS_STORAGE_DEVICE,   /* 0/hardware/storage/HDD_OR_SSD      - block device    */
    NS_PARTITION,        /* DEVICE/partitions/N/...       - fs-capable range*/
    NS_PARTITIONS_DIR,   /* DEVICE/partitions               - topology listing*/
    NS_MEMORY_RAM,       /* 0/hardware/memory/ram             - RAM introspection*/
    NS_CPU,              /* 0/hardware/cpu                    - cpu introspection */
    NS_HWINFO,           /* 0/hardware/platform|pci|cpu-id|memory/info        */
    NS_HARDWARE_DIR,     /* 0/hardware, .../storage, .../memory - structural   */
    NS_SYSTEM_DIR,       /* 0/system, 0/system/kernel, ...    - structural      */
    NS_SYSTEM_NODE,      /* 0/system/ (any leaf)              - OS info/control */
    NS_DEV_DIR,          /* 0/dev                             - device shims dir*/
    NS_DEV_SHIM,         /* 0/dev/null|zero|console           - device shims    */
    NS_USER_STORE,       /* 0/user/...                        - logical user root*/
    NS_UNRESOLVED        /* parsed but no such resource                         */
} ns_kind_t;

typedef struct {
    ns_kind_t kind;
    int domain;                      /* numeric root (always 0 today) */
    int part_index;                  /* partition number, or -1       */
    const char* device;              /* "ssd" | "hdd" for storage kinds */
    char canonical[NS_MAX_PATH];     /* alias-expanded canonical path */
} ns_resource_t;

/* Parse + alias-expand + classify. Returns 0 on success (out filled),
 * -1 on grammar violation, -2 when grammatically valid but unknown. */
int ns_resolve(const char* path, ns_resource_t* out);

/* Expand aliases into the canonical spelling (identity when none).
 * Returns 0 on success, -1 on grammar violation / overflow. */
int ns_canonical(const char* path, char* out, size_t max);

/* Human-readable description of a resolved path (kind, canonical form,
 * alias links, backend facts, children). Returns 0 when the path
 * resolves, -1 parse error, -2 unknown. */
int ns_describe(const char* path, char* out, size_t max);

/* Grammar check only (no classification). 0 = well-formed. */
int ns_parse_valid(const char* path);

/* ---- Shell argument translation (spec §12) ----
 *
 * Translate one shell path argument into a filesystem path.
 *
 * Returns:
 *   NS_FS_OK        ( 0)  out holds the fs path ("/" for a volume root)
 *   NS_FS_NOT_NS    ( 1)  not a namespace path - caller passes through
 *   NS_FS_EPARSE    (-1)  invalid namespace syntax
 *   NS_FS_EUNKNOWN  (-2)  grammatically valid, no such resource
 *   NS_FS_EVOLUME   (-3)  addresses a volume other than the mounted one
 *   NS_FS_EKIND     (-4)  resource kind carries no filesystem
 */
#define NS_FS_OK       0
#define NS_FS_NOT_NS   1
#define NS_FS_EPARSE  (-1)
#define NS_FS_EUNKNOWN (-2)
#define NS_FS_EVOLUME (-3)
#define NS_FS_EKIND   (-4)

int ns_to_fs_path(const char* input, char* out, size_t max);

#endif /* NS_H */
