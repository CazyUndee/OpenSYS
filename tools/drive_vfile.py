#!/usr/bin/env python3
"""Drive Plan 0 shell via QEMU TCP monitor to test virtual filesystem."""
import subprocess, time, socket, sys, os

QEMU = r"D:\tools\qemu\qemu-system-x86_64.exe"
KERNEL = r"C:\Users\roone.DESKTOP-QK3UG2M\Downloads\projects\os\bin\kernel0.bin"
LOG = r"C:\Users\roone.DESKTOP-QK3UG2M\Downloads\projects\os\serial_vfile.log"
PORT = 4445

# Clean
if os.path.exists(LOG):
    os.remove(LOG)

# Start QEMU with -display none (not -nographic) so monitor TCP works
proc = subprocess.Popen([
    QEMU, "-kernel", KERNEL,
    "-accel", "whpx", "-m", "128",
    "-display", "none",
    "-no-reboot", "-no-shutdown",
    "-serial", "file:" + LOG,
    "-monitor", f"tcp:127.0.0.1:{PORT},server,nowait"
], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

print(f"QEMU started (PID {proc.pid}), waiting for boot...")
time.sleep(12)

def monitor_send(cmd):
    """Send a command to QEMU monitor via TCP."""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(3)
        s.connect(("127.0.0.1", PORT))
        # Read prompt
        time.sleep(0.3)
        try:
            s.recv(4096)
        except:
            pass
        s.sendall((cmd + "\r\n").encode())
        time.sleep(0.3)
        try:
            s.recv(4096)
        except:
            pass
        s.close()
    except Exception as e:
        print(f"  Monitor error: {e}")

def send_key(key):
    monitor_send(f"sendkey {key}")
    time.sleep(0.08)

def type_text(text):
    for ch in text:
        if ch == ' ':
            send_key("spc")
        elif ch == '/':
            send_key("slash")
        elif ch == '.':
            send_key("dot")
        elif ch == ':':
            send_key("shift-semicolon")
        elif ch == '-':
            send_key("minus")
        elif ch.isupper():
            send_key("shift-" + ch.lower())
        else:
            send_key(ch)
    send_key("ret")
    time.sleep(1.5)

# Check boot
if not os.path.exists(LOG):
    print("ERROR: No serial log - QEMU may have crashed")
    proc.kill()
    sys.exit(1)

with open(LOG, "r", errors="replace") as f:
    boot = f.read()
if "Starting Shell" not in boot:
    print("ERROR: Shell did not start")
    print(boot[-500:])
    proc.kill()
    sys.exit(1)

print("Shell is up! Running commands...\n")

commands = [
    ("ls /proc", "Virtual /proc directory listing"),
    ("read 0/system/runtime/uptime", "Virtual file: uptime"),
    ("read 0/hardware/memory/ram", "Virtual file: memory"),
    ("read 0/system/version", "Virtual file: version"),
    ("read 0/system/hostname", "Virtual file: hostname"),
    ("ls /sys", "Virtual /sys directory listing"),
    ("read 0/system/kernel/name", "Virtual file: kernel name"),
    ("read 0/system/kernel/arch", "Virtual file: kernel arch"),
    ("ls /dev", "Virtual /dev directory listing"),
    ("read 0/system/processes", "Virtual file: processes"),
    ("read 0/hardware/memory/info", "Virtual file: meminfo"),
    ("echo hello from shell", "Echo test (should work)"),
]

for cmd, desc in commands:
    print(f">>> {cmd}  ({desc})")
    type_text(cmd)

time.sleep(2)

# Kill QEMU
try:
    proc.kill()
except:
    pass
proc.wait(timeout=5)

# Print serial log
print("\n" + "=" * 60)
print("SERIAL OUTPUT (last 120 lines)")
print("=" * 60)
if os.path.exists(LOG):
    with open(LOG, "r", errors="replace") as f:
        lines = f.readlines()
    for line in lines[-120:]:
        print(line, end="")
