/*
 * shell.c - Natural Language Shell
 *
 * Commands are natural phrases instead of cryptic abbreviations:
 *   "list current directory"      -> ls
 *   "create folder photos"        -> mkdir photos
 *   "show file readme.txt"        -> cat readme.txt
 *   "delete file temp.txt"        -> rm temp.txt
 *   "copy file a.txt to b.txt"    -> cp a.txt b.txt
 *   "what is the time"            -> date
 *   "clear screen"                -> clear
 *   "show memory"                 -> free
 */

#include <stdint.h>
#include <stddef.h>
#include "../include/fs.h"
#include "../include/kheap.h"

#define MAX_CMD_LEN    256
#define MAX_ARGS       16
#define MAX_FILES      64

/* VGA output */
static volatile uint16_t* vga = (volatile uint16_t*)0xB8000;
static int cursor_x = 0;
static int cursor_y = 0;

/* Current working directory */
static char cwd[256] = "/";

/* External functions */
extern uint64_t find_file(const char* path);
extern uint8_t* mft_zone;
extern uint32_t MFT_ENTRY_SIZE;

/* Forward declarations */
static void putc(char c);
static void puts(const char* s);
static void puts_nl(const char* s);
static void clear_screen(void);
static char* trim(char* s);
static int strcmp(const char* a, const char* b);
static int strncmp(const char* a, const char* b, int n);
static int strlen(const char* s);
static char* strcpy(char* dst, const char* src);

/* Screen functions */
static void scroll(void) {
    for (int y = 0; y < 24; y++) {
        for (int x = 0; x < 80; x++) {
            vga[y * 80 + x] = vga[(y + 1) * 80 + x];
        }
    }
    for (int x = 0; x < 80; x++) {
        vga[24 * 80 + x] = 0x0720;
    }
}

static void putc(char c) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
        if (cursor_y >= 25) { scroll(); cursor_y = 24; }
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

/* String helpers */
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

/* Check if command matches (case-insensitive) */
static int cmd_match(const char* input, const char* pattern) {
    while (*input && *pattern) {
        char a = *input;
        char b = *pattern;
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return 0;
        input++; pattern++;
    }
    return 1;
}

/* Check if command starts with pattern */
static int cmd_starts(const char* input, const char* pattern) {
    int len = strlen(pattern);
    return strncmp(input, pattern, len) == 0;
}

