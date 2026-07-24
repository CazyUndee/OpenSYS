import struct, sys

proj = r'C:\Users\roone.DESKTOP-QK3UG2M\Downloads\projects\os'

# ELF LOAD segment details from readelf
base_addr = 0x100000
file_off = 0x1000
file_sz = 0x23e00
mem_sz = 0xf8df0

with open(f'{proj}/bin/kernel0.bin', 'rb') as f:
    elf = f.read()

load_data = elf[file_off:file_off + file_sz]
raw = bytearray(load_data + b'\x00' * (mem_sz - file_sz))

# Print header fields
for off, name in [(0, 'magic'), (4, 'flags'), (8, 'checksum'),
                  (12, 'header_addr'), (16, 'load_addr'), (20, 'load_end'),
                  (24, 'bss_end'), (28, 'entry')]:
    val = struct.unpack_from('<I', raw, off)[0]
    print(f'  [{off:2}] {name}: 0x{val:08X}')

print(f'Raw binary: {len(raw)} bytes (0x{len(raw):X})')

# Verify checksum
magic = struct.unpack_from('<I', raw, 0)[0]
flags = struct.unpack_from('<I', raw, 4)[0]
ck = struct.unpack_from('<I', raw, 8)[0]
total = (magic + flags + ck) & 0xFFFFFFFF
print(f'Checksum check: 0x{magic:08X} + 0x{flags:08X} + 0x{ck:08X} = 0x{total:08X} {"OK" if total == 0 else "FAIL"}')

with open(f'{proj}/bin/kernel0.raw', 'wb') as f:
    f.write(raw)
print('Written OK')
