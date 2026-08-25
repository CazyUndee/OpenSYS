#!/usr/bin/env python3
"""Dump GDT entries, idle context struct, and iretq stack frame at panic."""
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
    print("No panic. Serial tail:")
    if os.path.exists(LOG): print(open(LOG, "r", errors="replace").read()[-1500:])
    sys.exit(1)

time.sleep(1)
def qemu_cmd(cmd):
    try:
        mon.sendall(cmd.encode() + b"\r\n")
        time.sleep(0.7)
        return mon.recv(65536).decode(errors="replace")
    except Exception as e:
        return f"(err {e})"

print("=== GDT base 0x125780 (8 qwords) ===")
print(qemu_cmd("xp /8gx 0x125780"))
print("=== idle_process @0x15A000 — context starts after pid/name/state ===")
print(qemu_cmd("xp /40gx 0x15A000"))
print("=== stack @ 0x851E0 (physical alias of 0xFFFF8000000851E0) ===")
print(qemu_cmd("xp /8gx 0x851E0"))

try:
    mon.sendall(b"quit\r\n")
except: pass
time.sleep(1)
subprocess.run(["powershell.exe", "-Command",
    "Stop-Process -Name qemu-system-x86_64 -Force -ErrorAction SilentlyContinue"],
    capture_output=True, timeout=5)
