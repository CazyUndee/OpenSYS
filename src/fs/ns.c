/*
 * ns.c - Plan0 Unified Resource Namespace resolver
 *
 * Copyright (C) 2026 CazyUndee
 * SPDX-License-Identifier: AGPL-3.0
 *
 * Implements docs/NAMESPACE.md. Pipeline:
 *   normalize → alias expansion → split → table-driven classification.
 *
 * Backends register facts (disk/GPT/partition state); the resolver never
 * matches individual path strings outside the declarative node walk, and
 * aliases expand before classification so both spellings resolve to one
 * resource object.
 */

#include "ns.h"
#include "kstring.h"
#include "pmm.h"
#include "part.h"
#include "disk.h"
#include "volume.h"

/* ================================================================
 * Alias table — pure renames. Expansion happens BEFORE classification
 * so no backend ever sees an alias spelling. Aliases cover whole
 * components at component boundaries (0/hss/partitions/1 expands too).
 * ================================================================ */

typedef struct {
    const char* short_path;
    const char* canonical;
} ns_alias_t;

static const ns_alias_t ns_aliases[] = {
    { "0/hmr", "0/hardware/memory/ram" },
    { "0/hsh", "0/hardware/storage/hdd" },
    { "0/hss", "0/hardware/storage/ssd" },
};
#define NS_ALIAS_COUNT (sizeof(ns_aliases) / sizeof(ns_aliases[0]))

/* ================================================================
 * Normalization + grammar
 * ================================================================ */

/* Lowercase, strip trailing slashes. Returns length or -1 on overflow. */
static int ns_normalize(const char* in, char* out) {
    if (!in) return -1;
    size_t i = 0;
    while (in[i] && i < NS_MAX_PATH - 1) {
        char c = in[i];
        out[i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
        i++;
    }
    if (in[i] != '\0') return -1;              /* too long */
    out[i] = '\0';
    /* Strip trailing slashes ("0/hss/" -> "0/hss"; bare "/" is invalid). */
    while (i > 0 && out[i - 1] == '/') out[--i] = '\0';
    return (int)i;
}

static int valid_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
           c == '-' || c == '_' || c == '.';
}

/* Grammar:
 *   path      := digit ( '/' component )*            [bare root allowed]
 *   component := (lower|digit|'-'|'_'|'.')+          [not ALL dots]
 *   depth <= NS_MAX_DEPTH                                            */
int ns_parse_valid(const char* path) {
    char norm[NS_MAX_PATH];
    int len = ns_normalize(path, norm);
    if (len < 1) return 0;
    if (norm[0] < '0' || norm[0] > '9') return 0;
    if (len == 1) return 1;                    /* bare machine root "0" */
    if (norm[1] != '/') return 0;

    int depth = 0;
    int comp_len = 0;
    int nondot = 0;
    for (int i = 2; ; i++) {
        char c = norm[i];
        if (c == '\0' || c == '/') {
            if (comp_len == 0) return 0;       /* empty component       */
            if (!nondot) return 0;             /* ".", "..", "..."      */
            depth++;
            if (depth > NS_MAX_DEPTH) return 0;
            comp_len = 0;
            nondot = 0;
            if (c == '\0') break;
        } else if (valid_char(c)) {
            comp_len++;
            if (c != '.') nondot = 1;
        } else {
            return 0;                          /* illegal character     */
        }
    }
    return 1;
}

/* Split a normalized path into components (pointers into `buf`). */
static int ns_split(char* buf, char* comps[], int max) {
    int count = 0;
    char* p = buf;
    while (*p && count < max) {
        comps[count++] = p;
        while (*p && *p != '/') p++;
        if (*p == '/') {
            *p = '\0';
            p++;
            if (!*p) break;                    /* trailing slash already stripped */
        }
    }
    return count;
}

/* ================================================================
 * Alias expansion — whole-component prefix replacement, repeated
 * (bounded) so chained aliases would also resolve.
 * ================================================================ */

