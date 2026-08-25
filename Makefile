# Plan 0 v0.4.0 - Build System
# 
# Copyright (C) 2026 CazyUndee
# 
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU Affero General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Affero General Public License for more details.
# 
# You should have received a copy of the GNU Affero General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

# Plan 0 Makefile
# 64-bit only build

# Tools
CC = x86_64-elf-gcc
LD = x86_64-elf-ld
NASM = nasm
OBJCOPY = x86_64-elf-objcopy
BINDIR_ABS = /c/Users/roone.DESKTOP-QK3UG2M/x86_64-elf/bin

# Flags
CFLAGS = -m64 -ffreestanding -O0 -g -Wall -Wextra -fno-exceptions -nostdlib -fno-builtin -Iinclude -mcmodel=large -fno-asynchronous-unwind-tables -B $(BINDIR_ABS)/
USER_CFLAGS = -m64 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector -mcmodel=small -g0 -B $(BINDIR_ABS)/ -fno-dwarf2-cfi-asm
LDFLAGS = -m elf_x86_64 -T linker/linker.ld -nostdlib
NASMFLAGS = -f elf64

# Targets
KERNEL = kernel0
# Source files organized by directory
KERNEL_SRCS = kernel/kernel.c kernel/sys.c kernel/elf.c kernel/state_graph.c kernel/intent_dispatcher.c kernel/intent_schema.c kernel/switch.c kernel/kstring.c
MEMORY_SRCS = memory/memory.c memory/kheap.c memory/paging.c
FS_SRCS = fs/fs.c fs/fs_vfs.c fs/vfs.c fs/ramfs.c fs/path.c fs/vfile.c fs/ns.c
ARCH_SRCS = arch/gdt.c arch/gdt_flush.asm arch/idt.c arch/tss.c arch/pic.c arch/timer.c arch/interrupt_handlers.c
DRIVER_SRCS = drivers/disk.c drivers/vga.c drivers/hid.c drivers/input.c drivers/ps2_keyboard.c drivers/pci.c drivers/usb_host.c drivers/serial.c drivers/io.c drivers/part.c drivers/partition_table.c drivers/rtc.c drivers/ehci.c drivers/net.c
PROCESS_SRCS = process/process.c process/scheduler.c process/programs.c process/vm.c
NET_SRCS = net/ip.c net/icmp.c net/tcp.c
UI_SRCS = ui/shell.c ui/ui_command.c ui/ui_state.c ui/nl_parser.c

SRCS = $(KERNEL_SRCS) $(MEMORY_SRCS) $(FS_SRCS) $(ARCH_SRCS) $(DRIVER_SRCS) $(PROCESS_SRCS) $(UI_SRCS) $(NET_SRCS)

# Directories
SRCDIR = src
BOOTDIR = boot
OBJDIR = obj
BINDIR = bin

# Files
BOOT_ASM = $(BOOTDIR)/boot.asm
SOURCES = $(SRCS:%=$(SRCDIR)/%)
C_OBJECTS = $(patsubst %.c,$(OBJDIR)/%.o,$(filter %.c,$(SRCS)))
ASM_OBJECTS = $(patsubst %.asm,$(OBJDIR)/%.o,$(filter %.asm,$(SRCS)))
OBJECTS = $(C_OBJECTS) $(ASM_OBJECTS)
BOOT_OBJ = $(OBJDIR)/boot.o
ISR_OBJ = $(OBJDIR)/interrupts.o
CTXTSW_OBJ = $(OBJDIR)/context_switch.o
SYSCALL_OBJ = $(OBJDIR)/syscall_asm.o
PCID_OBJ = $(OBJDIR)/switch_to_pcid.o
EXTRA_ASM_OBJS = $(ISR_OBJ) $(CTXTSW_OBJ) $(SYSCALL_OBJ) $(PCID_OBJ)

TARGET = $(BINDIR)/$(KERNEL).bin
ISO = $(BINDIR)/os.iso

# Phony targets
.PHONY: all clean run run-iso run-usb run-wsl run-wsl-iso run-wsl-usb run-wsl-gui iso test check smoke-test smoke-test-wsl

# Default target
all: user_bin $(TARGET)

# User binary: embed into src/process/programs.c and include/user_bin.h
user_bin: user/init.bin
	python tools/embed_binary.py user/init.bin include/user_bin
	python tools/embed_binary.py user/init.bin src/process/programs

# Create directories
$(OBJDIR):
	@mkdir -p $(OBJDIR)

$(BINDIR):
	@mkdir -p $(BINDIR)

# Boot assembly
$(BOOT_OBJ): $(BOOT_ASM) | $(OBJDIR)
	$(NASM) $(NASMFLAGS) -o $@ $<

# 64-bit assembly objects
$(ISR_OBJ): $(BOOTDIR)/interrupts.asm | $(OBJDIR)
	$(NASM) $(NASMFLAGS) -o $@ $<

$(CTXTSW_OBJ): $(BOOTDIR)/context_switch.asm | $(OBJDIR)
	$(NASM) $(NASMFLAGS) -o $@ $<

