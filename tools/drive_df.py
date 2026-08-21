#!/usr/bin/env python3
"""Drive Plan 0 shell via QEMU TCP monitor to verify df ramfs accounting."""
import subprocess, time, socket, sys, os

QEMU = r"C:\Program Files\qemu\qemu-system-x86_64.exe"
KERNEL = r"C:\Users\roone.DESKTOP-QK3UG2M\Downloads\projects\os\bin\kernel0.bin"
LOG = r"C:\Users\roone.DESKTOP-QK3UG2M\Downloads\projects\os\serial_df.log"
PORT = 4446

try:
    with open(LOG, 'w'): pass
except:
    pass

proc = subprocess.Popen([
    QEMU, "-kernel", KERNEL,
    "-accel", "whpx",
    "-display", "none",
    "-serial", "file:" + LOG,
    "-monitor", f"tcp:127.0.0.1:{PORT},server,nowait",
    "-m", "128",
    "-no-reboot", "-no-shutdown",
], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

time.sleep(2)

def send_key(key):
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(3)
        s.connect(('127.0.0.1', PORT))
        s.recv(4096)
        s.sendall(f"sendkey {key}\r".encode())
        time.sleep(0.1)
        s.close()
    except Exception as e:
        print(f"  Key error ({key}): {e}", file=sys.stderr)

def type_text(text):
    for ch in text:
        if ch == ' ':
            send_key("spc")
        elif ch == '\n':
            send_key("ret")
        elif ch == '.':
            send_key("dot")
        elif ch == '/':
            send_key("slash")
        elif ch == '-':
            send_key("minus")
        elif ch == ':':
            send_key("shift-semicolon")
        elif ch == '=':
            send_key("equal")
        elif ch == ',':
            send_key("comma")
        elif ch.isupper():
            send_key(f"shift-{ch.lower()}")
        else:
            send_key(ch)
        time.sleep(0.08)

try:
    time.sleep(6)

    commands = [
        "df",
        "create hello.txt",
        "write hello.txt hello world",
        "df",
        "mkdir docs",
        "df",
        "rm hello.txt",
        "df",
    ]

    for cmd in commands:
        print(f"  Sending: {cmd}")
        type_text(cmd + "\n")
        time.sleep(0.7)

    time.sleep(1)

    with open(LOG, 'r', errors='replace') as f:
        log = f.read()

    print("\n=== Serial log (last 80 lines) ===")
    lines = log.strip().split('\n')
    for line in lines[-80:]:
        print(line)

    print("\n=== Verification ===")
    checks = [
        ("df shows fs filesystem", "  fs              " in log),
        ("df no longer equals RAM total", "130944" not in log),
        ("create works", "Created: hello.txt" in log),
        ("write works", "Wrote 11 bytes" in log),
        ("file count reported", "files," in log),
        ("mkdir works", "Created directory: docs" in log),
        ("rm works", "Deleted: hello.txt" in log),
        ("df still works after ops", log.count("files,") >= 3),
    ]
    all_pass = True
    for name, result in checks:
        status = "PASS" if result else "FAIL"
        if not result:
            all_pass = False
        print(f"  {status}: {name}")

    if all_pass:
        print("\nAll verification checks passed!")
    else:
        print("\nSome checks failed — check the log above.")

finally:
    proc.terminate()
    try:
        proc.wait(timeout=3)
    except:
        proc.kill()
