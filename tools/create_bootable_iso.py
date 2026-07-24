"""
Create a bootable ISO with proper GRUB boot image.

Strategy:
1. Build proper boot image from GRUB components
2. Place boot image as a file on the ISO (via pycdlib)
3. Let pycdlib set up El Torito boot catalog pointing to this file
4. After ISO is written, find the LBA of the boot image file on the CD
5. Patch cdboot.img with LBA (file_lba + 1) and core_data size
6. Overwrite the boot image data at correct LBA on the ISO
"""
import pycdlib, os, struct

BASE = r'C:\Users\roone.DESKTOP-QK3UG2M\Downloads\projects\os'
GRUB_DIR = os.path.join(BASE, 'usr', 'lib', 'grub', 'i386-pc')
BIN_DIR = os.path.join(BASE, 'bin')
KERNEL = os.path.join(BIN_DIR, 'kernel0.bin')
GRUB_CFG = os.path.join(BASE, 'grub.cfg')
OUTPUT = os.path.join(BIN_DIR, 'os.iso')
CD_SECTOR = 2048

# Read GRUB components
print("Reading GRUB components...")
with open(os.path.join(GRUB_DIR, 'cdboot.img'), 'rb') as f:
    cdboot_raw = bytearray(f.read())
with open(os.path.join(GRUB_DIR, 'diskboot.img'), 'rb') as f:
    diskboot_raw = bytearray(f.read())
with open(os.path.join(GRUB_DIR, 'kernel.img'), 'rb') as f:
    kernel_img_raw = f.read()

assert len(cdboot_raw) == CD_SECTOR, f"cdboot.img must be {CD_SECTOR} bytes!"

# Build core_data
core_data = bytes(diskboot_raw) + kernel_img_raw
core_data_size = len(core_data)
total_512_sectors = (core_data_size + 511) // 512
struct.pack_into('<I', diskboot_raw, 0x04, total_512_sectors)
print(f"diskboot.img: patched total_sectors at offset 4 = {total_512_sectors}")

core_data_padded = core_data + b'\x00' * ((CD_SECTOR - len(core_data) % CD_SECTOR) % CD_SECTOR)
boot_image_size = len(cdboot_raw) + len(core_data_padded)
boot_sectors = boot_image_size // CD_SECTOR
print(f"Boot image: {boot_image_size} bytes ({boot_sectors} CD sectors)")

# Create stub boot image (all zeros)
boot_stub = os.path.join(BIN_DIR, 'grub_boot_stub.img')
with open(boot_stub, 'wb') as f:
    f.write(b'\x00' * boot_image_size)

# Create ISO
print("Creating ISO 9660 filesystem...")
iso = pycdlib.PyCdlib()
iso.new(interchange_level=3, log_block_size=CD_SECTOR, rock_ridge="1.09", joliet=3)

iso.add_directory(iso_path="/BOOT", rr_name="boot")
iso.add_directory(iso_path="/BOOT/GRUB", rr_name="grub")
iso.add_directory(iso_path="/BOOT/GRUB/I386_PC", rr_name="i386-pc")
iso.add_joliet_directory("/boot")
iso.add_joliet_directory("/boot/grub")
iso.add_joliet_directory("/boot/grub/i386-pc")

# Add boot image stub
iso.add_file(boot_stub, iso_path="/BOOT/GRUB/I386_PC/CORE.IMG;1",
             rr_name="core.img", joliet_path="/boot/grub/i386-pc/core.img")
print("Added boot image stub")

# Add kernel
print("Adding kernel...")
iso.add_file(KERNEL, iso_path="/BOOT/KERNEL;1", rr_name="kernel", joliet_path="/boot/kernel")

# Add grub.cfg
print("Adding grub.cfg...")
iso.add_file(GRUB_CFG, iso_path="/BOOT/GRUB/GRUB.CFG;1", rr_name="grub.cfg",
             joliet_path="/boot/grub/grub.cfg")

# Add modules
print("Adding GRUB modules...")
mods = sorted(os.listdir(GRUB_DIR))
mod_count = 0
for mod_name in mods:
    mod_path = os.path.join(GRUB_DIR, mod_name)
    if os.path.isfile(mod_path) and mod_name not in ('boot_hybrid.img', 'boot.img'):
        iso_8_3 = mod_name.upper().replace('-', '_')
        if len(iso_8_3) > 8:
            base, ext = os.path.splitext(iso_8_3)
            iso_8_3 = base[:8] + ext
        iso.add_file(mod_path, iso_path="/BOOT/GRUB/I386_PC/" + iso_8_3 + ";1",
                     rr_name=mod_name, joliet_path="/boot/grub/i386-pc/" + mod_name)
        mod_count += 1
print(f"Added {mod_count} modules")

# El Torito
print("Setting up El Torito boot...")
iso.add_eltorito(
    bootfile_path="/BOOT/GRUB/I386_PC/CORE.IMG;1",
    bootcatfile="/BOOT.CAT;1",
    media_name="noemul",
    boot_load_seg=0x07C0,
    platform_id=0,
    bootable=True
)

temp_iso = OUTPUT + ".tmp"
print(f"Writing ISO to {temp_iso}...")
iso.write(temp_iso)
iso.close()
print(f"ISO written: {os.path.getsize(temp_iso)} bytes")

# Find boot image file LBA
print("Finding boot image file LBA...")
iso2 = pycdlib.PyCdlib()
iso2.open(temp_iso)

