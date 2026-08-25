#!/usr/bin/env python3
"""Drive Plan 0 with a synthetic GPT disk attached to validate partitioned
disk access end-to-end (tools/make_gpt_image.py builds the image).

Checks:
  1. Boot log shows the GPT was parsed: "[PART] Partition Management Ready!"
     and "Found 2 partitions".
  2. `parts` lists both used entries: "plan0-test" @ LBA 2048 (1024 KB) and
     "data-two" @ LBA 4096.
  3. Shell regression: `wc 0/system/runtime/uptime` still works.

fs_mount() reads LBA 0, sees the protective MBR (not FS_MAGIC), and falls
back to its in-memory format path - expected; the image is disposable and
regenerated before every run.
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
HERE = os.path.dirname(os.path.abspath(__file__))
KERNEL = os.path.join(HERE, "..", "bin", "kernel0.bin")
IMAGE = os.path.join(HERE, "gpt_test.img")
LOG = os.path.join(HERE, "parts_serial.log")
PORT = 4451
P1_LBA = 2048  # first sector of partition 1 (where the fs volume starts)

# Regenerate the disposable GPT image so fs_format's LBA0 write never
# corrupts a previous run's protective MBR.
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
], stdout=subprocess.DEVNULL,
   stderr=open(os.path.join(HERE, "parts_qemu_err.log"), "w"))

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

    # Boot-time checks: GPT parsed from the attached disk.
    boot = read_log()
    ok_ready = "[PART] Partition Management Ready!" in boot
    ok_found = "Found 2 partitions" in boot
    ok_volume = "FS volume: storage partitions/1" in boot
    checks = [
        ("boot: [PART] ready marker", ok_ready, boot[-400:]),
        ("boot: Found 2 partitions", ok_found, boot[-400:]),
        ("boot: fs bound to partitions/1", ok_volume, boot[-500:]),
    ]

    w = LogWatcher()
    if "> " not in read_log():
        if not w.wait_for("> ", timeout=60):
            print("FAIL: shell prompt never appeared")
            sys.exit(1)
    print("OK: shell prompt reached")

    # parts command lists both partitions with start/size/label.
    # cmd_parts prints listing AND probe-read results synchronously, so
    # one tail covers both (marker-position tracking would skip the
    # probe lines that land between poll ticks).
    for attempt in range(2):
        type_text("parts\n")
        if w.wait_for_echo("parts"):
            break
    time.sleep(3)  # let the synchronous probe reads finish printing
    out = w.tail(1200)
    ok = ("plan0-test" in out and "2048" in out and "1024" in out
          and "data-two" in out and "4096" in out)
    checks.append(("parts lists both partitions", ok, out))

    # The probe reads exercise part_read_sectors (partition-relative LBA
    # translation) against the real ATA disk.
    probe_ok = ("read p0 sector 0: ok" in out
                and "read p1 sector 0: ok" in out)
    checks.append(("partition-relative probe reads", probe_ok, w.tail(300)))

    # ---- Namespace shell syntax: file ops through 0/... paths ----
    # The bound volume is partitions/1; the QEMU disk is HDD-class, so
    # the volume is addressable as 0/hsh/partitions/1 and (logically)
    # as 0/user/.
    ok = w.type_and_wait("write hello ns to 0/hsh/partitions/1/nsfile.txt\n",
                         "bytes to", timeout=25)
    checks.append(("write via namespace path", ok, w.tail(300)))

    ok = w.type_and_wait("cat 0/user/nsfile.txt\n", "hello ns", timeout=20)
    checks.append(("read via 0/user alias", ok, w.tail(300)))

    ok = w.type_and_wait("cat /nsfile.txt\n", "hello ns", timeout=20)
    checks.append(("legacy path reads the same resource", ok, w.tail(300)))

    # Regression: shell file tooling still works on the GPT-disk boot.
    ok = w.type_and_wait("wc 0/system/runtime/uptime\n", "lines,", timeout=20)
    checks.append(("wc 0/system/runtime/uptime regression", ok, w.tail(200)))

    print()
    all_ok = True
    for name, ok, tail in checks:
        status = "PASS" if ok else "FAIL"
        if not ok:
            all_ok = False
        print(f"{status}: {name}")
        if not ok:
            print(f"  tail: {tail[-300:]!r}")

    # ---- Post-run disk-image inspection (host side) ----
    # The fs must have formatted INSIDE partition 1, leaving the
    # protective MBR (LBA 0) and GPT header (LBA 1) untouched.
    proc.terminate()
    try:
        proc.wait(timeout=3)
    except Exception:
        proc.kill()
    time.sleep(1)
    try:
        with open(IMAGE, 'rb') as f:
            img = f.read()
        SECT = 512
        mbr_ok = img[0x1BE + 4] == 0xEE and img[510:512] == b'\x55\xAA'
        gpt_ok = img[1 * SECT:1 * SECT + 8] == b'EFI PART'
        fs_magic = bytes.fromhex('504c414e')      # FS_MAGIC 0x4E414C50 LE -> "PLAN"
        p1_fs_ok = img[P1_LBA * SECT:P1_LBA * SECT + 4] == fs_magic
        p2_untouched = img[4096 * SECT:4096 * SECT + 4] != fs_magic
        img_checks = [
            ("image: protective MBR survived", mbr_ok, ''),
            ("image: GPT header survived", gpt_ok, ''),
            ("image: FS boot sector inside partition 1", p1_fs_ok, ''),
            ("image: partition 2 not formatted", p2_untouched, ''),
        ]
        for name, ok, tail in img_checks:
            status = "PASS" if ok else "FAIL"
            if not ok:
                all_ok = False
            print(f"{status}: {name}")
    except OSError as e:
        print(f"FAIL: image inspection unavailable ({e})")
        all_ok = False

    sys.exit(0 if all_ok else 1)

finally:
    proc.terminate()
    try:
        proc.wait(timeout=3)
    except Exception:
        proc.kill()
