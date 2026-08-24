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
  ✓ Host test suite (124 tests) runs in CI and gates the verify job
  ✓ GRUB-ISO boot test in CI gates on reaching the shell (Starting Shell)
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
- ~~`cat` streams beyond 255 bytes~~ DONE — `read`/`cat` now print the whole file in chunks instead of silently truncating at 255 bytes; verified under QEMU/WHPX 70/70 (2026-08-24)
- ~~`grep` command + NL-parser hijack fix~~ DONE — `grep <pat> <file>` searches real and virtual files (line numbers, 4 KiB cap); nl_parser now only claims phrases whose verb is at the start (after fillers), so mid-command verb tokens like "grep Uptime /proc/uptime" no longer hijack into `uptime`; 3 new host assertions; verified under QEMU/WHPX 65/65 (2026-08-24)
- ~~Read-only markers in `ls`~~ DONE — listings mark protected files `[RO]` (full-path resolution in list_callback via list_cur_dir); virtual entries unaffected; verified under QEMU/WHPX 56/56 (2026-08-24)
- ~~Shell command history recall (arrow keys)~~ DONE — PS/2 driver now handles 0xE0-prefixed extended keys (Up→`\x01`, Down→`\x02`, others ignored); shell input loop recalls history with line redraw; `history_add` dedups consecutive entries so up-up walks older commands; verified under QEMU/WHPX 8/8 + vcat regression 53/53 (2026-08-24)
- ~~Recursive directory copy~~ DONE — `copy <dir> <dst>` mirrors a tree (fs_readdir walk, fs_mkdir subdirs, byte copy with truncate); Unix `cp -r` semantics (existing dir → inside it); self-copy/descendant refused; depth guard; verified under QEMU/WHPX 53/53 (2026-08-24)
- ~~Recursive `find` + index-root corruption fix~~ DONE — shell `find` now walks the real tree recursively (prints full paths, descends subdirs, depth guard) instead of the count-only intent search; fixed `add_dir_entry` reading uninitialized/stale `last->length` on an empty index in a reused MFT slot (caused `ls` on a dir to show `(empty)` after create/delete churn); verified under QEMU/WHPX 46/46 incl. nested find + churn scenario (2026-08-24)
- ~~fs_stat + fs_is_directory~~ DONE — `fs_stat` (declared but missing) fills the filename attr (name/size/times/parent); `fs_is_directory` added alongside `fs_is_readonly`; shell `info` rewritten on top of them so it works on directories and shows Type + Access (read-only/read-write); bare `info <name>` alias; 1 new test; verified under QEMU/WHPX 40/40 (2026-08-24)
- ~~fs_rmdir (safe directory deletion)~~ DONE — implemented (was declared but missing): refuses non-empty/read-only directories; `fs_unlink` now refuses directories so a bare `delete` can never orphan children; shell `delete` tries `fs_unlink` then `fs_rmdir`; 1 new test; verified under QEMU/WHPX 33/33 incl. mkdir→child→refuse→child-delete→rmdir flow (2026-08-24)
- ~~fs_unlink index consistency~~ DONE — deleting a file now removes its parent directory index entry, so a later file reusing the freed MFT slot is not listed under the old name (phantom entries); verified under QEMU/WHPX 28/28 (2026-08-23)
- ~~fs_rename (atomic move)~~ DONE — implemented (was declared but missing): updates filename attr + directory index entry in place, supports cross-directory moves via index-entry removal/addition; shell `move`/`rename` use it first so the read-only flag and clusters survive; copy+delete fallback kept; 1 new test; verified under QEMU/WHPX 23/23 incl. chmod→move→delete-refused flow (2026-08-23)
- ~~File truncation / VFS_O_TRUNC~~ DONE — `fs_truncate` implemented (frees clusters, shrinks resident attr, syncs real_size); fs VFS adapter honors `VFS_O_TRUNC`; shell `copy` truncates the destination so overwriting a larger file leaves no stale tail; 1 new test; verified under QEMU/WHPX 15/15 incl. copy-overwrite (2026-08-23)
- ~~Standard fds 0/1/2 (stdio)~~ DONE — console device backs stdin/stdout/stderr in every fd table; `dup2(pipe, 1)` redirects stdout (shell `stdio` command demos it); `fs_write` now keeps `file->size` in sync (fixes EOF/seek after write); 4 std-fd tests; verified under QEMU/WHPX 2/2 plus dup/pipe/vcat regressions green (2026-08-23)
- ~~Integrate ramfs with VFS (currently ramfs is hardcoded in VFS)~~ DONE — `find_ops()` is now path-aware: longest-prefix match over the mount table (component-boundary aware), bare names resolve against the root mount, and `/proc`/`/sys`/`/dev` (NULL-ops mounts) route through a new vfile VFS adapter (`vfile_vfs_ops`: open/close/read/write/size); `vfs_open` uses `ops->size` instead of a hardcoded ramfs size; `vfile_exists()` added; 7 new tests (path dispatch, virtual files through the fd layer, dynamic `/proc/self/fd`); verified end-to-end under QEMU/WHPX via the new `vcat` shell command (reads any path through the fd layer) 6/6 (2026-08-23)
- Add `part.c` to disk I/O path for partitioned access
- ~~Add QEMU integration tests to CI pipeline~~ DONE — CI now runs the full 124-test host suite (`host-tests` job gates `verify`) and the boot test is a hard gate booting the GRUB ISO (`-cdrom bin/os.iso`) and requiring `Starting Shell` in serial output; `tools/qemu_boot_test.sh` fixed for the relocated toolchain (2026-08-21)
- ~~Add kernel heap leak detection (magic number validation)~~ DONE — `kheap_validate()`/`kheap_dump()` implemented, `kheap_get_stats()` complete, exposed as `/proc/heap` (2026-08-21)
- ~~Filesystem usage accounting / honest df~~ DONE — `fs_get_stats()` (cluster bitmap + MFT) and `ramfs_get_stats()` implemented; `df` reports real size/used/avail plus live file/dir counts; 14-test ramfs suite added (2026-08-21)
- ~~Wire VFS into boot / live mount table~~ DONE — `ramfs_init()`+`vfs_init()` now called at boot (VFS was dead code); `vfs_mount` is idempotent; `vfs_mount_count()`/`vfs_get_mount()` accessors added; `/proc/mounts` and `mount` read the real VFS mount table (2026-08-21)
- ~~File descriptors through the VFS fd table~~ DONE — kernel-context fallback fd table added; `sys_open`/`sys_read`/`sys_write` route through `vfs_*`; 3 new fd round-trip tests (2026-08-21)
- ~~VFS pipes (IPC)~~ DONE — `vfs_pipe()` with a shared ring buffer + `SYS_PIPE` syscall + shell `pipe` command; fixed `alloc_node` same-slot bug and `vfs_open` node leak; 5 pipe tests (2026-08-21)
- ~~/proc/self/fd introspection~~ DONE — `/proc/self/fd` directory + `/proc/self/fdinfo` file + `/proc/self/fd/<N>` dynamic read-through-descriptor; `vfs_fd_count()`/`vfs_fd_info()` accessors; 5 tests (2026-08-21)
- ~~fd duplication~~ DONE — `vfs_dup`/`vfs_dup2` (shared-node POSIX semantics, dup2 replaces target, same-fd no-op) + `SYS_DUP`/`SYS_DUP2` syscalls + shell `dup` command; 4 tests; enables shell redirection (2026-08-21)
- ~~Shell redirection~~ DONE — `cmd > file` truncate / `cmd >> file` append via `terminal_capture_begin/end()`; captured output written through the normal file path (virtual or real); fixed the `>>` split bug that leaked a `>` into the command; verified end-to-end under QEMU/WHPX 9/9 (2026-08-21)
- ~~Permissions~~ DONE — `MFT_FLAG_READONLY` + `fs_set_readonly`/`fs_is_readonly`; fs_open denies write/append on read-only files, fs_unlink denies deletion; shell `chmod <file>` / `chmod -w` (protect) / `chmod +w` (unprotect); verified end-to-end under QEMU/WHPX 8/8 (2026-08-21)
- ~~QEMU integration tests in CI~~ DONE — `host-tests` job runs the full 128-test suite and gates `verify`; boot test is a hard gate booting the GRUB ISO (`-cdrom bin/os.iso`) and requiring `Starting Shell`; also fixed the unindented lines that broke the workflow YAML and the stale `tools/qemu_boot_test.sh` toolchain path (2026-08-21)

### Medium-term
- Add AHCI/SATA driver for modern disk access
- Implement proper ELF loading with ASLR
- ~~Unified VFS mount for ramfs + NTFS-style fs~~ DONE — `fs_vfs_ops` adapter exposes the real fs.c filesystem through the VFS fd layer; mounted at `/` at boot (vfile keeps `/proc`/`/sys`/`/dev` via longest-prefix); `fs_seek` implemented; functional fs mock in host suite + 4 adapter tests; verified end-to-end under QEMU/WHPX (`write` a real file, `vcat` it back through fds) 9/9 (2026-08-23)
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
4. **Two filesystem layers not fully unified**: `fs.c` (NTFS-style) and `ramfs.c` are separate backends. `find_ops()` dispatches by mount path — the root `/` now serves `fs.c` via `fs_vfs_ops` (mounted at boot), `/proc`/`/sys`/`/dev` route to vfile. `ramfs` remains as the VFS root only in host tests (which call `vfs_init()` directly); the running kernel serves real files through the fd layer. `fs_seek` (declared but unimplemented) is now implemented. Remaining: the shell's `read`/`write`/`copy`/`delete` commands still call `fs_*` directly rather than through fds (behaviorally identical, just not yet unified at the command layer).
