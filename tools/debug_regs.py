#!/usr/bin/env python3
"""Dump QEMU CPU registers at panic time to diagnose the context-switch #GP."""
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

# Connect monitor
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

# Wait for panic in serial log
def serial_has_panic():
    if not os.path.exists(LOG): return False
    with open(LOG, "r", errors="replace") as f:
        return "KERNEL PANIC" in f.read()

deadline = time.time() + 60
while time.time() < deadline:
    if serial_has_panic():
        break
    time.sleep(0.5)

if not serial_has_panic():
    print("No panic seen in 60s. Serial tail:")
    if os.path.exists(LOG):
        print(open(LOG, "r", errors="replace").read()[-2000:])
    sys.exit(1)

print("=== PANIC DETECTED — dumping CPU state ===")
time.sleep(1)

def qemu_cmd(cmd):
    try:
        mon.sendall(cmd.encode() + b"\r\n")
        time.sleep(0.6)
        return mon.recv(65536).decode(errors="replace")
    except Exception as e:
        return f"(err {e})"

print(qemu_cmd("info registers"))
print(qemu_cmd("info tlb 0xFFFF800000085000"))
print(qemu_cmd("xp /16gx 0xFFFF8000000851C0"))

# Cleanup
try:
    mon.sendall(b"quit\r\n")
except: pass
time.sleep(1)
subprocess.run(["powershell.exe", "-Command",
    "Stop-Process -Name qemu-system-x86_64 -Force -ErrorAction SilentlyContinue"],
    capture_output=True, timeout=5)
