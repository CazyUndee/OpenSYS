"""
Create a bootable disk image for QEMU.
Uses: boot_hybrid.img as MBR, diskboot.img + kernel.img as boot loader,
      FAT filesystem for /boot/grub/ contents.
"""
import os, struct, shutil

OUTPUT = r'C:\Users\roone.DESKTOP-QK3UG2M\Downloads\projects\os\bin\os_disk.img'
GRUB_DIR = r'C:\Users\roone.DESKTOP-QK3UG2M\Downloads\projects\os\usr\lib\grub\i386-pc'
KERNEL = r'C:\Users\roone.DESKTOP-QK3UG2M\Downloads\projects\os\bin\kernel0.bin'
GRUB_CFG = r'C:\Users\roone.DESKTOP-QK3UG2M\Downloads\projects\os\grub.cfg'

# Read GRUB boot files
with open(os.path.join(GRUB_DIR, 'boot_hybrid.img'), 'rb') as f:
    boot_hybrid = f.read()

with open(os.path.join(GRUB_DIR, 'diskboot.img'), 'rb') as f:
    diskboot = f.read()

with open(os.path.join(GRUB_DIR, 'kernel.img'), 'rb') as f:
    kernel_img = f.read()

# Build core.img = diskboot.img + kernel.img
# diskboot.img loads kernel.img (both are linked together in a core.img)
# In a proper core.img, diskboot.img is the first sector (512 bytes) and
# kernel.img follows. diskboot.img has the total size of core.img baked in.
core_img = diskboot + kernel_img

# Pad to sector boundary (512 bytes)
if len(core_img) % 512 != 0:
    core_img += b'\x00' * (512 - len(core_img) % 512)

print(f"boot_hybrid.img: {len(boot_hybrid)} bytes")
print(f"diskboot.img: {len(diskboot)} bytes")
print(f"kernel.img: {len(kernel_img)} bytes")
print(f"core.img: {len(core_img)} bytes ({len(core_img)//512} sectors)")

# Create disk image: 64MB
DISK_SIZE = 64 * 1024 * 1024
SECTOR_SIZE = 512

# Layout:
# Sector 0: MBR (boot_hybrid.img)
# Sector 1: Reserved (embed area for GRUB)
# Sector 2-127: Reserved
# Sector 128: FAT32 partition starts (this is where filesystem lives)
# 
# But actually for simplicity, let's use the simpler layout:
# Sector 0: MBR (boot.img or boot_hybrid.img)
# Sectors 1-63: Post-MBR gap (can contain core.img)
# Sector 64: Partition starts (FAT32)
#
# boot_hybrid.img is designed for this layout - the partition entry in it
# points to the first partition.

# Actually, let me check what boot_hybrid.img contains
# If it has the partition table ready, we need to match it

# Let's look at the MBR partition table
print(f"\nMBR partition entry at 0x1BE: {boot_hybrid[0x1BE:0x1CE].hex()}")

# If the partition table is empty (all zeros), we need to fill it
pe = boot_hybrid[0x1BE:0x1CE]
if pe == b'\x00' * 16:
    print("Partition table is empty - need to create it")

# Approach: start with a blank disk image, add partition, then write MBR
print("\nCreating disk image...")
# Open file and size it
with open(OUTPUT, 'wb') as f:
    f.truncate(DISK_SIZE)

# We'll fill this in using the partition approach
# Actually, the simplest approach: put everything in a single partition
# starting at sector 2048 (1MB alignment), use FAT32

# Patch MBR partition table:
# CHS geometry: 64 heads, 32 sectors/track (standard for disk images)
HEADS = 64
SPT = 32  # sectors per track
total_sectors = DISK_SIZE // SECTOR_SIZE
cylinders = total_sectors // (HEADS * SPT)

# Partition 1 starts at sector 2048
PART1_START = 2048
PART1_SIZE = total_sectors - PART1_START

