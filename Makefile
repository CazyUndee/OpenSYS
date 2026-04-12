# OpenCode OS Makefile
# Supports both standard gcc (with -m32) and i686-elf-gcc cross-compiler

# Try to detect cross-compiler, fall back to native gcc
CROSS_PREFIX := $(shell which i686-elf-gcc 2>/dev/null || echo "")

ifeq ($(CROSS_PREFIX),)
  # No cross-compiler found, use native with -m32
  CC = gcc
  LD = ld
  NASM = nasm
  CFLAGS = -m32 -ffreestanding -O0 -g -Wall -Wextra -fno-exceptions -nostdlib -fno-builtin -Iinclude
  LDFLAGS = -m elf_i386 -T linker/linker.ld -nostdlib
  NASMFLAGS = -f elf32
else
  # Use cross-compiler
  CC = i686-elf-gcc
  LD = i686-elf-ld
  NASM = nasm
  CFLAGS = -ffreestanding -O0 -g -Wall -Wextra -fno-exceptions -nostdlib -fno-builtin -Iinclude
  LDFLAGS = -T linker/linker.ld -nostdlib
  NASMFLAGS = -f elf32
endif

SRCDIR = src
BOOTDIR = boot
OBJDIR = obj
BINDIR = bin

# Source files
BOOT_SRC = $(BOOTDIR)/boot_c.c
BOOT_ASM = $(BOOTDIR)/interrupts.asm
SOURCES = $(wildcard $(SRCDIR)/*.c)
OBJECTS = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SOURCES))
BOOT_OBJ = $(OBJDIR)/boot.o
ISR_OBJ = $(OBJDIR)/interrupts.o

TARGET = $(BINDIR)/kernel.bin
ISO = $(BINDIR)/os.iso

.PHONY: all clean run iso test check

all: $(TARGET)

# Directories
$(OBJDIR):
	@mkdir -p $(OBJDIR)

$(BINDIR):
	@mkdir -p $(BINDIR)

# Compile C boot file
$(BOOT_OBJ): $(BOOT_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# Assemble interrupt stubs
$(ISR_OBJ): $(BOOT_ASM) | $(OBJDIR)
	$(NASM) $(NASMFLAGS) -o $@ $<

# Compile kernel sources
$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# Link
$(TARGET): $(BOOT_OBJ) $(ISR_OBJ) $(OBJECTS) | $(BINDIR)
	$(LD) $(LDFLAGS) -o $@ $(BOOT_OBJ) $(ISR_OBJ) $(OBJECTS)
	@echo "========================================"
	@echo "Build complete: $@"
	@echo "========================================"
	@ls -la $@

# Create bootable ISO
iso: $(TARGET)
	@mkdir -p iso/boot/grub
	@cp $(TARGET) iso/boot/kernel
	@cp grub.cfg iso/boot/grub/grub.cfg
	@grub-mkrescue -o $(ISO) iso
	@echo "ISO creation complete: $(ISO)"

# Run in QEMU (direct kernel load)
run: $(TARGET)
	@echo "Starting QEMU..."
	@echo "Press Ctrl+A then X to exit"
	@echo ""
	qemu-system-i386 -kernel $(TARGET) -serial stdio -m 128

# Run ISO in QEMU
run-iso: iso
	qemu-system-i386 -cdrom $(ISO) -serial stdio -m 128

# Test build (CI/CD)
test: $(TARGET)
	@echo "Running basic boot test..."
	@timeout 5s qemu-system-i386 -kernel $(TARGET) -display none -serial stdio -m 128 2>&1 | grep -q "OpenCode OS" && echo "PASS: Kernel boots successfully" || (echo "FAIL: Kernel did not boot correctly"; exit 1)

# Check dependencies
check:
	@echo "Checking build dependencies..."
	@which $(CC) >/dev/null 2>&1 || (echo "ERROR: $(CC) not found. Install gcc or i686-elf-gcc"; exit 1)
	@which $(LD) >/dev/null 2>&1 || (echo "ERROR: $(LD) not found"; exit 1)
	@which $(NASM) >/dev/null 2>&1 || (echo "ERROR: $(NASM) not found. Install nasm"; exit 1)
	@which qemu-system-i386 >/dev/null 2>&1 || echo "WARNING: qemu-system-i386 not found (run target will fail)"
	@echo "All required tools found"

clean:
	@rm -rf $(OBJDIR) $(BINDIR) iso
	@echo "Cleaned build artifacts"

# Windows batch build (when make is not available)
win:
	@build.bat

# Show build configuration
info:
	@echo "OpenCode OS Build Configuration"
	@echo "==============================="
	@echo "CC: $(CC)"
	@echo "LD: $(LD)"
	@echo "NASM: $(NASM)"
	@echo "CFLAGS: $(CFLAGS)"
	@echo "LDFLAGS: $(LDFLAGS)"
	@echo ""
	@echo "Sources:"
	@echo "  Boot: $(BOOT_SRC)"
	@echo "  ISR: $(BOOT_ASM)"
	@echo "  Kernel: $(SOURCES)"
