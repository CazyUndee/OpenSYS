#!/usr/bin/env python3
"""Drive Plan 0 shell via QEMU monitor (sendkey) to verify virtual files are
reachable through the VFS fd layer (the unified mount), plus fs-level
operations (truncate, rename, phantom-entry, rmdir) end-to-end.

Robustness: keystrokes occasionally drop under WHPX, garbling commands.
Every command is therefore echo-verified: we wait for the exact typed text
to be echoed back by the shell (proving the keys arrived), and retype once
if the echo is garbled or absent.
"""
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

    def wait_for_echo(self, text, timeout=12):
        """Wait for the exact command text to appear in NEW output — the
        shell echoes typed characters, so this proves the keys arrived.
        Advances pos only to the END of the echoed text, so the command's
        output (which follows the echo) is still searchable."""
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
        """Type a command line, verify the echo (retyping once if the keys
        were garbled or dropped), then wait for the expected marker."""
        stripped = text.rstrip("\n")
        for attempt in range(2):
            type_text(text)
            if self.wait_for_echo(stripped):
                return self.wait_for(marker, timeout=timeout)
        # Echo never verified even after a retype — the channel may be stuck.
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

    # 1. vcat /proc/uptime through the fd layer
    ok = w.type_and_wait("vcat /proc/uptime\n", "bytes via fd")
    out = w.tail()
    ok = ok and "Uptime:" in out
    checks.append(("vcat /proc/uptime (fd layer)", ok, out))

    # 2. vcat /sys/kernel/name
    ok = w.type_and_wait("vcat /sys/kernel/name\n", "bytes via fd")
    out = w.tail()
    ok = ok and "Plan 0" in out
    checks.append(("vcat /sys/kernel/name (fd layer)", ok, out))

    # 3. vcat /proc/processes
    ok = w.type_and_wait("vcat /proc/processes\n", "bytes via fd")
    out = w.tail()
    ok = ok and "PID" in out
    checks.append(("vcat /proc/processes (fd layer)", ok, out))

    # 4. vcat /dev/null — empty but still opens through fd layer
    ok = w.type_and_wait("vcat /dev/null\n", "0 bytes via fd")
    checks.append(("vcat /dev/null empty via fd", ok, w.tail(300)))

    # 5. vcat of a non-existent virtual file fails at open
    ok = w.type_and_wait("vcat /proc/nope\n", "could not open")
    checks.append(("vcat /proc/nope fails at open", ok, w.tail(300)))

    # 6. ordinary ramfs file still opens through the fd layer
    ok = w.type_and_wait("vcat /sys/kernel/arch\n", "bytes via fd")
    out = w.tail()
    ok = ok and "x86_64" in out
    checks.append(("vcat /sys/kernel/arch (fd layer)", ok, out))

    # 7. a REAL (fs.c) file is reachable through the fd layer: write it
    #    via the shell's write command, then vcat it back via fds.
    ok = w.type_and_wait("write vcattest.txt hello-from-fs\n", "bytes to vcattest.txt")
    checks.append(("write real file via shell", ok, w.tail(200)))

    ok = w.type_and_wait("vcat /vcattest.txt\n", "bytes via fd")
    out = w.tail()
    ok = ok and "hello-from-fs" in out
    checks.append(("vcat real fs file through fd layer", ok, out))

    # 8. copy-overwrite truncates: write a longer file, then copy a
    #    shorter one over it — no stale tail bytes may remain.
    ok = w.type_and_wait("write longfile.txt this-is-a-long-content-that-is-long\n", "bytes to longfile.txt")
    checks.append(("write long source", ok, w.tail(120)))

    ok = w.type_and_wait("write shortfile.txt short\n", "bytes to shortfile.txt")
    checks.append(("write short source", ok, w.tail(120)))

    ok = w.type_and_wait("copy shortfile.txt longfile.txt\n", "Copied:")
    checks.append(("copy shorter over longer", ok, w.tail(120)))

    ok = w.type_and_wait("read longfile.txt\n", "> ")
    out = w.tail(600)
    marker = out.rfind("read longfile.txt")
    region = out[marker:] if marker >= 0 else ""
    ok = ok and region.count("short") >= 1 and "long-content-that-is-long" not in region
    checks.append(("overwritten file has no stale tail", ok, region))

    # 9. rename preserves content and the read-only flag.
    ok = w.type_and_wait("write renfile.txt rename-content\n", "bytes to renfile.txt")
    checks.append(("write rename source", ok, w.tail(120)))

    ok = w.type_and_wait("chmod -w renfile.txt\n", "read-only")
    checks.append(("protect rename source", ok, w.tail(120)))

    ok = w.type_and_wait("move renfile.txt renamed.txt\n", "Moved:")
    out = w.tail(200)
    ok = ok and "renamed.txt" in out
    checks.append(("move uses fs_rename", ok, out))

    ok = w.type_and_wait("read renamed.txt\n", "rename-content")
    checks.append(("renamed file content intact", ok, w.tail(200)))

    ok = w.type_and_wait("chmod renamed.txt\n", "read-only")
    out = w.tail(200)
    ok = ok and "renamed.txt: read-only" in out
    checks.append(("read-only flag survives rename", ok, out))

    ok = w.type_and_wait("delete renamed.txt\n", "read-only")
    checks.append(("read-only renamed file cannot be deleted", ok, w.tail(120)))
    ok = w.type_and_wait("chmod +w renamed.txt\n", "writable")
    checks.append(("unprotect renamed file", ok, w.tail(120)))
    ok = w.type_and_wait("delete renamed.txt\n", "Deleted: renamed.txt")
    checks.append(("delete renamed file", ok, w.tail(120)))

    # 10. phantom-entry regression: create a file, delete it, create a
    #    new file (reusing the freed MFT slot) — the listing must show
    #    only the new name, never the deleted one.
    ok = w.type_and_wait("write ghost1.txt first-ghost\n", "bytes to ghost1.txt")
    checks.append(("write ghost1", ok, w.tail(100)))
    ok = w.type_and_wait("delete ghost1.txt\n", "Deleted: ghost1.txt")
    checks.append(("delete ghost1", ok, w.tail(100)))
    ok = w.type_and_wait("write ghost2.txt second-ghost\n", "bytes to ghost2.txt")
    checks.append(("write ghost2 (slot reuse)", ok, w.tail(100)))
    ok = w.type_and_wait("ls\n", "ghost2.txt")
    out = w.tail(600)
    # Only the region after the "ls" echo matters — earlier typed commands
    # echo "ghost1.txt" into the log too.
    marker = out.rfind("> ls")
    region = out[marker:] if marker >= 0 else out
    ok = ok and "ghost2.txt" in region and "ghost1.txt" not in region
    checks.append(("no phantom ghost1 entry after slot reuse", ok, region))
    ok = w.type_and_wait("delete ghost2.txt\n", "Deleted: ghost2.txt")
    checks.append(("delete ghost2", ok, w.tail(100)))

    # 11. rmdir flow: mkdir, add a file inside, delete the dir (refused,
    #    not empty), delete the child, then delete the dir (works).
    ok = w.type_and_wait("mkdir rmdirtest\n", "Created directory: rmdirtest")
    checks.append(("mkdir rmdirtest", ok, w.tail(100)))
    ok = w.type_and_wait("write rmdirtest/child.txt inside-child\n", "bytes to rmdirtest/child.txt")
    checks.append(("write file inside dir", ok, w.tail(120)))
    ok = w.type_and_wait("delete rmdirtest\n", "not empty")
    checks.append(("delete non-empty dir refused", ok, w.tail(120)))
    ok = w.type_and_wait("delete rmdirtest/child.txt\n", "Deleted: rmdirtest/child.txt")
    checks.append(("delete child file", ok, w.tail(120)))
    ok = w.type_and_wait("delete rmdirtest\n", "Deleted directory: rmdirtest")
    checks.append(("delete empty dir works", ok, w.tail(120)))

    # 12. info command: reports type (file vs dir), size, and access mode.
    ok = w.type_and_wait("mkdir infodir\n", "Created directory: infodir")
    checks.append(("mkdir infodir", ok, w.tail(120)))
    ok = w.type_and_wait("info vcattest.txt\n", "read-write")
    out = w.tail(300)
    ok = ok and "Type:   File" in out and "Size:" in out
    checks.append(("info file shows type/size/access", ok, out))
    ok = w.type_and_wait("info infodir\n", "Directory")
    checks.append(("info dir shows Directory type", ok, w.tail(300)))
    ok = w.type_and_wait("chmod -w vcattest.txt\n", "read-only")
    checks.append(("protect vcattest", ok, w.tail(120)))
    ok = w.type_and_wait("info vcattest.txt\n", "read-only")
    out = w.tail(300)
    ok = ok and "Access: read-only" in out
    checks.append(("info shows read-only access", ok, out))
    ok = w.type_and_wait("ls\n", "vcattest.txt")
    out = w.tail(300)
    ok = ok and "[RO]" in out
    checks.append(("ls marks read-only file [RO]", ok, out))
    ok = w.type_and_wait("chmod +w vcattest.txt\n", "writable")
    checks.append(("unprotect vcattest", ok, w.tail(120)))
    ok = w.type_and_wait("ls\n", "vcattest.txt")
    out = w.tail(300)
    marker = out.rfind("> ls")
    region = out[marker:] if marker >= 0 else out
    ok = ok and "[RO]" not in region
    checks.append(("ls drops [RO] after unprotect", ok, region))
    ok = w.type_and_wait("delete infodir\n", "Deleted directory: infodir")
    checks.append(("delete infodir", ok, w.tail(120)))

    # 13. recursive find: create nested files and search from the root —
    #     matches in subdirectories must be found with full paths.
    ok = w.type_and_wait("mkdir findtest\n", "Created directory: findtest")
    checks.append(("mkdir findtest", ok, w.tail(120)))
    ok = w.type_and_wait("write findtest/deep-note.txt hidden-note\n", "bytes to findtest/deep-note.txt")
    checks.append(("write nested file", ok, w.tail(120)))
    ok = w.type_and_wait("find note\n", "matching files")
    out = w.tail(500)
    # Only the find output region matters — the earlier `write` echo also
    # contains the full path and would fool a whole-log substring check.
    marker = out.rfind("Searching for")
    region = out[marker:] if marker >= 0 else out
    ok = ok and "findtest/deep-note.txt" in region
    checks.append(("recursive find finds nested match", ok, region))
    ok = w.type_and_wait("find ghost\n", "matching files")
    out = w.tail(500)
    # No files match "ghost" (ghost1/ghost2 were deleted earlier), and
    # crucially the search must not descend into stale entries.
    marker = out.rfind("Searching for")
    region = out[marker:] if marker >= 0 else out
    ok = ok and "Found 0 matching files" in region and "ghost1" not in region
    checks.append(("recursive find reports zero matches", ok, region))
    ok = w.type_and_wait("delete findtest/deep-note.txt\n", "Deleted: findtest/deep-note.txt")
    checks.append(("delete nested file", ok, w.tail(120)))
    ok = w.type_and_wait("delete findtest\n", "Deleted directory: findtest")
    checks.append(("delete findtest", ok, w.tail(120)))

    # 14. recursive directory copy: build a small tree, copy it, verify
    #     the mirror has the same files and contents, then check the
    #     self-copy refusal.
    ok = w.type_and_wait("mkdir srcdir\n", "Created directory: srcdir")
    checks.append(("mkdir srcdir", ok, w.tail(120)))
    ok = w.type_and_wait("write srcdir/a.txt alpha\n", "bytes to srcdir/a.txt")
    checks.append(("write srcdir/a.txt", ok, w.tail(120)))
    ok = w.type_and_wait("mkdir srcdir/sub\n", "Created directory: srcdir/sub")
    checks.append(("mkdir srcdir/sub", ok, w.tail(120)))
    ok = w.type_and_wait("write srcdir/sub/b.txt beta\n", "bytes to srcdir/sub/b.txt")
    checks.append(("write srcdir/sub/b.txt", ok, w.tail(120)))
    ok = w.type_and_wait("copy srcdir dstdir\n", "Copied directory: /dstdir")
    out = w.tail(200)
    ok = ok and "(2 files)" in out
    checks.append(("copy srcdir -> dstdir", ok, out))
    ok = w.type_and_wait("vcat dstdir/a.txt\n", "alpha")
    checks.append(("vcat dstdir/a.txt == alpha", ok, w.tail(120)))
    ok = w.type_and_wait("vcat dstdir/sub/b.txt\n", "beta")
    checks.append(("vcat dstdir/sub/b.txt == beta", ok, w.tail(120)))
    ok = w.type_and_wait("copy srcdir srcdir\n", "into itself")
    checks.append(("copy srcdir srcdir refused", ok, w.tail(120)))

    # 15. grep: build a multi-line file with the interactive edit command,
    #     then search it (matching lines, no-match, and a virtual file).
    ok = w.type_and_wait("edit grepfile.txt\n", "type a single")
    checks.append(("edit opens grepfile.txt", ok, w.tail(160)))
    for line in ("alpha beta", "gamma delta", "beta omega"):
        type_text(line + "\n")
        ok = w.wait_for_echo(line, timeout=15)
        checks.append((f"edit line {line!r}", ok, w.tail(160)))
    type_text(".\n")
    ok = w.wait_for("Done.", timeout=15)
    checks.append(("edit finishes", ok, w.tail(160)))

    ok = w.type_and_wait("grep beta grepfile.txt\n", "3: beta omega")
    out = w.tail(300)
    ok = ok and "1: alpha beta" in out
    checks.append(("grep beta finds lines 1+3", ok, out))
    ok = w.type_and_wait("grep zzz grepfile.txt\n", "no matches")
    checks.append(("grep zzz no matches", ok, w.tail(120)))
    ok = w.type_and_wait("grep Uptime /proc/uptime\n", "1: Uptime")
    checks.append(("grep Uptime /proc/uptime", ok, w.tail(200)))

    # 16. cat streams files larger than 255 bytes: build one with edit
    #     (20 lines x 24 chars = ~480 bytes) and confirm the LAST line
    #     is visible in cat output (the old cap truncated at 255).
    ok = w.type_and_wait("edit bigfile.txt\n", "type a single")
    checks.append(("edit opens bigfile.txt", ok, w.tail(160)))
    for i in range(20):
        line = f"line-{i:02d}-xxxxxxxxxxxxxxxxx"
        type_text(line + "\n")
        ok = w.wait_for_echo(line, timeout=15)
        if not ok:
            break
    checks.append((f"edit typed {i + 1} lines", ok, w.tail(160)))
    type_text(".\n")
    ok = w.wait_for("Done.", timeout=15)
    checks.append(("edit bigfile finishes", ok, w.tail(160)))
    ok = w.type_and_wait("cat bigfile.txt\n", "line-19-xxxxxxxxxxxxxxxxx")
    out = w.tail(800)
    ok = ok and "line-00-xxxxxxxxxxxxxxxxx" in out
    checks.append(("cat shows first+last line (streamed)", ok, out))

    # 17. cleanup
    ok = w.type_and_wait("delete bigfile.txt\n", "Deleted: bigfile.txt")
    checks.append(("delete bigfile", ok, w.tail(120)))
    ok = w.type_and_wait("delete grepfile.txt\n", "Deleted: grepfile.txt")
    checks.append(("delete grepfile", ok, w.tail(120)))
    ok = w.type_and_wait("delete vcattest.txt\n", "Deleted: vcattest.txt")
    checks.append(("delete vcattest", ok, w.tail(120)))
    ok = w.type_and_wait("delete longfile.txt\n", "Deleted: longfile.txt")
    checks.append(("delete longfile", ok, w.tail(120)))
    ok = w.type_and_wait("delete shortfile.txt\n", "Deleted: shortfile.txt")
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
