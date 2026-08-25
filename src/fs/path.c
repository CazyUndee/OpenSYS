/*
 * path.c - Filesystem Path Resolution Helpers
 *
 * Copyright (C) 2026 CazyUndee
 * SPDX-License-Identifier: AGPL-3.0
 */

#include "path.h"

int resolve_path(char* out, const char* name) {
    if (!name || !*name) return -1;

    int len = 0;

    if (name[0] == '/') {
        /* Absolute path — copy verbatim */
        while (name[len] && len < 255) {
            out[len] = name[len];
            len++;
        }
    } else {
        /* Relative path — prepend '/' */
        out[0] = '/';
        len = 1;
        int i = 0;
        while (name[i] && len < 255) {
            out[len] = name[i];
            len++;
            i++;
        }
    }

    out[len] = 0;
    return 0;
}
