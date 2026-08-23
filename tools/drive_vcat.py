#!/usr/bin/env python3
"""Drive Plan 0 shell via QEMU monitor (sendkey) to verify virtual files are
reachable through the VFS fd layer (the unified mount)."""
import subprocess, time, socket, sys, os

QEMU = os.path.expanduser(r"~\qemu\qemu-system-x86_64.exe")
KERNEL = os.path.join(os.path.dirname(__file__), "..", "bin", "kernel0.bin")
LOG = os.path.join(os.path.dirname(__file__), "vcat_serial.log")
PORT = 4449

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
        elif ch == '+':
            send_key("shift-equal")
        elif ch == '>':
            send_key("shift-dot")
        elif ch.isupper():
            send_key(f"shift-{ch.lower()}")
        else:
            send_key(ch)
        time.sleep(0.06)

def read_log():
    with open(LOG, 'r', errors='replace') as f:
        return f.read()

class LogWatcher:
    """Tracks a byte offset into the log so each wait only matches NEW output."""
    def __init__(self):
        self.pos = 0
        self.buf = read_log()
        self.pos = len(self.buf)

    def wait_for(self, needle, timeout=30):
        deadline = time.time() + timeout
        while time.time() < deadline:
            self.buf = read_log()
            if needle in self.buf[self.pos:]:
                self.pos = len(self.buf)
                return True
            time.sleep(0.4)
        self.pos = len(self.buf)
        return False

    def tail(self, n=400):
        self.buf = read_log()
        return self.buf[-n:]

try:
    time.sleep(14)

    w = LogWatcher()
    if not w.wait_for("> ", timeout=60):
        print("FAIL: shell prompt never appeared")
        sys.exit(1)
    print("OK: shell prompt reached")

    checks = []

    # 1. vcat /proc/uptime through the fd layer
    type_text("vcat /proc/uptime\n")
    ok = w.wait_for("bytes via fd", timeout=30)
    out = w.tail()
    ok = ok and "Uptime:" in out
    checks.append(("vcat /proc/uptime (fd layer)", ok, out))

    # 2. vcat /sys/kernel/name
    type_text("vcat /sys/kernel/name\n")
    ok = w.wait_for("bytes via fd", timeout=30)
    out = w.tail()
    ok = ok and "Plan 0" in out
    checks.append(("vcat /sys/kernel/name (fd layer)", ok, out))

    # 3. vcat /proc/processes
    type_text("vcat /proc/processes\n")
    ok = w.wait_for("bytes via fd", timeout=30)
    out = w.tail()
    ok = ok and "PID" in out
    checks.append(("vcat /proc/processes (fd layer)", ok, out))

    # 4. vcat /dev/null — empty but still opens through fd layer
    type_text("vcat /dev/null\n")
    ok = w.wait_for("0 bytes via fd", timeout=30)
    checks.append(("vcat /dev/null empty via fd", ok, w.tail(300)))

    # 5. vcat of a non-existent virtual file fails at open
    type_text("vcat /proc/nope\n")
    ok = w.wait_for("could not open", timeout=30)
    checks.append(("vcat /proc/nope fails at open", ok, w.tail(300)))

    # 6. ordinary ramfs file still opens through the fd layer
    type_text("vcat /sys/kernel/arch\n")
    ok = w.wait_for("bytes via fd", timeout=30)
    out = w.tail()
    ok = ok and "x86_64" in out
    checks.append(("vcat /sys/kernel/arch (fd layer)", ok, out))

    print()
    all_ok = True
    for name, ok, tail in checks:
        status = "PASS" if ok else "FAIL"
        if not ok:
            all_ok = False
        print(f"{status}: {name}")
        if not ok:
            print(f"  tail: {tail[-300:]!r}")
    sys.exit(0 if all_ok else 1)

finally:
    proc.terminate()
    try:
        proc.wait(timeout=3)
    except Exception:
        proc.kill()
