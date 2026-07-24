import pycdlib, os, struct

osdir = r"C:\Users\roone.DESKTOP-QK3UG2M\Downloads\projects\os"
bin_dir = os.path.join(osdir, "bin")
boot_img_path = os.path.join(bin_dir, "cd-core.img")
kernel_path = os.path.join(bin_dir, "kernel0.bin")
grub_cfg_path = os.path.join(osdir, "grub.cfg")
grub_mods_dir = os.path.join(osdir, ".", "usr", "lib", "grub", "i386-pc")
boot_hybrid_path = os.path.join(osdir, ".", "usr", "lib", "grub", "i386-pc", "boot_hybrid.img")
output_iso = os.path.join(bin_dir, "os.iso")

for p, name in [(boot_img_path, "boot image"), (kernel_path, "kernel"), 
                (grub_cfg_path, "grub.cfg"), (boot_hybrid_path, "boot_hybrid.img")]:
    if not os.path.exists(p):
        print("ERROR: " + name + " not found at " + p)
        exit(1)
    print(name + ": " + str(os.path.getsize(p)) + " bytes")

mods = sorted(os.listdir(grub_mods_dir))

iso = pycdlib.PyCdlib()
iso.new(interchange_level=3, log_block_size=2048, rock_ridge="1.09", joliet=3)

iso.add_directory(iso_path="/BOOT", rr_name="boot")
iso.add_directory(iso_path="/BOOT/GRUB", rr_name="grub")
iso.add_directory(iso_path="/BOOT/GRUB/I386_PC", rr_name="i386-pc")

iso.add_joliet_directory("/boot")
iso.add_joliet_directory("/boot/grub")
iso.add_joliet_directory("/boot/grub/i386-pc")

# Add boot image (will be used as El Torito boot image)
print("Adding boot image...")
iso.add_file(boot_img_path,
             iso_path="/BOOT/GRUB/I386_PC/CORE.IMG;1",
             rr_name="core.img",
             joliet_path="/boot/grub/i386-pc/core.img")

# Add kernel
print("Adding kernel...")
iso.add_file(kernel_path,
             iso_path="/BOOT/KERNEL;1",
             rr_name="kernel",
             joliet_path="/boot/kernel")

# Add grub.cfg
print("Adding grub.cfg...")
iso.add_file(grub_cfg_path,
             iso_path="/BOOT/GRUB/GRUB.CFG;1",
             rr_name="grub.cfg",
             joliet_path="/boot/grub/grub.cfg")

# Add GRUB modules (but NOT boot_hybrid.img - that goes in the MBR)
print("Adding " + str(len(mods)) + " GRUB module files...")
mod_count = 0
for mod_name in mods:
    mod_path = os.path.join(grub_mods_dir, mod_name)
    if os.path.isfile(mod_path) and mod_name != "boot_hybrid.img":
        iso_8_3 = mod_name.upper().replace("-", "_")
        if len(iso_8_3) > 8:
            base, ext = os.path.splitext(iso_8_3)
            base = base[:8]
            iso_8_3 = base + ext
        iso.add_file(mod_path,
                     iso_path="/BOOT/GRUB/I386_PC/" + iso_8_3 + ";1",
                     rr_name=mod_name,
                     joliet_path="/boot/grub/i386-pc/" + mod_name)
        mod_count += 1
print("Added " + str(mod_count) + " module files")

# Set up El Torito boot
print("Setting up El Torito boot...")
iso.add_eltorito(
    bootfile_path="/BOOT/GRUB/I386_PC/CORE.IMG;1",
    bootcatfile="/BOOT.CAT;1",
    media_name="noemul",
    boot_load_seg=0,
    platform_id=0,
    bootable=True
)
print("El Torito boot record added")

# Write ISO to temp location first
temp_iso = output_iso + ".tmp"
print("Writing temporary ISO to " + temp_iso + "...")
iso.write(temp_iso)
iso.close()
print("Temporary ISO created: " + str(os.path.getsize(temp_iso)) + " bytes")

# Now add hybrid MBR support
print("\nAdding hybrid MBR...")
with open(temp_iso, "r+b") as f:
    raw = f.read()
    
    # Read boot_hybrid.img
    with open(boot_hybrid_path, "rb") as bh:
        hybrid_mbr = bh.read()
    
    # Overwrite the first 512 bytes with boot_hybrid.img
    # This is safe because ISO 9660 reserves first 16 sectors (32768 bytes)
    # as the System Area, and the Volume Descriptors start at sector 16
    raw = bytearray(raw)
    raw[0:512] = hybrid_mbr
    
    # Now set up the partition table
    # Sector size = 2048 (ISO)
    # The ISO data starts at sector 0 (logically)
    # For isohybrid, we create a partition that starts at LBA 0
    # But GRUB uses 512-byte sectors, so we need to adjust
    # In a isohybrid setup:
    # - The partition starts at sector 0 in 2048-byte sectors
    # - But in 512-byte sector addressing, it starts at 0
    # - The partition size is the total ISO size in 512-byte sectors
    
    total_size_512 = len(raw) // 512
    partition_size = total_size_512
    
    # Partition table starts at offset 0x1BE in the MBR
    # Each entry is 16 bytes
    # Entry 1: offset 0x1BE
    pentry = struct.pack("<B", 0x80)  # Boot flag (bootable)
    pentry += struct.pack("<BBB", 0x00, 0x02, 0x00)  # CHS start
    pentry += struct.pack("<B", 0x17)  # Partition type (0x17 = Windows-friendly, also used by isohybrid)
    # CHS end = approximate
    h = 64  # heads
    s = 32  # sectors per track
    total_cyl = total_size_512 // (h * s)
    cyl = min(total_cyl - 1, 1023)
    head_end = h - 1
    sec_end = s
    pentry += struct.pack("<BBB", head_end, sec_end, cyl & 0xFF)
    pentry += struct.pack("<I", 0)  # Start LBA (sector 0)
    pentry += struct.pack("<I", total_size_512)  # Size in sectors
    
    # Write partition entry
    raw[0x1BE:0x1CE] = pentry
    
    # Update boot signature (already in boot_hybrid.img, but just in case)
    raw[510] = 0x55
    raw[511] = 0xAA
    
    # Write back
    f.seek(0)
    f.write(bytes(raw))

# Rename temp to final
if os.path.exists(output_iso):
    os.remove(output_iso)
os.rename(temp_iso, output_iso)
print("Final ISO with hybrid MBR: " + str(os.path.getsize(output_iso)) + " bytes")
print("Done!")
