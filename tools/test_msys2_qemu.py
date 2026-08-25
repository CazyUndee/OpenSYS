#!/usr/bin/env python3
"""Boot kernel under MSYS2 QEMU (WHPX then TCG) to compare context-switch behavior."""
import subprocess, time, sys, os

QEMU = r"C:\msys64\mingw64\bin\qemu-system-x86_64.exe"
KERNEL = r"C:\Users\roone.DESKTOP-QK3UG2M\Downloads\projects\os\bin\kernel0.bin"

def test(accel, label, wait_s=45):
    LOG = rf"C:\Users\roone.DESKTOP-QK3UG2M\Downloads\projects\os\serial_{label}.log"
    if os.path.exists(LOG): os.remove(LOG)
    args = [QEMU, "-kernel", KERNEL, "-display", "none", "-m", "128",
            "-serial", f"file:{LOG}", "-no-reboot", "-no-shutdown",
            "-accel", accel]
    proc = subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(wait_s)
    proc.terminate()
    time.sleep(1)
    subprocess.run(["powershell.exe", "-Command",
        "Stop-Process -Name qemu-system-x86_64 -Force -ErrorAction SilentlyContinue"],
        capture_output=True, timeout=5)
    time.sleep(1)
    print(f"\n=== {label} ({accel}) ===")
    if os.path.exists(LOG):
        content = open(LOG, "r", errors="replace").read()
        print(f"serial size: {len(content)}")
        print(content[-2200:])
    else:
        print("NO LOG")

test("whpx", "msys2_whpx")
test("tcg", "msys2_tcg")
