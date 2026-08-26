#!/usr/bin/env python3
"""Drive Plan 0 namespace introspection (`ns` command) over serial.
Boots WITH the synthetic GPT image attached (tools/make_gpt_image.py)
so storage resources exist; the QEMU disk reports as HDD-class, so the
ssd class must truthfully answer 'Unknown resource'."""
import subprocess, time, socket, sys, os

def _qemu_path():
    cands = [
        os.path.join(os.environ.get("USERPROFILE", ""), "qemu", "qemu-system-x86_64.exe"),
        r"C:\Users\roone.DESKTOP-QK3UG2M\qemu\qemu-system-x86_64.exe",
    ]
    for c in cands:
        if c and os.path.exists(c):
            return c
    return cands[0]

QEMU = _qemu_path()
HERE = os.path.dirname(os.path.abspath(__file__))
KERNEL = os.path.join(HERE, "..", "bin", "kernel0.bin")
IMAGE = os.path.join(HERE, "gpt_test.img")
LOG = os.path.join(HERE, "ns_serial.log")
PORT = 4453

# Regenerate the disposable GPT image before each run.
import make_gpt_image  # noqa: E402  (runs the generator on import)

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
    "-drive", f"file={IMAGE},if=ide,format=raw,index=0",
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

class LogWatcher:
    def __init__(self):
        self.pos = len(read_log())

    def wait_for(self, needle, timeout=30):
        deadline = time.time() + timeout
        while time.time() < deadline:
            buf = read_log()
            if needle in buf[self.pos:]:
                self.pos = len(buf)
                return True
            time.sleep(0.4)
        return False

    def wait_for_echo(self, text, timeout=12):
        deadline = time.time() + timeout
        while time.time() < deadline:
            buf = read_log()
            idx = buf.find(text, self.pos)
            if idx >= 0:
                self.pos = idx + len(text)
                return True
            time.sleep(0.3)
        return False

    def type_and_wait(self, text, marker, timeout=20):
        stripped = text.rstrip("\n")
        for attempt in range(2):
            type_text(text)
            if self.wait_for_echo(stripped):
                deadline = time.time() + timeout
                while time.time() < deadline:
                    buf = read_log()
                    if marker in buf[self.pos:]:
                        self.pos = len(buf)
                        return True
                    time.sleep(0.4)
                self.pos = len(read_log())
                return False
        return False

    def tail(self, n=500):
        return read_log()[-n:]

try:
    time.sleep(14)
    w = LogWatcher()
    if "> " not in read_log():
        if not w.wait_for("> ", timeout=150):
            print("FAIL: shell prompt never appeared")
            sys.exit(1)
    print("OK: shell prompt reached")
    checks = []

    ok = w.type_and_wait("ns 0/hsh\n", "path: 0/hardware/storage/hdd")
    out = w.tail(400)
    ok = ok and "storage device" in out and "alias: 0/hsh" in out
    checks.append(("ns 0/hsh describes canonical+kind+alias", ok, out))

    # Topology honesty: the QEMU disk is HDD-class; ssd must not exist.
    ok = w.type_and_wait("ns 0/hss\n", "Unknown resource")
    checks.append(("absent device class (ssd) is unknown", ok, w.tail(300)))

    ok = w.type_and_wait("ns 0/hardware/storage/hdd\n", "alias: 0/hsh")
    checks.append(("canonical path reveals its alias", ok, w.tail(400)))

    ok = w.type_and_wait("ns 0/hardware/memory/ram\n", "system memory")
    time.sleep(1.5)   # let the multi-line description finish printing
    out = w.tail(400)
    ok = ok and "total mb:" in out
    checks.append(("memory ram reports totals", ok, out))

    ok = w.type_and_wait("ns 0/nothing/here\n", "Unknown resource")
    checks.append(("unknown resource errors cleanly", ok, w.tail(300)))

    ok = w.type_and_wait("ns /proc/uptime\n", "invalid path syntax")
    checks.append(("legacy unix-style path rejected as syntax error", ok, w.tail(300)))

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
