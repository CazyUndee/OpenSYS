#!/usr/bin/env python3
"""Drive Plan 0 shell via QEMU monitor (sendkey) to verify the stdio
command: fd 1 (stdout) redirected into a pipe via dup2, write through
stdout, read back from the pipe."""
import subprocess, time, socket, sys, os

QEMU = os.path.expanduser(r"~\qemu\qemu-system-x86_64.exe")
KERNEL = os.path.join(os.path.dirname(__file__), "..", "bin", "kernel0.bin")
LOG = os.path.join(os.path.dirname(__file__), "stdio_serial.log")
PORT = 4451

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
        elif ch.isupper():
            send_key(f"shift-{ch.lower()}")
        else:
            send_key(ch)
        time.sleep(0.06)

def read_log():
    with open(LOG, 'r', errors='replace') as f:
        return f.read()

class LogWatcher:
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
    if "> " not in read_log():
        if not w.wait_for("> ", timeout=60):
            print("FAIL: shell prompt never appeared")
            sys.exit(1)
    w.buf = read_log()
    w.pos = len(w.buf)
    print("OK: shell prompt reached")

    # 1. stdio redirects stdout into a pipe and reads it back
    type_text("stdio\n")
    ok = w.wait_for("hello via stdout", timeout=30)
    out = w.tail()
    ok = ok and "read back" in out and "hello via stdout" in out
    print(("PASS" if ok else "FAIL") + ": stdio stdout->pipe roundtrip")
    if not ok:
        print(f"  tail: {out[-300:]!r}")

    # 2. /proc/self/fd shows the std descriptors (0/1/2 = device)
    type_text("vcat /proc/self/fdinfo\n")
    ok = w.wait_for("bytes via fd", timeout=30)
    out = w.tail()
    ok = ok and "device" in out
    print(("PASS" if ok else "FAIL") + ": fdinfo lists std device fds")
    if not ok:
        print(f"  tail: {out[-300:]!r}")

    sys.exit(0 if ok else 1)

finally:
    proc.terminate()
    try:
        proc.wait(timeout=3)
    except Exception:
        proc.kill()