boot_lba = None
# Try to find the file extent using pycdlib's internal data
# pycdlib stores extent location; let's search by opening the file record
try:
    # Use list_dir to get detailed info
    for (dirname, dirlist, filelist) in iso2.walk(iso_path='/'):
        if dirname == '/BOOT/GRUB/I386_PC':
            print(f"  Files in {dirname}:")
            for f_name in filelist:
                print(f"    {f_name}")
            break
    
    # Try to get the extent directly using pycdlib API
    # The file is CORE.IMG;1
    file_stat = iso2.stat(iso_path='/BOOT/GRUB/I386_PC/CORE.IMG;1')
    print(f"  CORE.IMG stat: {file_stat}")
    
except Exception as e:
    print(f"  Error: {e}")

iso2.close()

# Binary search for the zero block
print("Searching ISO binary for boot image data...")
with open(temp_iso, 'rb') as f:
    iso_data = bytearray(f.read())

# Find zero block of boot_image_size
zero_target = b'\x00' * boot_image_size
# First, find the directory record for CORE.IMG to get its LBA
# ISO 9660 directory records contain the LBA as first 4 bytes (LE) then 4 bytes (BE)

# Search for the string "CORE.IMG" or "CORE IMG" in the ISO
search_pos = 0
while True:
    pos = iso_data.find(b'CORE.IMG', search_pos)
    if pos == -1:
        pos = iso_data.find(b'CORE.IMG', search_pos)
    if pos == -1:
        break
    
    # Check if preceded by a valid directory record
    # Directory record format: len(1) + ext_attr_len(1) + lba(8) + size(8) + ...
    # The filename is at variable offset after date/time fields
    # For a simple check, go back up to 128 bytes to find the record start
    record_start = max(0, pos - 80)
    for rs in range(record_start, pos):
        rec_len = iso_data[rs]
        if rs + rec_len > pos + 11 and rec_len > 30:
            # Check if this record could contain our filename
            # Filename should be at some offset within the record
            # Check for the "CORE.IMG" or "CORE.IMG;1" string
            name_offset = pos - rs
            if name_offset >= 33 and name_offset < rec_len:
                # Found a likely directory record
                # LBA is at offset 2 (8 bytes, LE then BE)
                lba_le = struct.unpack_from('<I', iso_data, rs + 2)[0]
                lba_be = struct.unpack_from('>I', iso_data, rs + 6)[0]
                if lba_le == lba_be:  # Sanity check
                    file_size = struct.unpack_from('<I', iso_data, rs + 10)[0]
                    print(f"  Found directory record at offset 0x{rs:x}")
                    print(f"  LBA: {lba_le}, size: {file_size}")
                    boot_lba = lba_le
    
    search_pos = pos + 8

if boot_lba is None:
    # Fallback: find the first large block of zeros
    # The boot stub is the ONLY file with all-zeros of this size
    print("  Binary scanning for zero block...")
    for sector in range(16, 1000):
        offset = sector * CD_SECTOR
        if offset + boot_image_size > len(iso_data):
            break
        if iso_data[offset:offset+boot_image_size] == zero_target:
            boot_lba = sector
            print(f"  Found zero block at LBA {sector}")
            break

if boot_lba is not None:
    print(f"Boot image at LBA {boot_lba}")
    
    # Patch cdboot.img
    patch_lba = boot_lba + 1  # core_data starts at next sector
    patch_size = len(core_data)  # actual data size
    
    patched_cdboot = bytearray(cdboot_raw)
    struct.pack_into('<I', patched_cdboot, 0x0C, patch_lba)
    struct.pack_into('<I', patched_cdboot, 0x10, patch_size)
    
    # Rebuild core_data with patched diskboot
    patched_core_data = bytes(diskboot_raw) + kernel_img_raw
    patched_core_data += b'\x00' * ((CD_SECTOR - len(patched_core_data) % CD_SECTOR) % CD_SECTOR)
    
    patched_boot_image = bytes(patched_cdboot) + patched_core_data
    assert len(patched_boot_image) == boot_image_size, f"Size mismatch!"
    
    print(f"Patched cdboot.img: LBA={patch_lba}, size={patch_size}")
    
    # Overwrite in ISO
    with open(temp_iso, 'r+b') as f:
        f.seek(boot_lba * CD_SECTOR)
        f.write(patched_boot_image)
    print("Boot image patched in ISO")
    
    # Save reference copy
    shutil_path = os.path.join(BIN_DIR, 'grub_boot.img')
    with open(shutil_path, 'wb') as f:
        f.write(patched_boot_image)
    print(f"Saved reference boot image to {shutil_path}")
else:
    print("ERROR: Could not find boot image LBA!")
    print("ISO will NOT be bootable!")

# Cleanup
if os.path.exists(boot_stub):
    os.remove(boot_stub)
if os.path.exists(OUTPUT):
    os.remove(OUTPUT)
os.rename(temp_iso, OUTPUT)

print(f"\nFinal ISO: {OUTPUT}")
print(f"  Size: {os.path.getsize(OUTPUT)} bytes")
if boot_lba is not None:
    print(f"  Boot LBA: {boot_lba}")
print(f"\nTest: qemu-system-x86_64w -cdrom \"{OUTPUT}\" -serial file:serial_log.txt -m 128 -accel tcg")
