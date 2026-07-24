"""
Check if kernel0.bin has a valid Multiboot1 header and analyze its ELF structure.
"""
import struct

kernel = r'C:\Users\roone.DESKTOP-QK3UG2M\Downloads\projects\os\bin\kernel0.bin'
with open(kernel, 'rb') as f:
    data = f.read()

print(f"File size: {len(data)} bytes")

# Check ELF header
if data[:4] == b'\x7f\x45\x4c\x46':
    ei_class = data[4]  # 1=32-bit, 2=64-bit
    ei_data = data[5]   # 1=LE, 2=BE
    ei_osabi = data[7]
    e_type = struct.unpack_from('<H', data, 16)[0]
    e_machine = struct.unpack_from('<H', data, 18)[0]
    e_entry = struct.unpack_from('<Q', data, 24)[0] if ei_class == 2 else struct.unpack_from('<I', data, 24)[0]
    e_phoff = struct.unpack_from('<Q', data, 32)[0] if ei_class == 2 else struct.unpack_from('<I', data, 28)[0]
    e_phnum = struct.unpack_from('<H', data, 56 if ei_class == 2 else 44)[0]
    
    print(f"ELF class: {'32-bit' if ei_class == 1 else '64-bit'}")
    print(f"Endian: {'Little' if ei_data == 1 else 'Big'}")
    print(f"OS/ABI: {ei_osabi}")
    print(f"Type: {e_type} (2=EXEC)")
    print(f"Machine: {e_machine} (0x3E=x86_64, 0x03=i386)")
    print(f"Entry point: 0x{e_entry:x}")
    print(f"Program headers: {e_phnum} at offset 0x{e_phoff:x}")
    
    # Check program headers
    ph_size = 56 if ei_class == 2 else 32
    for i in range(e_phnum):
        ph_off = e_phoff + i * ph_size
        if ei_class == 2:
            p_type = struct.unpack_from('<I', data, ph_off)[0]
            p_flags = struct.unpack_from('<I', data, ph_off+4)[0]
            p_offset = struct.unpack_from('<Q', data, ph_off+8)[0]
            p_vaddr = struct.unpack_from('<Q', data, ph_off+16)[0]
            p_paddr = struct.unpack_from('<Q', data, ph_off+24)[0]
            p_filesz = struct.unpack_from('<Q', data, ph_off+32)[0]
            p_memsz = struct.unpack_from('<Q', data, ph_off+40)[0]
        else:
            p_type = struct.unpack_from('<I', data, ph_off)[0]
            p_offset = struct.unpack_from('<I', data, ph_off+4)[0]
            p_vaddr = struct.unpack_from('<I', data, ph_off+8)[0]
            p_paddr = struct.unpack_from('<I', data, ph_off+12)[0]
            p_filesz = struct.unpack_from('<I', data, ph_off+16)[0]
            p_memsz = struct.unpack_from('<I', data, ph_off+20)[0]
            p_flags = struct.unpack_from('<I', data, ph_off+24)[0]
        
        p_type_name = {1: 'LOAD', 2: 'DYNAMIC', 3: 'INTERP', 4: 'NOTE', 
                       0x6474e550: 'GNU_EH_FRAME', 0x6474e551: 'GNU_STACK',
                       0x6474e552: 'GNU_RELRO', 0x6474e553: 'GNU_PROPERTY'}.get(p_type, f'0x{p_type:x}')
        print(f"\n  PH {i}: type={p_type_name} flags={p_flags} offset=0x{p_offset:x}")
        print(f"       vaddr=0x{p_vaddr:x} paddr=0x{p_paddr:x}")
        print(f"       filesz=0x{p_filesz:x} memsz=0x{p_memsz:x}")
else:
    print("Not an ELF file!")

# Search for Multiboot1 magic (0x1BADB002) - should be at offset 0x1000 as noted
print(f"\n--- Multiboot1 Header Search ---")
pos = 0
while True:
    pos = data.find(b'\x02\xb0\xad\x1b', pos)  # 0x1BADB002 in little-endian
    if pos == -1:
        break
    print(f"  Found Multiboot magic at offset 0x{pos:x}")
    # Read the next fields
    flags = struct.unpack_from('<I', data, pos+4)[0]
    checksum = struct.unpack_from('<I', data, pos+8)[0]
    calc = (0x1BADB002 + flags + checksum) & 0xFFFFFFFF
    print(f"    flags=0x{flags:x}, checksum=0x{checksum:x}, verify={calc == 0}")
    if pos + 12 <= len(data):
        header_addr = struct.unpack_from('<I', data, pos+12)[0] if pos+12 <= len(data) else 0
        load_addr = struct.unpack_from('<I', data, pos+16)[0] if pos+16 <= len(data) else 0
        load_end_addr = struct.unpack_from('<I', data, pos+20)[0] if pos+20 <= len(data) else 0
        bss_end_addr = struct.unpack_from('<I', data, pos+24)[0] if pos+24 <= len(data) else 0
        entry_addr = struct.unpack_from('<I', data, pos+28)[0] if pos+28 <= len(data) else 0
        print(f"    header_addr=0x{header_addr:x}")
        print(f"    load_addr=0x{load_addr:x}")
        print(f"    load_end_addr=0x{load_end_addr:x}")
        print(f"    bss_end_addr=0x{bss_end_addr:x}")
        print(f"    entry_addr=0x{entry_addr:x}")
    pos += 1

# Check if the kernel is PIE or not
# ELF type 2 = EXEC (not PIE), type 3 = DYN (PIE)
if data[:4] == b'\x7f\x45\x4c\x46':
    print(f"\nELF type: {e_type} (2=EXEC, 3=DYN/PIE)")
    
print("\nDone.")
