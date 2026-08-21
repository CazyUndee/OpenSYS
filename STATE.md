# STATE.md — Project Strategic State

## Current Version
Plan 0 v0.5.0

## Overall Roadmap

```
Phase 1: Build System & Boot (COMPLETE)
  ✓ Multiboot1/GRUB boot
  ✓ 64-bit long mode entry
  ✓ Serial + VGA output

Phase 2: Memory System (COMPLETE)
  ✓ PMM bitmap allocator
  ✓ 4-level paging (identity + higher-half)
  ✓ Kernel heap (linked-list allocator)
  ✓ Virtual memory per-process (vm.c)
  ✓ Copy-on-Write (COW) for fork

Phase 3: Interrupts & Timers (COMPLETE)
  ✓ IDT (256 entries)
  ✓ 8259 PIC remapping (IRQ 32-47)
  ✓ PIT timer at 1000 Hz
  ✓ Kernel panic with register dump

Phase 4: Drivers (COMPLETE)
  ✓ VGA text mode
  ✓ Serial (COM1)
  ✓ ATA PIO disk
  ✓ PS/2 keyboard (IRQ1, scan code set 1)
  ✓ USB HID keyboard (via UHCI)
  ✓ PCI enumeration
  ✓ UHCI (USB 1.1) host controller
  ✓ EHCI (USB 2.0) host controller
  ✓ RTC (real-time clock)
  ✓ GPT partition table
  ✓ Network driver stub (PCI NIC detection)
  ✗ NVMe driver (not started)
  ✗ AHCI/SATA driver (not started)

Phase 5: Process Management (FUNCTIONAL)
  ✓ Process creation (kernel + user mode)
  ✓ Round-robin scheduler with time slicing
  ✓ ELF64 loader
  ✓ User-mode execution (ring 3)
  ✓ System call interface (INT 0x80)
  ✓ fork() / exec() / wait() / exit()
  ✓ Preemptive context switch (wired to timer IRQ0)
  ✗ No signal handling
  ✗ No process groups or sessions

Phase 6: Filesystem (DUAL LAYER)
  ✓ ramfs (in-memory, used by VFS and shell)
  ✓ NTFS-style fs (MFT, attributes, resident/non-resident data)
  ✓ VFS abstraction layer
  ✓ Path resolution
  ✓ File create/read/write/delete/mkdir
  ✗ No disk persistence for ramfs
  ✗ fs.c and VFS not integrated (VFS uses ramfs only)

Phase 7: Shell & UI (FUNCTIONAL)
  ✓ Natural-language shell with 40+ commands
  ✓ Window management commands (open/close/move/focus)
  ✓ Intent dispatcher (command → state graph mutation)
  ✓ State graph (kernel-level UI node tree)
  ✓ Command history
  ✗ No graphical rendering (VGA text only)
  ✗ Window management is logical only, no visual compositing

Phase 8: Networking (EARLY)
  ✓ IP packet handling
  ✓ ICMP echo (ping)
  ✓ TCP stub
  ✗ No actual NIC driver yet (only PCI detection)
  ✗ No socket API

Phase 9: Stability & Testing (FUNCTIONAL)
  ✓ Host-side test suite compiles and runs (37/37 tests pass)
  ✓ Test framework uses longjmp for proper failure tracking
  ✓ Mock implementations for kernel functions (kmalloc, PMM, etc.)
  ✓ QEMU boot validation script (tools/qemu_boot_test.sh)
  ✗ No QEMU integration tests automated in CI
  ✗ No memory leak detection
  ✗ No kernel assertions/debug infrastructure
```

## Active Milestones

### Milestone: Production-Grade Kernel Foundation
- **Goal**: Clean build, no warnings, no unresolved symbols, bootable, testable
- **Status**: BUILD PASSES with zero compiler warnings and no linker errors
- **Test Suite**: 37/37 tests pass across 4 suites (Memory, Filesystem, Process, Integration)
- **Context Switch**: Preemptive multitasking wired to timer IRQ0
- **Remaining**: QEMU boot validation, CI test integration

## Architectural Decisions

### Decision: Monolithic kernel (no modules)
**Reason**: Simplicity for a from-scratch OS. All subsystems compile into a single binary.

### Decision: Natural-language shell commands
**Reason**: Plan 0 philosophy — no cryptic Unix abbreviations. Commands like "show files", "make directory", "go to". Short aliases (ls, mkdir, cd) also supported for familiarity.

### Decision: Intent dispatcher pattern
**Reason**: All user actions (shell commands, GUI clicks) are translated into typed `intent_t` structs and dispatched through a central system with ACL checking, schema validation, and audit logging. This provides a single mutation path for the state graph.

### Decision: State graph for UI state
**Reason**: A kernel-maintained tree of UI nodes (windows, buttons, etc.) that can be serialized to the filesystem for persistence. Observers subscribe for change notifications.

