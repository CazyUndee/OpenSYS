/*
 * process.c - Process Management
 */

#include <stdint.h>
#include "../include/process.h"
#include "../include/scheduler.h"
#include "../include/kheap.h"
#include "../include/timer.h"
#include "../include/paging.h"
#include "../include/vm.h"
#include "../include/pmm.h"
#include "../include/elf.h"
#include "../include/tss.h"

#define STACK_SIZE 16384  /* 16KB stacks */

static process_t process_table[MAX_PROCESSES];
static pid_t next_pid = 1;
static process_t* idle_process = 0;

/* Assembly context switch */
extern void context_switch(cpu_context_t* old_ctx, cpu_context_t* new_ctx);

/* Find a free process slot */
static process_t* alloc_process(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state == PROC_STATE_UNUSED) {
            return &process_table[i];
        }
    }
    return 0;
}

/* Idle process - runs when nothing else to do */
static void idle_process_func(void* arg) {
    (void)arg;
    while (1) {
        __asm__ volatile ("hlt");
    }
}

/* Process wrapper - called when process starts */
static void process_wrapper(void) {
    process_t* proc = process_current();
    if (proc && proc->context.rdi) {
        /* Call process entry point */
        typedef void (*entry_t)(void*);
        entry_t entry = (entry_t)proc->context.rdi;
        void* arg = (void*)proc->context.rsi;
        entry(arg);
    }
    process_exit(0);
}

void process_init(void) {
    /* Clear process table */
    for (int i = 0; i < MAX_PROCESSES; i++) {
        process_table[i].state = PROC_STATE_UNUSED;
        process_table[i].pid = 0;
    }
    
    next_pid = 1;
    
    /* Create idle process */
    idle_process = alloc_process();
    idle_process->pid = next_pid++;
    idle_process->name[0] = 'i';
    idle_process->name[1] = 'd';
    idle_process->name[2] = 'l';
    idle_process->name[3] = 'e';
    idle_process->name[4] = 0;
    idle_process->state = PROC_STATE_READY;
    idle_process->kernel_stack = (uint64_t*)kmalloc(STACK_SIZE);
    idle_process->kernel_stack_top = (uint64_t)((uint8_t*)idle_process->kernel_stack + STACK_SIZE);
    
    /* Setup idle context */
    idle_process->context.rip = (uint64_t)idle_process_func;
    idle_process->context.cs = 0x08;
    idle_process->context.rflags = 0x202;
    idle_process->context.rsp = idle_process->kernel_stack_top;
    idle_process->context.ss = 0x10;
    
    scheduler_init();
    scheduler_add(idle_process);
}

pid_t process_create(const char* name, process_entry_t entry, void* arg) {
    process_t* proc = alloc_process();
    if (!proc) return 0;
    
    /* Allocate stack */
    proc->kernel_stack = (uint64_t*)kmalloc(STACK_SIZE);
    if (!proc->kernel_stack) return 0;
    
    proc->kernel_stack_top = (uint64_t)((uint8_t*)proc->kernel_stack + STACK_SIZE);
    
    /* Setup process */
    proc->pid = next_pid++;
    
    /* Copy name */
    int i = 0;
    while (name[i] && i < PROCESS_NAME_LEN - 1) {
        proc->name[i] = name[i];
        i++;
    }
    proc->name[i] = 0;
    
    proc->state = PROC_STATE_READY;
    proc->priority = 0;
    proc->cpu_time = 0;
    proc->wake_time = 0;
    
    /* Setup context for process_wrapper */
    uint64_t* stack = (uint64_t*)proc->kernel_stack_top;
    
    /* Push dummy SS and RSP for iretq */
    *--stack = 0x10;  /* SS */
    *--stack = (uint64_t)proc->kernel_stack_top;  /* RSP */
    *--stack = 0x202; /* RFLAGS */
    *--stack = 0x08;  /* CS */
    *--stack = (uint64_t)process_wrapper;  /* RIP */
    
    /* Push dummy registers */
    *--stack = 0;  /* RAX */
    *--stack = 0;  /* RCX */
    *--stack = 0;  /* RDX */
    *--stack = 0;  /* RBX */
    *--stack = (uint64_t)arg;    /* RBP (will be used for arg) */
    *--stack = (uint64_t)entry;  /* RSI (entry point) */
    *--stack = 0;  /* RDI */
    *--stack = 0;  /* R8 */
    *--stack = 0;  /* R9 */
    *--stack = 0;  /* R10 */
    *--stack = 0;  /* R11 */
    *--stack = 0;  /* R12 */
    *--stack = 0;  /* R13 */
    *--stack = 0;  /* R14 */
    *--stack = 0;  /* R15 */
    
    proc->context.rsp = (uint64_t)stack;
    proc->context.rip = (uint64_t)process_wrapper;
    proc->context.cs = 0x08;
    proc->context.ss = 0x10;
    proc->context.rflags = 0x202;

    /* Use kernel address space */
    proc->vm = 0;

    /* Add to scheduler */
    scheduler_add(proc);

    return proc->pid;
}

