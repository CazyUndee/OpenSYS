# Codebase Audit Findings

This file records issues, risks, and potential future problems discovered during a review of the codebase.

## Executive Summary

The codebase is a solid early kernel skeleton, but several core paths are currently unsafe or inconsistent enough to break booting, paging, or development workflows.

The highest-risk problems are:

- the interrupt stack frame is malformed and the common ISR/IRQ stubs corrupt `ESP`
- paging initialization is internally inconsistent and likely loads a bad CR3 / page directory
- the Windows build script is missing required objects and will not link
- the IDT leaves unused vectors present with null handlers
- the PMM fallback path is effectively unimplemented
- build/test tooling and docs do not match the actual code paths

---

## Critical Correctness Issues

### 1) Interrupt frame layout is wrong, and the common stubs corrupt the stack
**Files:** `boot/interrupts.asm:10-36`, `boot/interrupts.asm:38-64`, `include/interrupts.h:9-16`, `src/interrupt_handlers.c:147-175`

**What I found**
- The assembly stubs push `ds/es/fs/gs` before calling C, but `struct cpu_state` starts with `ds` instead of `gs/fs/es/ds`.
- Both `isr_common_stub` and `irq_common_stub` do `pop esp` after the C handler returns.
- `sti` is executed before `iret`, which can re-enable interrupts before the saved state is restored.

**Impact**
- The C handlers will read the wrong register values from the stack.
- `pop esp` does not discard the argument; it loads a new stack pointer from the top of stack and can immediately corrupt execution.
- Any interrupt/exception path is likely to crash or triple fault.

**Recommendation**
- Make the C struct match the push order exactly, or change the push order to match the struct.
- Replace `pop esp` with normal stack cleanup (`add esp, 4` or `pop eax`).
- Remove `sti` from the common stubs; let `iret` restore IF.

---

### 2) Paging initialization is broken before paging is even enabled
**Files:** `src/paging.c:59-89`, `src/paging.c:168-199`, `src/paging.c:205-210`, `src/kernel.c:123-150`

**What I found**
- `get_or_create_pt()` writes to `pt = (pte_t*)0xFFC00000 + ...` before the self-map exists.
- `paging_init()` computes `pd_phys` via `paging_get_physical((uint32_t)kernel_pd)` before the PD is mapped, so it can return `0`.
- That `0` is then loaded into CR3.
- `paging_alloc()` ignores whether mapping actually succeeded and returns the virtual address anyway.
- `kernel_main()` immediately dereferences a high virtual address after `paging_init()` with no page-fault handler in place.

**Impact**
- Paging setup is very likely to fail with a fault or triple fault.
- A successful-looking `paging_alloc()` call can hide a mapping failure.
- The paging test in `kernel_main()` is dangerous because there is no safe exception path if paging is wrong.

**Recommendation**
- Initialize and access page tables using their physical addresses until recursive mapping is actually established.
- Do not derive PD physical address through the paging translator before the translator exists.
- Make `paging_alloc()` return an error if mapping fails and free the allocated physical page on failure.
- Add a page-fault handler before testing high virtual memory.

---

### 3) The Windows batch build is incomplete and will not link
**Files:** `build.bat:14-65`, `src/kernel.c:89-155`, `src/gdt.c:73-102`

**What I found**
- `build.bat` compiles `boot_c.c`, `interrupts.asm`, `kernel.c`, `vga.c`, `gdt.c`, `idt.c`, `interrupt_handlers.c`, and `keyboard.c`.
- It does **not** compile `src/pmm.c`, `src/paging.c`, or either `gdt_flush` implementation.
- `kernel.c` depends on PMM/paging symbols, and `gdt.c` depends on `gdt_flush`.

**Impact**
- The batch build should fail at link time with unresolved symbols.
- The Windows path is materially out of sync with the Makefile path.

**Recommendation**
- Make both build systems consume the same source list.
- Prefer generating the object list from one source of truth instead of hand-maintaining two separate build manifests.

