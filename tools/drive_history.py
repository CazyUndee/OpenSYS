#!/usr/bin/env python3
"""Drive Plan 0 shell via QEMU monitor (sendkey) to verify command-history
recall: up-arrow recalls the previous command, Enter re-runs it, up-up walks
further back, and down returns to more recent entries."""
import subprocess, time, socket, sys, os

def _qemu_path():
    cands = [
        os.path.join(os.environ.get("HOME", ""), "qemu", "qemu-system-x86_64.exe"),
        os.path.join(os.path.expanduser("~"), "qemu", "qemu-system-x86_64.exe"),
        os.path.join(os.environ.get("USERPROFILE", ""), "qemu", "qemu-system-x86_64.exe"),
        r"C:\Users\roone.DESKTOP-QK3UG2M\qemu\qemu-system-x86_64.exe",
        r"D:\tools\qemu\qemu-system-x86_64.exe",
    ]
    for c in cands:
        if c and os.path.exists(c):
            return c
    return cands[0]

QEMU = _qemu_path()
KERNEL = os.path.join(os.path.dirname(__file__), "..", "bin", "kernel0.bin")
LOG = os.path.join(os.path.dirname(__file__), "history_serial.log")
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

    def wait_for_echo(self, text, timeout=12):
        deadline = time.time() + timeout
        while time.time() < deadline:
            self.buf = read_log()
            idx = self.buf.find(text, self.pos)
            if idx >= 0:
                self.pos = idx + len(text)
                return True
            time.sleep(0.3)
        return False

    def type_and_wait(self, text, marker, timeout=30):
        stripped = text.rstrip("\n")
        for attempt in range(2):
            type_text(text)
            if self.wait_for_echo(stripped):
                return self.wait_for(marker, timeout=timeout)
        return self.wait_for(marker, timeout=timeout)

    def tail(self, n=400):
        self.buf = read_log()
        return self.buf[-n:]

try:
    time.sleep(14)

    w = LogWatcher()
    if "> " not in read_log():
        if not w.wait_for("> ", timeout=60):
            print("FAIL: shell prompt never appeared")
            sys.exit(1)
    w.buf = read_log()
    w.pos = len(w.buf)
    print("OK: shell prompt reached")

    checks = []

    # 1. seed two commands in history
    ok = w.type_and_wait("write histtest.txt hello\n", "bytes to histtest.txt")
    checks.append(("write histtest.txt", ok, w.tail(120)))
    ok = w.type_and_wait("vcat histtest.txt\n", "hello")
    checks.append(("vcat histtest.txt", ok, w.tail(120)))

    # 2. up arrow recalls the last command (its echo appears again)
    send_key("up")
    ok = w.wait_for("vcat histtest.txt", timeout=10)
    checks.append(("up recalls last command", ok, w.tail(160)))

    # 3. Enter re-runs the recalled command -> vcat prints the content
    send_key("ret")
    ok = w.wait_for("hello", timeout=10)
    checks.append(("recalled command executes", ok, w.tail(120)))

    # 4. up twice walks back to the write command
    send_key("up")
    send_key("up")
    ok = w.wait_for("write histtest.txt hello", timeout=10)
    checks.append(("up twice recalls write", ok, w.tail(160)))

    # 5. down returns to the vcat command, Enter re-runs it
    send_key("down")
    ok = w.wait_for("vcat histtest.txt", timeout=10)
    checks.append(("down returns to vcat", ok, w.tail(160)))
    send_key("ret")
    ok = w.wait_for("hello", timeout=10)
    checks.append(("down-recalled command executes", ok, w.tail(120)))

    # 6. cleanup
    ok = w.type_and_wait("delete histtest.txt\n", "Deleted: histtest.txt")
    checks.append(("delete histtest", ok, w.tail(120)))

    print()
    fails = 0
    for name, ok, out in checks:
        status = "PASS" if ok else "FAIL"
        if not ok:
            fails += 1
            out = out.replace("\r", "\\r").replace("\x08", "<BS>")
        print(f"{status}: {name}")
        if not ok:
            print(f"  tail: {out[:300]!r}")
    print()
    print(f"RESULT: {len(checks) - fails}/{len(checks)} checks passed")
    sys.exit(0 if fails == 0 else 1)
finally:
    proc.terminate()
    time.sleep(1)
    try:
        proc.kill()
    except Exception:
        pass