int ns_canonical(const char* path, char* out, size_t max) {
    char work[NS_MAX_PATH];
    if (ns_normalize(path, work) < 0) return -1;
    if (!ns_parse_valid(work)) return -1;

    for (int pass = 0; pass < 8; pass++) {
        int replaced = 0;
        for (size_t a = 0; a < NS_ALIAS_COUNT; a++) {
            const char* s = ns_aliases[a].short_path;
            size_t slen = k_strlen(s);
            if (k_strncmp(work, s, slen) != 0) continue;
            char next = work[slen];
            if (next != '\0' && next != '/') continue;   /* boundary */

            /* Rebuild: canonical + remainder of work after the alias. */
            char rest[NS_MAX_PATH];
            k_strcpy(rest, work + slen);                 /* "" or "/..." */
            int clen = (int)k_strlen(ns_aliases[a].canonical);
            if (clen + (int)k_strlen(rest) >= NS_MAX_PATH) return -1;
            k_strcpy(work, ns_aliases[a].canonical);
            k_strcpy(work + clen, rest);
            replaced = 1;
            break;
        }
        if (!replaced) break;
    }

    if ((size_t)k_strlen(work) >= max) return -1;
    k_strcpy(out, work);
    return 0;
}

/* ================================================================
 * Classification — declarative node walk over the canonical path.
 * Partition numbers are dense indices over USED GPT entries,
 * matching part_list_partitions / the shell `parts` command.
 * ================================================================ */

static void set_resource(ns_resource_t* out, ns_kind_t kind, int domain,
                         int part_index, const char* device) {
    out->kind = kind;
    out->domain = domain;
    out->part_index = part_index;
    out->device = device;
}

static int parse_index(const char* s, int* out) {
    if (!*s) return -1;
    int v = 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9') return -1;
        v = v * 10 + (*s - '0');
        if (v > 1000000) return -1;            /* absurd index guard */
    }
    if (v < 1) return -1;                      /* partitions are 1-based */
    *out = v;
    return 0;
}

int ns_resolve(const char* path, ns_resource_t* out) {
    char canon[NS_MAX_PATH];
    if (!out) return -1;
    if (ns_canonical(path, canon, sizeof(canon)) < 0) return -1;

    char buf[NS_MAX_PATH];
    k_strcpy(buf, canon);
    char* comps[NS_MAX_DEPTH];
    int n = ns_split(buf, comps, NS_MAX_DEPTH);

    k_strcpy(out->canonical, canon);
    out->domain = comps[0][0] - '0';
    out->part_index = -1;
    out->device = 0;

    if (n == 1) {                              /* "0" — machine root */
        set_resource(out, NS_ROOT, out->domain, -1, 0);
        return 0;
    }

    if (k_strcmp(comps[1], "hardware") == 0) {
        if (n == 2) { set_resource(out, NS_HARDWARE_DIR, out->domain, -1, 0); return 0; }

        if (k_strcmp(comps[2], "memory") == 0) {
            if (n == 4 && k_strcmp(comps[3], "ram") == 0) {
                set_resource(out, NS_MEMORY_RAM, out->domain, -1, 0);
                return 0;
            }
            out->kind = NS_UNRESOLVED;
            return -2;
        }

        if (k_strcmp(comps[2], "storage") == 0) {
            if (n == 3) { set_resource(out, NS_HARDWARE_DIR, out->domain, -1, 0); return 0; }

            /* Topology honesty: a storage class exists only when the
             * detected device actually belongs to it (single-device
             * until multi-device support lands). */
            const char* owner = part_storage_device();
            const char* dev = 0;
            if (k_strcmp(comps[3], "hdd") == 0) dev = "hdd";
            else if (k_strcmp(comps[3], "ssd") == 0) dev = "ssd";

            if (!dev || !owner || k_strcmp(dev, owner) != 0) {
                out->kind = NS_UNRESOLVED;
                return -2;
            }

            if (n == 4) {
                set_resource(out, NS_STORAGE_DEVICE, out->domain, -1, dev);
                return 0;
            }

            if (k_strcmp(comps[4], "partitions") == 0) {
                if (n == 5) {
                    set_resource(out, NS_PARTITIONS_DIR, out->domain, -1, dev);
                    return 0;
                }
                if (n >= 6) {
                    int idx;
                    if (parse_index(comps[5], &idx) < 0) {
                        out->kind = NS_UNRESOLVED;
                        return -2;
                    }
                    /* The partition resource covers its whole volume-
                     * relative tree: anything at depth > 6 is content
                     * within the volume and still resolves here. */
                    set_resource(out, NS_PARTITION, out->domain, idx, dev);
                    return 0;
                }
            }

            out->kind = NS_UNRESOLVED;
            return -2;
        }

        if (k_strcmp(comps[2], "cpu") == 0) {
            if (n == 3) { set_resource(out, NS_CPU, out->domain, -1, 0); return 0; }
            out->kind = NS_UNRESOLVED;
            return -2;
        }

        out->kind = NS_UNRESOLVED;
        return -2;
    }

    if (k_strcmp(comps[1], "user") == 0) {
        set_resource(out, NS_USER_STORE, out->domain, -1, 0);
        return 0;
    }

    out->kind = NS_UNRESOLVED;
    return -2;
}

