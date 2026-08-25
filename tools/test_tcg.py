#!/usr/bin/env python3
"""Boot kernel under TCG (no WHPX) to compare context-switch behavior."""
import subprocess, time, sys, os

QEMU = r"D:\tools\qemu\qemu-system-x86_64.exe"
KERNEL = r"C:\Users\roone.DESKTOP-QK3UG2M\Downloads\projects\os\bin\kernel0.bin"
LOG = r"C:\Users\roone.DESKTOP-QK3UG2M\Downloads\projects\os\serial_tcg.log"

subprocess.run(["powershell.exe", "-Command",
    "Stop-Process -Name qemu-system-x86_64 -Force -ErrorAction SilentlyContinue"],
    capture_output=True, timeout=5)
time.sleep(1)

if os.path.exists(LOG): os.remove(LOG)

# TCG is slow; boot needs more time
args = [QEMU, "-kernel", KERNEL, "-display", "none", "-m", "128",
        "-serial", f"file:{LOG}", "-no-reboot", "-no-shutdown",
        "-accel", "tcg"]

proc = subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

# Wait up to 120s for boot to complete or panic
deadline = time.time() + 120
last_size = -1
while time.time() < deadline:
    time.sleep(2)
    if os.path.exists(LOG):
        with open(LOG, "r", errors="replace") as f:
            content = f.read()
        if "KERNEL PANIC" in content:
            print("=== PANIC under TCG ===")
            print(content[-2500:])
            break
        if "> " in content or "scheduler active" in content:
            print("=== BOOTED under TCG ===")
            print(content[-2500:])
            break

proc.terminate()
time.sleep(1)
subprocess.run(["powershell.exe", "-Command",
    "Stop-Process -Name qemu-system-x86_64 -Force -ErrorAction SilentlyContinue"],
    capture_output=True, timeout=5)
