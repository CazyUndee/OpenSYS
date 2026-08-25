#!/usr/bin/env python3
"""Build a minimal raw disk image with a synthetic GPT for Plan 0 testing.

Layout (16 MiB / 32768 sectors):
  LBA 0      protective MBR (single 0xEE entry)
  LBA 1      GPT primary header (CRC fields zeroed - the kernel driver
             has GPT_VALIDATE_CRC=0)
  LBA 2      entry array (4 entries x 128 bytes = 512 bytes)
  LBA 2048   partition 1: "plan0-test" (Linux fs GUID), 2048 sectors
  LBA 4096   partition 2: "data-two"  (FAT32 GUID),  2048 sectors

The kernel fs_mount() reads LBA 0 and will not match FS_MAGIC, so it
falls back to its in-memory format path - the protective MBR may be
overwritten by fs_format during the run; regenerate before each use.
"""
import os, struct, uuid

OUT = os.path.join(os.path.dirname(__file__), "gpt_test.img")
SECTOR = 512
TOTAL_SECTORS = 32768  # 16 MiB

# Well-known type GUIDs as raw 16-byte sequences (byte order exactly as in
# include/gpt.h so gpt_get_partition's memcmp-style compare matches):
LINUX_FS = bytes([0xAF,0x3D,0xC6,0x0F,0x83,0x84,0x72,0x47,
                  0x8E,0x79,0x3D,0x69,0xD8,0x47,0x7D,0xE4])
FAT32 = bytes([0xE3,0xAF,0x2E,0xEB,0x5D,0xF3,0x46,0x47,
               0x9D,0x14,0xA5,0x43,0x51,0x5C,0xC8,0xB0])

img = bytearray(TOTAL_SECTORS * SECTOR)

def put_lba(lba, data):
    img[lba * SECTOR : lba * SECTOR + len(data)] = data

# --- Protective MBR (LBA 0) ---
mbr = bytearray(SECTOR)
mbr[0x1BE + 4] = 0xEE  # GPT protective type
struct.pack_into("<I", mbr, 0x1BE + 8, 1)                # start LBA
struct.pack_into("<I", mbr, 0x1BE + 12, TOTAL_SECTORS - 1)
struct.pack_into("<H", mbr, 0x1FE, 0xAA55)
put_lba(0, mbr)

# --- GPT entry array (LBA 2): 4 entries x 128 bytes ---
def entry(type_guid, name, start, end):
    e = bytearray(128)
    e[0:16] = type_guid
    e[16:32] = uuid.UUID("12345678-1234-5678-9abc-%012x" % (start & 0xFFFFFFFFFFFF)).bytes
    struct.pack_into("<Q", e, 32, start)
    struct.pack_into("<Q", e, 40, end)
    name16 = name.encode("utf-16-le")[:72]
    e[56 : 56 + len(name16)] = name16
    return bytes(e)

entries = entry(LINUX_FS, "plan0-test", 2048, 4095) \
        + entry(FAT32,    "data-two",   4096, 6143) \
        + bytes(128 * 2)
put_lba(2, entries)

# --- GPT primary header (LBA 1) ---
hdr = bytearray(SECTOR)
hdr[0:8] = b"EFI PART"
struct.pack_into("<I", hdr, 8, 0x00010000)   # revision 1.0
struct.pack_into("<I", hdr, 12, 92)          # header size
struct.pack_into("<Q", hdr, 24, 1)                    # current_lba
struct.pack_into("<Q", hdr, 32, TOTAL_SECTORS - 1)    # backup_lba
struct.pack_into("<Q", hdr, 40, 34)                   # first usable
struct.pack_into("<Q", hdr, 48, TOTAL_SECTORS - 34)   # last usable
hdr[56:72] = uuid.UUID("0abcdef0-1234-5678-9abc-def012345678").bytes
struct.pack_into("<Q", hdr, 72, 2)     # entry array LBA
struct.pack_into("<I", hdr, 80, 4)     # entry count
struct.pack_into("<I", hdr, 84, 128)   # entry size
put_lba(1, hdr)

with open(OUT, "wb") as f:
    f.write(img)

print(f"wrote {OUT} ({len(img)} bytes, {TOTAL_SECTORS} sectors)")
print("  partition 1: 'plan0-test' LBA 2048-4095 (1024 KB, Linux fs GUID)")
print("  partition 2: 'data-two'   LBA 4096-6143 (1024 KB, FAT32 GUID)")
