/*
 * syscall.c - System Call Implementation
 */

#include <stdint.h>
#include "syscall.h"
#include "process.h"
#include "scheduler.h"
#include "timer.h"
#include "ramfs.h"
#include "vm.h"
#include "pmm.h"
#include "vga.h"
#include "vfs.h"
#include "kheap.h"
#include "kstring.h"

extern void syscall_entry(void);

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
    if (fd < 0) return -1;
    /* Route through the VFS fd table so user programs get real,
     * offset-tracking file descriptors. */
    return vfs_read(fd, buf, count);
}

static int sys_write(int fd, const void* buf, uint32_t count) {
	if (fd == 1 || fd == 2) {
		const char* s = (const char*)buf;
		for (uint32_t i = 0; i < count; i++) {
			terminal_putchar(s[i]);
		}
		return count;
	}
	/* Regular file descriptor — route through the VFS fd table */
	return vfs_write(fd, buf, count);
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

static int sys_open(const char* name) {
    if (!name) return -1;
    /* Allocate a real VFS fd-table entry (ramfs backend at /). */
    return vfs_open(name, VFS_O_RDONLY);
}

static int sys_close(int fd) {
    fd_table_t* table = 0;
    process_t* proc = process_current();
    if (!proc) return -1;
    table = proc->fd_table;
    if (!table) return -1;
    if (fd < 0 || fd >= VFS_MAX_FDS) return -1;
    if (!table->fds[fd]) return -1;
    if (fd_table_close(table, fd) < 0) return -1;
    return 0;
}

static int sys_fork(void) {
    process_t* parent = process_current();
    if (!parent) return -1;
    char child_name[PROCESS_NAME_LEN];
    int i;
    for (i = 0; i < PROCESS_NAME_LEN - 7 && parent->name[i]; i++)
        child_name[i] = parent->name[i];
    child_name[i] = '_'; child_name[i+1] = 'c'; child_name[i+2] = 'h';
    child_name[i+3] = 'i'; child_name[i+4] = 'l'; child_name[i+5] = 'd';
    child_name[i+6] = '\0';
    pid_t child_pid = process_create(child_name, 0, 0);
    if (!child_pid) return -1;
    process_t* child = process_get(child_pid);
    vm_space_t* child_vm = vm_clone_space(parent->vm);
    if (!child_vm) return -1;
    child->vm = child_vm;
    return child->pid;
}

static int sys_exec(const char* path) {
	if (!path) return -1;
	process_t* proc = process_current();
	if (!proc) return -1;

	/* Parse argv from path (whitespace-split) */
	const char* argv[32];
	int argc = 0;

	const char* p = path;
	while (*p && argc < 32) {
		/* skip leading whitespace */
		while (*p == ' ' || *p == '\t') p++;
		if (!*p) break;
		argv[argc++] = p;
		/* skip to next whitespace */
		while (*p && *p != ' ' && *p != '\t') p++;
	}
	/* Null-terminate the last argument so k_strlen works later */
	if (argc > 0) {
		/* We need a writable copy of each argv string for the user stack.
		 * We'll let process_create_user() copy them; for now just hand the pointers.
		 * The actual strings live inside the caller's `path` buffer, so we must not
		 * free that here -- it's the kernel-space copy already in ramfs.
		 */
	}

	/* Look up file in ramfs (use first token as the path) */
	char path_buf[256];
	int i;
	for (i = 0; i < 255 && path[i] && path[i] != ' ' && path[i] != '\t'; i++)
		path_buf[i] = path[i];
	path_buf[i] = '\0';

	int fd = ramfs_find(path_buf);
	if (fd < 0) return -1;

	uint32_t file_size = ramfs_size(fd);
	if (file_size == 0) return -1;

	/* Read the ELF binary */
	uint8_t* elf_data = (uint8_t*)kmalloc(file_size);
	if (!elf_data) return -1;

	int read_bytes = ramfs_read(fd, elf_data, file_size, 0);
	if (read_bytes < 0 || (uint32_t)read_bytes != file_size) {
		kfree(elf_data);
		return -1;
	}

	/* Load ELF and replace current process */
	pid_t new_pid = process_create_user(proc->name, elf_data, file_size, argc, argv);
	kfree(elf_data);

	if (!new_pid) return -1;

	/* Terminate current process; scheduler switches to the new one */
	process_exit(0);
	return 0;
}

static int sys_wait(int* status) {
    process_t* parent = process_current();
    if (!parent) return -1;

    /* Find any zombie child process */
    for (int i = 0; i < MAX_PROCESSES; i++) {
        process_t* child = process_get_by_index(i);
        if (!child) continue;
        if (child->state != PROC_STATE_ZOMBIE) continue;
        if (child == parent) continue;

        pid_t child_pid = child->pid;

        /* Write exit status if requested */
        if (status) {
            *status = child->exit_code;
        }

        /* Free the zombie slot */
        child->state = PROC_STATE_UNUSED;
        child->pid = 0;

        return child_pid;
    }

    /* No zombie children — yield and retry */
    process_yield();
    return -1;
}

static int sys_kill(pid_t pid) {
    process_t* proc = process_get(pid);
    if (!proc) return -1;
    proc->state = PROC_STATE_ZOMBIE;
    return 0;
}

static uint64_t sys_brk(uint64_t addr) {
    process_t* proc = process_current();
    if (!proc || !proc->vm) return -1;
    if (addr == 0) return proc->vm->heap_end;
    if (addr > proc->vm->heap_start) {
        proc->vm->heap_end = addr;
        return addr;
    }
    return -1;
}

static uint64_t sys_mmap(uint64_t addr, uint64_t size, uint64_t flags) {
    (void)addr;
    process_t* proc = process_current();
    if (!proc || !proc->vm || size == 0) return -1;
    uint64_t count = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    void* virt = vm_alloc_pages(proc->vm, count, flags);
    return virt ? (uint64_t)virt : (uint64_t)-1;
}

static uint64_t sys_munmap(uint64_t addr, uint64_t size) {
    process_t* proc = process_current();
    if (!proc || !proc->vm || size == 0) return -1;
    uint64_t count = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    vm_free_pages(proc->vm, (void*)addr, count);
    return 0;
}

static int sys_pipe(int fds[2]) {
    if (!fds) return -1;
    return vfs_pipe(fds);
}

uint64_t syscall_handler(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3) {
    switch (num) {
    case SYS_EXIT: return sys_exit(a1);
    case SYS_READ: return sys_read(a1, (void*)a2, a3);
    case SYS_WRITE: return sys_write(a1, (const void*)a2, a3);
    case SYS_OPEN: return sys_open((const char*)a1);
    case SYS_CLOSE: return sys_close(a1);
    case SYS_FORK: return sys_fork();
    case SYS_EXEC: return sys_exec((const char*)a1);
    case SYS_WAIT: return sys_wait((int*)a1);
    case SYS_YIELD: return sys_yield();
    case SYS_SLEEP: return sys_sleep(a1);
    case SYS_GETPID: return sys_getpid();
    case SYS_KILL: return sys_kill(a1);
    case SYS_BRK: return sys_brk(a1);
    case SYS_MMAP: return sys_mmap(a1, a2, a3);
    case SYS_MUNMAP: return sys_munmap(a1, a2);
    case SYS_PIPE: return sys_pipe((int*)a1);
    default: return -1;
    }
}

void syscall_init(void) {
    extern void idt_set_syscall_gate_wrapper(void);
    idt_set_syscall_gate_wrapper();
}
