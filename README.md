# OpenCode OS

A minimal x86 32-bit operating system kernel built from scratch.

## What We Built

This is a complete foundation for a custom OS:

- **Boot loader**: Multiboot 1 compliant, works with GRUB
- **Kernel**: C-based with VGA text mode driver
- **Linker script**: ELF32 format, loads at 0x100000 (1MB)
- **Build system**: Makefile + batch files

## Project Structure

```
/
├── boot/
│   └── boot_c.c          # C-only boot stub (no nasm required)
├── boot/
│   └── boot.asm          # Alternative NASM version
├── src/
│   └── kernel.c          # Main kernel with VGA driver
├── linker/
│   └── linker.ld         # ELF linker script
├── include/
│   ├── stdint.h          # Standard integer types
│   └── stddef.h          # Standard definitions
├── grub.cfg              # GRUB configuration for ISO
├── Makefile              # Unix build
├── build.bat             # Windows build
└── README.md             # This file
```

## Prerequisites

### Windows

1. **MSYS2** or similar POSIX environment:
   - Install from https://www.msys2.org/
   - Install packages:
     ```bash
     pacman -S mingw-w64-i686-gcc
     pacman -S mingw-w64-i686-binutils
     pacman -S mingw-w64-i686-nasm
     pacman -S qemu
     ```

2. Add to PATH:
   - `C:\msys64\mingw32\bin`
   - `C:\msys64\usr\bin`

### Linux/macOS

```bash
# Debian/Ubuntu
sudo apt-get install gcc-multilib nasm qemu-system-x86 grub-pc-bin

# macOS (with Homebrew)
brew install i686-elf-gcc nasm qemu
```

## Building

### Windows (with MSYS2)

```bash
# In MSYS2 MinGW 32-bit terminal
make check    # Verify tools
make          # Build kernel
make run      # Run in QEMU (direct load)
make iso      # Build bootable ISO
make run-iso  # Run ISO in QEMU
```

### Linux

```bash
# Check dependencies
make check

# Build
make

# Run in QEMU
make run

# Run tests
make test
```

### Cross-Compiler (recommended)

For more reliable builds, use a dedicated i686-elf cross-compiler:

```bash
# Download from: https://github.com/lordmilko/i686-elf-tools
# Or build your own following OSDev wiki instructions

# The Makefile will auto-detect i686-elf-gcc if available
```

## CI/CD

This project includes GitHub Actions workflows:

- **Build workflow** (`.github/workflows/build.yml`):
  - Compiles the kernel on every push/PR
  - Creates bootable ISO
  - Runs boot test in QEMU
  - Uploads artifacts

- **Status badges:**
  ![Build](https://github.com/CazyUndee/OpenSYS/workflows/Build%20OpenCode%20OS/badge.svg)

## What It Does

When booted, the kernel:

1. Initializes VGA text mode (80x25 color)
2. Validates Multiboot signature from GRUB
3. Prints boot status messages
4. Halts the CPU with interrupts disabled

Expected output:
```
OpenCode OS v0.1
==================

[OK] Multiboot validated
[OK] Kernel loaded at 0x100000
[OK] Stack initialized
[OK] VGA driver active
[OK] MBI at 0x...

System ready. Halting...
```

## Technical Details

### Boot Process

1. **BIOS** → loads GRUB bootloader
2. **GRUB** → parses Multiboot header, loads kernel to 0x100000
3. **GRUB** → jumps to `_start` with:
   - `EAX = 0x2BADB002` (Multiboot magic)
   - `EBX` = pointer to Multiboot info structure
4. **Boot stub** → sets up stack, captures registers
5. **Kernel** → initializes VGA, prints messages, halts

### Memory Layout

```
0x00000000 - 0x000FFFFF   Low 1MB (reserved, BIOS, VGA, etc.)
0x00100000 - 0x00??????   Kernel code/data (where we load)
    .multiboot             Multiboot header (must be first)
    .text                  Code
    .rodata                Read-only data
    .data                  Initialized data
    .bss                   Uninitialized data + stack
```

### Why No Assembly?

The boot stub is pure C because:
- Modern GCC generates proper 32-bit code
- Easier to read/maintain
- Still meets Multiboot spec
- Works with standard toolchain

Traditional Assembly boot files are included as `boot.asm` for reference.

## Next Steps

To extend this kernel:

1. **GDT/IDT** - Proper segmentation and interrupts
2. **Keyboard input** - Port I/O for user interaction
3. **Memory management** - Parse Multiboot memory map, implement kmalloc
4. **Paging** - Virtual memory
5. **Processes** - Context switching, multitasking
6. **Filesystem** - Read/write storage
7. **Userspace** - ELF loading, system calls

## Troubleshooting

### "Invalid multiboot magic"
- GRUB not loading as Multiboot. Check:
  - Header magic: `0x1BADB002`
  - Checksum is correct (negative sum of magic + flags)
  - Header is in `.multiboot` section at start of file

### Black screen
- VGA not initialized. Check:
  - Running in QEMU or real hardware with VGA
  - `terminal_initialize()` was called
  - `VGA_BUFFER = 0xB8000` is correct for color text mode

### Build errors
- Ensure 32-bit toolchain: `gcc -m32`
- Install multilib if on 64-bit Linux: `gcc-multilib`

## License

Public domain. Build your own OS from here.
