#!/usr/bin/env python3
"""Test if the shell process runs with preemptive scheduling."""
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

# Start QEMU - use stdio for serial to avoid file buffering
proc = subprocess.Popen(
    [QEMU, "-kernel", KERNEL, "-display", "none", "-m", "128",
     "-serial", "file:" + LOG, "-no-reboot", "-no-shutdown",
     "-accel", "whpx",
     "-monitor", f"tcp:127.0.0.1:{PORT},server,nowait"],
    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

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

# Wait for boot (slow due to serial file buffering)
print("Waiting 35s for boot + serial flush...")
time.sleep(35)

# Read and display boot log
with open(LOG, "r", errors="replace") as f:
    log = f.read()

lines = log.strip().split("\n")
print(f"\n=== Boot log ({len(lines)} lines) ===")
for line in lines[-30:]:
    print(f"  {line}")

# Check key messages
checks = {
    "Shell process created": "Shell process created" in log,
    "PID 2": "PID 2" in log or "(PID 2)" in log,
    "Preemptive scheduling enabled": "Preemptive" in log or "preemptive" in log,
}
print("\n=== Boot checks ===")
for label, passed in checks.items():
    print(f"  {'PASS' if passed else 'FAIL'}  {label}")

# Now type commands and check if shell responds
print("\n=== Sending commands via keyboard ===")

def send_key(sock, key):
    sock.sendall(f"sendkey {key}\r\n".encode())
    time.sleep(0.08)

def type_text(sock, text):
    for ch in text:
        if ch == ' ':
            send_key(sock, "spc")
        elif ch == '/':
            send_key(sock, "slash")
        elif ch == '.':
            send_key(sock, "dot")
        else:
            send_key(sock, ch)
        time.sleep(0.02)
    time.sleep(0.1)
    send_key(sock, "ret")

# Type ps
type_text(mon, "ps")
time.sleep(2)

# Re-read log
with open(LOG, "r", errors="replace") as f:
    log2 = f.read()

lines2 = log2.strip().split("\n")
print(f"\n=== Log after ps ({len(lines2)} lines) ===")
for line in lines2[-20:]:
    print(f"  {line}")

# Check for shell prompt and ps output
has_prompt = ">" in log2
has_ps_output = "PID" in log2 and "Name" in log2
has_shell_proc = "shell" in log2.lower() and "PID" in log2

print(f"\n=== Shell response checks ===")
print(f"  {'PASS' if has_prompt else 'FAIL'}  Shell prompt appeared")
print(f"  {'PASS' if has_ps_output else 'FAIL'}  ps command produced output")
print(f"  {'PASS' if has_shell_proc else 'FAIL'}  Shell process visible in ps")

# Type more commands
type_text(mon, "echo test from shell process")
time.sleep(2)
type_text(mon, "read 0/system/runtime/uptime")
time.sleep(2)

with open(LOG, "r", errors="replace") as f:
    log3 = f.read()

lines3 = log3.strip().split("\n")
print(f"\n=== Full log ({len(lines3)} lines) ===")
for line in lines3[-25:]:
    print(f"  {line}")

has_echo = "test from shell process" in log3
has_uptime = "Uptime" in log3 or "uptime" in log3
print(f"\n=== Additional checks ===")
print(f"  {'PASS' if has_echo else 'FAIL'}  echo command works")
print(f"  {'PASS' if has_uptime else 'FAIL'}  read 0/system/runtime/uptime works")

# Cleanup
try:
    mon.sendall(b"quit\r\n")
except:
    pass
time.sleep(1)
subprocess.run(["powershell.exe", "-Command",
    "Stop-Process -Name qemu-system-x86_64 -Force -ErrorAction SilentlyContinue"],
    capture_output=True, timeout=5)
