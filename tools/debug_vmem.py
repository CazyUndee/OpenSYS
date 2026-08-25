#!/usr/bin/env python3
"""Read virtual memory at the iretq frame (x command translates via CR3)."""
import subprocess, time, socket, sys, os

QEMU = r"D:\tools\qemu\qemu-system-x86_64.exe"
KERNEL = r"C:\Users\roone.DESKTOP-QK3UG2M\Downloads\projects\os\bin\kernel0.bin"
LOG = r"C:\Users\roone.DESKTOP-QK3UG2M\Downloads\projects\os\serial.log"
PORT = 4444

subprocess.run(["powershell.exe", "-Command",
    "Stop-Process -Name qemu-system-x86_64 -Force -ErrorAction SilentlyContinue"],
    capture_output=True, timeout=5)
time.sleep(1)
if os.path.exists(LOG): os.remove(LOG)

args = [QEMU, "-kernel", KERNEL, "-display", "none", "-m", "128",
        "-serial", f"file:{LOG}", "-no-reboot", "-no-shutdown",
        "-accel", "whpx",
        "-monitor", f"tcp:127.0.0.1:{PORT},server,nowait"]
proc = subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

mon = None
for i in range(20):
    time.sleep(0.5)
    try:
        mon = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        mon.settimeout(2)
        mon.connect(("127.0.0.1", PORT))
        mon.recv(4096)
        break
    except:
        mon = None
if not mon:
    print("FAIL: no monitor"); sys.exit(1)

def serial_has_panic():
    if not os.path.exists(LOG): return False
    with open(LOG, "r", errors="replace") as f:
        return "KERNEL PANIC" in f.read()

deadline = time.time() + 60
while time.time() < deadline:
    if serial_has_panic(): break
    time.sleep(0.5)
if not serial_has_panic():
    print("No panic"); sys.exit(1)

time.sleep(1)
def qemu_cmd(cmd):
    try:
        mon.sendall(cmd.encode() + b"\r\n")
        time.sleep(0.7)
        return mon.recv(65536).decode(errors="replace")
    except Exception as e:
        return f"(err {e})"

# 'x' = virtual memory read (uses CR3), 'xp' = physical
print("=== virtual stack @ 0xFFFF8000000851D0 (iretq frame) ===")
print(qemu_cmd("x /12gx 0xFFFF8000000851D0"))
print("=== idle context @ 0x15A000 + context offset ===")
# context offset in process_t: pid(8) + name(32) + state(4->8) = 48 bytes
# but packed: pid_t is uint64 (8), name 32, state enum int (4, aligned 8)
# context starts at offset 8+32+8 = 48
print(qemu_cmd("xp /32gx 0x15A030"))
print("=== gdt @ 0x125780 ===")
print(qemu_cmd("xp /6gx 0x125780"))

try:
    mon.sendall(b"quit\r\n")
except: pass
time.sleep(1)
subprocess.run(["powershell.exe", "-Command",
    "Stop-Process -Name qemu-system-x86_64 -Force -ErrorAction SilentlyContinue"],
    capture_output=True, timeout=5)
