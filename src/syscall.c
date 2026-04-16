/*
 * syscall.c - System Call Implementation
 */

#include <stdint.h>
#include "../include/syscall.h"
#include "../include/process.h"
#include "../include/scheduler.h"
#include "../include/timer.h"
#include "../include/ramfs.h"

extern void syscall_entry(void);

static void* syscall_table[] = {
    /* 0: exit */     0,
    /* 1: read */     0,
    /* 2: write */    0,
    /* 3: open */     0,
    /* 4: close */    0,
    /* 5: fork */     0,
    /* 6: exec */     0,
    /* 7: wait */     0,
    /* 8: yield */    0,
    /* 9: sleep */    0,
    /* 10: getpid */  0,
    /* 11: kill */    0,
    /* 12: brk */     0,
    /* 13: mmap */    0,
    /* 14: munmap */  0,
};

static int sys_exit(int code) {
    (void)code;
    process_t* proc = process_current();
    if (proc) {
        proc->state = PROC_STATE_ZOMBIE;
        scheduler_reschedule();
    }
    return 0;
}

static int sys_read(int fd, void* buf, uint32_t count) {
    (void)fd; (void)buf; (void)count;
    return -1; /* TODO */
}

static int sys_write(int fd, const void* buf, uint32_t count) {
    if (fd == 1 || fd == 2) {
        /* stdout/stderr - write to VGA */
        const char* s = (const char*)buf;
        volatile uint16_t* vga = (volatile uint16_t*)0xB8000;
        static int x = 0, y = 2;
        
        for (uint32_t i = 0; i < count; i++) {
            if (s[i] == '\n') {
                x = 0;
                y++;
            } else {
                vga[y * 80 + x] = (uint8_t)s[i] | 0x0700;
                x++;
                if (x >= 80) { x = 0; y++; }
            }
        }
        return count;
    }
    return -1;
}

static int sys_yield(void) {
    process_yield();
    return 0;
}

static int sys_sleep(uint64_t ms) {
    process_sleep(ms);
    return 0;
}

static int sys_getpid(void) {
    process_t* proc = process_current();
    return proc ? proc->pid : 0;
}

uint64_t syscall_handler(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3) {
    switch (num) {
        case SYS_EXIT:    return sys_exit(a1);
        case SYS_READ:    return sys_read(a1, (void*)a2, a3);
        case SYS_WRITE:   return sys_write(a1, (const void*)a2, a3);
        case SYS_YIELD:   return sys_yield();
        case SYS_SLEEP:   return sys_sleep(a1);
        case SYS_GETPID:  return sys_getpid();
        default:          return -1;
    }
}

void syscall_init(void) {
    extern void idt_set_syscall_gate_wrapper(void);
    idt_set_syscall_gate_wrapper();
}