$(SYSCALL_OBJ): $(BOOTDIR)/syscall.asm | $(OBJDIR)
	$(NASM) $(NASMFLAGS) -o $@ $<

$(PCID_OBJ): $(BOOTDIR)/switch_to_pcid.asm | $(OBJDIR)
	$(NASM) $(NASMFLAGS) -o $@ $<

# C objects - handle subdirectories
$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

# Assembly objects - handle subdirectories
$(OBJDIR)/%.o: $(SRCDIR)/%.asm | $(OBJDIR)
	@mkdir -p $(dir $@)
	$(NASM) $(NASMFLAGS) -o $@ $<

# Link kernel
$(TARGET): $(BOOT_OBJ) $(EXTRA_ASM_OBJS) $(OBJECTS) | $(BINDIR)
	$(LD) $(LDFLAGS) -o $@ $(BOOT_OBJ) $(EXTRA_ASM_OBJS) $(OBJECTS)
	@echo "========================================"
	@echo "Build complete: $@"
	@echo "========================================"

# Create ISO (requires grub-mkrescue)
iso: $(TARGET)
	@mkdir -p iso/boot/grub
	@cp $(TARGET) iso/boot/kernel
	@cp grub.cfg iso/boot/grub/grub.cfg
	@grub-mkrescue -o $(ISO) iso
	@echo "ISO creation complete: $(ISO)"

# Create ISO using Python/pycdlib (no grub-mkrescue needed)
pyiso:
	@python tools/create_hybrid_iso.py

# Run in QEMU
run: $(TARGET)
	@echo "Starting QEMU..."
	qemu-system-x86_64 -kernel $(TARGET) -serial stdio -m 128

run-iso: pyiso
	qemu-system-x86_64 -cdrom $(ISO) -serial stdio -m 128

run-usb: pyiso
	qemu-system-x86_64 -drive file=$(ISO),if=ide,format=raw -serial stdio -m 128

# WSL QEMU targets (use -nographic for headless operation)
run-wsl: $(TARGET)
	@echo "Starting QEMU (WSL mode)..."
	qemu-system-x86_64 -kernel $(TARGET) -nographic -serial stdio -m 128

run-wsl-iso: pyiso
	qemu-system-x86_64 -cdrom $(ISO) -nographic -serial stdio -m 128

run-wsl-usb: pyiso
	qemu-system-x86_64 -drive file=$(ISO),if=ide,format=raw -nographic -serial stdio -m 128

# WSL with display (requires X server on Windows, e.g. VcXsrv)
run-wsl-gui: $(TARGET)
	@echo "Starting QEMU (WSL+GUI mode)..."
	qemu-system-x86_64 -kernel $(TARGET) -serial stdio -m 128

# Basic boot smoke test
smoke-test: $(TARGET)
	@echo "Running basic boot test..."
	@timeout 5s qemu-system-x86_64 -kernel $(TARGET) -display none -serial stdio -m 128 2>&1 | grep -q "Plan 0" && echo "PASS" || echo "FAIL"

# WSL smoke test
smoke-test-wsl: $(TARGET)
	@echo "Running basic boot test (WSL)..."
	@timeout 10s qemu-system-x86_64 -kernel $(TARGET) -display none -serial stdio -m 128 2>&1 | grep -q "Plan 0" && echo "PASS" || echo "FAIL"

# Check dependencies
check:
	@echo "Checking build dependencies..."
	@which $(CC) >/dev/null 2>&1 || (echo "ERROR: $(CC) not found"; exit 1)
	@which $(LD) >/dev/null 2>&1 || (echo "ERROR: $(LD) not found"; exit 1)
	@which $(NASM) >/dev/null 2>&1 || (echo "ERROR: $(NASM) not found"; exit 1)
	@echo "All required tools found"

# User space build
user/init.o: user/init.c
	$(CC) -m64 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector -mcmodel=small -g0 -c -o $@ $<

user/init.elf: user/init.o user/user.ld
	$(LD) -T user/user.ld -nostdlib -o $@ $<

user/init.bin: user/init.elf
	$(OBJCOPY) -O binary $< $@

# Test targets
test:
	@echo "Running Plan 0 test suite..."
	@$(MAKE) -C tests test

test-unit:
	@echo "Running unit tests..."
	@$(MAKE) -C tests unit

test-integration:
	@echo "Running integration tests..."
	@$(MAKE) -C tests integration

test-all:
	@echo "Running all tests with verbose output..."
	@$(MAKE) -C tests test-verbose

test-clean:
	@echo "Cleaning test build artifacts..."
	@$(MAKE) -C tests clean

# Clean build artifacts
clean:
	@rm -rf $(OBJDIR) $(BINDIR) iso
	@rm -f user/init.o user/init.elf user/init.bin
	@echo "Cleaned build artifacts"

clean-all: clean test-clean
	@echo "Cleaned all build artifacts"

# Show build configuration
info:
	@echo "Plan 0 Build Configuration (64-bit)"
	@echo "CC: $(CC)"
	@echo "LD: $(LD)"
	@echo "CFLAGS: $(CFLAGS)"
	@echo "Sources: $(SRCS)"
