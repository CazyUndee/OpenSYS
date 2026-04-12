# OpenSYS OS

A 64-bit operating system kernel built from scratch with modern features.

## Features

- **64-bit kernel**: Full long mode support (16-bit -> 32-bit -> 64-bit transition)
- **Memory management**: PMM with bitmap allocator, 4-level paging, kernel heap
- **OpenFS filesystem**: NTFS-style with Master File Table (MFT) and attributes
- **GPT support**: GUID Partition Table driver with ATA disk access
- **USB keyboard**: HID keyboard driver framework
- **Natural language shell**: Human-readable commands (not cryptic Unix names)

## Project Structure

```
/
├── boot/
│   ├── boot64.asm      # 64-bit long mode bootstrap
│   ├── boot.asm        # 32-bit alternative
│   ├── boot_c.c        # C boot stub
│   └── interrupts.asm  # ISR/IRQ stubs
├── src/
│   ├── kernel64.c      # 64-bit kernel main
│   ├── pmm64.c         # Physical memory manager
│   ├── paging64.c      # 4-level paging
│   ├── kheap64.c       # Kernel heap allocator
│   ├── fs.c            # OpenFS implementation
│   ├── gpt.c           # GPT partition driver
│   ├── disk.c          # ATA disk driver
│   ├── hid_keyboard.c  # USB HID keyboard
│   └── shell.c         # Natural language shell
├── include/
│   ├── stdint.h        # Standard integer types
│   ├── stddef.h        # Standard definitions
│   ├── pmm.h           # PMM interface
│   ├── paging.h        # Paging structures
│   ├── kheap.h         # Heap allocator
│   ├── fs.h            # OpenFS structures
│   ├── gpt.h           # GPT driver
│   ├── disk.h          # ATA driver
│   ├── usb.h           # USB host controller
│   ├── uhci.h          # UHCI driver
│   └── hid_keyboard.h  # USB HID keyboard
├── linker/
│   ├── linker64.ld     # 64-bit linker script
│   └── linker.ld       # 32-bit linker script
├── grub.cfg            # GRUB configuration
├── Makefile            # Build system
├── build.bat           # Windows build script
└── .github/workflows/
    └── build.yml       # GitHub Actions CI
```

## Prerequisites

### Windows (MSYS2)

```bash
pacman -S mingw-w64-x86_64-gcc
pacman -S mingw-w64-x86_64-binutils
pacman -S mingw-w64-x86_64-nasm
pacman -S qemu
```

Add to PATH:
- `C:\msys64\mingw64\bin`
- `C:\msys64\usr\bin`

### Linux

```bash
sudo apt-get install gcc nasm qemu-system-x86 grub-pc-bin xorriso
```

## Building

```bash
make check     # Verify tools
make arch64    # Build 64-bit kernel
make iso       # Create bootable ISO
make run-iso   # Run in QEMU
```

## Boot Process

1. **BIOS** -> GRUB bootloader
2. **GRUB** -> Loads kernel at 2MB, validates Multiboot
3. **boot64.asm** -> 16-bit real mode -> 32-bit protected mode -> 64-bit long mode
4. **kernel64.c** -> PMM init, paging, heap, filesystem, shell

## Memory Layout

```
0x0000000000000000 - 0x00007FFFFFFFFFFF  User space (128TB)
0xFFFF800000000000 - 0xFFFF8000000FFFFF  Physical memory map
0xFFFF800000100000 - 0xFFFF800000FFFFFF  Kernel heap
0xFFFF800001000000 - ...                  Higher-half kernel code
```

## OpenFS Filesystem

NTFS-inspired design:
- Master File Table (MFT) with attribute records
- Supports: `$FILE_NAME`, `$DATA`, `$STANDARD_INFORMATION`
- File names as UTF-16LE (converted to ASCII)
- Resident data storage for small files

## Shell Commands

Natural language commands:

| Command | Description |
|---------|-------------|
| `show files` | List files in current directory |
| `show file <name>` | Display file contents |
| `make folder <name>` | Create directory |
| `make file <name>` | Create empty file |
| `remove <name>` | Delete file/folder |
| `go to <path>` | Change directory |
| `help` | Show all commands |

## CI/CD

![Build](https://github.com/CazyUndee/OpenSYS/workflows/Build%20OpenCode%20OS/badge.svg)

Automated builds via GitHub Actions.

## Next Steps

- [ ] USB controller initialization (UHCI/EHCI)
- [ ] More shell commands
- [ ] Non-resident file data for large files
- [ ] Real hardware testing

## License

Public domain.