process_t* process_get(pid_t pid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].pid == pid && process_table[i].state != PROC_STATE_UNUSED) {
            return &process_table[i];
        }
    }
    return 0;
}

process_t* process_get_by_index(int i) {
    if (i >= 0 && i < MAX_PROCESSES) {
        return &process_table[i];
    }
    return 0;
}

void process_exit(int code) {
    (void)code;
    process_t* proc = process_current();
    if (!proc) return;
    
    /* Mark as zombie */
    proc->state = PROC_STATE_ZOMBIE;
    
    /* Yield forever */
    while (1) {
        __asm__ volatile ("hlt");
    }
}

void process_block(void) {
    process_t* proc = process_current();
    if (proc) {
        proc->state = PROC_STATE_BLOCKED;
    }
    scheduler_reschedule();
}

void process_wake(process_t* proc) {
    if (proc && proc->state == PROC_STATE_BLOCKED) {
        proc->state = PROC_STATE_READY;
        scheduler_add(proc);
    }
}

void process_sleep(uint64_t ms) {
    process_t* proc = process_current();
    if (proc) {
        proc->wake_time = timer_get_ms() + ms;
        proc->state = PROC_STATE_BLOCKED;
        scheduler_reschedule();
    }
}

void process_yield(void) {
    scheduler_reschedule();
}

vm_space_t* process_current_vm(void) {
    process_t* proc = process_current();
    return proc ? proc->vm : 0;
}

pid_t process_create_user(const char* name, const void* elf_data, size_t elf_size) {
    process_t* proc = alloc_process();
    if (!proc) return 0;

    /* Allocate kernel stack */
    proc->kernel_stack = (uint64_t*)kmalloc(STACK_SIZE);
    if (!proc->kernel_stack) return 0;
    proc->kernel_stack_top = (uint64_t)((uint8_t*)proc->kernel_stack + STACK_SIZE);

    /* Create address space */
    proc->vm = vm_create_space();
    if (!proc->vm) {
        kfree(proc->kernel_stack);
        return 0;
    }

    /* Allocate user stack */
    uint64_t user_stack = 0x7FFFFFFFFFFFULL;
    for (int i = 0; i < 16; i++) {
        phys_addr_t phys = pmm_alloc_page();
        if (!phys) {
            kfree(proc->kernel_stack);
            vm_destroy_space(proc->vm);
            return 0;
        }
        vm_map_page(proc->vm, user_stack - i * PAGE_SIZE, phys, VM_FLAG_USER | VM_FLAG_PRESENT | VM_FLAG_WRITABLE);
    }
    proc->user_stack_top = user_stack;

    /* Switch to process address space to load ELF */
    vm_space_t* old_space = process_current() ? process_current()->vm : 0;
    vm_switch_space(proc->vm);

    /* Load ELF */
    elf_info_t elf_info;
    if (elf_load(elf_data, elf_size, &elf_info) < 0) {
        if (old_space) vm_switch_space(old_space);
        kfree(proc->kernel_stack);
        vm_destroy_space(proc->vm);
        return 0;
    }

    /* Switch back */
    if (old_space) vm_switch_space(old_space);

    /* Setup process */
    proc->pid = next_pid++;
    int i = 0;
    while (name[i] && i < PROCESS_NAME_LEN - 1) {
        proc->name[i] = name[i];
        i++;
    }
    proc->name[i] = 0;
    proc->state = PROC_STATE_READY;
    proc->priority = 0;
    proc->cpu_time = 0;
    proc->exit_code = 0;

    /* Setup kernel stack for user mode entry */
    uint64_t* stack = (uint64_t*)proc->kernel_stack_top;

    /* User context for iretq */
    *--stack = 0x23;           /* SS (user data) */
    *--stack = user_stack;     /* RSP */
    *--stack = 0x202;          /* RFLAGS */
    *--stack = 0x1B;           /* CS (user code) */
    *--stack = elf_info.entry; /* RIP */

    /* Kernel context */
    for (int j = 0; j < 15; j++) {
        *--stack = 0;
    }

    proc->context.rsp = (uint64_t)stack;
    proc->context.cs = 0x08;
    proc->context.ss = 0x10;
    proc->context.rflags = 0x202;

    /* Set TSS rsp0 for this process */
    tss_set_rsp0(proc->kernel_stack_top);

    scheduler_add(proc);
    return proc->pid;
}

/* Check sleeping processes */
void process_check_sleepers(void) {
    uint64_t now = timer_get_ms();
    
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state == PROC_STATE_BLOCKED && 
            process_table[i].wake_time > 0 &&
            process_table[i].wake_time <= now) {
            process_table[i].wake_time = 0;
            process_table[i].state = PROC_STATE_READY;
            scheduler_add(&process_table[i]);
        }
    }
}
