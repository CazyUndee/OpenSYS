# AGENTS.md — How to Work in Plan 0

## Architecture Overview

Plan 0 is a pure 64-bit operating system built from scratch. It boots via Multiboot1/GRUB, enters long mode directly from 16-bit assembly, and runs a monolithic kernel in ring 0 with a basic user-mode program execution path.

### Subsystem Map

```
boot/                  Assembly bootstrap and interrupt stubs
  boot.asm             Multiboot header, 16→32→64-bit transition
  interrupts.asm       ISR/IRQ stubs, pushes register frame, calls C handlers
  context_switch.asm   Task context switch (not yet wired to scheduler)
  syscall.asm          INT 0x80 entry, calls syscall_handler()
  switch_to_pcid.asm   PCID (Process Context ID) TLB optimization

src/kernel/            Core kernel
  kernel.c             kernel_main() — init sequence and shell launch
  sys.c                syscall_handler() — INT 0x80 dispatch table
  elf.c                ELF64 loader for user programs
  state_graph.c        Kernel-level state graph (UI node tree, persisted to MFT)
  intent_dispatcher.c  Intent dispatch system (command → state graph mutation)
  intent_schema.c      Schema validation for intent types
  switch.c             Context switch helpers
  kstring.c            Kernel string utilities (k_strcmp, k_strlen, k_memcpy, etc.)

src/memory/            Memory management
  memory.c             PMM — bitmap allocator, 1 page per bit
  paging.c             4-level paging (PML4), identity + higher-half mapping
  kheap.c              Simple linked-list kernel heap (kmalloc/kfree)

src/arch/              CPU infrastructure
  gdt.c                64-bit GDT with TSS
  idt.c                256-entry IDT, ISR/IRQ/syscall gates
  tss.c                Task State Segment (rsp0 for kernel stack)
  pic.c                8259 PIC remapping (IRQ 32-47)
  timer.c              PIT at 1000 Hz (1ms tick)
  interrupt_handlers.c C-side ISR/IRQ handlers, kernel panic with register dump
  cpu_exceptions.c     DEAD CODE — alternative exception handler, not in build
  gdt_flush.asm        LGDT/LTR trampoline

src/drivers/           Device drivers
  vga.c                VGA text 80x25, terminal_* API
  serial.c             COM1 (0x3F8), serial output for QEMU
  io.c                 I/O port helpers (outb/inb/inw/outw in header io.h)
  disk.c               ATA PIO driver (primary master)
  rtc.c                Real-time clock (CMOS registers)
  pci.c                PCI config space read/write, bus enumeration
  ps2_keyboard.c       PS/2 keyboard driver (IRQ1, scan code set 1)
  hid.c                USB HID keyboard (interrupt transfers via UHCI)
  input.c              Unified input layer (PS/2 + USB HID)
  usb_host.c           UHCI (USB 1.1) host controller driver
  ehci.c               EHCI (USB 2.0) host controller driver
  net.c                Network driver stub (PCI NIC detection)
  part.c               GPT partition manager (wraps GPT driver)
  partition_table.c    GPT header parsing and partition enumeration

src/fs/                Filesystem
  fs.c                 NTFS-style filesystem (MFT, attributes, resident/non-resident data)
  vfs.c                VFS layer with ramfs backend, fd table per process
  ramfs.c              In-memory filesystem (no disk required)
  path.c               Path resolution helper (absolute/relative)
  vfile.c              Namespace resource content layer (0/system, 0/dev, 0/hardware)

src/process/           Process management
  process.c            PCB allocation, kernel/user process creation, ELF loading
  scheduler.c          Round-robin scheduler with time slicing
  programs.c           Embedded user binary data (auto-generated)
  vm.c                 Virtual memory per-process (page tables, COW, fork)

src/net/               Networking (early stage)
  ip.c                 IP packet handling
  icmp.c               ICMP echo (ping)
  tcp.c                TCP stub

src/ui/                User interface
  shell.c              Natural-language shell (main loop, command dispatch)
  nl_parser.c          Natural language phrase parser ("list files in documents" → cmd triple)
  ui_command.c         UI command parsing (window open/close/move/focus)
  ui_state.c           UI state management (windows, nodes, processes)

user/                  User-space programs
  init.c               Simple test program (prints hello, runs syscall)
  user.ld              User-mode linker script
```

## Build System

### Toolchain
- **Compiler**: `x86_64-elf-gcc` (cross-compiler, at `/d/tools/x86_64-elf/bin/`)
- **Assembler**: `nasm` (from MSYS2 `/c/msys64/usr/bin/nasm.exe`)
- **Linker**: `x86_64-elf-ld`
- **Flags**: `-m64 -ffreestanding -mcmodel=large -fno-asynchronous-unwind-tables`
- The `io.h` header defines `outb`/`inb`/`outw`/`inw` as inline asm functions. Some `.c` files (pic.c, timer.c, serial.c) define their own local `outb`/`inb` instead of including `io.h` — this is inconsistent but works.

### Building
```bash
export PATH="/d/tools/x86_64-elf/bin:/c/msys64/usr/bin:$PATH"
make clean && make all    # Build kernel
make iso                  # Create bootable ISO (requires grub-mkrescue)
```

### Source Files in Build
The Makefile `SRCS` variable lists all C and ASM files compiled into the kernel. If you add a new source file, it MUST be added to the appropriate `*_SRCS` variable in the Makefile.

### Key Directories
- `obj/` — build artifacts (gitignored)
- `bin/` — output kernel binary (gitignored)
- `include/` — all kernel headers

## Conventions

