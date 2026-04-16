/*
 * init.c - First user process (PID 1)
 * 
 * This gets loaded as the first user program after kernel init.
 */

/* Syscall numbers */
#define SYS_EXIT  0
#define SYS_WRITE 2
#define SYS_YIELD 8

/* Inline syscall wrapper */
static inline long syscall1(long n, long a1) {
    long ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(n), "D"(a1)
        : "memory"
    );
    return ret;
}

static inline long syscall3(long n, long a1, long a2, long a3) {
    long ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3)
        : "memory"
    );
    return ret;
}

void write(const char* s) {
    int len = 0;
    while (s[len]) len++;
    syscall3(SYS_WRITE, 1, (long)s, len);
}

void _start(void) {
    write("\n");
    write("  ==================================\n");
    write("  |    OpenSYS User Mode Test     |\n");
    write("  |        Hello from PID 1        |\n");
    write("  ==================================\n");
    write("\n");
    write("  User process running!\n");
    write("  Syscalls working!\n");
    write("\n");
    
    /* Blink cursor to show we're alive */
    volatile char* vga = (volatile char*)0xB8000;
    int i = 0;
    while (1) {
        vga[160 + 60] = 'U';
        vga[160 + 61] = 0x0A;
        for (volatile int j = 0; j < 1000000; j++);
        
        vga[160 + 60] = ' ';
        for (volatile int j = 0; j < 1000000; j++);
        
        i++;
        
        /* Yield occasionally */
        if (i > 10) {
            syscall1(SYS_YIELD, 0);
            i = 0;
        }
    }
}
