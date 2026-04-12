/*
 * shell.c - Natural Language Shell
 *
 * Commands are natural phrases instead of cryptic abbreviations.
 */

#include <stdint.h>
#include <stddef.h>
#include "../include/fs.h"
#include "../include/hid_keyboard.h"

#define MAX_CMD_LEN    256

/* VGA output */
static volatile uint16_t* vga = (volatile uint16_t*)0xB8000;
static int cursor_x = 0;
static int cursor_y = 0;

/* Current working directory */
static char cwd[256] = "/";

/* External functions */
extern uint64_t find_file(const char* path);
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
    cursor_x = 0; cursor_y = 0;
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
static char* strcpy(char* dst, const char* src) {
    char* d = dst; while ((*d++ = *src++)); return dst;
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

static char* extract_after(const char* input, const char* pattern) {
    int plen = strlen(pattern);
    const char* p = input;
    while (*p) {
        if (cmd_match(p, pattern) && (p == input || *(p-1) == ' ')) {
            p += plen;
            while (*p == ' ') p++;
            return (char*)p;
        }
        p++;
    }
    return 0;
}

/* ========== COMMANDS ========== */

static void cmd_list(const char* path) {
    puts_nl("  (directory listing coming soon)");
}

static void cmd_show_memory(void) {
    uint64_t total = pmm_get_total();
    uint64_t free = pmm_get_free();
    
    puts_nl("");
    puts("  Total RAM: ");
    // put_dec would go here
    puts("MB\n");
    puts("  Free RAM:  ");
    puts("MB\n\n");
}

static void show_help(void) {
    puts_nl("");
    puts_nl("  Natural Language Commands:");
    puts_nl("  --------------------------");
    puts_nl("  list [directory]       - show files");
    puts_nl("  show memory            - display memory usage");
    puts_nl("  clear screen           - clear display");
    puts_nl("  help                   - show this help");
    puts_nl("");
}

static void process_command(char* cmd) {
    cmd = trim(cmd);
    if (strlen(cmd) == 0) return;
    
    if (cmd_starts(cmd, "list")) {
        cmd_list(cwd);
    }
    else if (cmd_match(cmd, "show memory") || cmd_match(cmd, "memory")) {
        cmd_show_memory();
    }
    else if (cmd_match(cmd, "clear screen") || cmd_match(cmd, "clear") || cmd_match(cmd, "cls")) {
        clear_screen();
    }
    else if (cmd_match(cmd, "help") || cmd_match(cmd, "commands")) {
        show_help();
    }
    else if (cmd_match(cmd, "where am i") || cmd_match(cmd, "pwd")) {
        puts("  "); puts_nl(cwd);
    }
    else {
        puts("  Unknown: \"");
        puts(cmd);
        puts_nl("\"");
        puts_nl("  Type 'help' for commands.");
    }
}

/* ========== MAIN ========== */

void shell_run(void) {
    char cmd_buffer[MAX_CMD_LEN];
    int pos = 0;
    
    clear_screen();
    puts_nl("OpenSYS Natural Shell v1.0");
    puts_nl("Type 'help' for commands.\n");
    
    while (1) {
        puts(cwd);
        puts("> ");
        
        pos = 0;
        while (1) {
            /* Poll USB keyboard */
            hid_keyboard_poll();
            
            if (hid_keyboard_has_key()) {
                char c = hid_keyboard_getc();
                
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
            
            /* Small delay to not burn CPU */
            for (volatile int i = 0; i < 1000; i++);
        }
        
        process_command(cmd_buffer);
        puts_nl("");
    }
}

/* Fallback for PS/2 keyboard if USB not available */
void shell_run_ps2(void) {
    /* Same as above but uses PS/2 scancodes */
    shell_run();
}