---

## High-Priority Build and Runtime Hazards

### 4) The IDT marks every unused vector as present with a null handler
**Files:** `src/idt.c:39-45`, `src/idt.c:68-134`

**What I found**
- `idt_set_gate()` always ORs in `IDT_PRESENT`.
- `idt_init()` calls `idt_set_gate(i, 0, 0, 0)` for all 256 entries before installing the real handlers for vectors 0-47.

**Impact**
- Vectors 48-255 remain “present” but point at address `0`.
- Any accidental interrupt outside the installed range will jump to null and likely fault hard.

**Recommendation**
- Leave unused entries not present, or install a safe default stub that reports and halts cleanly.

---

### 5) The PMM fallback path is effectively unimplemented, and the bitmap code is fragile
**Files:** `src/pmm.c:23-33`, `src/pmm.c:54-137`, `src/pmm.c:165-182`, `include/pmm.h:22-36`

**What I found**
- The bitmap helpers use `1 << ...`, which becomes undefined for bit 31 on 32-bit `int`.
- If `MBOOT_FLAG_MMAP` is not set, the code estimates total memory from `mem_upper` but never frees any pages.
- `pmm_free_page()` accepts any pointer and does not validate alignment or reserved regions.
- `pmm_print_map()` is declared in the header but not implemented in the source.

**Impact**
- The allocator can silently be unusable without a memory map.
- Bad frees can corrupt the bitmap.
- The missing function is a future linkage trap if anything starts using it.
- The shift UB can bite as the project grows or gets recompiled under different assumptions.

**Recommendation**
- Use unsigned literals for all bitmap bit operations.
- Add alignment/range checks to free operations.
- Implement a real non-MMAP fallback or make the dependency explicit.
- Either define `pmm_print_map()` or remove the declaration.

---

### 6) The build/test workflow does not match the actual output path
**Files:** `Makefile:72-99`, `src/kernel.c:13-42`, `src/vga.c:24-93`

**What I found**
- The kernel currently writes only to VGA text memory.
- `make run` and `make test` invoke QEMU with `-serial stdio`, but there is no serial console driver.
- The ISO target writes a generated `iso/boot/grub/grub.cfg` instead of using the tracked `grub.cfg`.
- `grub-mkrescue` failure is swallowed by `|| echo ...`, so the target can appear successful even if no ISO was produced.

**Impact**
- The smoke test is unlikely to observe the kernel banner.
- Developers may think an ISO was created when it wasn’t.
- The checked-in GRUB config and the generated one can drift apart.

**Recommendation**
- Either add serial output or change the tests to validate VGA output another way.
- Make ISO generation fail loudly.
- Reuse the tracked GRUB config or delete the generated-copy path.

---

### 7) The project has two incompatible boot paths
**Files:** `boot/boot.asm:1-57`, `grub.cfg:1-6`, `boot/boot_c.c:10-18`, `README.md:63-114`, `README.md:163-171`

**What I found**
- `boot/boot_c.c` is Multiboot 1.
- `boot/boot.asm` is Multiboot 2.
- `grub.cfg` uses `multiboot /boot/kernel`, which matches Multiboot 1, not the ASM file.
- The README claims a C-only path and “no nasm required,” but the build still requires NASM for interrupt stubs.

**Impact**
- It is easy to pick the wrong boot path and end up with a non-booting image.
- The docs overstate the C-only story and understate the actual build requirements.

**Recommendation**
- Keep one boot path as the canonical implementation.
- Either remove the unused alternative or clearly isolate it as reference-only.
- Update the docs to match the real build.

---

### 8) The current kernel does not wire up the interrupt stack, IDT, or PIC
**Files:** `src/kernel.c:75-160`, `src/idt.c:68-134`, `src/interrupt_handlers.c:75-108`, `src/keyboard.c:153-170`

