#!/usr/bin/env python3
"""Drive Plan 0 shell via QEMU TCP monitor to verify shell-as-process."""
import subprocess, time, socket, sys, os

QEMU = r"D:\tools\qemu\qemu-system-x86_64.exe"
KERNEL = r"C:\Users\roone.DESKTOP-QK3UG2M\Downloads\projects\os\bin\kernel0.bin"
LOG = r"C:\Users\roone.DESKTOP-QK3UG2M\Downloads\projects\os\serial.log"
PORT = 4444
TIMEOUT = 30

def cleanup():
    subprocess.run(["powershell.exe", "-Command",
        "Stop-Process -Name qemu-system-x86_64 -Force -ErrorAction SilentlyContinue"],
        capture_output=True, timeout=5)
    time.sleep(1)

def start_qemu():
    args = [QEMU, "-kernel", KERNEL, "-display", "none", "-m", "128",
            "-serial", "file:" + LOG, "-no-reboot", "-no-shutdown",
            "-accel", "whpx", "-monitor", f"tcp:127.0.0.1:{PORT},server,nowait",
            "-d", "guest_errors"]
    proc = subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return proc

def wait_for_monitor(port, timeout=10):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(2)
            s.connect(("127.0.0.1", port))
            s.recv(4096)
            return s
        except (ConnectionRefusedError, socket.timeout, OSError):
            time.sleep(0.3)
    return None

def send_cmd(sock, cmd):
    """Send a command via QEMU monitor."""
    sock.sendall((cmd + "\r\n").encode())
    time.sleep(0.05)

def send_keys(sock, text):
    """Type text as keypresses, one char at a time, then press Return."""
    for ch in text:
        if ch == ' ':
            send_cmd(sock, "sendkey spc")
        elif ch == '/':
            send_cmd(sock, "sendkey slash")
        elif ch == '\n':
            send_cmd(sock, "sendkey ret")
        elif ch == '.':
            send_cmd(sock, "sendkey dot")
        else:
            send_cmd(sock, f"sendkey {ch}")
        time.sleep(0.03)
    # Press return to execute
    time.sleep(0.1)
    send_cmd(sock, "sendkey ret")
    time.sleep(0.3)

def read_log():
    try:
        with open(LOG, "r", errors="replace") as f:
            return f.read()
    except:
        return ""

def check(text, log, label):
    if text.lower() in log.lower():
        print(f"  PASS  {label}")
        return True
    else:
        print(f"  FAIL  {label} — '{text}' not found in log")
        return False

# Main
cleanup()
if os.path.exists(LOG):
    os.remove(LOG)

print("Starting QEMU with WHPX...")
proc = start_qemu()

mon = wait_for_monitor(PORT, timeout=15)
if not mon:
    print("FAIL: Could not connect to QEMU monitor")
    cleanup()
    sys.exit(1)
print("Connected to QEMU monitor")

# Wait for boot
print("Waiting for boot...")
time.sleep(15)

log = read_log()
print("\n=== Boot log (last 20 lines) ===")
for line in log.strip().split("\n")[-20:]:
    print(f"  {line}")

# Check for shell process creation
print("\n=== Shell-as-Process Checks ===")

results = []
results.append(check("Creating shell process", log, "Shell process creation message"))
results.append(check("Shell process created", log, "Shell process created"))
results.append(check("Enabling preemptive scheduler", log, "Preemptive scheduler enabled"))
results.append(check("System ready", log, "System ready"))
results.append(check("Starting Shell", log, "Shell start"))

# Type commands
print("\n=== Typing commands ===")
time.sleep(2)
send_keys(mon, "ps")
time.sleep(1)
send_keys(mon, "echo hello from process shell")
time.sleep(1)
send_keys(mon, "read /proc/uptime")
time.sleep(1)
send_keys(mon, "ls /proc")
time.sleep(1)

log = read_log()
print("\n=== Full log (last 40 lines) ===")
for line in log.strip().split("\n")[-40:]:
    print(f"  {line}")

print("\n=== Command Response Checks ===")
results.append(check("hello from process shell", log, "echo works in shell process"))
results.append(check("Uptime:", log, "read /proc/uptime works"))
results.append(check("uptime", log, "ls /proc shows uptime"))
results.append(check("hostname", log, "ls /proc shows hostname"))

# Check ps output for shell process
results.append(check("shell", log, "ps shows shell process"))

pass_count = sum(results)
total = len(results)
print(f"\n{'='*50}")
print(f"Results: {pass_count}/{total} passed")

# Cleanup
print("\nShutting down QEMU...")
try:
    mon.sendall(b"quit\r\n")
except:
    pass
time.sleep(1)
cleanup()

sys.exit(0 if pass_count == total else 1)
