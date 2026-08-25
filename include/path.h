/*
 * path.h - Filesystem Path Resolution Helpers
 *
 * Copyright (C) 2026 CazyUndee
 * SPDX-License-Identifier: AGPL-3.0
 */

#ifndef PATH_H
#define PATH_H

/*
 * resolve_path - Normalize a user-supplied path into an absolute path.
 *
 * If `name` starts with '/', it is copied verbatim.
 * Otherwise, '/' is prepended (e.g. "foo" -> "/foo").
 *
 * `out` must be at least 256 bytes. Returns 0 on success, -1 if
 * the result would exceed 255 characters.
 */
int resolve_path(char* out, const char* name);

#endif /* PATH_H */
