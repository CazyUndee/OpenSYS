"""
Build proper bootable image for QEMU.
Strategy: Create a disk image with a minimal bootloader that loads the kernel
using INT 13h, parses ELF, and jumps to the Multiboot entry point.

Then also build a proper ISO with GRUB by patching cdboot.img.
"""
import os, struct

GRUB_DIR = r'C:\Users\roone.DESKTOP-QK3UG2M\Downloads\projects\os\usr\lib\grub\i386-pc'
KERNEL = r'C:\Users\roone.DESKTOP-QK3UG2M\Downloads\projects\os\bin\kernel0.bin'
GRUB_CFG = r'C:\Users\roone.DESKTOP-QK3UG2M\Downloads\projects\os\grub.cfg'
OUT_DIR = r'C:\Users\roone.DESKTOP-QK3UG2M\Downloads\projects\os\bin'

# ============================================================
# Strategy 1: Patch cdboot.img and build proper GRUB ISO boot image
# ============================================================
def build_grub_boot_image():
    """Create a proper GRUB CD boot image from components."""
    
    # Read the GRUB components
    with open(os.path.join(GRUB_DIR, 'cdboot.img'), 'rb') as f:
        cdboot = bytearray(f.read())
    
    with open(os.path.join(GRUB_DIR, 'diskboot.img'), 'rb') as f:
        diskboot = f.read()
    
    with open(os.path.join(GRUB_DIR, 'kernel.img'), 'rb') as f:
        kernel_img = f.read()
    
    print(f"cdboot.img:    {len(cdboot)} bytes ({len(cdboot)//2048} CD sectors)")
    print(f"diskboot.img:  {len(diskboot)} bytes ({len(diskboot)//512} sectors)")
    print(f"kernel.img:    {len(kernel_img)} bytes")
    
    # Build core.img as it should be on disk:
    # core.img = diskboot.img (512 bytes) + kernel.img
    # The diskboot.img is loaded first and it loads kernel.img
    core_img = diskboot + kernel_img
    
    # Pad core_img to full CD sector (2048 bytes)
    if len(core_img) % 2048 != 0:
        core_img += b'\x00' * (2048 - len(core_img) % 2048)
    
    core_sectors = len(core_img) // 2048
    core_size_bytes = len(diskboot) + len(kernel_img)  # Actual data size, not padded
    
    print(f"core.img:      {len(core_img)} bytes ({core_sectors} CD sectors)")
    print(f"  (data only: {core_size_bytes} bytes)")
    
    # Now we need to patch cdboot.img at the right offsets
    # From disassembly analysis:
    # cdboot.img loads ecx from [bx+0x0D] = absolute offset 0x10 -> SIZE IN BYTES
    # cdboot.img loads esi from [bx+0x09] = absolute offset 0x0C -> LBA
    
    # The LBA in cdboot.img is the absolute CD sector where core.img starts
    # We'll place core_data right after cdboot.img, so at LBA 1
    LBA = 1  # core.img starts at CD sector 1
    
    # Patch the size field at offset 0x10 (4 bytes, little-endian)
    struct.pack_into('<I', cdboot, 0x10, core_size_bytes)
    # Patch the LBA field at offset 0x0C (4 bytes, little-endian)
    struct.pack_into('<I', cdboot, 0x0C, LBA)
    
    print(f"\nPatched cdboot.img:")
    print(f"  LBA at 0x0C: {struct.unpack_from('<I', cdboot, 0x0C)[0]} (sector {LBA})")
    print(f"  SIZE at 0x10: {struct.unpack_from('<I', cdboot, 0x10)[0]} bytes ({core_size_bytes//2048 + (1 if core_size_bytes%2048 else 0)} sectors)")
    
    # The boot image = cdboot.img + core.img
    boot_image = bytes(cdboot) + core_img
    
    print(f"\nTotal boot image: {len(boot_image)} bytes ({len(boot_image)//2048} CD sectors)")
    
    # Save the boot image
    boot_path = os.path.join(OUT_DIR, 'grub_boot.img')
    with open(boot_path, 'wb') as f:
        f.write(boot_image)
    print(f"Saved to: {boot_path}")
    
    # Now, this boot image needs to be placed at the START of the ISO
    # (in the System Area, sectors 0+). The El Torito catalog should
    # point to the boot image starting at LBA 0.
    
    # But we also need to verify that this cdboot.img patch is correct.
    # The cdboot.img loading process:
    # 1. BIOS loads 1 sector (2048 bytes) of boot image (cdboot.img) at 0000:7C00
    # 2. cdboot.img runs, loads core_data from LBA 1 (next sector)
    # 3. Jumps to loaded core at 0x0820:0000
    
    # NOTE: diskboot.img normally expects the total sector count to be patched
    # at a specific offset. Let me check what offset that is.
    # In GRUB's diskboot.S: the sector count is at offset 0x4 (4 bytes)
    # But our diskboot.img might have it wrong since we didn't patch it.
    
    # Let me check:
    struct_offset = -1
    # In GRUB diskboot.S, the total_sectors field is at offset 0x04 (4 bytes)
    # It stores the total number of 512-byte sectors of core.img
    total_512_sectors = (len(diskboot) + len(kernel_img) + 511) // 512
    print(f"\ndiskboot.img: patching total_sectors at offset 0x4 = {total_512_sectors}")
    print(f"  Current value at 0x4: {diskboot[4]}")
    
    # Actually, looking at the GRUB source, diskboot.S:
    # The sector count is at offset 0x00 + 6 = 0x06
    # buffer_address at end:
    # _start:
    #   jmp after_BPB
    #   nop
    # after_BPB:
    #   ...
    #   movb $0x90, %al    ; 0x90 = "kernel.img" size field marker
    #   ...
    # The sector count is typically at offset 6 or at a specific marker.
    
    # Let me look for the kernel_img size in diskboot.img
    # The GRUB diskboot.S code does:
    # Total sectors = (kernel_img_size - start) / 512
    # where start = first sector after diskboot.img header
    
    # In the diskboot.img binary, let's look for the value that represents
    # the total sectors of the boot image
    kernel_total_sectors = (len(diskboot) + len(kernel_img) + 511) // 512
    
    # Standard GRUB convention: offset 0x4 in diskboot.img = total sectors
    # Let me write the patched version
    diskboot_patched = bytearray(diskboot)
    
    # The sector count could be at various offsets in diskboot.img
    # Let me try offset 4 (the most common location in GRUB2)
    # OR it might be at offset 2 (which is 0xBE=190 in our diskboot.img)
    current_val = struct.unpack_from('<H', diskboot, 2)[0]
    print(f"  Word at offset 2: {current_val} (0x{current_val:x})")
    
    # Actually, the sector count is often stored at the END of diskboot.img
    # or at specific marker locations
    # Let me look for a 16-bit value that could be the sector count
    for off in range(0, 512 - 2, 2):
        val = struct.unpack_from('<H', diskboot, off)[0]
        # Check if this value could represent the total sectors
        # kernel_total_sectors is about 69 (for 35124 bytes / 512)
        if val == kernel_total_sectors or abs(val - kernel_total_sectors) < 5:
            print(f"  Found possible sector count {val} at offset 0x{off:x}")
    
    # The sector count for kernel.img (without diskboot) would be:
    kernel_only_sectors = (len(kernel_img) + 511) // 512
    print(f"  kernel.img alone: {kernel_only_sectors} sectors")
    
    # I'm going to try patching the sector count at offset 0x04
    # This is the most common convention for GRUB diskboot
    struct.pack_into('<I', diskboot_patched, 0x04, kernel_total_sectors)
    print(f"  Patched total_sectors at offset 0x04 = {kernel_total_sectors}")
    
    # Rebuild core.img with patched diskboot
    core_img2 = bytes(diskboot_patched) + kernel_img
    if len(core_img2) % 2048 != 0:
        core_img2 += b'\x00' * (2048 - len(core_img2) % 2048)
    
    # Rebuild boot image
    boot_image2 = bytes(cdboot) + core_img2
    
    boot_path2 = os.path.join(OUT_DIR, 'grub_boot2.img')
    with open(boot_path2, 'wb') as f:
        f.write(boot_image2)
    print(f"\nSaved patched boot image to: {boot_path2}")
    print(f"  {len(boot_image2)} bytes ({len(boot_image2)//2048} CD sectors)")
    
    return boot_path2, core_img2

