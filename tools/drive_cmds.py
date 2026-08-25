#!/usr/bin/env python3
"""Drive Plan 0 shell via QEMU TCP monitor to verify new commands."""
import subprocess, time, socket, sys, os

QEMU = r"D:\tools\qemu\qemu-system-x86_64.exe"
KERNEL = r"C:\Users\roone.DESKTOP-QK3UG2M\Downloads\projects\os\bin\kernel0.bin"
LOG = r"C:\Users\roone.DESKTOP-QK3UG2M\Downloads\projects\os\serial_cmds.log"
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
    time.sleep(8)

    commands = [
        "pwd",
        "cd /proc",
        "pwd",
        "cd /",
        "pwd",
        "mount",
        "df",
        "ps",
        "create test.txt",
        "write test.txt hello",
        "read test.txt",
        "rm test.txt",
    ]

    for cmd in commands:
        print(f"  Sending: {cmd}")
        type_text(cmd + "\n")
        time.sleep(0.8)

    time.sleep(1)

    with open(LOG, 'r', errors='replace') as f:
        log = f.read()

    print("\n=== Serial log (last 100 lines) ===")
    lines = log.strip().split('\n')
    for line in lines[-100:]:
        print(line)

    print("\n=== Verification ===")
    checks = [
        ("pwd works", "/" in log),
        ("cd /proc works", "cd" in log),
        ("pwd after cd shows /proc", "/proc" in log),
        ("mount shows ramfs", "ramfs" in log),
        ("mount shows vfile", "vfile" in log),
        ("df shows filesystems", "Filesystem" in log and "ramfs" in log),
        ("ps shows processes", "PID" in log and "Name" in log),
        ("create/write/read works", "hello" in log),
        ("rm works", "Deleted" in log),
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
