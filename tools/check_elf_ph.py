import struct, os

BASE = r'C:\Users\roone.DESKTOP-QK3UG2M\Downloads\projects\os'
with open(os.path.join(BASE, 'bin', 'kernel0.bin'), 'rb') as f:
    data = f.read()

# Parse ELF64 header
e_phoff = struct.unpack_from('<Q', data, 0x20)[0]
e_phentsize = struct.unpack_from('<H', data, 0x36)[0]
e_phnum = struct.unpack_from('<H', data, 0x38)[0]
e_entry = struct.unpack_from('<Q', data, 0x18)[0]
print(f"Program headers: {e_phnum} entries at offset {e_phoff}")
print(f"Entry point: 0x{e_entry:x}")
print()

for i in range(e_phnum):
    off = e_phoff + i * e_phentsize
    p_type = struct.unpack_from('<I', data, off)[0]
    p_flags = struct.unpack_from('<I', data, off+4)[0]
    p_offset = struct.unpack_from('<Q', data, off+8)[0]
    p_vaddr = struct.unpack_from('<Q', data, off+16)[0]
    p_paddr = struct.unpack_from('<Q', data, off+24)[0]
    p_filesz = struct.unpack_from('<Q', data, off+32)[0]
    p_memsz = struct.unpack_from('<Q', data, off+40)[0]
    
    type_names = {1: 'LOAD', 2: 'DYNAMIC', 3: 'INTERP', 4: 'NOTE'}
    p_type_name = type_names.get(p_type, f'type={p_type}')
    
    print(f"  PH[{i}]: {p_type_name}")
    print(f"    vaddr=0x{p_vaddr:016x} paddr=0x{p_paddr:016x}")
    print(f"    filesz={p_filesz} memsz={p_memsz} offset=0x{p_offset:x}")
    print(f"    flags=0x{p_flags:x}", end="")
    flag_names = []
    if p_flags & 4: flag_names.append("R")
    if p_flags & 2: flag_names.append("W")
    if p_flags & 1: flag_names.append("X")
    print(f" ({''.join(flag_names)})")
    print()

print(f"Total file size: {len(data)} bytes")
