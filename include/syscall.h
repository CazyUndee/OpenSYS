/*
 * syscall.h - System Call Interface
 */

#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

/* Syscall numbers */
#define SYS_EXIT    0
#define SYS_READ    1
#define SYS_WRITE   2
#define SYS_OPEN    3
#define SYS_CLOSE   4
#define SYS_FORK    5
#define SYS_EXEC    6
#define SYS_WAIT    7
#define SYS_YIELD   8
#define SYS_SLEEP   9
#define SYS_GETPID  10
#define SYS_KILL    11
#define SYS_BRK     12
#define SYS_MMAP    13
#define SYS_MUNMAP  14

/* Initialize syscall handler */
void syscall_init(void);

/* Syscall handler (called from assembly) */
uint64_t syscall_handler(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3);

#endif