/* ================================================================
 * Introspection — the namespace describes itself (spec §13).
 * Storage kinds report REAL topology from disk/GPT/part when present.
 * ================================================================ */

static const char* kind_name(ns_kind_t k) {
    switch (k) {
        case NS_ROOT:           return "machine root";
        case NS_STORAGE_DEVICE: return "storage device";
        case NS_PARTITION:      return "partition";
        case NS_PARTITIONS_DIR: return "partition table";
        case NS_MEMORY_RAM:     return "system memory";
        case NS_CPU:            return "processor";
        case NS_HARDWARE_DIR:   return "hardware category";
        case NS_USER_STORE:     return "user storage";
        default:                return "unknown";
    }
}

/* Append helper with bounds tracking. */
struct desc_buf {
    char* buf;
    size_t max;
    size_t pos;
};

static void dput(struct desc_buf* d, const char* s) {
    while (*s && d->pos + 1 < d->max) d->buf[d->pos++] = *s++;
    d->buf[d->pos] = '\0';
}

static void dput_dec(struct desc_buf* d, uint64_t v) {
    char tmp[24];
    int i = 23;
    if (v == 0) { dput(d, "0"); return; }
    while (v > 0 && i > 0) { tmp[--i] = (char)('0' + (v % 10)); v /= 10; }
    dput(d, tmp + i);
}

/* If `path` is itself an alias, report its canonical; otherwise report
 * any alias that targets this canonical path. */
static void describe_alias_link(struct desc_buf* d, const char* canon_or_alias) {
    for (size_t a = 0; a < NS_ALIAS_COUNT; a++) {
        if (k_strcmp(canon_or_alias, ns_aliases[a].short_path) == 0) {
            dput(d, "canonical: ");
            dput(d, ns_aliases[a].canonical);
            dput(d, "\n");
            return;
        }
        if (k_strcmp(canon_or_alias, ns_aliases[a].canonical) == 0) {
            dput(d, "alias: ");
            dput(d, ns_aliases[a].short_path);
            dput(d, "\n");
        }
    }
}

static void describe_storage_facts(struct desc_buf* d) {
    if (!disk_is_ready()) {
        dput(d, "backend: none attached\n");
        return;
    }
    dput(d, "backend: ATA primary master, ");
    dput_dec(d, disk_get_size() / (1024ULL * 1024ULL));
    dput(d, " MB\n");
    if (part_is_ready()) {
        part_info_t list[16];
        int count = part_list_partitions(list, 16);
        dput_dec(d, count > 0 ? count : 0);
        dput(d, " partition(s) detected\n");
    } else {
        dput(d, "no GPT partition table\n");
    }
}

