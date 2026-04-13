/*
 * shell.c - Natural Language Shell
 *
 * Commands are natural phrases instead of cryptic abbreviations.
 */

#include <stdint.h>
#include <stddef.h>
#include "../include/ps2_keyboard.h"
#include "../include/ramfs.h"
#include "../include/process.h"
#include "../include/scheduler.h"

#define MAX_CMD_LEN 256

/* VGA output */
static volatile uint16_t* vga = (volatile uint16_t*)0xB8000;
static int cursor_x = 0;
static int cursor_y = 0;

/* Current working directory */
static char cwd[256] = "/";

/* External functions */
extern uint64_t pmm_get_total(void);
extern uint64_t pmm_get_free(void);

static void putc(char c) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
        if (cursor_y >= 25) {
            for (int y = 0; y < 24; y++) {
                for (int x = 0; x < 80; x++) {
                    vga[y * 80 + x] = vga[(y + 1) * 80 + x];
                }
            }
            for (int x = 0; x < 80; x++) {
                vga[24 * 80 + x] = 0x0720;
            }
            cursor_y = 24;
        }
    } else {
        vga[cursor_y * 80 + cursor_x] = (uint16_t)c | 0x0700;
        cursor_x++;
        if (cursor_x >= 80) { cursor_x = 0; cursor_y++; }
    }
}

static void puts(const char* s) { while (*s) putc(*s++); }
static void puts_nl(const char* s) { puts(s); putc('\n'); }

static void clear_screen(void) {
    for (int i = 0; i < 80 * 25; i++) vga[i] = 0x0720;
    cursor_x = 0;
    cursor_y = 0;
}

static void put_dec(uint64_t n) {
    char buf[24];
    int i = 23;
    buf[i] = 0;
    if (n == 0) { puts("0"); return; }
    while (n > 0) { buf[--i] = '0' + (n % 10); n /= 10; }
    puts(&buf[i]);
}

static int strlen(const char* s) { int n = 0; while (*s++) n++; return n; }
static int strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return *a - *b;
}
static int strncmp(const char* a, const char* b, int n) {
    while (n-- && *a && *a == *b) { a++; b++; }
    return n < 0 ? 0 : *a - *b;
}
static char* trim(char* s) {
    while (*s == ' ') s++;
    char* end = s + strlen(s) - 1;
    while (end > s && *end == ' ') *end-- = 0;
    return s;
}

static int cmd_match(const char* input, const char* pattern) {
    while (*input && *pattern) {
        char a = *input, b = *pattern;
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return 0;
        input++; pattern++;
    }
    return 1;
}

static int cmd_starts(const char* input, const char* pattern) {
    return strncmp(input, pattern, strlen(pattern)) == 0;
}

static int cmd_equals(const char* input, const char* pattern) {
    return strcmp(input, pattern) == 0;
}

/* ========== FILE LISTING CALLBACK ========== */
static void list_callback(const char* name, int is_dir, uint32_t size) {
    puts("  ");
    if (is_dir) {
        puts("[DIR]  ");
    } else {
        puts("       ");
        put_dec(size);
        puts(" bytes  ");
    }
    puts_nl(name);
}

/* ========== COMMANDS ========== */

static void cmd_list(void) {
    int count = ramfs_get_file_count();
    if (count == 0) {
        puts_nl("  (empty filesystem)");
        return;
    }
    puts_nl("");
    ramfs_list(list_callback);
    puts_nl("");
}

static void cmd_show_memory(void) {
    uint64_t total = pmm_get_total() / (1024 * 1024);
    uint64_t free = pmm_get_free() / (1024 * 1024);

    puts_nl("");
    puts("  Total RAM: ");
    put_dec(total);
    puts_nl(" MB");
    puts("  Free RAM:  ");
    put_dec(free);
    puts_nl(" MB");
    puts_nl("");
}

static void cmd_ps(void) {
    puts_nl("");
    puts_nl("  PID  Name       State     CPU Time");
    puts_nl("  ---  ---------  --------  --------");
    
    extern process_t* process_get_by_index(int i);
    extern int process_get_count(void);
    
    int count = 0;
    for (int i = 0; i < 64; i++) {
        process_t* p = process_get_by_index(i);
        if (p && p->state != 0) {
            puts("  ");
            put_dec(p->pid);
            puts("  ");
            puts(p->name);
            
            int namelen = strlen(p->name);
            for (int j = namelen; j < 10; j++) putc(' ');
            
            const char* state_str = "???";
            if (p->state == 1) state_str = "READY";
            else if (p->state == 2) state_str = "RUNNING";
            else if (p->state == 3) state_str = "BLOCKED";
            else if (p->state == 4) state_str = "ZOMBIE";
            puts(state_str);
            
            puts("  ");
            put_dec(p->cpu_time);
            puts("ms");
            putc('\n');
            count++;
        }
    }
    
    puts("\n  ");
    put_dec(count);
    puts_nl(" processes total");
    puts_nl("");
}

static void cmd_create_file(const char* name) {
    if (!name || !*name) {
        puts_nl("  Usage: create <filename>");
        return;
    }
    int fd = ramfs_create(name);
    if (fd < 0) {
        puts_nl("  Error: Could not create file (filesystem full?)");
    } else {
        puts("  Created: ");
        puts_nl(name);
    }
}

static void cmd_mkdir(const char* name) {
    if (!name || !*name) {
        puts_nl("  Usage: mkdir <dirname>");
        return;
    }
    int result = ramfs_mkdir(name);
    if (result < 0) {
        puts_nl("  Error: Could not create directory");
    } else {
        puts("  Created directory: ");
        puts_nl(name);
    }
}