**What I found**
- `kernel_main()` initializes PMM and paging only.
- There is no call path for `gdt_init()`, `idt_init()`, `pic_remap()`, or `keyboard_init()` in the current boot flow.

**Impact**
- Large parts of the codebase are compiled but not actually exercised.
- Any future attempt to enable interrupts will run into the broken interrupt frame described above.

**Recommendation**
- Establish a real early boot sequence and wire the subsystems in explicitly.
- Keep initialization order documented in code, not just in the README.

---

## Medium-Priority Maintainability and Future-Risk Items

### 9) Interrupt handler storage uses `void*`, and error handling is incomplete
**Files:** `src/interrupt_handlers.c:57-67`, `src/interrupt_handlers.c:147-209`

**What I found**
- Handler tables are `void*` arrays rather than typed function-pointer arrays.
- `isr_handler()` only prints the error code if `regs->err_code != 0`.

**Impact**
- Type safety is weak.
- A valid zero-valued error code will be treated as “no code printed,” which makes debugging misleading.

**Recommendation**
- Store typed callbacks in typed arrays.
- Print error information based on the exception vector, not on whether the code happens to be nonzero.

---

### 10) The keyboard driver is not safe for concurrent IRQ/main-loop access
**Files:** `src/keyboard.c:13-17`, `src/keyboard.c:71-95`, `src/keyboard.c:101-170`

**What I found**
- The ring buffer indices are not `volatile` or protected.
- `keyboard_has_data()` reports controller status, not whether the internal buffer has data.
- `keyboard_init()` can spin forever waiting for the controller to become ready.

**Impact**
- Input can be racy if read outside interrupt context.
- The API name can mislead callers.
- A hung controller can stall boot.

**Recommendation**
- Decide whether the driver is interrupt-only or polled and document that contract.
- Add a timeout or recovery path.
- Make the API reflect buffered data vs hardware status clearly.

---

### 11) There are multiple duplicate/unused implementations that will drift apart
**Files:** `boot/boot_c.c:10-18`, `boot/boot.asm:1-18`, `src/gdt_flush.c:1-44`, `src/gdt_flush.asm:1-36`, `src/io.c:1-15`, `include/io.h:6-38`, `src/kernel.c:13-42`, `src/vga.c:15-93`

**What I found**
- Two boot stubs exist, but only one is used by the Makefile.
- Two `gdt_flush` implementations exist, but only one is built in each environment.
- `io.c` duplicates header-inline helpers and does not provide meaningful exported symbols.
- The kernel has its own VGA output path while the reusable VGA driver exists separately.

**Impact**
- The project is already showing drift between parallel implementations.
- The Multiboot 1 vs 2 mismatch is exactly the kind of bug this duplication invites.

**Recommendation**
- Keep one implementation per subsystem unless there is a strong reason to retain a second one.
- If a reference file is kept, mark it clearly as non-buildable reference material.

---

### 12) The GDT advertises user-mode support without a real TSS
**Files:** `src/gdt.c:27-102`, `include/gdt.h:20-48`

**What I found**
- User code/data selectors are defined.
- Entry 5 is reserved as a TSS placeholder, but no real TSS is initialized or loaded.

**Impact**
- Any future ring-3 work or privilege transitions will break if the project assumes those selectors are ready.
- The API implies a level of support that does not yet exist.

**Recommendation**
- Either remove the user-mode/TSS exports until they are actually implemented, or finish the TSS path before exposing them.

---

## Concise Priority Recommendations

1. Fix the interrupt stack frame and remove the `pop esp`/early `sti` bugs first.
2. Repair paging so it uses real physical addresses before recursive mapping exists.
3. Make the build scripts consistent across Unix and Windows.
4. Stop marking unused IDT entries present.
5. Tighten PMM validation and finish the fallback path.
6. Unify the console/output path and make tests observe the same output channel.
7. Remove or quarantine duplicate boot/helper implementations to prevent future drift.
