#!/usr/bin/env python3
"""Debug recursive find: create nested dir/file, then ls and find."""
import subprocess, time, socket, sys, os

def _qemu_path():
    cands = [
        os.path.join(os.environ.get("HOME", ""), "qemu", "qemu-system-x86_64.exe"),
        os.path.join(os.path.expanduser("~"), "qemu", "qemu-system-x86_64.exe"),
        r"C:\Users\roone.DESKTOP-QK3UG2M\qemu\qemu-system-x86_64.exe",
        r"D:\tools\qemu\qemu-system-x86_64.exe",
    ]
    for c in cands:
        if c and os.path.exists(c):
            return c
    return cands[0]

QEMU = _qemu_path()
KERNEL = os.path.join(os.path.dirname(__file__), "..", "bin", "kernel0.bin")
LOG = os.path.join(os.path.dirname(__file__), "find_debug.log")
PORT = 4461

with open(LOG, 'w') as f:
    pass

proc = subprocess.Popen([
    QEMU, "-kernel", KERNEL, "-accel", "whpx", "-display", "none",
    "-serial", "file:" + LOG, "-monitor", f"tcp:127.0.0.1:{PORT},server,nowait",
    "-m", "128", "-no-reboot", "-no-shutdown",
], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

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
        else:
            send_key(ch)
        time.sleep(0.06)

def read_log():
    with open(LOG, 'r', errors='replace') as f:
        return f.read()

def wait_for(needle, timeout=30):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if needle in read_log():
            return True
        time.sleep(0.4)
    return False

try:
    time.sleep(14)
    if not wait_for("> ", timeout=60):
        print("FAIL: no prompt")
        sys.exit(1)

    for cmd in [
        # replicate the vcat churn: create/delete files and dirs
        "write a1.txt x\n",
        "write a2.txt x\n",
        "delete a1.txt\n",
        "mkdir d1\n",
        "write d1/c.txt x\n",
        "delete d1/c.txt\n",
        "delete d1\n",
        "mkdir d2\n",
        "delete d2\n",
        "mkdir ft\n",
        "write ft/deep-note.txt hello\n",
        "ls\n",
        "ls ft\n",
        "find note\n",
        "find deep\n",
    ]:
        print(f">>> typing: {cmd.strip()}")
        type_text(cmd)
        time.sleep(2)

    time.sleep(2)
    print("=== LOG ===")
    print(read_log())
finally:
    proc.terminate()
    try:
        proc.wait(timeout=3)
    except Exception:
        proc.kill()
