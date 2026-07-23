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

void kernel_main(uint64_t magic, uint64_t mbi) {
    terminal_initialize();
    serial_init();

    terminal_writestring("Plan 0 v0.4.1\n");
    serial_writestring("Plan 0 v0.4.1\n");
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

	terminal_writestring("\n[INIT] Interrupts...\n");
	idt_init();
	terminal_writestring(" IDT loaded\n");

	pic_init();
	terminal_writestring(" PIC remapped (IRQ 32-47)\n");

	timer_init();
	terminal_writestring(" Timer initialized (1000 Hz)\n");

	terminal_writestring("\n[INIT] PS/2 Keyboard...\n");
	if (ps2_keyboard_init() == 0) {
		terminal_writestring(" PS/2 keyboard initialized\n");

		__asm__ volatile (
			"inb $0x21, %%al\n"
			"and $0xFC, %%al\n"
			"outb %%al, $0x21\n"
			: : : "al"
		);
	} else {
		terminal_writestring(" PS/2 keyboard FAILED\n");
	}

	terminal_writestring("\n[INIT] Disk...\n");
	if (disk_init() < 0) {
		terminal_writestring(" ATA disk init FAILED (no disk?)\n");
	} else {
		terminal_writestring(" ATA disk ready\n");
	}

	terminal_writestring("[INIT] Filesystem...\n");
	if (fs_mount() < 0) {
		terminal_writestring(" No filesystem found, formatting...\n");
		if (fs_format(100 * 1024 * 1024) < 0) {
			terminal_writestring(" ERROR: Failed to format filesystem\n");
		} else {
			terminal_writestring(" Formatted and mounted\n");
		}
	} else {
		terminal_writestring(" Mounted successfully\n");
		terminal_writestring("// Phase 1: Initialize Memory and Filesystem\n");
		terminal_writestring("[BOOT] Phase 1: Memory and Filesystem...\n");
		memory_init(mbi);
		paging_init();
		kheap_init(0xFFFF800000000000ULL, 64 * 1024 * 1024);

		// Phase 2: Load MFT UI records into kernel-space State Graph
		terminal_writestring("[BOOT] Phase 2: Loading State Graph from MFT...\n");
		state_graph_init();
		state_graph_load_from_mft();

		// Phase 3: Initialize Intent Dispatcher
		terminal_writestring("[BOOT] Phase 3: Initializing Intent Dispatcher...\n");
		intent_dispatcher_init();

		// Phase 4: Initialize PCID system
		terminal_writestring("[BOOT] Phase 4: Initializing PCID...\n");
		extern int init_pcid_system_c(void);
		if (init_pcid_system_c() == 0) {
			terminal_writestring(" PCID enabled\n");
		} else {
			terminal_writestring(" PCID not supported (falling back to full TLB flushes)\n");
		}

// Phase 5: Input System (deferred - needs input.h API implementation)
// TODO: Once include/input.h API is implemented, spawn a user-mode input
// listener process and register a real notify callback here. Currently PS/2
// and USB HID keyboards work via their own drivers, but the abstract
// input.h layer has no implementations in src/.
// NOTE: state_graph_add_observer(NULL) registrations removed (they were dead code).

// Phase 6: Spawn Shell (CLI) and GUI Renderer as first two "Observer" processes
terminal_writestring("[BOOT] Phase 6: Starting UI Processes...\n");

terminal_writestring("[BOOT] Unified System Ready!\n\n");
		terminal_writestring("System is stable and running.\n");
		terminal_writestring("Keyboard driver needs PS/2 hardware support.\n");
	}

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

    terminal_writestring("[DONE] System ready!\n");

	terminal_writestring("\nStarting Shell...\n\n");

	// Initialize process system (needed for user-mode programs)
	process_init();
	terminal_writestring(" Process system initialized\n");

	// Start the shell
	shell_run();

	// Should never reach here
	terminal_writestring("\n[ERROR] Shell returned unexpectedly - halting\n");
	while (1) {
		__asm__ volatile ("hlt");
	}
}