### Decision: Dual filesystem layers
**Reason**: `ramfs` is used for immediate functionality (shell file operations). The NTFS-style `fs.c` provides disk-backed persistence and is used by the state graph for saving UI state across reboots. The two are not yet unified under VFS.

### Decision: Use `interrupt_handlers.c` not `cpu_exceptions.c`
**Reason**: `cpu_exceptions.c` has incompatible function signatures and duplicate symbols. It is dead code and should not be compiled. `interrupt_handlers.c` is the authoritative exception/IRQ handler.

### Decision: `vm_space_t.pml4` stores physical address
**Reason**: Page table manipulation requires physical addresses for CR3 loads. Virtual pointers to page tables only work in identity-mapped regions.

### Decision: PS/2 keyboard driver polls via IRQ1
**Reason**: The PS/2 keyboard handler is called from `interrupts.asm` IRQ1 stub. It reads scan code set 1, tracks modifier state, and puts ASCII characters into a ring buffer. The shell polls `keyboard_has_key()` / `keyboard_getc()`.

### Decision: Preemptive context switch via interrupt frame swap
**Reason**: When the timer IRQ fires, the CPU pushes an interrupt frame onto the current process's kernel stack. The scheduler saves this RSP into the process context, switches to the next process's saved RSP, and executes `iretq` to resume it. This avoids the complexity of matching the `cpu_context_t` register layout with the interrupt frame layout.

### Decision: Standardize I/O port helpers via io.h
**Reason**: The `io.h` header provides canonical `outb`/`inb`/`outw`/`inw`/`io_wait` inline functions. Previously, `pic.c`, `timer.c`, and `serial.c` each defined local copies. Now all three include `io.h` instead.

### Decision: Host-side test suite uses mock kernel functions
**Reason**: Kernel functions like `kmalloc`, `pmm_get_total`, `memory_get_stats` cannot run on the host. The test suite includes `tests/mocks/mock_kernel.c` which provides stub implementations using host `malloc`/`free`. Tests validate structure layouts and constants, not runtime kernel behavior.

## Planned Changes

### Near-term
- Integrate ramfs with VFS (currently ramfs is hardcoded in VFS)
- Add `part.c` to disk I/O path for partitioned access
- Add QEMU integration tests to CI pipeline
- ~~Add kernel heap leak detection (magic number validation)~~ DONE — `kheap_validate()`/`kheap_dump()` implemented, `kheap_get_stats()` complete, exposed as `/proc/heap` (2026-08-21)
- ~~Filesystem usage accounting / honest df~~ DONE — `fs_get_stats()` (cluster bitmap + MFT) and `ramfs_get_stats()` implemented; `df` reports real size/used/avail plus live file/dir counts; 14-test ramfs suite added (2026-08-21)
- ~~Wire VFS into boot / live mount table~~ DONE — `ramfs_init()`+`vfs_init()` now called at boot (VFS was dead code); `vfs_mount` is idempotent; `vfs_mount_count()`/`vfs_get_mount()` accessors added; `/proc/mounts` and `mount` read the real VFS mount table (2026-08-21)
- ~~File descriptors through the VFS fd table~~ DONE — kernel-context fallback fd table added; `sys_open`/`sys_read`/`sys_write` route through `vfs_*`; 3 new fd round-trip tests (2026-08-21)
- ~~VFS pipes (IPC)~~ DONE — `vfs_pipe()` with a shared ring buffer + `SYS_PIPE` syscall + shell `pipe` command; fixed `alloc_node` same-slot bug and `vfs_open` node leak; 5 pipe tests (2026-08-21)

### Medium-term
- Add AHCI/SATA driver for modern disk access
- Implement proper ELF loading with ASLR
- Unified VFS mount for ramfs + NTFS-style fs
- Kernel debug infrastructure (assertions, stack traces)

### Long-term
- Graphics driver (VBE or EFI GOP)
- Network stack with real NIC driver
- User-space dynamic linking
- POSIX compatibility layer

## Known Issues

1. **Linker RWX warning**: The boot section is mapped as both writable and executable. This is inherent to the current memory layout — the boot section handles 16→32→64-bit transitions that require executable writable memory.
2. **tcp.o missing .note.GNU-stack**: The freestanding cross-compiler doesn't automatically add this section to C files. The linker warning is informational and harmless.
3. **Test suite validates structure layouts, not runtime behavior**: Host-side tests verify that kernel struct sizes and constants match expectations. Runtime behavior (actual kmalloc, paging, interrupts) requires QEMU validation.
4. **Two filesystem layers not unified**: `fs.c` (NTFS-style) and `ramfs.c` are separate. The VFS only uses ramfs. The state graph uses `fs.c` for persistence. Unification is planned.
