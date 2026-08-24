/*
 * shell.c - Natural Language Shell
 *
 * Copyright (C) 2026 CazyUndee
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * Commands are natural phrases instead of cryptic abbreviations.
 */

#include <stdint.h>
#include <stddef.h>
#include "vga.h"
#include "input.h"
#include "fs.h"
#include "process.h"
#include "scheduler.h"
#include "pmm.h"
#include "ramfs.h"
#include "vfs.h"
#include "rtc.h"
#include "ui_command.h"
#include "intent_dispatcher.h"
#include "timer.h"
#include "io.h"
#include "kstring.h"
#include "path.h"
#include "net.h"
#include "ip.h"
#include "icmp.h"
#include "nl_parser.h"
#include "vfile.h"
#include "version.h"

#define MAX_CMD_LEN 256

static int cmd_equals(const char* input, const char* pattern) {
    return k_strcmp(input, pattern) == 0;
}

static char list_cur_dir[256];  /* set by cmd_list so the callback can
                                 * resolve full paths for read-only checks */

static void list_callback(const char* name, int is_dir, uint32_t size) {
    char full[256];
    if (list_cur_dir[0] && k_strcmp(list_cur_dir, "/") != 0) {
        int n = k_strlen(list_cur_dir);
        k_strcpy(full, list_cur_dir);
        full[n] = '/';
        k_strcpy(full + n + 1, name);
    } else {
        k_strcpy(full, "/");
        k_strcpy(full + 1, name);
    }
    terminal_writestring("  ");
    if (is_dir) {
        terminal_writestring("[DIR]  ");
    } else {
        if (fs_is_readonly(full) == 1) terminal_writestring("[RO]  ");
        else terminal_writestring("      ");
        terminal_put_dec(size);
        terminal_writestring(" bytes  ");
    }
    terminal_writestring_nl(name);
}

static void cmd_list(const char* name) {
    char path[256];
    if (name && *name) {
        if (resolve_path(path, name) < 0) {
            terminal_writestring_nl("  Error: path too long");
            return;
        }
    } else {
        k_strcpy(path, "/");
    }

    terminal_writestring_nl("");
    k_strcpy(list_cur_dir, path);

    /* Check virtual filesystem first */
    int vcount = vfile_list(path, list_callback);

    /* Also try real filesystem */
    int rcount = fs_readdir(path, list_callback);
    list_cur_dir[0] = 0;

    if (vcount == 0 || rcount > 0) {
        terminal_writestring_nl("");
    } else if (vcount < 0 && rcount == 0) {
        terminal_writestring_nl("  (empty)");
    } else if (vcount < 0 && rcount < 0) {
        terminal_writestring_nl("  Error: directory not found");
    }
}

static void cmd_show_memory(void) {
    uint64_t total = pmm_get_total() / (1024 * 1024);
    uint64_t free = pmm_get_free() / (1024 * 1024);

    terminal_writestring_nl("");
    terminal_writestring("  Total RAM: ");
    terminal_put_dec(total);
    terminal_writestring_nl(" MB");
    terminal_writestring("  Free RAM:  ");
    terminal_put_dec(free);
    terminal_writestring_nl(" MB");
    terminal_writestring_nl("");
}

static void cmd_pipe(void) {
    int fds[2];
    if (vfs_pipe(fds) < 0) {
        terminal_writestring_nl("  Error: could not create pipe");
        return;
    }

    terminal_writestring("  Pipe created: read fd=");
    terminal_put_dec(fds[0]);
    terminal_writestring(", write fd=");
    terminal_put_dec(fds[1]);
    terminal_writestring_nl("");

    const char* msg = "hello through pipe";
    int w = vfs_write(fds[1], msg, k_strlen(msg));
    terminal_writestring("  Wrote ");
    terminal_put_dec(w);
    terminal_writestring_nl(" bytes to pipe");

    char buf[64];
    int n = vfs_read(fds[0], buf, sizeof(buf));
    buf[n] = 0;
    terminal_writestring("  Read ");
    terminal_put_dec(n);
    terminal_writestring(" bytes: \"");
    terminal_writestring(buf);
    terminal_writestring_nl("\"");

    /* Drain: second read should be 0 (empty pipe) */
    int n2 = vfs_read(fds[0], buf, sizeof(buf));
    terminal_writestring("  Second read: ");
    terminal_put_dec(n2);
    terminal_writestring_nl(" bytes (empty pipe)");

    vfs_close(fds[0]);
    vfs_close(fds[1]);
    terminal_writestring_nl("  Pipe closed");
}

static void cmd_dup(void) {
    /* End-to-end dup2 demonstration: open a file, dup2 it onto a
     * specific fd, read through the copy, close the original, and
     * confirm the duplicated descriptor still works. */
    const char* name = "dup_test.txt";
    int fd = vfs_open(name, VFS_O_CREAT | VFS_O_RDWR);
    if (fd < 0) {
        terminal_writestring_nl("  Error: could not create dup_test.txt");
        return;
    }

    const char* payload = "dup test payload";
    int w = vfs_write(fd, payload, k_strlen(payload));
    if (w < 0) {
        terminal_writestring_nl("  Error: write failed");
        vfs_close(fd);
        return;
    }
    terminal_writestring("  Wrote ");
    terminal_put_dec(w);
    terminal_writestring_nl(" bytes");

    /* Rewind, then duplicate onto fd 5 (a free slot). */
    vfs_seek(fd, VFS_SEEK_SET, 0);
    int dupfd = vfs_dup(fd);
    if (dupfd < 0) {
        terminal_writestring_nl("  Error: dup failed");
        vfs_close(fd);
        return;
    }
    terminal_writestring("  dup(");
    terminal_put_dec(fd);
    terminal_writestring(") = ");
    terminal_put_dec(dupfd);
    terminal_writestring_nl("");

    /* Read through the duplicate — it shares the offset with the original. */
    char buf[64];
    int n = vfs_read(dupfd, buf, sizeof(buf) - 1);
    buf[n] = 0;
    terminal_writestring("  Read via dup fd ");
    terminal_put_dec(dupfd);
    terminal_writestring(": \"");
    terminal_writestring(buf);
    terminal_writestring_nl("\"");

    /* Close the original; the duplicate must survive. */
    vfs_close(fd);
    int n2 = vfs_read(dupfd, buf, sizeof(buf) - 1);
    terminal_writestring("  After closing original, dup fd read returns ");
    terminal_put_dec(n2);
    terminal_writestring_nl(" (0 = survived close)");

    vfs_close(dupfd);
    vfs_unlink(name);
    terminal_writestring_nl("  Dup test complete");
}

static void cmd_stdio(void) {
    /* Demonstrate the Unix standard descriptors: redirect fd 1 (stdout)
     * into a pipe with dup2, write through fd 1, and read it back from
     * the pipe — proving dup2/pipe compose with a process's stdout. */
    int fds[2];
    if (vfs_pipe(fds) < 0) {
        terminal_writestring_nl("  Error: could not create pipe");
        return;
    }

    /* Save the current stdout (fd 1), then point it at the pipe write end. */
    int saved_stdout = vfs_dup(1);
    if (saved_stdout < 0 || vfs_dup2(fds[1], 1) < 0) {
        terminal_writestring_nl("  Error: dup2 onto stdout failed");
        vfs_close(fds[0]);
        vfs_close(fds[1]);
        return;
    }

    const char* msg = "hello via stdout";
    int w = vfs_write(1, msg, k_strlen(msg));

    /* Restore stdout and read the redirected bytes from the pipe. */
    vfs_dup2(saved_stdout, 1);
    vfs_close(saved_stdout);

    char buf[64];
    int n = vfs_read(fds[0], buf, sizeof(buf) - 1);
    buf[n] = 0;
    vfs_close(fds[0]);
    vfs_close(fds[1]);

    terminal_writestring("  Wrote ");
    terminal_put_dec(w);
    terminal_writestring(" bytes to fd 1 (stdout) -> pipe; read back \"");
    terminal_writestring(buf);
    terminal_writestring_nl("\"");
}