# ============================================================
# Strategy 2: Minimal multiboot-compatible bootloader
# ============================================================
def build_minimal_bootloader():
    """
    Write a tiny x86 bootloader that:
    1. Reads kernel from disk sectors (the kernel ELF is loaded directly after the bootloader)
    2. Parses ELF program headers
    3. Loads segments
    4. Jumps to entry point with Multiboot signature
    
    The kernel binary is embedded directly after the 512-byte boot sector.
    """
    
    with open(KERNEL, 'rb') as f:
        kernel_data = f.read()
    
    # Check ELF header
    if kernel_data[:4] != b'\x7f\x45\x4c\x46':
        print("Not an ELF file!")
        return None
    
    ei_class = kernel_data[4]
    if ei_class != 2:
        print("Not a 64-bit ELF!")
        return None
    
    # Get program headers
    e_phoff = struct.unpack_from('<Q', kernel_data, 32)[0]
    e_phnum = struct.unpack_from('<H', kernel_data, 56)[0]
    e_entry = struct.unpack_from('<Q', kernel_data, 24)[0]
    e_phentsize = struct.unpack_from('<H', kernel_data, 54)[0]
    
    print(f"\nKernel ELF: entry=0x{e_entry:x}, {e_phnum} PHs")
    
    # We need a bootloader in 512 bytes (boot sector)
    # The bootloader will:
    # 1. Load rest of disk into memory
    # 2. Parse ELF
    # 3. Jump to entry
    
    # Since the bootloader is complex, let's use GRUB's cdboot.img instead
    # and focus on getting the ISO right.
    
    return None

# ============================================================
# Strategy 3: Create disk image with GRUB-style boot
# ============================================================
def create_hybrid_disk_image():
    """
    Create a disk image that works with QEMU's -drive option.
    Use boot_hybrid.img as the MBR.
    
    Layout:
    Sector 0: boot_hybrid.img (512 bytes) - MBR
    Sectors 1-63: core.img (diskboot.img + kernel.img + padding)
    Sector 64: Partition start
    
    The challenge: core.img must fit in 63 sectors (32256 bytes).
    kernel.img is 34612 bytes, too big.
    
    Instead, use a layout where the first partition starts at sector 1
    (or very close) and GRUB is installed on that partition.
    """
    pass

# ============================================================
# Main 
# ============================================================
def main():
    print("=" * 60)
    print("Building GRUB boot image from components")
    print("=" * 60)
    
    boot_path, _ = build_grub_boot_image()
    
    print("\n" + "=" * 60)
    print("Next step: Rebuild ISO with this boot image")
    print("=" * 60)
    print(f"1. The boot image ({boot_path}) needs to be placed")
    print("   in the ISO's System Area (sectors 0-N)")
    print("2. The El Torito catalog should point to LBA 0")
    print("3. The pycdlib ISO builder needs to be updated")

if __name__ == '__main__':
    main()
