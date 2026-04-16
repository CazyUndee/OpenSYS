# OpenSYS OS Makefile
# Supports 32-bit and 64-bit builds

# Architecture selection
ARCH ?= 64

ifeq ($(ARCH),64)
# 64-bit build
CC = gcc
LD = ld
NASM = nasm
CFLAGS = -m64 -ffreestanding -O0 -g -Wall -Wextra -fno-exceptions -nostdlib -fno-builtin -Iinclude -mcmodel=large
LDFLAGS = -m elf_x86_64 -T linker/linker64.ld -nostdlib
NASMFLAGS = -f elf64
KERNEL = kernel64
SRCS = kernel64.c pmm64.c paging64.c kheap64.c fs.c gpt.c disk.c hid_keyboard.c shell.c vga.c io.c interrupt_handlers.c usb.c process.c scheduler.c vm.c elf.c syscall.c idt64.c gdt64.c tss.c user_bin.c
else
# 32-bit build
CROSS_PREFIX := $(shell which i686-elf-gcc 2>/dev/null || echo "")
ifeq ($(CROSS_PREFIX),)
CC = gcc
LD = ld
CFLAGS = -m32 -ffreestanding -O0 -g -Wall -Wextra -fno-exceptions -nostdlib -fno-builtin -Iinclude
LDFLAGS = -m elf_i386 -T linker/linker.ld -nostdlib
else
CC = i686-elf-gcc
LD = i686-elf-ld
CFLAGS = -ffreestanding -O0 -g -Wall -Wextra -fno-exceptions -nostdlib -fno-builtin -Iinclude
LDFLAGS = -T linker/linker.ld -nostdlib
endif
NASM = nasm
NASMFLAGS = -f elf32
KERNEL = kernel
SRCS = kernel.c pmm.c paging.c gdt.c gdt_flush.c keyboard.c vga.c io.c idt.c interrupt_handlers.c fs.c gpt.c disk.c hid_keyboard.c shell.c
endif

SRCDIR = src
BOOTDIR = boot
OBJDIR = obj
BINDIR = bin

BOOT_ASM = $(BOOTDIR)/boot$(if $(filter 64,$(ARCH)),64,).asm
SOURCES = $(SRCS:%=$(SRCDIR)/%)
OBJECTS = $(SRCS:%.c=$(OBJDIR)/%.o)
BOOT_OBJ = $(OBJDIR)/boot.o
ISR_OBJ = $(OBJDIR)/interrupts.o

TARGET = $(BINDIR)/$(KERNEL).bin
ISO = $(BINDIR)/os.iso

.PHONY: all clean run iso test check arch32 arch64

all: $(TARGET)

arch32:
	$(MAKE) ARCH=32

arch64:
	$(MAKE) ARCH=64

$(OBJDIR):
	@mkdir -p $(OBJDIR)

$(BINDIR):
	@mkdir -p $(BINDIR)

$(BOOT_OBJ): $(BOOT_ASM) | $(OBJDIR)
	$(NASM) $(NASMFLAGS) -o $@ $<

$(ISR_OBJ): $(BOOTDIR)/interrupts.asm | $(OBJDIR)
	$(NASM) $(NASMFLAGS) -o $@ $<

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(TARGET): $(BOOT_OBJ) $(ISR_OBJ) $(OBJECTS) | $(BINDIR)
	$(LD) $(LDFLAGS) -o $@ $(BOOT_OBJ) $(ISR_OBJ) $(OBJECTS)
	@echo "========================================"
	@echo "Build complete: $@"
	@echo "========================================"

iso: $(TARGET)
	@mkdir -p iso/boot/grub
	@cp $(TARGET) iso/boot/kernel
	@cp grub.cfg iso/boot/grub/grub.cfg
	@grub-mkrescue -o $(ISO) iso
	@echo "ISO creation complete: $(ISO)"

run: $(TARGET)
	@echo "Starting QEMU..."
	qemu-system-x86_64 -kernel $(TARGET) -serial stdio -m 128

run-iso: iso
	qemu-system-x86_64 -cdrom $(ISO) -serial stdio -m 128

test: $(TARGET)
	@echo "Running basic boot test..."
	@timeout 5s qemu-system-x86_64 -kernel $(TARGET) -display none -serial stdio -m 128 2>&1 | grep -q "OpenSYS" && echo "PASS" || echo "FAIL"

check:
	@echo "Checking build dependencies..."
	@which $(CC) >/dev/null 2>&1 || (echo "ERROR: $(CC) not found"; exit 1)
	@which $(LD) >/dev/null 2>&1 || (echo "ERROR: $(LD) not found"; exit 1)
	@which $(NASM) >/dev/null 2>&1 || (echo "ERROR: $(NASM) not found"; exit 1)
	@echo "All required tools found"

clean:
	@rm -rf $(OBJDIR) $(BINDIR) iso
	@echo "Cleaned build artifacts"

info:
	@echo "OpenSYS OS Build Configuration"
	@echo "ARCH: $(ARCH)"
	@echo "CC: $(CC)"
	@echo "LD: $(LD)"
	@echo "CFLAGS: $(CFLAGS)"
	@echo "Sources: $(SRCS)"
