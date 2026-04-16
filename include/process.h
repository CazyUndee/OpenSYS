/*
 * process.h - Process Management
 */

#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include "vm.h"

#define MAX_PROCESSES 64
#define PROCESS_NAME_LEN 32

typedef uint64_t pid_t;

/* Process states */
typedef enum {
    PROC_STATE_UNUSED = 0,
    PROC_STATE_READY,
    PROC_STATE_RUNNING,
    PROC_STATE_BLOCKED,
    PROC_STATE_ZOMBIE
} process_state_t;

/* Saved CPU context for context switching */
typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rbx, rdx, rcx, rax;
    uint64_t rip, cs, rflags;
    uint64_t rsp, ss;
} __attribute__((packed)) cpu_context_t;

/* Process Control Block */
typedef struct process {
    pid_t pid;
    char name[PROCESS_NAME_LEN];
    process_state_t state;

    /* CPU context (saved on switch) */
    cpu_context_t context;

    /* Stacks */
    uint64_t* kernel_stack;
    uint64_t kernel_stack_top;
    uint64_t user_stack_top;

    /* Memory */
vm_space_t* vm; /* Address space */

    /* Scheduling */
    uint64_t wake_time;
    int priority;
    uint64_t cpu_time;

    /* Exit status */
    int exit_code;

    /* Linked list */
    struct process* next;
    struct process* prev;
} process_t;

/* Process function type */
typedef void (*process_entry_t)(void*);

/* Initialize process manager */
void process_init(void);

/* Create new process */
pid_t process_create(const char* name, process_entry_t entry, void* arg);

/* Get current process */
process_t* process_current(void);

/* Get process by PID */
process_t* process_get(pid_t pid);

/* Terminate current process */
void process_exit(int code);

/* Block current process */
void process_block(void);

/* Wake up a process */
void process_wake(process_t* proc);

/* Sleep for milliseconds */
void process_sleep(uint64_t ms);

/* Yield CPU */
void process_yield(void);

/* Create user process from ELF binary */
pid_t process_create_user(const char* name, const void* elf_data, size_t elf_size);

/* Get current process's VM space */
vm_space_t* process_current_vm(void);

#endif
