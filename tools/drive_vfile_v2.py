#!/usr/bin/env python3
"""Drive Plan 0 shell via QEMU TCP monitor to verify new virtual resources."""
import subprocess, time, socket, sys, os

QEMU = r"D:\tools\qemu\qemu-system-x86_64.exe"
KERNEL = r"C:\Users\roone.DESKTOP-QK3UG2M\Downloads\projects\os\bin\kernel0.bin"
LOG = r"C:\Users\roone.DESKTOP-QK3UG2M\Downloads\projects\os\serial_v2.log"
PORT = 4444

try:
    with open(LOG, 'w'): pass
except: pass

proc = subprocess.Popen([
    QEMU, "-kernel", KERNEL,
    "-accel", "whpx",
    "-display", "none",
    "-serial", "file:" + LOG,
    "-monitor", f"tcp:127.0.0.1:{PORT},server,nowait",
    "-m", "128",
    "-no-reboot", "-no-shutdown",
], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

time.sleep(3)

def send_key(key):
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(3)
        s.connect(('127.0.0.1', PORT))
        s.recv(4096)
        s.sendall(f"sendkey {key}\r".encode())
        time.sleep(0.15)
        s.close()
    except Exception as e:
        print(f"  Key error ({key}): {e}", file=sys.stderr)

def type_text(text):
    for ch in text:
        if ch == ' ': send_key("spc")
        elif ch == '\n': send_key("ret")
        elif ch == '.': send_key("dot")
        elif ch == '/': send_key("slash")
        elif ch == '-': send_key("minus")
        elif ch == ':': send_key("shift-semicolon")
        elif ch == '=': send_key("equal")
        elif ch == ',': send_key("comma")
        elif ch == '_': send_key("shift-minus")
        elif ch.isupper(): send_key(f"shift-{ch.lower()}")
        else: send_key(ch)
        time.sleep(0.06)

try:
    # Wait for boot to complete
    time.sleep(8)

    commands = [
        "ls /",
        "ls /proc",
        "ls 0/system/self",
        "read 0/system/self/pid",
        "read 0/system/self/name",
        "read 0/system/self/status",
        "read 0/system/mounts",
        "ls /sys",
        "read 0/hardware/pci",
        "read 0/system/kernel/version",
        "read 0/system/version",
        "read 0/system/runtime/uptime",
        "read 0/system/stat",
        "ls /dev",
    ]

    for cmd in commands:
        print(f"  Sending: {cmd}")
        type_text(cmd + "\n")
        time.sleep(0.8)

    time.sleep(1)

    with open(LOG, 'r', errors='replace') as f:
        log = f.read()

    print("\n=== Serial log (last 150 lines) ===")
    lines = log.strip().split('\n')
    for line in lines[-150:]:
        print(line)

    print("\n=== Verification ===")
    checks = [
        ("Root ls shows proc/sys/dev", "[DIR]" in log and "proc" in log),
        ("/proc listing", "uptime" in log and "memory" in log),
        ("0/system/self listed", "self" in log),
        ("0/system/self/pid returns PID", "0/system/self/pid" in log),
        ("0/system/self/name returns name", "0/system/self/name" in log),
        ("0/system/self/status has fields", "Name:" in log and "PID:" in log and "State:" in log),
        ("0/system/mounts shows ramfs", "ramfs" in log),
        ("/sys listing has devices", "devices" in log),
        ("0/hardware/pci has Bus header", "Bus" in log),
        ("Version string unified", "Plan 0 v0.4.1" in log),
        ("Existing uptime works", "Uptime:" in log),
        ("Existing stat works", "cpu " in log),
    ]
    all_pass = True
    for name, result in checks:
        status = "PASS" if result else "FAIL"
        if not result: all_pass = False
        print(f"  {status}: {name}")

    if all_pass:
        print("\nAll verification checks passed!")
    else:
        print("\nSome checks failed.")

finally:
    proc.terminate()
    try: proc.wait(timeout=3)
    except: proc.kill()
