#!/bin/bash
set -e
apt-get update -qq && apt-get install -y -qq nasm xorriso grub-pc-bin grub-common binutils gcc > /dev/null 2>&1

echo "=== Compiling boot.asm ==="
nasm -f elf64 /os/boot/boot.asm -o /os/obj/boot.o

echo "=== Compiling kernel.c ==="
gcc -m64 -c /os/src/kernel/kernel.c -o /os/obj/kernel/kernel.o \
    -ffreestanding -O0 -Wall -Wextra -fno-exceptions -nostdlib -fno-builtin \
    -I/os/include -mcmodel=large -g -Wno-unused-variable -Wno-unused-parameter

echo "=== Compiling memory.c ==="
gcc -m64 -c /os/src/memory/memory.c -o /os/obj/memory/memory.o \
    -ffreestanding -O0 -Wall -Wextra -fno-exceptions -nostdlib -fno-builtin \
    -I/os/include -mcmodel=large -g -Wno-unused-variable -Wno-unused-parameter

echo "=== Linking kernel ==="
ld -m elf_x86_64 -T /os/linker/linker.ld -nostdlib -o /os/bin/kernel0.bin \
    /os/obj/boot.o \
    /os/obj/interrupts.o \
    /os/obj/context_switch.o \
    /os/obj/syscall_asm.o \
    /os/obj/switch_to_pcid.o \
    /os/obj/arch/*.o \
    /os/obj/drivers/*.o \
    /os/obj/fs/*.o \
    /os/obj/kernel/*.o \
    /os/obj/memory/*.o \
    /os/obj/process/*.o \
    /os/obj/ui/*.o

echo "=== Checking kernel ==="
ls -la /os/bin/kernel0.bin
echo "=== Kernel size check ==="
stat --format=%s /os/bin/kernel0.bin

echo "=== Building ISO ==="
mkdir -p /os/iso/boot/grub
cp /os/bin/kernel0.bin /os/iso/boot/kernel
cp /os/grub.cfg /os/iso/boot/grub/grub.cfg
grub-mkrescue -o /os/bin/os-docker.iso /os/iso 2>/dev/null
echo "=== ISO created ==="
ls -la /os/bin/os-docker.iso
