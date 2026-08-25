#!/usr/bin/env python3
"""Debug context switch by monitoring QEMU state and checking for resets."""
import subprocess, time, socket, sys, os

QEMU = r"D:\tools\qemu\qemu-system-x86_64.exe"
KERNEL = r"C:\Users\roone.DESKTOP-QK3UG2M\Downloads\projects\os\bin\kernel0.bin"
LOG = r"C:\Users\roone.DESKTOP-QK3UG2M\Downloads\projects\os\serial.log"
DBGLOG = r"C:\Users\roone.DESKTOP-QK3UG2M\Downloads\projects\os\qemu_debug.log"
PORT = 4444

# Kill existing
subprocess.run(["powershell.exe", "-Command",
    "Stop-Process -Name qemu-system-x86_64 -Force -ErrorAction SilentlyContinue"],
    capture_output=True, timeout=5)
time.sleep(1)

for f in [LOG, DBGLOG]:
    if os.path.exists(f): os.remove(f)

# Start QEMU with cpu_reset debug
args = [QEMU, "-kernel", KERNEL, "-display", "none", "-m", "128",
        "-serial", f"file:{LOG}", "-no-reboot", "-no-shutdown",
        "-accel", "whpx",
        "-monitor", f"tcp:127.0.0.1:{PORT},server,nowait",
        "-d", "cpu_reset,guest_errors",
        "-D", DBGLOG]

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
    print("FAIL: no monitor")
    sys.exit(1)

print("Waiting 40s for boot...")
time.sleep(40)

# Check if QEMU is still alive by querying status
try:
    mon.sendall(b"info status\r\n")
    time.sleep(0.5)
    status = mon.recv(4096).decode(errors='replace')
    print(f"QEMU status: {status.strip()}")
except:
    print("QEMU monitor not responding!")

# Read serial log
with open(LOG, "r", errors="replace") as f:
    log = f.read()

lines = log.strip().split("\n")
print(f"\n=== Serial log ({len(lines)} lines) ===")
for line in lines[-15:]:
    print(f"  {line}")

# Read debug log
if os.path.exists(DBGLOG):
    with open(DBGLOG, "r", errors="replace") as f:
        dbg = f.read()
    if dbg.strip():
        print(f"\n=== QEMU debug log ===")
        for line in dbg.strip().split("\n")[-10:]:
            print(f"  {line}")
    else:
        print("\n=== QEMU debug log: empty (no resets) ===")
else:
    print("\n=== No QEMU debug log ===")

# Check for the 'S' debug marker I put in switch.c
has_S_marker = 'S' in log and log.index('S') > log.rindex('scheduler')
print(f"\nSwitch marker in log: {'YES' if has_S_marker else 'NO'}")

# Check serial port data register for pending data
try:
    mon.sendall(b"info qtree 2>&1 | head -5\r\n")
    time.sleep(0.5)
    info = mon.recv(4096).decode(errors='replace')
except:
    info = "failed"

# Cleanup
try:
    mon.sendall(b"quit\r\n")
except:
    pass
time.sleep(1)
subprocess.run(["powershell.exe", "-Command",
    "Stop-Process -Name qemu-system-x86_64 -Force -ErrorAction SilentlyContinue"],
    capture_output=True, timeout=5)