static void cmd_delete(const char* name) {
    if (!name || !*name) {
        puts_nl("  Usage: delete <filename>");
        return;
    }
    int result = ramfs_delete(name);
    if (result < 0) {
        puts_nl("  Error: File not found");
    } else {
        puts("  Deleted: ");
        puts_nl(name);
    }
}

static void cmd_write_file(const char* name, const char* content) {
    if (!name || !*name) {
        puts_nl("  Usage: write <filename> <content>");
        return;
    }
    int fd = ramfs_find(name);
    if (fd < 0) {
        puts_nl("  Error: File not found");
        return;
    }
    int len = strlen(content);
    ramfs_write(fd, content, len);
    puts("  Wrote ");
    put_dec(len);
    puts(" bytes to ");
    puts_nl(name);
}

static void cmd_read_file(const char* name) {
    if (!name || !*name) {
        puts_nl("  Usage: read <filename>");
        return;
    }
    int fd = ramfs_find(name);
    if (fd < 0) {
        puts_nl("  Error: File not found");
        return;
    }
    uint32_t size = ramfs_size(fd);
    if (size == 0) {
        puts_nl("  (empty file)");
        return;
    }
    
    char buf[256];
    uint32_t to_read = size > 255 ? 255 : size;
    ramfs_read(fd, buf, to_read, 0);
    buf[to_read] = 0;
    
    puts_nl("");
    puts_nl(buf);
    puts_nl("");
}

static void cmd_file_info(const char* name) {
    if (!name || !*name) {
        puts_nl("  Usage: info <filename>");
        return;
    }
    int fd = ramfs_find(name);
    if (fd < 0) {
        puts_nl("  Error: File not found");
        return;
    }
    
    puts_nl("");
    puts("  Name: ");
    puts_nl(ramfs_name(fd));
    puts("  Size: ");
    put_dec(ramfs_size(fd));
    puts_nl(" bytes");
    puts("  Type: ");
    puts_nl(ramfs_is_dir(fd) ? "Directory" : "File");
    puts_nl("");
}

static void show_help(void) {
    puts_nl("");
    puts_nl("  Natural Language Commands:");
    puts_nl("  -------------------------");
    puts_nl("  list        - show all files");
    puts_nl("  create <n>  - create new file");
    puts_nl("  mkdir <n>   - create directory");
    puts_nl("  delete <n>  - delete file/dir");
    puts_nl("  write <n> <text> - write to file");
    puts_nl("  read <n>    - display file contents");
    puts_nl("  info <n>    - show file information");
    puts_nl("  ps          - list processes");
    puts_nl("  memory      - show memory usage");
    puts_nl("  clear       - clear screen");
    puts_nl("  help        - show this help");
    puts_nl("");
}

static void process_command(char* cmd) {
    cmd = trim(cmd);
    if (strlen(cmd) == 0) return;

    /* Skip leading word to find argument */
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

    if (cmd_equals(cmd, "list") || cmd_equals(cmd, "ls") || cmd_equals(cmd, "dir")) {
        cmd_list();
    }
    else if (cmd_equals(cmd, "ps") || cmd_equals(cmd, "procs") || cmd_equals(cmd, "processes")) {
        cmd_ps();
    }
    else if (cmd_equals(cmd, "create") || cmd_equals(cmd, "touch") || cmd_equals(cmd, "make")) {
        cmd_create_file(arg1);
    }
    else if (cmd_equals(cmd, "mkdir")) {
        cmd_mkdir(arg1);
    }
    else if (cmd_equals(cmd, "delete") || cmd_equals(cmd, "rm") || cmd_equals(cmd, "del")) {
        cmd_delete(arg1);
    }
    else if (cmd_equals(cmd, "write")) {
        cmd_write_file(arg1, arg2);
    }
    else if (cmd_equals(cmd, "read") || cmd_equals(cmd, "cat") || cmd_equals(cmd, "type")) {
        cmd_read_file(arg1);
    }
    else if (cmd_equals(cmd, "info")) {
        cmd_file_info(arg1);
    }
    else if (cmd_equals(cmd, "memory") || cmd_equals(cmd, "mem")) {
        cmd_show_memory();
    }
    else if (cmd_equals(cmd, "clear") || cmd_equals(cmd, "cls")) {
        clear_screen();
    }
    else if (cmd_equals(cmd, "help") || cmd_equals(cmd, "?")) {
        show_help();
    }
    else {
        puts("  Unknown command: \"");
        puts(cmd);
        puts_nl("\"");
        puts_nl("  Type 'help' for available commands.");
    }
}

/* ========== MAIN ========== */

void shell_run(void) {
    char cmd_buffer[MAX_CMD_LEN];
    int pos = 0;

    clear_screen();
    puts_nl("OpenSYS Natural Shell v1.0");
    puts_nl("RAM-based filesystem ready.");
    puts_nl("Type 'help' for commands.\n");

    while (1) {
        puts("> ");

        pos = 0;
        while (1) {
            if (ps2_keyboard_has_key()) {
                char c = ps2_keyboard_getc();

                if (c == '\n') {
                    putc('\n');
                    cmd_buffer[pos] = 0;
                    break;
                } else if (c == '\b' && pos > 0) {
                    pos--;
                    putc('\b');
                    putc(' ');
                    putc('\b');
                } else if (c >= ' ' && pos < MAX_CMD_LEN - 1) {
                    cmd_buffer[pos++] = c;
                    putc(c);
                }
            }

            for (volatile int i = 0; i < 1000; i++);
        }

        process_command(cmd_buffer);
    }
}
