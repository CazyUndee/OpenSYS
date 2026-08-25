/*
 * kernel.c - Kernel Main Entry Point
 * 
 * Copyright (C) 2026 CazyUndee
 * SPDX-License-Identifier: AGPL-3.0
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
 */

#include <stdint.h>
#include <stddef.h>
#include "multiboot.h"
#include "vga.h"
#include "serial.h"
#include "pmm.h"
#include "ps2_keyboard.h"
#include "memory.h"
#include "kheap.h"
#include "paging.h"
#include "gdt.h"
#include "idt.h"
#include "timer.h"
#include "tss.h"
#include "interrupts.h"
#include "disk.h"
#include "part.h"
#include "volume.h"
#include "scheduler.h"
#include "process.h"
#include "fs.h"
#include "ramfs.h"
#include "sys.h"
#include "input.h"
#include "pci.h"
#include "ehci.h"
#include "usb.h"
#include "net_drv.h"
#include "ui_state.h"
#include "ui_command.h"
#include "intent_dispatcher.h"
#include "syscall.h"
#include "shell.h"
#include "rtc.h"
#include "vfs.h"
#include "fs_vfs.h"
#include "vfile.h"
#include "version.h"

void kernel_main(uint64_t magic, uint64_t mbi) {
    terminal_initialize();
    serial_init();

    terminal_writestring(PLAN0_FULL_NAME "\n");
    serial_writestring(PLAN0_FULL_NAME "\n");
	terminal_writestring("=============================\n\n");

	if (magic == 0x2BADB002) {
		terminal_writestring("[BOOT] Multiboot 1: OK\n\n");
	} else {
		terminal_writestring("[BOOT] Multiboot 1: ");
		terminal_put_hex(magic);
		terminal_writestring("\n\n");
	}

	terminal_writestring("[INIT] Physical Memory...\n");
	// Debug: write marker before and after pmm_init
	__asm__ volatile ("mov $'A', %%al; mov $0x3F8, %%dx; out %%al, %%dx" ::: "al", "dx");
	pmm_init(mbi);
	__asm__ volatile ("mov $'B', %%al; mov $0x3F8, %%dx; out %%al, %%dx" ::: "al", "dx");
	terminal_writestring(" Total: ");
	terminal_put_dec(pmm_get_total() / (1024 * 1024));
	terminal_writestring(" MB\n Free: ");
	terminal_put_dec(pmm_get_free() / (1024 * 1024));
	terminal_writestring(" MB\n\n");

	terminal_writestring("[INIT] 64-bit Paging...\n");
	paging_init();
	terminal_writestring(" 4-level paging enabled\n\n");

	terminal_writestring("[INIT] Kernel Heap...\n");
	kheap_init(0xFFFF800000000000ULL, 64 * 1024 * 1024);
	terminal_writestring(" Heap: 64MB at 0xFFFF800000000000\n\n");

	terminal_writestring("[TEST] Memory Allocation...\n");

	void* p1 = kmalloc(128);
	void* p2 = kmalloc(1024);

	terminal_writestring(" kmalloc(128) = "); terminal_put_hex((uint64_t)p1); terminal_writestring("\n");
	terminal_writestring(" kmalloc(1024) = "); terminal_put_hex((uint64_t)p2); terminal_writestring("\n");

	kfree(p2);
	terminal_writestring(" kfree(1024) done\n\n");

	terminal_writestring("[DONE] 64-bit memory system ready!\n");

	terminal_writestring("\n[INIT] GDT/TSS...\n");
	tss_init();
	/* Kernel stack used for ring-0 entry (user -> kernel transitions) */
	tss_set_rsp0((uint64_t)kmalloc(16384) + 16384);
	gdt64_init();
	terminal_writestring(" GDT loaded with TSS (ltr done)\n");

	terminal_writestring("\n[INIT] Interrupts...\n");
	idt_init();
	terminal_writestring(" IDT loaded\n");

	pic_init();
	terminal_writestring(" PIC remapped (IRQ 32-47)\n");

	timer_init();
	terminal_writestring(" Timer initialized (1000 Hz)\n");

	terminal_writestring("\n[INIT] PS/2 Keyboard...\n");
	if (input_init() == 0) {
		terminal_writestring(" PS/2 keyboard FAILED\n");
	} else {
		terminal_writestring(" PS/2 keyboard initialized\n");

		/* Unmask IRQ1 (keyboard) only. IRQ0 (timer) stays masked so the
		 * round-robin scheduler cannot preempt the kernel shell context
		 * (the shell runs directly in kernel_main, not as a process). */
		__asm__ volatile (
			"inb $0x21, %%al\n"
			"and $0xFD, %%al\n"
			"outb %%al, $0x21\n"
			: : : "al"
		);
	}

	terminal_writestring("\n[INIT] Disk...\n");
	if (disk_init() < 0) {
		terminal_writestring(" ATA disk init FAILED (no disk?)\n");
	} else {
		terminal_writestring(" ATA disk ready\n");
		/* Parse the GPT partition table (part_init no-ops without a GPT) */
		part_init();
	}

	terminal_writestring("[INIT] Filesystem...\n");
	/* Namespace storage volumes (docs/NAMESPACE.md): with a GPT present,
	 * the filesystem binds to partition 1 so it lives INSIDE the volume
	 * instead of over LBA 0. Without a disk/GPT the legacy in-memory
	 * path applies unchanged. */
	if (part_is_ready() && volume_use_partition(1) == 0) {
		terminal_writestring(" FS volume: storage partitions/1\n");
	}
	if (fs_mount() < 0) {
		terminal_writestring(" No filesystem found, formatting...\n");
		if (fs_format(100 * 1024 * 1024) < 0) {
			terminal_writestring(" ERROR: Failed to format filesystem\n");
		} else {
			terminal_writestring(" Formatted and mounted\n");
		}
	} else {
		terminal_writestring(" Mounted successfully\n");

		// Load MFT UI records into kernel-space State Graph
		terminal_writestring("[BOOT] Loading State Graph from MFT...\n");
		state_graph_init();
		state_graph_load_from_mft();

		// Initialize PCID system
		terminal_writestring("[BOOT] Initializing PCID...\n");
		extern int init_pcid_system_c(void);
		if (init_pcid_system_c() == 0) {
			terminal_writestring(" PCID enabled\n");
		} else {
			terminal_writestring(" PCID not supported (falling back to full TLB flushes)\n");
		}

		terminal_writestring("[BOOT] Unified System Ready!\n\n");
	}

	// Initialize Intent Dispatcher (always, not just when disk is mounted)
	// This enables chdir/pwd/search even without a disk.
	terminal_writestring("[BOOT] Initializing Intent Dispatcher...\n");
	intent_dispatcher_init();

	__asm__ volatile ("sti");
	terminal_writestring(" Interrupts enabled\n\n");

	terminal_writestring("[INIT] USB Host Controller...\n");
	if (usb_init() == 0) {
		terminal_writestring(" UHCI initialized, enumerating devices...\n");
		usb_enumerate();
	} else {
		terminal_writestring(" USB not available (no UHCI controller)\n");
	}

    terminal_writestring("[INIT] USB 2.0 EHCI Controller...\n");
    if (ehci_init() == 0) {
        terminal_writestring(" EHCI initialized\n");
    } else {
        terminal_writestring(" EHCI not available\n");
    }

    terminal_writestring("[INIT] Network Stack...\n");
    if (net_init() == 0) {
        terminal_writestring(" Network initialized\n");
    } else {
        terminal_writestring(" Network not available\n");
    }

    terminal_writestring("[DONE] System ready!\n");    terminal_writestring("\n[INIT] VFS layer...\n");
	ramfs_init();
	vfs_init();
	/* Mount the real (fs.c) filesystem at the root so file descriptors
	 * reach actual files, not just ramfs. Virtual namespaces
	 * (/proc, /sys, /dev) keep routing to vfile via longest-prefix. */
	vfs_mount("/", &fs_vfs_ops);
	terminal_writestring(" VFS mounted (fs at /)\n");

	terminal_writestring("\n[INIT] Virtual filesystem...\n");
	vfile_init();
	terminal_writestring(" Virtual resources registered\n\n");

	terminal_writestring("\nStarting Shell...\n\n");

	// Initialize process system (needed for user-mode programs)
	process_init();
	terminal_writestring(" Process system initialized\n");

	// Run the shell directly in the kernel context (preemptive scheduling is
	// deferred: the context-switch iretq is mishandled by WHPX even with a
	// byte-perfect frame, so IRQ0 stays masked and the shell runs inline).
	shell_run();

	// Should never return
	while (1) {
		__asm__ volatile ("hlt");
	}
}