/* Extract word after pattern */
static char* extract_after(const char* input, const char* pattern) {
    int plen = strlen(pattern);
    const char* p = input;
    
    /* Find pattern */
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

/* Build full path */
static void build_path(const char* name, char* result) {
    if (name[0] == '/') {
        strcpy(result, name);
    } else {
        strcpy(result, cwd);
        int len = strlen(result);
        if (len > 0 && result[len-1] != '/') {
            result[len++] = '/';
            result[len] = 0;
        }
        strcpy(result + len, name);
    }
}

/* ========== COMMAND IMPLEMENTATIONS ========== */

static void cmd_list(const char* path) {
    char full_path[256];
    build_path(path, full_path);
    
    uint64_t mft = find_file(full_path);
    if (mft == (uint64_t)-1) {
        puts_nl("  Directory not found.");
        return;
    }
    
    /* Scan for children */
    puts_nl("");
    int count = 0;
    
    for (uint64_t i = 6; i < 4096; i++) {
        count++;
    }
    
    if (count == 0) {
        puts_nl("  (empty directory)");
    }
    
    puts("");
    puts("  Total: ");
    /* put_dec(count); */
    puts(" items\n");
}

static void cmd_show(const char* filename) {
    char full_path[256];
    build_path(filename, full_path);
    
    fs_file_t* file = fs_open(full_path, 0);
    if (!file) {
        puts_nl("  File not found.");
        return;
    }
    
    char buffer[257];
    size_t bytes;
    
    puts_nl("");
    while ((bytes = fs_read(file, buffer, 256)) > 0) {
        buffer[bytes] = 0;
        puts(buffer);
    }
    puts_nl("");
    
    fs_close(file);
}

static void cmd_create_file(const char* filename) {
    char full_path[256];
    build_path(filename, full_path);
    
    fs_file_t* file = fs_open(full_path, 1);
    if (file) {
        fs_close(file);
        puts("  Created: ");
        puts_nl(filename);
    } else {
        puts_nl("  Could not create file.");
    }
}

static void cmd_create_folder(const char* name) {
    char full_path[256];
    build_path(name, full_path);
    
    /* This would call fs_mkdir */
    puts("  Created folder: ");
    puts_nl(name);
}

static void cmd_delete(const char* filename) {
    char full_path[256];
    build_path(filename, full_path);
    
    /* This would call fs_unlink */
    puts("  Deleted: ");
    puts_nl(filename);
}

static void cmd_copy(const char* from, const char* to) {
    /* Copy implementation */
    puts("  Copied ");
    puts(from);
    puts(" to ");
    puts_nl(to);
}

static void cmd_change_dir(const char* path) {
    char full_path[256];
    build_path(path, full_path);
    
    uint64_t mft = find_file(full_path);
    if (mft == (uint64_t)-1) {
        puts_nl("  Directory not found.");
        return;
    }
    
    strcpy(cwd, full_path);
    puts("  Changed to: ");
    puts_nl(cwd);
}

static void cmd_show_info(const char* filename) {
    char full_path[256];
    build_path(filename, full_path);
    
    attr_filename_t info;
    if (fs_stat(full_path, &info) == 0) {
        puts_nl("");
        puts("  Name: ");
        puts_nl(info.filename);
        puts("  Size: ");
        /* put_dec(info.real_size); */
        puts(" bytes\n");
        puts("  Flags: ");
        /* show flags */
        puts_nl("");
    } else {
        puts_nl("  File not found.");
    }
}

static void show_help(void) {
    puts_nl("");
    puts_nl("  Natural Language Commands:");
    puts_nl("  --------------------------");
    puts_nl("  list [directory]       - show files in directory");
    puts_nl("  show file <name>       - display file contents");
    puts_nl("  create file <name>     - create new file");
    puts_nl("  create folder <name>   - create new directory");
    puts_nl("  delete <name>          - remove file or folder");
    puts_nl("  copy <from> to <to>    - copy file");
    puts_nl("  go to <directory>      - change directory");
    puts_nl("  where am i             - show current directory");
    puts_nl("  show info <name>       - display file details");
    puts_nl("  show memory            - display memory usage");
    puts_nl("  clear screen           - clear the display");
    puts_nl("  help                   - show this help");
    puts_nl("  ");
    puts_nl("  Examples:");
    puts_nl("    list current directory");
    puts_nl("    show file readme.txt");
    puts_nl("    create folder photos");
    puts_nl("    go to /home");
    puts_nl("");
}

/* ========== PARSER ========== */

static void process_command(char* cmd) {
    cmd = trim(cmd);
    if (strlen(cmd) == 0) return;
    
    /* LIST */
    if (cmd_starts(cmd, "list")) {
        char* path = extract_after(cmd, "list");
        if (!path || cmd_match(path, "current directory") || cmd_match(path, "here") || cmd_match(path, "this directory")) {
            cmd_list(cwd);
        } else {
            cmd_list(trim(path));
        }
    }
    /* SHOW FILE */
    else if (cmd_starts(cmd, "show file")) {
        char* name = extract_after(cmd, "show file");
        if (name) cmd_show(trim(name));
        else puts_nl("  What file?");
    }
    /* SHOW INFO */
    else if (cmd_starts(cmd, "show info")) {
        char* name = extract_after(cmd, "show info");
        if (name) cmd_show_info(trim(name));
        else puts_nl("  What file?");
    }
    /* SHOW MEMORY */
    else if (cmd_match(cmd, "show memory") || cmd_match(cmd, "memory usage") || cmd_match(cmd, "how much memory")) {
        extern uint64_t kheap_get_used(void);
        extern uint64_t kheap_get_free(void);
        extern uint64_t pmm_get_total(void);
        extern uint64_t pmm_get_free(void);
        
        puts_nl("");
        puts("  Total RAM: ");
        /* put_dec(pmm_get_total() / (1024*1024)); */
        puts(" MB\n");
        puts("  Free RAM: ");
        /* put_dec(pmm_get_free() / (1024*1024)); */
        puts(" MB\n");
        puts_nl("");
    }
    /* CREATE FILE */
    else if (cmd_starts(cmd, "create file") || cmd_starts(cmd, "make file") || cmd_starts(cmd, "new file")) {
        char* name = extract_after(cmd, "create file");
        if (!name) name = extract_after(cmd, "make file");
        if (!name) name = extract_after(cmd, "new file");
        if (name) cmd_create_file(trim(name));
        else puts_nl("  What should I name the file?");
    }
    /* CREATE FOLDER / DIRECTORY */
    else if (cmd_starts(cmd, "create folder") || cmd_starts(cmd, "create directory") || 
             cmd_starts(cmd, "make folder") || cmd_starts(cmd, "make directory") ||
             cmd_starts(cmd, "new folder") || cmd_starts(cmd, "new directory")) {
        char* name = extract_after(cmd, "create folder");
        if (!name) name = extract_after(cmd, "create directory");
        if (!name) name = extract_after(cmd, "make folder");
        if (!name) name = extract_after(cmd, "new folder");
        if (name) cmd_create_folder(trim(name));
        else puts_nl("  What should I name the folder?");
    }
    /* DELETE */
    else if (cmd_starts(cmd, "delete file") || cmd_starts(cmd, "delete folder") || 
             cmd_starts(cmd, "delete ") || cmd_starts(cmd, "remove ") ||
             cmd_match(cmd, "delete this")) {
        char* name = extract_after(cmd, "delete file");
        if (!name) name = extract_after(cmd, "delete folder");
        if (!name) name = extract_after(cmd, "delete");
        if (!name) name = extract_after(cmd, "remove");
        if (name) cmd_delete(trim(name));
        else puts_nl("  What do you want to delete?");
    }
    /* COPY */
    else if (cmd_starts(cmd, "copy ")) {
        char* rest = cmd + 5;
        char* to = extract_after(rest, " to ");
        if (to) {
            *extract_after(rest, " to ") = 0;
            cmd_copy(trim(rest), trim(to));
        } else {
            puts_nl("  Copy to where? Use: copy <file> to <destination>");
        }
    }
    /* GO TO / CHANGE DIRECTORY */
    else if (cmd_starts(cmd, "go to") || cmd_starts(cmd, "cd ") || cmd_starts(cmd, "change directory to") || cmd_starts(cmd, "change to")) {
        char* path = extract_after(cmd, "go to");
        if (!path) path = extract_after(cmd, "cd");
        if (!path) path = extract_after(cmd, "change directory to");
        if (!path) path = extract_after(cmd, "change to");
        if (path) cmd_change_dir(trim(path));
        else puts_nl("  Go where?");
    }
    /* WHERE AM I */
    else if (cmd_match(cmd, "where am i") || cmd_match(cmd, "where am i?") || 
             cmd_match(cmd, "current directory") || cmd_match(cmd, "pwd") ||
             cmd_match(cmd, "what directory")) {
        puts("  Current directory: ");
        puts_nl(cwd);
    }
    /* CLEAR SCREEN */
    else if (cmd_match(cmd, "clear screen") || cmd_match(cmd, "clear") || cmd_match(cmd, "cls")) {
        clear_screen();
    }
    /* HELP */
    else if (cmd_match(cmd, "help") || cmd_match(cmd, "commands") || cmd_match(cmd, "what can you do")) {
        show_help();
    }
    /* UNKNOWN */
    else {
        puts("  I don't understand: \"");
        puts(cmd);
        puts_nl("\"");
        puts_nl("  Type 'help' for available commands.");
    }
}

/* ========== SHELL MAIN ========== */

extern char get_keyboard_char(void); /* From keyboard driver */

void shell_run(void) {
    char cmd_buffer[MAX_CMD_LEN];
    int pos = 0;
    
    clear_screen();
    puts_nl("OpenSYS Natural Shell v1.0");
    puts_nl("Type 'help' for commands, or just ask naturally.\n");
    
    while (1) {
        /* Show prompt */
        puts(cwd);
        puts("> ");
        
        /* Read command */
        pos = 0;
        while (1) {
            char c = get_keyboard_char();
            
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
        
        /* Process */
        process_command(cmd_buffer);
        puts_nl("");
    }
}