### Naming
- Functions: `subsystem_verb_noun()` — e.g., `pmm_alloc_page()`, `timer_get_ticks()`
- Constants: `UPPER_SNAKE_CASE` — e.g., `FS_CLUSTER_SIZE`, `MFT_ENTRY_SIZE`
- Types: `snake_case_t` — e.g., `process_t`, `kheap_block_t`
- Header guards: `SUBSYSTEM_H` — e.g., `#ifndef PROCESS_H`

### File Organization
- Each subsystem has a `src/subsystem/` directory and corresponding `include/subsystem.h` header
- Boot assembly lives in `boot/`, arch assembly in `src/arch/`
- The header `io.h` provides canonical I/O port helpers (`outb`, `inb`, `outw`, `inw`, `io_wait`). All drivers should include it instead of defining local copies.

### Error Handling
- Most functions return `0` for success, negative for error, or NULL pointer for allocation failure
- Kernel panics are triggered by `panic()` in `interrupt_handlers.c` for unrecoverable exceptions
- The shell catches keyboard input via polling (`keyboard_has_key()` / `keyboard_getc()`)

## Important Pitfalls

1. **PS/2 keyboard was missing** — The header `ps2_keyboard.h` existed but the implementation file was absent. Always verify that every header's functions have a corresponding `.c` implementation and that the file is in the Makefile.

2. **Duplicate exception handlers** — `cpu_exceptions.c` has `isr_handler()` and `irq_handler()` with different signatures than `interrupt_handlers.c`. Only ONE can be in the build. The Makefile uses `interrupt_handlers.c`.

3. **No cross-compilation on CI** — The CI uses `gcc` (host compiler) not `x86_64-elf-gcc`. This works on Ubuntu x86_64 but is technically wrong for a freestanding kernel. The `-ffreestanding` flag prevents host library linking.

4. **Linker warnings** — `gdt_flush.asm` needs `.note.GNU-stack` section. The RWX LOAD segment warning comes from the boot section being mapped as both writable and executable — inherent to the current design.

5. **Two filesystem layers** — `fs.c` (NTFS-style on disk) and `ramfs.c` (in-memory) are both present. The VFS layer (`vfs.c`) currently only uses `ramfs`. The disk-based `fs.c` is mounted separately and used by the state graph for persistence.

6. **Preemptive context switch** — The timer IRQ0 handler in `boot/interrupts.asm` calls `scheduler_timer_tick()` → `switch_timer_tick()`. This saves the current RSP (pointing at the interrupt frame) into the process context, picks the next process, and calls `context_switch()` which uses `iretq` to resume the new process. The context_switch assembly in `boot/context_switch.asm` handles register save/restore.

6. **`vm_space_t.pml4` is a physical address** — It stores the physical address of the PML4 table, not a virtual pointer. Code that accesses page tables through it must use identity-mapped addresses or physical-to-virtual translation.

7. **Stack-allocated VLA in `fs_read`** — `fs.c` uses `uint8_t cluster_buf[FS_CLUSTER_SIZE]` (4KB on stack). This is fine for now but may cause stack overflows with deep call chains.

8. **Host-side test suite** — Tests in `tests/` compile with host `gcc` (not cross-compiler). The `tests/mocks/mock_kernel.c` provides stub implementations of `kmalloc`, `kfree`, `pmm_*`, `memory_*` using host `malloc`/`free`. Tests validate structure layouts and constants, not runtime kernel behavior. The test framework uses `setjmp`/`longjmp` for proper failure tracking via ASSERT macros.

9. **Namespace resources** — `vfile.c` registers resource content under the Plan0 namespace (`0/system/*`, `0/dev/*`, `0/hardware/*`) with VFS-internal keys under `/0/...`; legacy Unix-style paths are REMOVED. Shell arguments starting with a digit-slash are translated by `ns_to_fs_path()` in `dispatch()`. `vfile_init()` must be called before `shell_run()`. Writable resources: `0/dev/null`, `0/dev/zero`, `0/dev/console`, `0/system/hostname`. `vfile_irq_tick()` is called from `irq_handler()` to feed `0/system/interrupts`.

10. **Natural-language shell** — `nl_parser.c` translates English-like phrases ("list files in documents", "write hello world to notes.txt") into the canonical (cmd, arg1, arg2) triple. The parser only claims unambiguous phrases; everything else falls through to the token-based parser. Tests in `tests/unit/test_nl_parser.c`.

## Rules for Future Agents

1. Never add a source file without adding it to the Makefile `*_SRCS` variable.
2. Never add a source file without also adding it to `.github/workflows/ci.yml` driver/kernel loop.
3. Never use `cpu_exceptions.c` — it is dead code with conflicting symbols.
4. Preserve the `terminal_*` API (wraps `vga_*` + `serial_*`). All output goes through both VGA and serial.
5. The `io.h` header defines I/O port helpers. Prefer including it over defining local `outb`/`inb`.
6. Run `make clean && make all` to verify the build after any changes.
7. Run tests with `cd tests && export PATH="/c/msys64/mingw64/bin:/c/msys64/usr/bin:$PATH" && make clean && make all TMP=C:/tmp TEMP=C:/tmp && ./bin/test_runner`. The MSYS2 gcc needs a writable temp dir — pass `TMP=C:/tmp` as a make variable.
8. When adding virtual file resources, add both the read_fn and optionally write_fn to `vfile_init()`, update the directory listing callbacks, and add tests to `tests/unit/test_vfile.c`.
9. Never hardcode the version string. Use `#include "version.h"` and the macros `PLAN0_VERSION`, `PLAN0_FULL_NAME`, etc. Changing the version in `include/version.h` updates it everywhere.
10. When adding PCI mocks for tests, add stubs to `tests/mocks/mock_kernel.c` — the test suite compiles `vfile.c` directly, so any kernel function it calls must have a host-side mock.
