#!/usr/bin/env python3
"""Quick test: boot QEMU, wait 30 seconds, check serial log for shell."""
import subprocess, time, os, sys

QEMU = r"D:\tools\qemu\qemu-system-x86_64.exe"
KERNEL = r"C:\Users\roone.DESKTOP-QK3UG2M\Downloads\projects\os\bin\kernel0.bin"
LOG = r"C:\Users\roone.DESKTOP-QK3UG2M\Downloads\projects\os\serial.log"
PORT = 4444

# Kill any existing QEMU
subprocess.run(["powershell.exe", "-Command",
    "Stop-Process -Name qemu-system-x86_64 -Force -ErrorAction SilentlyContinue"],
    capture_output=True, timeout=5)
time.sleep(1)

# Remove old log
if os.path.exists(LOG):
    os.remove(LOG)

# Start QEMU
args = [QEMU, "-kernel", KERNEL, "-display", "none", "-m", "128",
        "-serial", f"file:{LOG}", "-no-reboot", "-no-shutdown",
        "-accel", "whpx",
        "-monitor", f"tcp:127.0.0.1:{PORT},server,nowait"]
proc = subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

# Monitor connection
import socket
mon = None
for i in range(10):
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
    proc.kill()
    sys.exit(1)

# Check log every 5 seconds for 30 seconds
prev_size = 0
for t in range(6):
    time.sleep(5)
    size = os.path.getsize(LOG) if os.path.exists(LOG) else 0
    growth = size - prev_size
    prev_size = size
    
    with open(LOG, "r", errors="replace") as f:
        content = f.read()
    
    lines = content.strip().split("\n")
    last = lines[-1] if lines else ""
    
    has_shell = "Starting Shell" in content or "shell" in content.lower()
    has_ps = "shell" in content and "PID" in content
    
    print(f"t={5*(t+1)}s  log={size}B  growth=+{growth}B  lines={len(lines)}  last='{last[:80]}'  shell_proc={has_ps}")
    
    if has_ps:
        print("\n=== Full log ===")
        for line in lines:
            print(f"  {line}")
        break

# Type ps command and check
if mon:
    for ch in "ps":
        mon.sendall(f"sendkey {ch}\r\n".encode())
        time.sleep(0.05)
    time.sleep(0.1)
    mon.sendall(b"sendkey ret\r\n")
    time.sleep(2)
    
    with open(LOG, "r", errors="replace") as f:
        content = f.read()
    
    print("\n=== Log after ps command ===")
    for line in content.strip().split("\n")[-30:]:
        print(f"  {line}")

# Cleanup
try:
    mon.sendall(b"quit\r\n")
except:
    pass
time.sleep(1)
subprocess.run(["powershell.exe", "-Command",
    "Stop-Process -Name qemu-system-x86_64 -Force -ErrorAction SilentlyContinue"],
    capture_output=True, timeout=5)
