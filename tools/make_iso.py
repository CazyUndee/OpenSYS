#!/usr/bin/env python3
"""Create a minimal bootable ISO with GRUB for testing."""
import os, sys, shutil

kernel_bin = sys.argv[1] if len(sys.argv) > 1 else "bin/kernel0.bin"
output_iso = sys.argv[2] if len(sys.argv) > 2 else "bin/os.iso"
iso_dir = "iso_build"

# Clean and create ISO directory
if os.path.exists(iso_dir):
    shutil.rmtree(iso_dir)

grub_dir = os.path.join(iso_dir, "boot", "grub")
os.makedirs(grub_dir, exist_ok=True)

# Copy kernel
shutil.copy(kernel_bin, os.path.join(iso_dir, "boot", "kernel"))

# Write grub.cfg
with open(os.path.join(grub_dir, "grub.cfg"), "w") as f:
    f.write('set timeout=0\n')
    f.write('set default=0\n\n')
    f.write('menuentry "Plan 0" {\n')
    f.write('    multiboot /boot/kernel\n')
    f.write('    boot\n')
    f.write('}\n')

print(f"ISO dir created at {iso_dir}")
print(f"Kernel: {os.path.getsize(kernel_bin)} bytes")

# Now we need grub-mkrescue or xorriso
# Let's check if we can use a pre-built GRUB image
print("\nNeed grub-mkrescue or xorriso to create final ISO.")
print("Files ready:")
for root, dirs, files in os.walk(iso_dir):
    for f in files:
        fp = os.path.join(root, f)
        print(f"  {fp} ({os.path.getsize(fp)} bytes)")