static void cmd_vcat(const char* name) {
    /* Read a file through the VFS fd layer. Unlike `read`, this proves
     * virtual resources (/proc, /sys, /dev) are reachable through the
     * normal file descriptor interface, not just a shell special-case. */
    if (!name || !*name) {
        terminal_writestring_nl("  Usage: vcat <path>");
        return;
    }
    char path[256];
    if (resolve_path(path, name) < 0) {
        terminal_writestring_nl("  Error: path too long");
        return;
    }

    int fd = vfs_open(path, VFS_O_RDONLY);
    if (fd < 0) {
        terminal_writestring_nl("  Error: could not open (fd layer)");
        return;
    }

    char buf[512];
    int total = 0;
    terminal_writestring_nl("");
    int n;
    while ((n = vfs_read(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = 0;
        terminal_writestring(buf);
        total += n;
    }
    terminal_writestring_nl("");
    vfs_close(fd);
    terminal_writestring("  (");
    terminal_put_dec(total);
    terminal_writestring(" bytes via fd ");
    terminal_put_dec(fd);
    terminal_writestring_nl(")");
}

static void cmd_ps(void) {
    terminal_writestring_nl("");
    terminal_writestring_nl("  PID  Name        Active    Window");
    terminal_writestring_nl("  ---  ----------  --------  ------");

    int count = 0;
    for (int i = 0; i < 64; i++) {
        process_t* p = process_get_by_index(i);
        if (p && p->state != PROC_STATE_UNUSED) {
            terminal_writestring("  ");
            terminal_put_dec(p->pid);
            terminal_writestring("  ");
            terminal_writestring(p->name);

            int namelen = k_strlen(p->name);
            for (int j = namelen; j < 10; j++) terminal_putchar(' ');

            terminal_writestring(p->state == PROC_STATE_RUNNING ? "YES       " : "NO        ");

            terminal_put_dec(0);
            terminal_writestring_nl("");
            count++;
        }
    }

    terminal_writestring("\n  ");
    terminal_put_dec(count);
    terminal_writestring_nl(" processes total");
    terminal_writestring_nl("");
}

static void cmd_date(void) {
    rtc_time_t t;
    rtc_read_time(&t);

    terminal_writestring_nl("");
    terminal_writestring("  ");

    terminal_put_dec(t.month);
    terminal_putchar('/');
    terminal_put_dec(t.day);
    terminal_putchar('/');
    terminal_put_dec(t.century);
    terminal_put_dec(t.year);

    terminal_writestring("  ");

    if (t.hour < 10) terminal_putchar('0');
    terminal_put_dec(t.hour);
    terminal_putchar(':');
    if (t.minute < 10) terminal_putchar('0');
    terminal_put_dec(t.minute);
    terminal_putchar(':');
    if (t.second < 10) terminal_putchar('0');
    terminal_put_dec(t.second);

    terminal_writestring_nl("");
    terminal_writestring_nl("");
}

static void cmd_create_file(const char* name) {
    if (!name || !*name) {
        terminal_writestring_nl("  Usage: create <filename>");
        return;
    }
    char path[256];
    if (resolve_path(path, name) < 0) {
        terminal_writestring_nl("  Error: path too long");
        return;
    }
    
    fs_file_t* file = fs_open(path, 1);
    if (!file) {
        terminal_writestring_nl("  Error: Could not create file");
    } else {
        fs_close(file);
        terminal_writestring("  Created: ");
        terminal_writestring_nl(name);
    }
}

static void cmd_mkdir(const char* name) {
    if (!name || !*name) {
        terminal_writestring_nl("  Usage: mkdir <dirname>");
        return;
    }
    char path[256];
    if (resolve_path(path, name) < 0) {
        terminal_writestring_nl("  Error: path too long");
        return;
    }
    
    if (fs_mkdir(path) < 0) {
        terminal_writestring_nl("  Error: Could not create directory");
    } else {
        terminal_writestring("  Created directory: ");
        terminal_writestring_nl(name);
    }
}

static void cmd_delete(const char* name) {
    if (!name || !*name) {
        terminal_writestring_nl("  Usage: delete <filename>");
        return;
    }
    char path[256];
    if (resolve_path(path, name) < 0) {
        terminal_writestring_nl("  Error: path too long");
        return;
    }
    
    /* fs_unlink handles files (refuses directories); fs_rmdir handles
     * empty directories (refuses non-empty and files). */
    int r = fs_unlink(path);
    if (r == 0) {
        terminal_writestring("  Deleted: ");
        terminal_writestring_nl(name);
        return;
    }
    if (fs_rmdir(path) == 0) {
        terminal_writestring("  Deleted directory: ");
        terminal_writestring_nl(name);
        return;
    }
    if (fs_is_readonly(path) == 1) {
        terminal_writestring_nl("  Error: File is read-only (chmod +w to unprotect)");
    } else {
        terminal_writestring_nl("  Error: File not found (or directory not empty)");
    }
}

static void cmd_write_file(const char* name, const char* content) {
    if (!name || !*name) {
        terminal_writestring_nl("  Usage: write <filename> <content>");
        return;
    }
    char path[256];
    if (resolve_path(path, name) < 0) {
        terminal_writestring_nl("  Error: path too long");
        return;
    }

    /* Check virtual filesystem first */
    if (vfile_is_virtual(path)) {
        if (!vfile_is_writable(path)) {
            terminal_writestring_nl("  Error: Virtual file is read-only");
            return;
        }
        int len = k_strlen(content);
        int written = vfile_write(path, content, len);
        if (written < 0) {
            terminal_writestring_nl("  Error: Write failed");
        } else {
            terminal_writestring("  Wrote ");
            terminal_put_dec(written);
            terminal_writestring(" bytes to ");
            terminal_writestring_nl(name);
        }
        return;
    }
    
    fs_file_t* file = fs_open(path, 1);
    if (!file) {
        if (fs_is_readonly(path) == 1) {
            terminal_writestring_nl("  Error: File is read-only (chmod +w to unprotect)");
        } else {
            terminal_writestring_nl("  Error: Could not open file for writing");
        }
        return;
    }
    int len = k_strlen(content);
    size_t written = fs_write(file, content, len);
    fs_close(file);
    terminal_writestring("  Wrote ");
    terminal_put_dec(written);
    terminal_writestring(" bytes to ");
    terminal_writestring_nl(name);
}

static void cmd_read_file(const char* name) {
    if (!name || !*name) {
        terminal_writestring_nl("  Usage: read <filename>");
        return;
    }
    char path[256];
    if (resolve_path(path, name) < 0) {
        terminal_writestring_nl("  Error: path too long");
        return;
    }

    /* Check virtual filesystem first */
    char vbuf[VFILE_MAX_CONTENT];
    int vlen = vfile_read(path, vbuf, sizeof(vbuf));
    if (vlen >= 0) {
        vbuf[vlen] = 0;
        terminal_writestring_nl("");
        terminal_writestring_nl(vbuf);
        terminal_writestring_nl("");
        return;
    }
    
    fs_file_t* file = fs_open(path, 0);
    if (!file) {
        terminal_writestring_nl("  Error: File not found");
        return;
    }
    
    if (file->size == 0) {
        terminal_writestring_nl("  (empty file)");
        fs_close(file);
        return;
    }

    char buf[256];
    size_t to_read = file->size > 255 ? 255 : file->size;
    size_t read = fs_read(file, buf, to_read);
    buf[read] = 0;
    fs_close(file);

    terminal_writestring_nl("");
    terminal_writestring_nl(buf);
    terminal_writestring_nl("");
}

/* ---- Recursive directory copy ----
 * `copy <dir> <dst>` mirrors a directory tree: subdirectories are
 * recreated with fs_mkdir and every file is copied byte-for-byte.
 * If dst is an existing directory the tree lands inside it under the
 * source's basename (Unix `cp -r src dst/` semantics); otherwise dst
 * becomes the copy root. */
static char copy_dir_src_cur[256];
static char copy_dir_dst_cur[256];
static int copy_dir_errors;
static int copy_dir_file_count;
static int copy_dir_depth;

static void copy_dir_walk(const char* src, const char* dst, int depth);

static int path_has_prefix(const char* path, const char* prefix) {
    const char* p = path;
    const char* q = prefix;
    while (*q) {
        if (*p != *q) return 0;
        p++;
        q++;
    }
    return *p == '/';  /* component boundary, not a bare string prefix */
}

static const char* path_basename(const char* p) {
    const char* base = p;
    for (const char* c = p; *c; c++) {
        if (*c == '/') base = c + 1;
    }
    return base;
}

static void path_join(char* out, const char* dir, const char* name) {
    if (k_strcmp(dir, "/") == 0) {
        k_strcpy(out, "/");
        k_strcpy(out + 1, name);
    } else {
        int n = k_strlen(dir);
        k_strcpy(out, dir);
        out[n] = '/';
        k_strcpy(out + n + 1, name);
    }
}

static void copy_dir_callback(const char* name, int is_dir, uint32_t size) {
    (void)size;
    char src_child[256];
    char dst_child[256];
    path_join(src_child, copy_dir_src_cur, name);
    path_join(dst_child, copy_dir_dst_cur, name);

    if (is_dir) {
        int exists = fs_is_directory(dst_child);
        if (exists != 1 && fs_mkdir(dst_child) != 0) {
            copy_dir_errors++;
            return;
        }
        copy_dir_walk(src_child, dst_child, copy_dir_depth - 1);
        return;
    }

    /* Plain file: open, truncate any existing target, copy bytes. */
    fs_file_t* sf = fs_open(src_child, 0);
    if (!sf) { copy_dir_errors++; return; }
    fs_file_t* df = fs_open(dst_child, 1);
    if (!df) { fs_close(sf); copy_dir_errors++; return; }
    fs_truncate(df, 0);
    char buf[256];
    size_t total = 0;
    while (total < sf->size) {
        size_t to_read = sf->size - total > 256 ? 256 : sf->size - total;
        size_t r = fs_read(sf, buf, to_read);
        if (r == 0) break;
        if (fs_write(df, buf, r) != r) { copy_dir_errors++; break; }
        total += r;
    }
    fs_close(sf);
    fs_close(df);
    copy_dir_file_count++;
}

static void copy_dir_walk(const char* src, const char* dst, int depth) {
    if (depth <= 0) return;  /* guard against cycles */
    char src_saved[256];
    char dst_saved[256];
    k_strcpy(src_saved, copy_dir_src_cur);
    k_strcpy(dst_saved, copy_dir_dst_cur);
    k_strcpy(copy_dir_src_cur, src);
    k_strcpy(copy_dir_dst_cur, dst);
    copy_dir_depth = depth;
    fs_readdir(src, copy_dir_callback);
    k_strcpy(copy_dir_src_cur, src_saved);
    k_strcpy(copy_dir_dst_cur, dst_saved);
}

static void cmd_copy(const char* src, const char* dst) {
    if (!src || !*src || !dst || !*dst) {
        terminal_writestring_nl("  Usage: copy <source> <destination>");
        return;
    }
    
    char src_path[256];
    char dst_path[256];
    if (resolve_path(src_path, src) < 0 || resolve_path(dst_path, dst) < 0) {
        terminal_writestring_nl("  Error: path too long");
        return;
    }

    /* Directory source: mirror the whole tree. */
    if (fs_is_directory(src_path) == 1) {
        char dst_root[256];
        if (fs_is_directory(dst_path) == 1) {
            path_join(dst_root, dst_path, path_basename(src_path));
        } else {
            k_strcpy(dst_root, dst_path);
        }
        /* Refuse copying a directory into itself (avoids infinite descent). */
        if (k_strcmp(dst_root, src_path) == 0 ||
            path_has_prefix(dst_root, src_path)) {
            terminal_writestring_nl("  Error: cannot copy a directory into itself");
            return;
        }
        int exists = fs_is_directory(dst_root);
        if (exists != 1 && fs_mkdir(dst_root) != 0) {
            terminal_writestring_nl("  Error: could not create destination directory");
            return;
        }
        copy_dir_errors = 0;
        copy_dir_file_count = 0;
        copy_dir_walk(src_path, dst_root, 8);
        if (copy_dir_errors > 0) {
            terminal_writestring("  Error: ");
            terminal_put_dec(copy_dir_errors);
            terminal_writestring_nl(" entries failed during directory copy");
            return;
        }
        terminal_writestring("  Copied directory: ");
        terminal_writestring(dst_root);
        terminal_writestring(" (");
        terminal_put_dec(copy_dir_file_count);
        terminal_writestring_nl(" files)");
        return;
    }

    // Open source file
    fs_file_t* src_file = fs_open(src_path, 0);
    if (!src_file) {
        terminal_writestring_nl("  Error: Source file not found");
        return;
    }
    
    // Create destination file
    fs_file_t* dst_file = fs_open(dst_path, 1);
    if (!dst_file) {
        fs_close(src_file);
        terminal_writestring_nl("  Error: Could not create destination file");
        return;
    }
    /* fs_open(mode 1) does not shrink an existing larger file — truncate
     * explicitly so a shorter source cannot leave stale tail bytes. */
    fs_truncate(dst_file, 0);
    
    // Copy data
    char buf[256];
    size_t total_copied = 0;
    while (total_copied < src_file->size) {
        size_t to_read = src_file->size - total_copied > 256 ? 256 : src_file->size - total_copied;
        size_t read = fs_read(src_file, buf, to_read);
        if (read == 0) break;
        
        size_t written = fs_write(dst_file, buf, read);
        if (written != read) {
            terminal_writestring_nl("  Error: Write failed during copy");
            break;
        }
        
        total_copied += written;
    }
    
    fs_close(src_file);
    fs_close(dst_file);
    
    terminal_writestring("  Copied: ");
    terminal_put_dec(total_copied);
    terminal_writestring_nl(" bytes");
}

static void cmd_move(const char* src, const char* dst) {
    if (!src || !*src || !dst || !*dst) {
        terminal_writestring_nl("  Usage: move <source> <destination>");
        return;
    }

    // Prefer an atomic fs_rename — it preserves the read-only flag and
    // the file's data clusters (no copy+delete). Falls back to
    // copy+delete when the rename is refused (e.g. target exists).
    char src_path[256];
    char dst_path[256];
    if (resolve_path(src_path, src) < 0 || resolve_path(dst_path, dst) < 0) {
        terminal_writestring_nl("  Error: path too long");
        return;
    }

    if (fs_rename(src_path, dst_path) == 0) {
        terminal_writestring("  Moved: ");
        terminal_writestring(src);
        terminal_writestring(" -> ");
        terminal_writestring_nl(dst);
        return;
    }

    // Fallback: copy the file
    cmd_copy(src, dst);

    if (fs_unlink(src_path) >= 0) {
        terminal_writestring_nl("  Move completed (copy+delete)");
    } else {
        terminal_writestring_nl("  Warning: Original file not deleted");
    }
}

static void cmd_append(const char* name, const char* content) {
    if (!name || !*name) {
        terminal_writestring_nl("  Usage: append <filename> <content>");
        return;
    }
    
    char path[256];
    if (resolve_path(path, name) < 0) {
        terminal_writestring_nl("  Error: path too long");
        return;
    }
    
    fs_file_t* file = fs_open(path, 2);  /* mode 2 = append */
    if (!file) {
        terminal_writestring_nl("  Error: Could not open file");
        return;
    }
    
    if (content && *content) {
        size_t len = k_strlen(content);
        size_t written = fs_write(file, content, len);
        fs_close(file);
        
        terminal_writestring("  Appended ");
        terminal_put_dec(written);
        terminal_writestring_nl(" bytes");
    } else {
        fs_close(file);
        terminal_writestring_nl("  No content to append");
    }
}

static void cmd_rename(const char* old_name, const char* new_name) {
    if (!old_name || !*old_name || !new_name || !*new_name) {
        terminal_writestring_nl("  Usage: rename <oldname> <newname>");
        return;
    }
    
    // Use move command for rename
    cmd_move(old_name, new_name);
}

/* ---- Recursive find ----
 * Walks the real filesystem tree (fs_readdir) and prints the full path
 * of every entry whose name contains the pattern. Unlike the flat
 * intent-layer search (which only counts root entries), this descends
 * into subdirectories. */
static char find_current_dir[256];
static const char* find_pattern;
static int find_match_count;
static int find_depth;

static void find_walk(const char* dir, const char* pattern, int depth);

static void find_callback(const char* name, int is_dir, uint32_t size) {
    (void)size;
    if (find_pattern && k_strstr(name, find_pattern)) {
        terminal_writestring("  ");
        if (k_strcmp(find_current_dir, "/") != 0) {
            terminal_writestring(find_current_dir);
            terminal_writestring("/");
        }
        terminal_writestring_nl(name);
        find_match_count++;
    }
    if (is_dir) {
        /* Descend: join current dir + name, then recurse. */
        char child[256];
        if (k_strcmp(find_current_dir, "/") == 0) {
            k_strcpy(child, "/");
            k_strcpy(child + 1, name);
        } else {
            int base = k_strlen(find_current_dir);
            k_strcpy(child, find_current_dir);
            child[base] = '/';
            k_strcpy(child + base + 1, name);
        }
        find_walk(child, find_pattern, find_depth - 1);
    }
}

static void find_walk(const char* dir, const char* pattern, int depth) {
    (void)pattern;
    if (depth <= 0) return;  /* guard against cycles */
    char saved[256];
    k_strcpy(saved, find_current_dir);
    k_strcpy(find_current_dir, dir);
    find_depth = depth;
    fs_readdir(dir, find_callback);
    k_strcpy(find_current_dir, saved);
}

static void cmd_find(const char* pattern) {
    if (!pattern || !*pattern) {
        terminal_writestring_nl("  Usage: find <pattern>");
        return;
    }

    terminal_writestring_nl("");
    terminal_writestring("  Searching for: \"");
    terminal_writestring(pattern);
    terminal_writestring_nl("\"");

    find_pattern = pattern;
    find_match_count = 0;
    find_walk("/", pattern, 8);

    terminal_writestring("  Found ");
    terminal_put_dec(find_match_count);
    terminal_writestring_nl(" matching files");
    terminal_writestring_nl("");
}

// Change directory — dispatches INTENT_FS_CHDIR
static void cmd_chdir(const char* path) {
    if (!path || !*path) {
        terminal_writestring_nl("  Usage: go to <path>  (or: here / up / home)");
        return;
    }
    intent_t intent = intent_fs_chdir(path);
    intent.source_process_id = 2;
    error_t r = intent_dispatch(&intent);
    if (r != ERR_SUCCESS) {
        terminal_writestring_nl("  cd: path must be absolute (start with /)");
    }
}

// Go to parent directory
static void cmd_cd_parent(void) {
    intent_t intent = intent_fs_chdir_parent();
    intent.source_process_id = 2;
    intent_dispatch(&intent);
}

// Go to home (root for now)
static void cmd_cd_home(void) {
    intent_t intent = intent_fs_chdir_home();
    intent.source_process_id = 2;
    intent_dispatch(&intent);
}

// Print working directory — dispatch INTENT_FS_PRINT_CWD, read result
static void cmd_pwd(void) {
    intent_t intent = intent_fs_print_cwd();
    intent.source_process_id = 2;
    error_t r = intent_dispatch(&intent);
    if (r == ERR_SUCCESS) {
        terminal_writestring("  ");
        terminal_writestring_nl(intent.param1);
    }
}

static void cmd_echo(const char* text) {
    if (text && *text) {
        terminal_writestring("  ");
        terminal_writestring_nl(text);
    } else {
        terminal_putchar('\n');
    }
}

static void cmd_version(void) {
    terminal_writestring_nl("");
    terminal_writestring(" " PLAN0_FULL_NAME "\n");
    terminal_writestring_nl("  Plan 0 - 64-bit Operating System");
    terminal_writestring_nl("  FS - NTFS-style filesystem");
    terminal_writestring_nl("  Shell - Natural language interface");
    terminal_writestring_nl("  Copyright (C) 2026 CazyUndee");
    terminal_writestring_nl("  Licensed under GNU AGPLv3");
    terminal_writestring_nl("");
}

static void cmd_system_info(void) {
    terminal_writestring_nl("");
    terminal_writestring_nl("  System Information:");
    terminal_writestring_nl("  ------------------");
    
    // Memory info
    uint64_t total = pmm_get_total() / (1024 * 1024);
    uint64_t free = pmm_get_free() / (1024 * 1024);
    terminal_writestring("  Total RAM: ");
    terminal_put_dec(total);
    terminal_writestring(" MB, Free: ");
    terminal_put_dec(free);
    terminal_writestring_nl(" MB");
    
    // Process info
    extern int process_get_count(void);
    int proc_count = process_get_count();
    terminal_writestring("  Running processes: ");
    terminal_put_dec(proc_count);
    terminal_writestring_nl("");
    
    // Date/Time
    rtc_time_t t;
    rtc_read_time(&t);
    terminal_writestring("  Date: ");
    terminal_put_dec(t.month);
    terminal_putchar('/');
    terminal_put_dec(t.day);
    terminal_putchar('/');
    terminal_put_dec(t.century);
    terminal_put_dec(t.year);
    terminal_writestring("  Time: ");
    if (t.hour < 10) terminal_putchar('0');
    terminal_put_dec(t.hour);
    terminal_putchar(':');
    if (t.minute < 10) terminal_putchar('0');
    terminal_put_dec(t.minute);
    terminal_putchar(':');
    if (t.second < 10) terminal_putchar('0');
    terminal_put_dec(t.second);
    terminal_writestring_nl("");
    terminal_writestring_nl("");
}

static void cmd_file_info(const char* name) {
    if (!name || !*name) {
        terminal_writestring_nl("  Usage: info <filename>");
        return;
    }
    char path[256];
    if (resolve_path(path, name) < 0) {
        terminal_writestring_nl("  Error: path too long");
        return;
    }
    
    attr_filename_t st;
    if (fs_stat(path, &st) < 0) {
        terminal_writestring_nl("  Error: File not found");
        return;
    }

    terminal_writestring_nl("");
    terminal_writestring("  Name:   ");
    terminal_writestring_nl(name);
    terminal_writestring("  Size:   ");
    terminal_put_dec(st.real_size);
    terminal_writestring_nl(" bytes");
    terminal_writestring("  Type:   ");
    terminal_writestring_nl(fs_is_directory(path) == 1 ? "Directory" : "File");
    terminal_writestring("  Access: ");
    terminal_writestring_nl(fs_is_readonly(path) == 1 ? "read-only" : "read-write");
    terminal_writestring("  Parent: ");
    terminal_put_dec(st.parent_mft);
    terminal_writestring_nl("");
}

static void show_help(void) {
    terminal_writestring_nl("");
    terminal_writestring_nl("  Plan 0 understands natural language:");
    terminal_writestring_nl("    list files in documents");
    terminal_writestring_nl("    list current directory");
    terminal_writestring_nl("    create a file called notes.txt");
    terminal_writestring_nl("    write hello world to notes.txt");
    terminal_writestring_nl("    delete the file notes.txt");
    terminal_writestring_nl("    show me the memory");
    terminal_writestring_nl("");
    terminal_writestring_nl("  Commands:");
    terminal_writestring_nl("  ---------");
    terminal_writestring_nl("  File Operations:");
    terminal_writestring_nl("    list              - show all files");
    terminal_writestring_nl("    create <name>     - create new file");
    terminal_writestring_nl("    make directory <name> - create directory");
    terminal_writestring_nl("    delete <name>     - delete file or directory");
    terminal_writestring_nl("    copy <source> <destination> - copy file");
    terminal_writestring_nl("    move <source> <destination> - move file");
    terminal_writestring_nl("    rename <old> <new> - rename file");
    terminal_writestring_nl("    write <name> <text> - write text to file");
    terminal_writestring_nl("    append <name> <text> - add text to file");
    terminal_writestring_nl("    read <name>       - display file contents");
    terminal_writestring_nl("    information about <name> - show file details");
    terminal_writestring_nl("    find <pattern>    - search for files");
    terminal_writestring_nl("    go to <path>      - change directory");
    terminal_writestring_nl("    here              - print working directory");
    terminal_writestring_nl("    up                - go to parent directory");
    terminal_writestring_nl("    home              - go to home (root)");
    terminal_writestring_nl("");
    terminal_writestring_nl("  Window Management:");
    terminal_writestring_nl("    open window <app> - open application window");
    terminal_writestring_nl("    close window <id> - close window");
    terminal_writestring_nl("    move window <id> <x> <y> - move window");
    terminal_writestring_nl("    focus window <id> - focus window");
    terminal_writestring_nl("    list windows     - show all open windows");
    terminal_writestring_nl("");
    terminal_writestring_nl("  System Commands:");
    terminal_writestring_nl("    show processes    - list running processes");
    terminal_writestring_nl("    show memory       - display memory usage");
    terminal_writestring_nl("    system information - detailed system status");
    terminal_writestring_nl("    current date time - show date and time");
    terminal_writestring_nl("    version          - show OS version");
    terminal_writestring_nl("");
    terminal_writestring_nl("  Shell Utilities:");
    terminal_writestring_nl("    echo <text>       - display text");
    terminal_writestring_nl("    stdio             - redirect fd 1 (stdout) into a pipe via dup2");
    terminal_writestring_nl("    vcat <path>       - read a file through the fd layer (incl. /proc, /sys, /dev)");
    terminal_writestring_nl("    chmod <file>      - show read-only status");
    terminal_writestring_nl("    chmod -w <file>   - protect file (read-only)");
    terminal_writestring_nl("    chmod +w <file>   - unprotect file (writable)");
    terminal_writestring_nl("    <cmd> > <file>    - redirect command output to a file");
    terminal_writestring_nl("    <cmd> >> <file>   - append command output to a file");
    terminal_writestring_nl("    clear screen      - clear terminal");
    terminal_writestring_nl("    help              - show this help");
    terminal_writestring_nl("");
    terminal_writestring_nl("  Aliases & Short Commands:");
    terminal_writestring_nl("    ls                - list files");
    terminal_writestring_nl("    cat <name>        - read file (alias for read)");
    terminal_writestring_nl("    mkdir <name>      - make directory (alias)");
    terminal_writestring_nl("    touch <name>      - create empty file (alias)");
    terminal_writestring_nl("    rm <name>         - delete file (alias)");
    terminal_writestring_nl("    cp <src> <dst>    - copy file (alias)");
    terminal_writestring_nl("    mv <src> <dst>    - move file (alias)");
    terminal_writestring_nl("    cd <path>         - change directory (alias)");
    terminal_writestring_nl("    pwd               - print working directory (alias)");
    terminal_writestring_nl("    edit <name>       - simple text editor");
terminal_writestring_nl(" ping <addr> - ping an address");
    terminal_writestring_nl("    uptime            - show system uptime");
    terminal_writestring_nl("    mount             - show mounted filesystems");
    terminal_writestring_nl("    df                - disk free (filesystem usage)");
    terminal_writestring_nl("    ps                - list processes (alias for show processes)");
    terminal_writestring_nl("    reboot            - reboot the system");
    terminal_writestring_nl("    shutdown          - shutdown the system");
    terminal_writestring_nl("    whoami            - show current user");
    terminal_writestring_nl("    uname             - show system name");
    terminal_writestring_nl("    env               - show environment variables");
    terminal_writestring_nl("    history           - show command history");
    terminal_writestring_nl("    clear             - clear the screen");
    terminal_writestring_nl("");
}

#define HISTORY_SIZE 16
#define HISTORY_MAX_LEN 256
static char history[HISTORY_SIZE][HISTORY_MAX_LEN];
static int history_count = 0;

static void history_add(const char* cmd) {
    if (!cmd || !*cmd) return;
    /* Consecutive-dedup: re-running a recalled command must not push a
     * copy of itself, or up-arrow recall would land on the duplicate. */
    if (history_count > 0) {
        const char* last = history[(history_count - 1) % HISTORY_SIZE];
        int same = 1;
        for (int i = 0; ; i++) {
            if (cmd[i] != last[i]) { same = 0; break; }
            if (cmd[i] == 0) break;
        }
        if (same) return;
    }
    int idx = history_count % HISTORY_SIZE;
    int i = 0;
    while (cmd[i] && i < HISTORY_MAX_LEN - 1) {
        history[idx][i] = cmd[i];
        i++;
    }
    history[idx][i] = 0;
    history_count++;
}

static void cmd_history(void) {
    terminal_writestring_nl("");
    int start = (history_count > HISTORY_SIZE) ? history_count - HISTORY_SIZE : 0;
    for (int n = start; n < history_count; n++) {
        int idx = n % HISTORY_SIZE;
        terminal_writestring("  ");
        terminal_put_dec(n - start + 1);
        terminal_writestring("  ");
        terminal_writestring_nl(history[idx]);
    }
    terminal_writestring_nl("");
}

static void cmd_edit(const char* filename) {
    if (!filename || !*filename) {
        terminal_writestring_nl("  Usage: edit <filename>");
        return;
    }
    char path[256];
    if (resolve_path(path, filename) < 0) {
        terminal_writestring_nl("  Error: path too long");
        return;
    }
    fs_file_t* file = fs_open(path, 1);
    if (!file) {
        terminal_writestring_nl("  Error: Could not open file for editing");
        return;
    }
    terminal_writestring("  Editing: ");
    terminal_writestring(filename);
    terminal_writestring_nl(" (type a single '.' on a line to finish)");
    terminal_writestring_nl("  ");
    char line_buf[256];
    int line_pos = 0;
    while (1) {
        if (keyboard_has_key()) {
            char c = keyboard_getc();
            if (c == '\n') {
                line_buf[line_pos] = 0;
                if (line_pos == 1 && line_buf[0] == '.') break;
                line_buf[line_pos] = '\n';
                fs_write(file, line_buf, line_pos + 1);
                line_pos = 0;
                terminal_writestring("  ");
            } else if (c == '\b' && line_pos > 0) {
                line_pos--;
                terminal_putchar('\b');
                terminal_putchar(' ');
                terminal_putchar('\b');
            } else if (c >= ' ' && line_pos < 254) {
                line_buf[line_pos++] = c;
                terminal_putchar(c);
            }
        }
        for (volatile int i = 0; i < 1000; i++);
    }
    fs_close(file);
    terminal_writestring_nl("");
    terminal_writestring_nl("  Done.");
}

static void cmd_uptime(void) {
    uint64_t ticks = timer_get_ticks();
    uint64_t seconds = ticks / 1000;
    uint64_t minutes = seconds / 60;
    uint64_t hours = minutes / 60;
    terminal_writestring_nl("");
    terminal_writestring("  Uptime: ");
    terminal_put_dec(hours);
    terminal_writestring(" hours, ");
    terminal_put_dec(minutes % 60);
    terminal_writestring(" minutes, ");
    terminal_put_dec(seconds % 60);
    terminal_writestring_nl(" seconds");
    terminal_writestring_nl("");
}

static void cmd_reboot(void) {
    terminal_writestring_nl("  Rebooting...");
    /* QEMU / common reboot: write 0xFE to keyboard controller */
    outb(0x64, 0xFE);
    /* Fallback: infinite loop if that didn't work */
    while (1) {
        __asm__ volatile("hlt");
    }
}

static void cmd_shutdown(void) {
    terminal_writestring_nl("  Shutting down...");
    /* QEMU ACPI shutdown port */
    outw(0x604, 0x2000);
    /* Bochs / older QEMU alternative */
    outw(0xB004, 0x2000);
    while (1) {
        __asm__ volatile("hlt");
    }
}

static void cmd_whoami(void) {
    terminal_writestring_nl("  root");
}

static void cmd_uname(void) {
    terminal_writestring("  " PLAN0_FULL_NAME);
}

static void cmd_env(void) {
    terminal_writestring_nl("");
    terminal_writestring_nl("  PATH=/bin:/usr/bin");
    terminal_writestring_nl("  HOME=/");
    terminal_writestring_nl("  USER=root");
    terminal_writestring_nl("  TERM=shell");
    terminal_writestring_nl("");
}

static void cmd_ping(const char* addr) {
  if (!addr || !*addr) {
    terminal_writestring_nl(" Usage: ping <ip-address>");
    return;
  }

  /* Parse d.d.d.d into ip_addr_t */
  ip_addr_t dst = {0};
  int octet = 0, digit_idx = 0;
  char digits[4];
  for (int i = 0; addr[i] && octet < 4; i++) {
    if (addr[i] >= '0' && addr[i] <= '9') {
      if (digit_idx < 3) digits[digit_idx++] = addr[i];
    } else if (addr[i] == '.' && digit_idx > 0) {
      int val = 0;
      for (int j = 0; j < digit_idx; j++) val = val * 10 + (digits[j] - '0');
      dst.addr[octet++] = (uint8_t)val;
      digit_idx = 0;
    }
  }
  if (digit_idx > 0 && octet < 4) {
    int val = 0;
    for (int j = 0; j < digit_idx; j++) val = val * 10 + (digits[j] - '0');
    dst.addr[octet++] = (uint8_t)val;
  }

  if (octet != 4) {
    terminal_writestring_nl(" Usage: ping <ip-address>  e.g. ping 192.168.1.1");
    return;
  }

  terminal_writestring(" Pinging ");
  terminal_writestring(addr);
  terminal_writestring_nl("...");

  icmp_ping_start(1);
  if (icmp_send_echo_request(dst, 0xABCD, 1) < 0) {
    terminal_writestring_nl(" Failed to send echo request");
    return;
  }

  /* Poll for reply (~2 seconds) */
  uint32_t start = timer_get_ms();
  while (timer_get_ms() - start < 2000) {
    uint8_t buf[256];
    int pkt_len = net_recv_packet(buf, sizeof(buf));
    if (pkt_len > 0) {
      net_handle_packet(buf, (uint16_t)pkt_len);
      if (icmp_ping_has_reply()) {
        terminal_writestring(" Reply from ");
        terminal_writestring(addr);
        terminal_writestring_nl(": time ok");
        return;
      }
    }
    for (volatile int i = 0; i < 10000; i++);
  }

  terminal_writestring(" No reply from ");
  terminal_writestring(addr);
  terminal_writestring_nl(" (timeout)");
}

static void cmd_clear(void) {
    terminal_clear();
}

static void cmd_mount(void) {
    char buf[512];
    int len = vfile_read("/proc/mounts", buf, sizeof(buf));
    if (len > 0) {
        buf[len] = 0;
        terminal_writestring_nl("");
        terminal_writestring_nl(buf);
    } else {
        terminal_writestring_nl("  Error: could not read mount table");
    }
}

static void cmd_df(void) {
    terminal_writestring_nl("");
    terminal_writestring_nl("  Filesystem      Size      Used      Avail     Mount");
    terminal_writestring_nl("  --------------  --------  --------  --------  -------");

    /* The shell's real file operations go through fs.c (the NTFS-style
     * filesystem, kept in memory when no disk is attached). Report its
     * actual usage from the cluster bitmap and MFT. */
    fs_stats_t fst;
    if (fs_get_stats(&fst) == 0) {
        terminal_writestring("  fs              ");
        terminal_put_dec(fst.total_bytes / 1024);
        terminal_writestring(" KB    ");
        terminal_put_dec(fst.used_bytes / 1024);
        terminal_writestring(" KB    ");
        terminal_put_dec(fst.free_bytes / 1024);
        terminal_writestring(" KB    /\n");

        terminal_writestring("  ");
        terminal_put_dec(fst.file_count);
        terminal_writestring(" files, ");
        terminal_put_dec(fst.dir_count);
        terminal_writestring(" directories\n");
    } else {
        /* Fallback: filesystem not mounted yet */
        ramfs_stats_t rs;
        ramfs_get_stats(&rs);
        terminal_writestring("  ramfs           ");
        terminal_put_dec(rs.total_capacity / 1024);
        terminal_writestring(" KB    ");
        terminal_put_dec(rs.used_bytes / 1024);
        terminal_writestring(" KB    ");
        terminal_put_dec(rs.free_bytes / 1024);
        terminal_writestring(" KB    /\n");
    }

    /* vfile */
    terminal_writestring("  vfile           0 KB      0 KB      0 KB      /proc,/sys,/dev\n");
    terminal_writestring_nl("");
}

/* chmod — toggle the read-only flag on a real (fs.c) file.
 *   chmod <file>        show status
 *   chmod +w <file>     make writable
 *   chmod -w <file>     make read-only (protect)
 */
static void cmd_chmod(const char* arg1, const char* arg2) {
    const char* target = arg1;
    int set_ro = -1;  /* -1 = show status */

    if (arg1 && arg2 && *arg2) {
        if (k_strcmp(arg1, "+w") == 0) {
            set_ro = 0;
            target = arg2;
        } else if (k_strcmp(arg1, "-w") == 0) {
            set_ro = 1;
            target = arg2;
        } else {
            terminal_writestring_nl("  Usage: chmod <file> | chmod +w <file> | chmod -w <file>");
            return;
        }
    }

    if (!target || !*target) {
        terminal_writestring_nl("  Usage: chmod <file> | chmod +w <file> | chmod -w <file>");
        return;
    }

    char path[256];
    if (resolve_path(path, target) < 0) {
        terminal_writestring_nl("  Error: path too long");
        return;
    }

    if (set_ro >= 0) {
        if (fs_set_readonly(path, set_ro) < 0) {
            terminal_writestring_nl("  Error: file not found");
            return;
        }
    }

    int ro = fs_is_readonly(path);
    if (ro < 0) {
        terminal_writestring_nl("  Error: file not found");
        return;
    }

    terminal_writestring("  ");
    terminal_writestring(target);
    terminal_writestring(ro ? ": read-only" : ": writable");
    terminal_writestring_nl("");
}

/* Dispatch a canonical (verb, arg1, arg2) triple. Used both by the plain
 * token-based parser and by the natural-language parser (nl_parser.c). */
static void dispatch(char* cmd, char* arg1, char* arg2) {
    /* Multi-word commands — matched token-wise BEFORE their single-token
     * prefixes so that e.g. "move window 1 10 20" does not hit "move". */
    if (cmd_equals(cmd, "open") && (k_strcmp(arg1, "window") == 0 || k_strcmp(arg1, "application") == 0)) {
        cmd_open_window(arg2);
    }
    else if (cmd_equals(cmd, "close") && k_strcmp(arg1, "window") == 0) {
        cmd_close_window(arg2);
    }
    else if (cmd_equals(cmd, "move") && k_strcmp(arg1, "window") == 0) {
        char* win_id = arg2;
        char* x_str = win_id;
        while (*x_str && *x_str != ' ') x_str++;
        if (*x_str == ' ') {
            *x_str = 0;
            x_str++;
            while (*x_str == ' ') x_str++;
        }
        char* y_str = x_str;
        while (*y_str && *y_str != ' ') y_str++;
        if (*y_str == ' ') {
            *y_str = 0;
            y_str++;
            while (*y_str == ' ') y_str++;
        }
        
        if (*win_id && *x_str && *y_str) {
            int x = 0, y = 0;
            char* p = x_str;
            while (*p >= '0' && *p <= '9') {
                x = x * 10 + (*p - '0');
                p++;
            }
            p = y_str;
            while (*p >= '0' && *p <= '9') {
                y = y * 10 + (*p - '0');
                p++;
            }
            cmd_move_window(win_id, x, y);
        } else {
            terminal_writestring_nl("  Usage: move window <id> <x> <y>");
        }
    }
    else if (cmd_equals(cmd, "focus") && k_strcmp(arg1, "window") == 0) {
        cmd_focus_window(arg2);
    }
    else if ((cmd_equals(cmd, "list") || cmd_equals(cmd, "show")) && k_strcmp(arg1, "windows") == 0) {
        cmd_list_windows();
    }
    else if (cmd_equals(cmd, "show") && k_strcmp(arg1, "processes") == 0) {
        cmd_ps();
    }
    else if (cmd_equals(cmd, "show") && k_strcmp(arg1, "memory") == 0) {
        cmd_show_memory();
    }
    else if (cmd_equals(cmd, "system") && k_strcmp(arg1, "information") == 0) {
        cmd_system_info();
    }
    else if (cmd_equals(cmd, "current") && k_strcmp(arg1, "date") == 0 && k_strcmp(arg2, "time") == 0) {
        cmd_date();
    }
    else if (cmd_equals(cmd, "date") && k_strcmp(arg1, "time") == 0) {
        cmd_date();
    }
    else if (cmd_equals(cmd, "make") && k_strcmp(arg1, "directory") == 0) {
        cmd_mkdir(arg2);
    }
    else if (cmd_equals(cmd, "information") && k_strcmp(arg1, "about") == 0) {
        cmd_file_info(arg2);
    }
    else if (cmd_equals(cmd, "info") && arg1 && *arg1 && (!arg2 || !*arg2)) {
        cmd_file_info(arg1);
    }
    else if (cmd_equals(cmd, "clear") && k_strcmp(arg1, "screen") == 0) {
        terminal_clear();
    }
    else if (cmd_equals(cmd, "go") && k_strcmp(arg1, "to") == 0) {
        cmd_chdir(arg2);
    }
    /* Single-word commands */
    else if (cmd_equals(cmd, "list")) {
        cmd_list(arg1);
    }
    else if (cmd_equals(cmd, "create")) {
        cmd_create_file(arg1);
    }
    else if (cmd_equals(cmd, "delete")) {
        cmd_delete(arg1);
    }
    else if (cmd_equals(cmd, "copy")) {
        cmd_copy(arg1, arg2);
    }
    else if (cmd_equals(cmd, "move")) {
        cmd_move(arg1, arg2);
    }
    else if (cmd_equals(cmd, "rename")) {
        cmd_rename(arg1, arg2);
    }
    else if (cmd_equals(cmd, "write")) {
        cmd_write_file(arg1, arg2);
    }
    else if (cmd_equals(cmd, "append")) {
        cmd_append(arg1, arg2);
    }
    else if (cmd_equals(cmd, "read")) {
        cmd_read_file(arg1);
    }
    else if (cmd_equals(cmd, "find")) {
        cmd_find(arg1);
    }
    else if (cmd_equals(cmd, "here")) {
        cmd_pwd();
    }
    else if (cmd_equals(cmd, "up")) {
        cmd_cd_parent();
    }
    else if (cmd_equals(cmd, "home")) {
        cmd_cd_home();
    }
    else if (cmd_equals(cmd, "echo")) {
        cmd_echo(arg1);
    }
    else if (cmd_equals(cmd, "version")) {
        cmd_version();
    }
    /* New aliases and short commands */
    else if (cmd_equals(cmd, "ls")) {
        cmd_list(arg1);
    }
    else if (cmd_equals(cmd, "cat")) {
        cmd_read_file(arg1);
    }
    else if (cmd_equals(cmd, "mkdir")) {
        cmd_mkdir(arg1);
    }
    else if (cmd_equals(cmd, "touch")) {
        cmd_create_file(arg1);
    }
    else if (cmd_equals(cmd, "rm")) {
        cmd_delete(arg1);
    }
    else if (cmd_equals(cmd, "cp")) {
        cmd_copy(arg1, arg2);
    }
    else if (cmd_equals(cmd, "mv")) {
        cmd_move(arg1, arg2);
    }
    else if (cmd_equals(cmd, "cd")) {
        cmd_chdir(arg1);
    }
    else if (cmd_equals(cmd, "pwd")) {
        cmd_pwd();
    }
    else if (cmd_equals(cmd, "edit")) {
        cmd_edit(arg1);
    }
    else if (cmd_equals(cmd, "ping")) {
        cmd_ping(arg1);
    }
    else if (cmd_equals(cmd, "uptime")) {
        cmd_uptime();
    }
    else if (cmd_equals(cmd, "reboot")) {
        cmd_reboot();
    }
    else if (cmd_equals(cmd, "shutdown")) {
        cmd_shutdown();
    }
    else if (cmd_equals(cmd, "whoami")) {
        cmd_whoami();
    }
    else if (cmd_equals(cmd, "uname")) {
        cmd_uname();
    }
    else if (cmd_equals(cmd, "env")) {
        cmd_env();
    }
    else if (cmd_equals(cmd, "history")) {
        cmd_history();
    }
    else if (cmd_equals(cmd, "mount")) {
        cmd_mount();
    }
    else if (cmd_equals(cmd, "df")) {
        cmd_df();
    }
    else if (cmd_equals(cmd, "ps")) {
        cmd_ps();
    }
    else if (cmd_equals(cmd, "pipe")) {
        cmd_pipe();
    }
    else if (cmd_equals(cmd, "dup")) {
        cmd_dup();
    }
    else if (cmd_equals(cmd, "stdio")) {
        cmd_stdio();
    }
    else if (cmd_equals(cmd, "vcat")) {
        cmd_vcat(arg1);
    }
    else if (cmd_equals(cmd, "chmod")) {
        cmd_chmod(arg1, arg2);
    }
    else if (cmd_equals(cmd, "clear")) {
        cmd_clear();
    }
    else if (cmd_equals(cmd, "help")) {
        show_help();
    }
    else {
        terminal_writestring("  Unknown command: \"");
        terminal_writestring(cmd);
        terminal_writestring_nl("\"");
        terminal_writestring_nl("  Type 'help' for available commands.");
    }
}

static void process_command(char* cmd) {
    cmd = k_trim(cmd);
    if (k_strlen(cmd) == 0) return;

    /* Shell redirection: `cmd > file` (truncate) or `cmd >> file` (append).
     * Find a standalone '>' token, split the line there, run the command
     * with terminal output captured, then write the captured text to the
     * target (virtual file or real fs file). */
    {
        char* redir = 0;
        int append = 0;
        for (char* p = cmd; *p; p++) {
            if (*p == '>' && (p == cmd || *(p - 1) == ' ')) {
                redir = p;
                if (*(p + 1) == '>') append = 1;
                break;
            }
        }
        if (redir) {
            char* file_part = redir;
            *file_part = 0;          /* terminate the command part at the '>' */
            file_part++;
            if (append) file_part++; /* skip the second '>' of '>>' */
            while (*file_part == '>') file_part++;
            file_part = k_trim(file_part);

            if (k_strlen(file_part) == 0) {
                terminal_writestring_nl("  Error: redirection needs a target file");
                return;
            }

            /* Run the command with output captured (screen + serial silent). */
            char cap[8192];
            terminal_capture_begin(cap, sizeof(cap));
            process_command(cmd);
            size_t captured = terminal_capture_end();

            char path[256];
            if (resolve_path(path, file_part) < 0) {
                terminal_writestring_nl("  Error: path too long");
                return;
            }

            size_t written = 0;
            if (vfile_is_virtual(path)) {
                if (!vfile_is_writable(path)) {
                    terminal_writestring_nl("  Error: virtual target is read-only");
                    return;
                }
                written = (size_t)vfile_write(path, cap, (int)captured);
            } else {
                fs_file_t* file = fs_open(path, append ? 2 : 1);
                if (!file) {
                    terminal_writestring_nl("  Error: could not open target for writing");
                    return;
                }
                written = fs_write(file, cap, captured);
                fs_close(file);
            }

            terminal_writestring("  Redirected ");
            terminal_put_dec(written);
            terminal_writestring(" bytes to ");
            terminal_writestring_nl(file_part);
            return;
        }
    }

    /* Natural-language layer: translate English-like phrases such as
     * "list files in documents" or "write hello world to notes.txt"
     * into the canonical (verb, arg1, arg2) triple. If the phrase is
     * not recognized or is ambiguous, fall through to the plain
     * token-based parser so existing syntax keeps working. */
    char nl_cmd[32];
    char nl_arg1[256];
    char nl_arg2[256];
    if (nl_parse(cmd, nl_cmd, nl_arg1, nl_arg2) == 0) {
        dispatch(nl_cmd, nl_arg1, nl_arg2);
        return;
    }

    char* arg1 = cmd;
    while (*arg1 && *arg1 != ' ') arg1++;
    if (*arg1 == ' ') {
        *arg1 = 0;
        arg1++;
        while (*arg1 == ' ') arg1++;
    }

    char* arg2 = arg1;
    while (*arg2 && *arg2 != ' ') arg2++;
    if (*arg2 == ' ') {
        *arg2 = 0;
        arg2++;
        while (*arg2 == ' ') arg2++;
    }

    dispatch(cmd, arg1, arg2);
}

void shell_run(void) {
    char cmd_buffer[MAX_CMD_LEN];
    int pos = 0;

    // Initialize unified UI system
    ui_command_init();

    terminal_clear();
    terminal_writestring_nl(PLAN0_FULL_NAME);
    terminal_writestring_nl("Filesystem ready.");
    terminal_writestring_nl("Unified UI system initialized.");
    terminal_writestring_nl("Type 'help' for commands.\n");

    while (1) {
        terminal_writestring("> ");

        pos = 0;
        int hist_pos = history_count;  /* == count means editing fresh input */
        while (1) {
            if (keyboard_has_key()) {
                char c = keyboard_getc();

                if (c == '\n') {
                    terminal_putchar('\n');
                    cmd_buffer[pos] = 0;
                    break;
                } else if (c == '\b' && pos > 0) {
                    pos--;
                    terminal_putchar('\b');
                    terminal_putchar(' ');
                    terminal_putchar('\b');
                } else if ((c == '\x01' || c == '\x02') && history_count > 0) {
                    /* Up (0x01) / Down (0x02): recall command history.
                     * Erase the whole line, repaint prompt + entry, and
                     * pad with spaces when the recalled line is shorter. */
                    int old_pos = pos;
                    if (c == '\x01') {
                        if (hist_pos > 0) hist_pos--;
                    } else {
                        if (hist_pos < history_count) hist_pos++;
                    }
                    for (int i = 0; i < old_pos + 2; i++) {
                        terminal_putchar('\b');
                        terminal_putchar(' ');
                        terminal_putchar('\b');
                    }
                    terminal_writestring("> ");
                    pos = 0;
                    if (hist_pos < history_count) {
                        const char* h = history[hist_pos % HISTORY_SIZE];
                        while (h[pos] && pos < MAX_CMD_LEN - 1) {
                            cmd_buffer[pos] = h[pos];
                            terminal_putchar(h[pos]);
                            pos++;
                        }
                    }
                    for (int i = pos; i < old_pos; i++) terminal_putchar(' ');
                    for (int i = pos; i < old_pos; i++) terminal_putchar('\b');
                    cmd_buffer[pos] = 0;
                } else if (c >= ' ' && pos < MAX_CMD_LEN - 1) {
                    cmd_buffer[pos++] = c;
                    terminal_putchar(c);
                }
            }

            for (volatile int i = 0; i < 1000; i++);
        }

        history_add(cmd_buffer);
        process_command(cmd_buffer);
    }
}
