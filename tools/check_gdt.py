import struct, os

BASE = r'C:\Users\roone.DESKTOP-QK3UG2M\Downloads\projects\os'

# Read the assembled ELF file
with open(os.path.join(BASE, 'obj', 'boot.o'), 'rb') as f:
    data = f.read()

# Look for the GDT values in the binary
# Null descriptor: 8 bytes of zeros
# Code descriptor: (1<<43) | (1<<44) | (1<<47) | (1<<53)
# Data descriptor: (1<<44) | (1<<47) | (1<<41)

code_val = (1<<43) | (1<<44) | (1<<47) | (1<<53)
data_val = (1<<44) | (1<<47) | (1<<41)

print(f"Expected code descriptor: 0x{code_val:016x}")
print(f"Expected data descriptor: 0x{data_val:016x}")

# Parse as 64-bit values packed in little-endian
code_bytes = struct.pack('<Q', code_val)
data_bytes = struct.pack('<Q', data_val)

# Search for these in the binary
code_pos = data.find(code_bytes)
data_pos = data.find(data_bytes)

if code_pos >= 0:
    print(f"Code descriptor found at offset 0x{code_pos:x}")
    # Check what comes before (should be null descriptor)
    if code_pos >= 8:
        null_bytes = data[code_pos-8:code_pos]
        print(f"  Preceding 8 bytes: {null_bytes.hex()} (should be 0000000000000000)")
else:
    print("Code descriptor NOT found in binary")

if data_pos >= 0:
    print(f"Data descriptor found at offset 0x{data_pos:x}")
else:
    print("Data descriptor NOT found in binary")

# Also check the GDT pointer (6 bytes: limit + base)
# Format: [2-byte limit][8-byte base] (but stored as 6 bytes for lgdt)
# Actually, in the asm code: dw limit, dq base
# But the .pointer structure is: dw (size-1), dq gdt64
# This is packed as: 2 bytes LE + 8 bytes LE = 10 bytes for lgdt
# But lgdt reads: 2 bytes limit, 4/8 bytes base (depending on mode)

# Let's check the .rodata section of the linked kernel
with open(os.path.join(BASE, 'bin', 'kernel0.bin'), 'rb') as f:
    kernel = f.read()

print("\nSearching kernel binary...")
kcode_pos = kernel.find(code_bytes)
kdata_pos = kernel.find(data_bytes)
if kcode_pos >= 0:
    print(f"Code descriptor in kernel at 0x{kcode_pos:x}")
    # Check GDT pointer (should be just after the descriptors)
    gdt_size_offset = kcode_pos + 8 + 8  # after null + code + data
    if gdt_size_offset + 6 <= len(kernel):
        gdt_limit = struct.unpack_from('<H', kernel, gdt_size_offset)[0]
        gdt_base = struct.unpack_from('<I', kernel, gdt_size_offset + 2)[0]  # 4 bytes base in compat mode
        print(f"  GDT pointer at 0x{gdt_size_offset:x}: limit={gdt_limit}, base=0x{gdt_base:x}")
    
    # Check the code descriptor value more carefully
    cd_val = struct.unpack_from('<Q', kernel, kcode_pos)[0]
    print(f"  Actual code descriptor: 0x{cd_val:016x}")
    # Parse the descriptor
    print(f"    P (present):     {bool(cd_val & (1<<47))}")
    print(f"    DPL:             {(cd_val >> 45) & 3}")
    print(f"    S (code/data):   {bool(cd_val & (1<<44))}")
    print(f"    Type:            {(cd_val >> 41) & 0xF}")
    print(f"    L (64-bit):      {bool(cd_val & (1<<53))}")
    print(f"    D (32-bit):      {bool(cd_val & (1<<54))}")
    print(f"    G (granularity): {bool(cd_val & (1<<55))}")
else:
    print("Code descriptor NOT found in kernel")
