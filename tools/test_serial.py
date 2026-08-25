#!/usr/bin/env python3
"""Test with -serial stdio to avoid file buffering."""
import subprocess, time, socket, sys, os

QEMU = r"D:\tools\qemu\qemu-system-x86_64.exe"
KERNEL = r"C:\Users\roone.DESKTOP-QK3UG2M\Downloads\projects\os\bin\kernel0.bin"
LOG = r"C:\Users\roone.DESKTOP-QK3UG2M\Downloads\projects\os\serial.log"
PORT = 4444

# Kill existing
subprocess.run(["powershell.exe", "-Command",
    "Stop-Process -Name qemu-system-x86_64 -Force -ErrorAction SilentlyContinue"],
    capture_output=True, timeout=5)
time.sleep(1)

if os.path.exists(LOG):
    os.remove(LOG)

# Start QEMU with both file serial and monitor
args = [QEMU, "-kernel", KERNEL, "-display", "none", "-m", "128",
        "-serial", "file:" + LOG, "-no-reboot", "-no-shutdown",
        "-accel", "whpx",
        "-monitor", f"tcp:127.0.0.1:{PORT},server,nowait",
        "-d", "int,cpu_reset"]

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

# Wait 60 seconds for serial output
print("Waiting 60s for serial output...")
prev_size = 0
for t in range(12):
    time.sleep(5)
    size = os.path.getsize(LOG) if os.path.exists(LOG) else 0
    growth = size - prev_size
    prev_size = size
    
    with open(LOG, "r", errors="replace") as f:
        content = f.read()
    
    lines = content.strip().split("\n")
    last = lines[-1] if lines else ""
    
    has_prompt = "> " in content
    has_shell_pid = "shell" in content.lower() and "PID" in content
    
    print(f"t={5*(t+1)}s  size={size}B  +{growth}B  lines={len(lines)}  prompt={has_prompt}  last='{last[:60]}'")
    
    if has_prompt:
        print("\n=== SHELL PROMPT FOUND! ===")
        break

# Final log
with open(LOG, "r", errors="replace") as f:
    log = f.read()

print(f"\n=== Full log ({len(log)} bytes) ===")
for line in log.strip().split("\n")[-40:]:
    print(f"  {line}")

# Try typing a command
if mon:
    print("\n=== Sending 'ps' ===")
    for ch in "ps":
        mon.sendall(f"sendkey {ch}\r\n".encode())
        time.sleep(0.05)
    time.sleep(0.1)
    mon.sendall(b"sendkey ret\r\n")
    time.sleep(3)
    
    with open(LOG, "r", errors="replace") as f:
        log2 = f.read()
    
    # Show only new lines
    old_lines = set(log.strip().split("\n"))
    new_lines = [l for l in log2.strip().split("\n") if l not in old_lines]
    if new_lines:
        print("=== New lines after ps ===")
        for line in new_lines:
            print(f"  {line}")
    else:
        print("  (no new output)")

# Cleanup
try:
    mon.sendall(b"quit\r\n")
except:
    pass
time.sleep(1)
subprocess.run(["powershell.exe", "-Command",
    "Stop-Process -Name qemu-system-x86_64 -Force -ErrorAction SilentlyContinue"],
    capture_output=True, timeout=5)
