#!/usr/bin/env python3
"""Drive Plan 0 shell via QEMU monitor (sendkey) to verify virtual files are
reachable through the VFS fd layer (the unified mount)."""
import subprocess, time, socket, sys, os

QEMU = os.path.join(os.path.expanduser("~"), "qemu", "qemu-system-x86_64.exe")
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
    # The prompt may already be in the log (fast boot) — scan from the
    # start for it, then advance the watcher past the current content.
    if "> " not in read_log():
        if not w.wait_for("> ", timeout=60):
            print("FAIL: shell prompt never appeared")
            sys.exit(1)
    w.buf = read_log()
    w.pos = len(w.buf)
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

    # 7. a REAL (fs.c) file is reachable through the fd layer: write it
    #    via the shell's write command, then vcat it back via fds.
    type_text("write vcattest.txt hello-from-fs\n")
    ok = w.wait_for("bytes to vcattest.txt", timeout=30)
    checks.append(("write real file via shell", ok, w.tail(200)))

    type_text("vcat /vcattest.txt\n")
    ok = w.wait_for("bytes via fd", timeout=30)
    out = w.tail()
    ok = ok and "hello-from-fs" in out
    checks.append(("vcat real fs file through fd layer", ok, out))

    # 8. copy-overwrite truncates: write a longer file, then copy a
    #    shorter one over it — no stale tail bytes may remain.
    type_text("write longfile.txt this-is-a-long-content-that-is-long\n")
    ok = w.wait_for("bytes to longfile.txt", timeout=30)
    checks.append(("write long source", ok, w.tail(120)))

    type_text("write shortfile.txt short\n")
    ok = w.wait_for("bytes to shortfile.txt", timeout=30)
    checks.append(("write short source", ok, w.tail(120)))

    type_text("copy shortfile.txt longfile.txt\n")
    ok = w.wait_for("Copied:", timeout=30)
    checks.append(("copy shorter over longer", ok, w.tail(120)))

    type_text("read longfile.txt\n")
    ok = w.wait_for("> ", timeout=30)
    out = w.tail(600)
    # The read output is the text between "read longfile.txt" and the
    # next prompt. The typed command echoes the long filename, so check
    # only the content region after the command line.
    marker = out.rfind("read longfile.txt")
    region = out[marker:] if marker >= 0 else ""
    ok = ok and region.count("short") >= 1 and "long-content-that-is-long" not in region
    checks.append(("overwritten file has no stale tail", ok, region))

    # 9. rename preserves content and the read-only flag: write, protect,
    #    rename, then confirm the renamed file is still read-only.
    type_text("write renfile.txt rename-content\n")
    ok = w.wait_for("bytes to renfile.txt", timeout=30)
    checks.append(("write rename source", ok, w.tail(120)))

    type_text("chmod -w renfile.txt\n")
    ok = w.wait_for("read-only", timeout=30)
    checks.append(("protect rename source", ok, w.tail(120)))

    type_text("move renfile.txt renamed.txt\n")
    ok = w.wait_for("Moved:", timeout=30)
    out = w.tail(200)
    ok = ok and "renamed.txt" in out
    checks.append(("move uses fs_rename", ok, out))

    type_text("read renamed.txt\n")
    ok = w.wait_for("rename-content", timeout=30)
    checks.append(("renamed file content intact", ok, w.tail(200)))

    type_text("chmod renamed.txt\n")
    ok = w.wait_for("read-only", timeout=30)
    out = w.tail(200)
    ok = ok and "renamed.txt: read-only" in out
    checks.append(("read-only flag survives rename", ok, out))

    type_text("delete renamed.txt\n")
    ok = w.wait_for("read-only", timeout=30)
    checks.append(("read-only renamed file cannot be deleted", ok, w.tail(120)))
    type_text("chmod +w renamed.txt\n")
    ok = w.wait_for("writable", timeout=30)
    checks.append(("unprotect renamed file", ok, w.tail(120)))
    type_text("delete renamed.txt\n")
    ok = w.wait_for("Deleted: renamed.txt", timeout=30)
    checks.append(("delete renamed file", ok, w.tail(120)))

    # 10. phantom-entry regression: create a file, delete it, create a
    #    new file (reusing the freed MFT slot) — the listing must show
    #    only the new name, never the deleted one.
    type_text("write ghost1.txt first-ghost\n")
    ok = w.wait_for("bytes to ghost1.txt", timeout=30)
    checks.append(("write ghost1", ok, w.tail(100)))
    type_text("delete ghost1.txt\n")
    ok = w.wait_for("Deleted: ghost1.txt", timeout=30)
    checks.append(("delete ghost1", ok, w.tail(100)))
    type_text("write ghost2.txt second-ghost\n")
    ok = w.wait_for("bytes to ghost2.txt", timeout=30)
    checks.append(("write ghost2 (slot reuse)", ok, w.tail(100)))
    type_text("ls\n")
    ok = w.wait_for("> ", timeout=30)
    out = w.tail(400)
    # Only the listing region after the "ls" command matters — the typed
    # commands echo "ghost1.txt" earlier in the log.
    marker = out.rfind("ls")
    region = out[marker:] if marker >= 0 else ""
    ok = ok and "ghost2.txt" in region and "ghost1.txt" not in region
    checks.append(("no phantom ghost1 entry after slot reuse", ok, region))
    type_text("delete ghost2.txt\n")
    ok = w.wait_for("Deleted: ghost2.txt", timeout=30)
    checks.append(("delete ghost2", ok, w.tail(100)))

    # 11. cleanup
    type_text("delete vcattest.txt\n")
    ok = w.wait_for("Deleted: vcattest.txt", timeout=30)
    checks.append(("delete vcattest", ok, w.tail(120)))
    type_text("delete longfile.txt\n")
    ok = w.wait_for("Deleted: longfile.txt", timeout=30)
    checks.append(("delete longfile", ok, w.tail(120)))
    type_text("delete shortfile.txt\n")
    ok = w.wait_for("Deleted: shortfile.txt", timeout=30)
    checks.append(("delete shortfile", ok, w.tail(120)))

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