int ns_describe(const char* path, char* out, size_t max) {    ns_resource_t r;
    int rc = ns_resolve(path, &r);
    if (rc < 0) return rc;

    struct desc_buf d = { out, max, 0 };
    out[0] = '\0';

    dput(&d, "path: ");
    dput(&d, r.canonical);
    dput(&d, "\nkind: ");
    dput(&d, kind_name(r.kind));
    dput(&d, "\ndomain: ");
    dput_dec(&d, (uint64_t)r.domain);
    dput(&d, "\n");
    describe_alias_link(&d, r.canonical);

    switch (r.kind) {
        case NS_STORAGE_DEVICE:
        case NS_PARTITIONS_DIR:
            describe_storage_facts(&d);
            break;

        case NS_PARTITION: {
            dput(&d, "index: ");
            dput_dec(&d, (uint64_t)r.part_index);
            dput(&d, "\n");
            if (part_is_ready()) {
                part_info_t list[16];
                int count = part_list_partitions(list, 16);
                if (r.part_index <= count) {
                    part_info_t* info = &list[r.part_index - 1];
                    dput(&d, "start lba: ");
                    dput_dec(&d, info->start_lba);
                    dput(&d, "\nsize kb: ");
                    dput_dec(&d, info->size_sectors / 2);
                    dput(&d, "\nlabel: ");
                    dput(&d, info->label[0] ? info->label : "(none)");
                    dput(&d, "\nbackend: partition-relative block I/O\n");
                } else {
                    dput(&d, "state: not present on device\n");
                }
            } else {
                dput(&d, "backend: unavailable (no GPT)\n");
            }
            break;
        }

        case NS_MEMORY_RAM: {
            dput(&d, "total mb: ");
            dput_dec(&d, pmm_get_total() / (1024ULL * 1024ULL));
            dput(&d, "\nfree mb: ");
            dput_dec(&d, pmm_get_free() / (1024ULL * 1024ULL));
            dput(&d, "\n");
            break;
        }

        case NS_USER_STORE:
            dput(&d, "binding: boot policy (first available storage volume)\n");
            break;

        default:
            break;
    }

    return 0;
}

/* ================================================================
 * Shell argument translation (spec §12)
 *
 * Namespace paths map onto the single mounted filesystem volume:
 *   <device>/partitions/<n>/rest  ->  /rest   (only when n is the
 *                                              bound/mounted volume)
 *   0/user/rest                   ->  /rest   (logical user root;
 *                                              v1 binds the active fs)
 * Everything else either passes through (not a namespace path) or
 * fails with a structured error. Output is never longer than input:
 * every mapping strips a >=2-char prefix and adds at most one '/'.
 * ================================================================ */

/* Advance past n '/'-separated components of a canonical path. */
static const char* skip_components(const char* s, int n) {
    while (n-- > 0) {
        while (*s && *s != '/') s++;
        if (*s == '/') s++;
    }
    return s;
}

int ns_to_fs_path(const char* input, char* out, size_t max) {
    if (!input || !out || max == 0) return NS_FS_EPARSE;

    /* Pass-through: anything not shaped "<digit>/" is legacy syntax. */
    if (!(input[0] >= '0' && input[0] <= '9') || input[1] != '/') {
        return NS_FS_NOT_NS;
    }

    ns_resource_t r;
    int rc = ns_resolve(input, &r);
#ifdef NS_TRANSLATE_DEBUG
    {
        extern int printf(const char*, ...);
        printf("DBG resolve(%s) rc=%d kind=%d canon=%s\n",
               input, rc, (int)r.kind, r.canonical);
    }
#endif
    if (rc == -1) return NS_FS_EPARSE;
    if (rc == -2) return NS_FS_EUNKNOWN;

    const char* rest = 0;

    if (r.kind == NS_PARTITION) {
        /* Only the mounted volume is file-addressable. */
        part_info_t list[16];
        int count = part_list_partitions(list, 16);
        if (r.part_index > count ||
            list[r.part_index - 1].start_lba != volume_base_lba() ||
            volume_base_lba() == 0) {
            return NS_FS_EVOLUME;
        }
        rest = skip_components(r.canonical, 6);   /* 0/hardware/storage/dev/partitions/N */
    } else if (r.kind == NS_USER_STORE) {
        rest = skip_components(r.canonical, 2);   /* 0/user */
    } else {
        return NS_FS_EKIND;
    }

    size_t need = 1 + k_strlen(rest) + 1;         /* '/' + rest + NUL */
    if (need > max) return NS_FS_EPARSE;
    out[0] = '/';
    k_strcpy(out + 1, rest);
    return NS_FS_OK;
}
