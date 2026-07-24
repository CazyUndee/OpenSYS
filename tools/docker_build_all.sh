#!/bin/bash
set -e

echo "=== Installing dependencies ==="
apt-get update -qq && apt-get install -y -qq nasm gcc xorriso grub-pc-bin grub-common binutils > /dev/null 2>&1

echo "=== Cleaning old kernel objects ==="
rm -f /os/obj/boot.o /os/obj/interrupts.o /os/obj/context_switch.o /os/obj/syscall_asm.o /os/obj/switch_to_pcid.o
rm -f /os/bin/kernel0.bin /os/bin/os-docker.iso
rm -rf /os/iso

echo "=== Compiling boot.asm ==="
nasm -f elf64 /os/boot/boot.asm -o /os/obj/boot.o

echo "=== Compiling extra ASM files ==="
nasm -f elf64 /os/boot/interrupts.asm -o /os/obj/interrupts.o
nasm -f elf64 /os/boot/context_switch.asm -o /os/obj/context_switch.o
nasm -f elf64 /os/boot/syscall.asm -o /os/obj/syscall_asm.o
nasm -f elf64 /os/boot/switch_to_pcid.asm -o /os/obj/switch_to_pcid.o

echo "=== Compiling arch ASM ==="
nasm -f elf64 /os/src/arch/gdt_flush.asm -o /os/obj/arch/gdt_flush.o

echo "=== Compiling C files ==="
CFLAGS="-m64 -ffreestanding -O0 -Wall -Wextra -fno-exceptions -nostdlib -fno-builtin -I/os/include -mcmodel=large -g -Wno-unused-variable -Wno-unused-parameter"

compile_c() {
    local src="$1"
    local obj="$2"
    mkdir -p "$(dirname "$obj")"
    echo "  CC $(basename "$src")"
    gcc $CFLAGS -c "$src" -o "$obj"
}

compile_c /os/src/kernel/kernel.c                /os/obj/kernel/kernel.o
compile_c /os/src/kernel/sys.c                   /os/obj/kernel/sys.o
compile_c /os/src/kernel/elf.c                   /os/obj/kernel/elf.o
compile_c /os/src/kernel/state_graph.c           /os/obj/kernel/state_graph.o
compile_c /os/src/kernel/intent_dispatcher.c     /os/obj/kernel/intent_dispatcher.o
compile_c /os/src/kernel/switch.c                /os/obj/kernel/switch.o
compile_c /os/src/memory/memory.c                /os/obj/memory/memory.o
compile_c /os/src/memory/kheap.c                 /os/obj/memory/kheap.o
compile_c /os/src/memory/paging.c                /os/obj/memory/paging.o
compile_c /os/src/fs/fs.c                        /os/obj/fs/fs.o
compile_c /os/src/fs/vfs.c                       /os/obj/fs/vfs.o
compile_c /os/src/fs/ramfs.c                     /os/obj/fs/ramfs.o
compile_c /os/src/arch/gdt.c                     /os/obj/arch/gdt.o
compile_c /os/src/arch/idt.c                     /os/obj/arch/idt.o
compile_c /os/src/arch/tss.c                     /os/obj/arch/tss.o
compile_c /os/src/arch/pic.c                     /os/obj/arch/pic.o
compile_c /os/src/arch/timer.c                   /os/obj/arch/timer.o
compile_c /os/src/arch/interrupt_handlers.c      /os/obj/arch/interrupt_handlers.o
compile_c /os/src/drivers/disk.c                 /os/obj/drivers/disk.o
compile_c /os/src/drivers/vga.c                  /os/obj/drivers/vga.o
compile_c /os/src/drivers/hid.c                  /os/obj/drivers/hid.o
compile_c /os/src/drivers/input.c                /os/obj/drivers/input.o
compile_c /os/src/drivers/pci.c                  /os/obj/drivers/pci.o
compile_c /os/src/drivers/usb_host.c             /os/obj/drivers/usb_host.o
compile_c /os/src/drivers/serial.c               /os/obj/drivers/serial.o
compile_c /os/src/drivers/io.c                   /os/obj/drivers/io.o
compile_c /os/src/drivers/part.c                 /os/obj/drivers/part.o
compile_c /os/src/drivers/partition_table.c      /os/obj/drivers/partition_table.o
compile_c /os/src/drivers/rtc.c                  /os/obj/drivers/rtc.o
compile_c /os/src/process/process.c              /os/obj/process/process.o
compile_c /os/src/process/scheduler.c            /os/obj/process/scheduler.o
compile_c /os/src/process/programs.c             /os/obj/process/programs.o
compile_c /os/src/process/vm.c                   /os/obj/process/vm.o
compile_c /os/src/ui/shell.c                     /os/obj/ui/shell.o
compile_c /os/src/ui/ui_command.c                /os/obj/ui/ui_command.o
compile_c /os/src/ui/ui_state.c                  /os/obj/ui/ui_state.o

echo "=== Linking kernel ==="
ld -m elf_x86_64 -T /os/linker/linker.ld -nostdlib -o /os/bin/kernel0.bin \
    /os/obj/boot.o \
    /os/obj/interrupts.o /os/obj/context_switch.o /os/obj/syscall_asm.o /os/obj/switch_to_pcid.o \
    /os/obj/kernel/*.o \
    /os/obj/memory/*.o \
    /os/obj/fs/*.o \
    /os/obj/arch/*.o \
    /os/obj/drivers/*.o \
    /os/obj/process/*.o \
    /os/obj/ui/*.o 2>&1

echo "=== Checking kernel ==="
ls -la /os/bin/kernel0.bin
stat --format=%s /os/bin/kernel0.bin

echo "=== Building ISO ==="
mkdir -p /os/iso/boot/grub
cp /os/bin/kernel0.bin /os/iso/boot/kernel
cp /os/grub.cfg /os/iso/boot/grub/grub.cfg
grub-mkrescue -o /os/bin/os-docker.iso /os/iso 2>/dev/null
echo "=== ISO created ==="
ls -la /os/bin/os-docker.iso