# CHS values for partition start
c1 = PART1_START // (HEADS * SPT)
h1 = (PART1_START // SPT) % HEADS
s1 = (PART1_START % SPT) + 1

# CHS values for partition end
last_sector = total_sectors - 1
c2 = last_sector // (HEADS * SPT)
h2 = (last_sector // SPT) % HEADS
s2 = (last_sector % SPT) + 1

if c1 > 1023: c1 = 1023
if c2 > 1023: c2 = 1023

pentry = struct.pack('<B', 0x80)  # Bootable
pentry += struct.pack('<BBB', h1, s1, c1 & 0xFF)  # CHS start
pentry += struct.pack('<B', 0x0C)  # FAT32 (LBA)
pentry += struct.pack('<BBB', h2, s2, c2 & 0xFF)  # CHS end
pentry += struct.pack('<I', PART1_START)  # LBA start
pentry += struct.pack('<I', PART1_SIZE)  # LBA size

print(f"Partition 1: LBA start={PART1_START}, size={PART1_SIZE} sectors, type=0x0C (FAT32)")
print(f"  CHS start: {h1}/{s1}/{c1}")
print(f"  CHS end:   {h2}/{s2}/{c2}")

# Write MBR with partition table
mbr = bytearray(boot_hybrid)
mbr[0x1BE:0x1CE] = pentry
# Ensure boot signature
mbr[510] = 0x55
mbr[511] = 0xAA

with open(OUTPUT, 'r+b') as f:
    f.write(mbr)
    # Write core.img in post-MBR gap (sectors 1-63)
    # core.img starts at sector 1
    core_start_sector = 1
    f.seek(core_start_sector * SECTOR_SIZE)
    f.write(core_img)
    print(f"core.img written at sector {core_start_sector}")

# Now create a FAT32 filesystem on the partition
# We'll use Python to write a basic FAT32
print(f"\nCreating FAT32 filesystem at partition (sector {PART1_START})...")

def write_sector(disk, sector, data):
    disk.seek(sector * SECTOR_SIZE)
    disk.write(data[:SECTOR_SIZE].ljust(SECTOR_SIZE, b'\x00'))

# FAT32 BIOS Parameter Block (at partition start)
# https://en.wikipedia.org/wiki/Design_of_the_FAT_file_system
sectors_per_cluster = 1  # 512 bytes per cluster (minimum for small images)
reserved_sectors = 32
num_fats = 2
root_entries = 0  # not used in FAT32
total_sectors_16 = 0
media_descriptor = 0xF8  # fixed disk
sectors_per_fat = 0  # filled below
sectors_per_track = SPT
num_heads = HEADS
hidden_sectors = PART1_START
total_sectors_32 = PART1_SIZE

# Calculate FAT size
total_clusters = (total_sectors_32 - reserved_sectors) // sectors_per_cluster
# FAT32 entries are 4 bytes each
sectors_per_fat_est = ((total_clusters * 4) + SECTOR_SIZE - 1) // SECTOR_SIZE
# Round up to a reasonable size
sectors_per_fat = max(sectors_per_fat_est, 1)

# Root directory cluster (first cluster of root directory)
root_cluster = 2

# First data sector
first_data_sector = reserved_sectors + (num_fats * sectors_per_fat)

print(f"  Sectors per cluster: {sectors_per_cluster}")
print(f"  Reserved sectors: {reserved_sectors}")
print(f"  Sectors per FAT: {sectors_per_fat}")
print(f"  First data sector: {first_data_sector}")
print(f"  Total clusters: {total_clusters}")

# Build BPB
bpb = b''
# Jump code (x86 jump over BPB)
bpb += struct.pack('<B', 0xEB)
bpb += struct.pack('<B', 0x58)
bpb += struct.pack('<B', 0x90)
# OEM name
bpb += b'MSWIN4.1'
# Bytes per sector
bpb += struct.pack('<H', SECTOR_SIZE)
# Sectors per cluster
bpb += struct.pack('<B', sectors_per_cluster)
# Reserved sectors
bpb += struct.pack('<H', reserved_sectors)
# Number of FATs
bpb += struct.pack('<B', num_fats)
# Root entries (not used in FAT32)
bpb += struct.pack('<H', root_entries)
# Total sectors 16
bpb += struct.pack('<H', total_sectors_16)
# Media descriptor
bpb += struct.pack('<B', media_descriptor)
# Sectors per FAT 16
bpb += struct.pack('<H', 0)
# Sectors per track
bpb += struct.pack('<H', sectors_per_track)
# Number of heads
bpb += struct.pack('<H', num_heads)
# Hidden sectors
bpb += struct.pack('<I', hidden_sectors)
# Total sectors 32
bpb += struct.pack('<I', total_sectors_32)

# FAT32 specific fields
# Sectors per FAT 32
bpb += struct.pack('<I', sectors_per_fat)
# Mirroring flags
bpb += struct.pack('<H', 0)
# Filesystem version
bpb += struct.pack('<H', 0)
# Root directory cluster
bpb += struct.pack('<I', root_cluster)
# FSInfo sector
bpb += struct.pack('<H', 1)
# Backup boot sector
bpb += struct.pack('<H', 6)
# Reserved
bpb += b'\x00' * 12
# Drive number
bpb += struct.pack('<B', 0x80)
# Reserved
bpb += struct.pack('<B', 0)
# Boot signature
bpb += struct.pack('<B', 0x29)
# Volume ID
bpb += struct.pack('<I', 0x12345678)
# Volume label
bpb += b'PLAN0       '
# Filesystem type
bpb += b'FAT32   '

# Pad to 512 bytes
bpb = bpb.ljust(510, b'\x00')
bpb += struct.pack('<H', 0xAA55)

# Write BPB to partition start
with open(OUTPUT, 'r+b') as f:
    f.seek(PART1_START * SECTOR_SIZE)
    f.write(bpb)

    # Write FSInfo sector
    fsinfo = struct.pack('<I', 0x41615252)  # Signature "RRaA"
    fsinfo += b'\x00' * 480
    fsinfo += struct.pack('<I', 0x61417272)  # Signature "rrAa"
    fsinfo += struct.pack('<I', total_clusters + 1)  # Last known free cluster
    fsinfo += struct.pack('<I', 0xFFFFFFFF)  # Hint for next free cluster
    fsinfo += b'\x00' * 12
    fsinfo += struct.pack('<I', 0xAA550000)  # Signature
    
    sec = PART1_START + 1  # FSInfo at sector 1 (after BPB sector 0)
    f.seek(sec * SECTOR_SIZE)
    f.write(fsinfo)

    # Write FATs
    fat_entry = b''
    
    # FAT[0] = 0x0FFFFFF8 (media descriptor)
    fat_entry += struct.pack('<I', 0x0FFFFFF8)
    # FAT[1] = 0x0FFFFFFF (end-of-chain)
    fat_entry += struct.pack('<I', 0x0FFFFFFF)
    # FAT[2] = 0x0FFFFFFF (end-of-chain for root dir cluster)
    fat_entry += struct.pack('<I', 0x0FFFFFFF)
    # FAT[3] onwards = 0x00000000 (free)
    fat_entry += b'\x00' * (sectors_per_fat * SECTOR_SIZE - 3 * 4)

    for i in range(num_fats):
        fat_sec = PART1_START + reserved_sectors + (i * sectors_per_fat)
        f.seek(fat_sec * SECTOR_SIZE)
        f.write(fat_entry)

    # Create root directory entries
    # Root directory starts at cluster 2 (first data cluster)
    root_dir_sector = PART1_START + first_data_sector
    
    # Create "boot" directory entry
    # Directory entry: 32 bytes each
    def make_dirent(name, ext, attrs, first_cluster, size, is_vol=False):
        entry = b''
        if is_vol:
            entry += struct.pack('<B', 0x05)  # Special: volume label
        entry += name.encode('ascii').ljust(8, b' ')[:8]
        entry += ext.encode('ascii').ljust(3, b' ')[:3]
        entry += struct.pack('<B', attrs)
        entry += b'\x00'  # Reserved (NT)
        entry += struct.pack('<B', 0)  # Creation time tenths
        entry += struct.pack('<H', 0)  # Creation time
        entry += struct.pack('<H', 0)  # Creation date
        entry += struct.pack('<H', 0)  # Last access date
        entry += struct.pack('<H', (first_cluster >> 16) & 0xFFFF)  # First cluster HIGH
        entry += struct.pack('<H', 0)  # Last write time
        entry += struct.pack('<H', 0)  # Last write date
        entry += struct.pack('<H', first_cluster & 0xFFFF)  # First cluster LOW
        entry += struct.pack('<I', size & 0xFFFFFFFF)  # File size
        return entry

    # '.' entry
    dot_entry = make_dirent('.', '', 0x10, root_cluster, 0)
    # '..' entry
    dotdot_entry = make_dirent('..', '', 0x10, root_cluster, 0)
    # boot directory
    boot_entry = make_dirent('BOOT', '', 0x10, root_cluster + 1, 0)
    # boot/grub directory
    grub_entry = make_dirent('GRUB', '', 0x10, root_cluster + 2, 0)

    # Write root directory (at first data cluster = cluster 2)
    root_dir_data = dot_entry + dotdot_entry + boot_entry + b'\x00' * (SECTOR_SIZE - 3*32)
    f.seek(root_dir_sector * SECTOR_SIZE)
    f.write(root_dir_data)

    # Create boot directory (cluster 3)
    boot_dir_sector = root_dir_sector + 1  # Next sector
    boot_dir_data = make_dirent('.', '', 0x10, root_cluster + 1, 0)
    boot_dir_data += make_dirent('..', '', 0x10, root_cluster, 0)
    boot_dir_data += make_dirent('GRUB', '', 0x10, root_cluster + 2, 0)
    boot_dir_data = boot_dir_data.ljust(SECTOR_SIZE, b'\x00')
    f.seek(boot_dir_sector * SECTOR_SIZE)
    f.write(boot_dir_data)

    # Create boot/grub directory (cluster 4)
    grub_dir_sector = boot_dir_sector + 1
    grub_dir_data = make_dirent('.', '', 0x10, root_cluster + 2, 0)
    grub_dir_data += make_dirent('..', '', 0x10, root_cluster, 0)
    grub_dir_data = grub_dir_data.ljust(SECTOR_SIZE, b'\x00')
    f.seek(grub_dir_sector * SECTOR_SIZE)
    f.write(grub_dir_data)

    # Write grub.cfg file (cluster 5)
    with open(GRUB_CFG, 'rb') as gf:
        grub_cfg_data = gf.read()
    
    grub_cfg_cluster = root_cluster + 3  # cluster 5
    grub_file_sec = grub_dir_sector + 1
    
    # Add file entry to boot/grub directory
    grub_entry2 = make_dirent('GRUB', 'CFG', 0x20, grub_cfg_cluster, len(grub_cfg_data))
    f.seek(grub_dir_sector * SECTOR_SIZE)
    f.write(grub_dir_data[:32])  # . entry
    f.write(grub_dir_data[32:64])  # .. entry
    f.write(grub_entry2)
    f.write(b'\x00' * (SECTOR_SIZE - 3*32))

    # Write grub.cfg data
    f.seek(grub_file_sec * SECTOR_SIZE)
    f.write(grub_cfg_data.ljust(SECTOR_SIZE, b'\x00'))

    # Write kernel file (cluster 6)
    with open(KERNEL, 'rb') as kf:
        kernel_data = kf.read()
    
    kernel_cluster = grub_cfg_cluster + 1  # cluster 6
    kernel_entry = make_dirent('KERNEL', '', 0x20, kernel_cluster, len(kernel_data))
    
    # Add kernel entry to boot directory
    f.seek(boot_dir_sector * SECTOR_SIZE)
    boot_entries = boot_dir_data[:32] + boot_dir_data[32:64] + boot_dir_data[64:96]  # keep existing entries
    f.write(boot_dir_data[:32])  # .
    f.write(boot_dir_data[32:64])  # ..
    f.write(kernel_entry)
    f.write(b'\x00' * (SECTOR_SIZE - 3*32))

    # Write kernel data
    kernel_sec = grub_file_sec + 1
    f.seek(kernel_sec * SECTOR_SIZE)
    f.write(kernel_data)

    # Now update the FAT to chain clusters properly
    # We need to update the FAT entries
    fat_sector = PART1_START + reserved_sectors
    
    # Read current FAT
    f.seek(fat_sector * SECTOR_SIZE)
    fat = bytearray(f.read(sectors_per_fat * SECTOR_SIZE))
    
    # Cluster 2 (root dir): end of chain
    struct.pack_into('<I', fat, 2 * 4, 0x0FFFFFFF)
    # Cluster 3 (boot dir): end of chain
    struct.pack_into('<I', fat, 3 * 4, 0x0FFFFFFF)
    # Cluster 4 (boot/grub dir): end of chain
    struct.pack_into('<I', fat, 4 * 4, 0x0FFFFFFF)
    # Cluster 5 (grub.cfg file): end of chain
    struct.pack_into('<I', fat, 5 * 4, 0x0FFFFFFF)
    # Cluster 6 (kernel file): end of chain
    struct.pack_into('<I', fat, 6 * 4, 0x0FFFFFFF)
    
    # Write FAT back (both copies)
    f.seek(fat_sector * SECTOR_SIZE)
    f.write(fat)
    f.seek((fat_sector + sectors_per_fat) * SECTOR_SIZE)
    f.write(fat)

print("\nDisk image created successfully!")
print(f"  Path: {OUTPUT}")
print(f"  Size: {DISK_SIZE} bytes ({DISK_SIZE//1024//1024} MB)")
print(f"  Boot: boot_hybrid.img MBR + core.img at sector 1")
print(f"  Filesystem: FAT32 at sector {PART1_START}")
print()
print("To test in QEMU:")
print(f'  qemu-system-x86_64 -drive file={OUTPUT},if=ide,format=raw -serial stdio -m 128')
