#!/usr/bin/env python3
"""Drive Plan 0 shell via QEMU TCP monitor to verify writable virtual files."""
import subprocess, time, socket, sys, os

QEMU = r"D:\tools\qemu\qemu-system-x86_64.exe"
KERNEL = r"C:\Users\roone.DESKTOP-QK3UG2M\Downloads\projects\os\bin\kernel0.bin"
LOG = r"C:\Users\roone.DESKTOP-QK3UG2M\Downloads\projects\os\serial_writable.log"
PORT = 4444

# Clean
try:
    with open(LOG, 'w'): pass
except:
    pass

# Start QEMU
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
    """Send a single key via QEMU TCP monitor."""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(3)
        s.connect(('127.0.0.1', PORT))
        s.recv(4096)  # discard banner
        s.sendall(f"sendkey {key}\r".encode())
        time.sleep(0.1)
        s.close()
    except Exception as e:
        print(f"  Key error ({key}): {e}", file=sys.stderr)

def type_text(text):
    """Type text character by character."""
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
        time.sleep(0.05)  # small gap between keys

try:
    # Wait for boot
    time.sleep(5)

    commands = [
        # Read-only resources
        "read /proc/uptime",
        "read /proc/version",
        "read /proc/hostname",
        "read /sys/kernel/arch",
        "read /proc/stat",
        "read /proc/interrupts",
        "ls /dev",

        # Writable: /dev/null
        "write /dev/null hello world",
        "read /dev/null",

        # Writable: /proc/hostname
        "write /proc/hostname mytesthost",
        "read /proc/hostname",
        "read /sys/kernel/hostname",

        # Restore hostname
        "write /proc/hostname plan0",
        "read /proc/hostname",

        # Writable: /dev/console
        "write /dev/console hello from console",
    ]

    for cmd in commands:
        print(f"  Sending: {cmd}")
        type_text(cmd + "\n")
        time.sleep(0.5)

    time.sleep(1)

    # Read serial log
    with open(LOG, 'r', errors='replace') as f:
        log = f.read()

    print("\n=== Serial log (last 100 lines) ===")
    lines = log.strip().split('\n')
    for line in lines[-100:]:
        print(line)

    print("\n=== Verification ===")
    checks = [
        ("Hostname write", "mytesthost" in log),
        ("Hostname restore", "plan0" in log),
        ("Stat content", "cpu " in log),
        ("Interrupts content", "IRQ" in log),
        ("/dev/null write", "Wrote 11 bytes to /dev/null" in log),
        ("/dev/zero listed", "zero" in log),
        ("Sys hostname mirror", "sys/kernel/hostname" in log),
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
