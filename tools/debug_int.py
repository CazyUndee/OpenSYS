#!/usr/bin/env python3
"""Trace QEMU int/exceptions to find the exact #GP delivery."""
import subprocess, time, socket, sys, os

QEMU = r"D:\tools\qemu\qemu-system-x86_64.exe"
KERNEL = r"C:\Users\roone.DESKTOP-QK3UG2M\Downloads\projects\os\bin\kernel0.bin"
LOG = r"C:\Users\roone.DESKTOP-QK3UG2M\Downloads\projects\os\serial.log"
DBG = r"C:\Users\roone.DESKTOP-QK3UG2M\Downloads\projects\os\int_trace.log"

subprocess.run(["powershell.exe", "-Command",
    "Stop-Process -Name qemu-system-x86_64 -Force -ErrorAction SilentlyContinue"],
    capture_output=True, timeout=5)
time.sleep(1)
for f in [LOG, DBG]:
    if os.path.exists(f): os.remove(f)

args = [QEMU, "-kernel", KERNEL, "-display", "none", "-m", "128",
        "-serial", f"file:{LOG}", "-no-reboot", "-no-shutdown",
        "-accel", "whpx",
        "-d", "int,guest_errors",
        "-D", DBG]
proc = subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

# Wait for panic
deadline = time.time() + 60
while time.time() < deadline:
    if os.path.exists(LOG):
        with open(LOG, "r", errors="replace") as f:
            if "KERNEL PANIC" in f.read():
                break
    time.sleep(0.5)

time.sleep(1)
proc.terminate()
time.sleep(1)
subprocess.run(["powershell.exe", "-Command",
    "Stop-Process -Name qemu-system-x86_64 -Force -ErrorAction SilentlyContinue"],
    capture_output=True, timeout=5)

print("=== int trace: last 60 lines ===")
with open(DBG, "r", errors="replace") as f:
    lines = f.readlines()
for line in lines[-60:]:
    print(line.rstrip())
