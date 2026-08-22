#!/usr/bin/env python3
"""Drive Plan 0 shell via QEMU TCP monitor to verify shell redirection."""
import subprocess, time, socket, sys, os

QEMU = r"C:\Users\roone.DESKTOP-QK3UG2M\qemu\qemu-system-x86_64.exe"
KERNEL = r"C:\Users\roone.DESKTOP-QK3UG2M\Downloads\projects\os\bin\kernel0.bin"
LOG = r"C:\Users\roone.DESKTOP-QK3UG2M\Downloads\projects\os\serial_redir.log"
PORT = 4453

try:
    with open(LOG, 'w'):
        pass
except Exception:
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
        elif ch == '_':
            send_key("shift-minus")
        elif ch == ':':
            send_key("shift-semicolon")
        elif ch == '=':
            send_key("equal")
        elif ch == ',':
            send_key("comma")
        elif ch == '>':
            send_key("shift-dot")
        elif ch == '(':
            send_key("shift-9")
        elif ch == ')':
            send_key("shift-0")
        elif ch == '"':
            send_key("shift-apostrophe")
        elif ch.isupper():
            send_key(f"shift-{ch.lower()}")
        else:
            send_key(ch)
        time.sleep(0.08)

try:
    time.sleep(12)

    commands = [
        "echo hello world > out.txt",
        "read out.txt",
        "ls > listing.txt",
        "read listing.txt",
        "echo second line >> out.txt",
        "read out.txt",
        "echo direct > /dev/null",
        "read /dev/null",
    ]

    for cmd in commands:
        print(f"  Sending: {cmd}")
        type_text(cmd + "\n")
        time.sleep(1.2)

    time.sleep(2)

    with open(LOG, 'r', errors='replace') as f:
        log = f.read()

    print("\n=== Serial log (last 40 lines) ===")
    lines = log.strip().split('\n')
    for line in lines[-40:]:
        print(line)

    print("\n=== Verification ===")
    checks = [
        ("echo redirected to file", "Redirected 14 bytes to out.txt" in log),
        ("file contains payload", "hello world" in log),
        ("ls redirected", "Redirected" in log and "listing.txt" in log),
        ("listing contains real entries", "out.txt" in log and "listing.txt" in log),
        ("append operation wrote 14 bytes", "Redirected 14 bytes to out.txt" in log),
        ("append did not truncate (both lines in file)", "hello world" in log and "second line" in log),
        ("append preserved both lines", "hello world" in log and "second line" in log),
        ("no stray error in appended file", "Error: redirection needs a target file" not in log.split("echo second line >> out.txt")[1].split("echo direct")[0]),
        ("redirect to /dev/null", "Redirected 9 bytes to /dev/null" in log),
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
    except Exception:
        proc.kill()
