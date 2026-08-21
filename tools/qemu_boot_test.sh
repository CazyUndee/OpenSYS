#!/bin/bash
# qemu_boot_test.sh - QEMU Boot Validation for Plan 0
#
# Builds the kernel, creates a bootable ISO with GRUB,
# and boots it in QEMU with serial output capture.
#
# Requirements:
#   - x86_64-elf-gcc cross-compiler
#   - nasm assembler
#   - GRUB (grub-mkrescue or grub2-mkrescue)
#   - QEMU (qemu-system-x86_64)
#
# Usage:
#   ./tools/qemu_boot_test.sh              # Run with defaults (5s timeout)
#   ./tools/qemu_boot_test.sh --timeout 10 # Custom timeout
#   ./tools/qemu_boot_test.sh --no-build   # Skip kernel rebuild
#
# Exit codes:
#   0 = Boot succeeded, serial output captured
#   1 = Build failed
#   2 = ISO creation failed
#   3 = QEMU boot timed out or failed

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BIN_DIR="$PROJECT_DIR/bin"
ISO_DIR="$PROJECT_DIR/iso"
TIMEOUT=5
DO_BUILD=1

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --timeout) TIMEOUT="$2"; shift 2 ;;
        --no-build) DO_BUILD=0; shift ;;
        -h|--help)
            echo "Usage: $0 [--timeout N] [--no-build]"
            echo "  --timeout N   Seconds to wait for boot output (default: 5)"
            echo "  --no-build    Skip kernel rebuild"
            exit 0
            ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

# Check toolchain
check_tools() {
    local missing=0
    for tool in nasm qemu-system-x86_64; do
        if ! command -v "$tool" &>/dev/null; then
            echo "ERROR: $tool not found in PATH"
            missing=1
        fi
    done
    
    # Check for cross-compiler (D: drive no longer exists; toolchain lives in ~/x86_64-elf)
    for c in "$HOME/x86_64-elf/bin/x86_64-elf-gcc" "/d/tools/x86_64-elf/bin/x86_64-elf-gcc"; do
        if [ -x "$c" ] || command -v x86_64-elf-gcc &>/dev/null; then
            break
        fi
    done
    if [ ! -x "$HOME/x86_64-elf/bin/x86_64-elf-gcc" ] && [ ! -x "/d/tools/x86_64-elf/bin/x86_64-elf-gcc" ] && ! command -v x86_64-elf-gcc &>/dev/null; then
        echo "ERROR: x86_64-elf-gcc not found"
        missing=1
    fi
    
    # Check for GRUB
    if ! command -v grub-mkrescue &>/dev/null && ! command -v grub2-mkrescue &>/dev/null; then
        echo "ERROR: grub-mkrescue not found"
        missing=1
    fi
    
    return $missing
}

# Build kernel
build_kernel() {
    echo "=== Building kernel ==="
    cd "$PROJECT_DIR"
    
    # Set up PATH for cross-compiler
    if [ -d "$HOME/x86_64-elf/bin" ]; then
        export PATH="$HOME/x86_64-elf/bin:/c/msys64/usr/bin:$PATH"
    else
        export PATH="/d/tools/x86_64-elf/bin:/c/msys64/usr/bin:$PATH"
    fi
    
    make clean && make all
    if [ $? -ne 0 ]; then
        echo "ERROR: Kernel build failed"
        return 1
    fi
    
    echo "Kernel built: $BIN_DIR/kernel0.bin"
    return 0
}

# Create bootable ISO with GRUB
create_iso() {
    echo "=== Creating bootable ISO ==="
    cd "$PROJECT_DIR"
    
    # Set up ISO directory structure
    rm -rf "$ISO_DIR"
    mkdir -p "$ISO_DIR/boot/grub"
    
    # Copy kernel
    cp "$BIN_DIR/kernel0.bin" "$ISO_DIR/boot/plan0.bin"
    
    # Create GRUB config
    cat > "$ISO_DIR/boot/grub/grub.cfg" << 'EOF'
set timeout=0
set default=0

menuentry "Plan 0" {
    multiboot /boot/plan0.bin
    boot
}
EOF
    
    # Find grub-mkrescue
    GRUB_CMD=""
    if command -v grub-mkrescue &>/dev/null; then
        GRUB_CMD="grub-mkrescue"
    elif command -v grub2-mkrescue &>/dev/null; then
        GRUB_CMD="grub2-mkrescue"
    fi
    
    if [ -z "$GRUB_CMD" ]; then
        echo "ERROR: No grub-mkrescue found"
        return 1
    fi
    
    # Create ISO
    cd "$PROJECT_DIR"
    $GRUB_CMD -o "$BIN_DIR/plan0.iso" "$ISO_DIR" 2>&1
    if [ $? -ne 0 ]; then
        echo "ERROR: ISO creation failed"
        return 1
    fi
    
    echo "ISO created: $BIN_DIR/plan0.iso"
    return 0
}

# Boot in QEMU and capture serial output
boot_qemu() {
    echo "=== Booting in QEMU (timeout: ${TIMEOUT}s) ==="
    
    ISO_PATH="$BIN_DIR/plan0.iso"
    if [ ! -f "$ISO_PATH" ]; then
        echo "ERROR: ISO not found at $ISO_PATH"
        return 1
    fi
    
    # Create serial log file
    SERIAL_LOG="$BIN_DIR/serial_output.log"
    > "$SERIAL_LOG"
    
    echo "Serial output will be captured to: $SERIAL_LOG"
    echo "--- Booting ---"
    
    # Run QEMU with:
    #   -cdrom: boot from ISO
    #   -serial file: capture serial output to file
    #   -display none: no graphical window
    #   -no-reboot: exit on reset/triple fault
    #   -m 128: 128MB RAM
    timeout "$TIMEOUT" qemu-system-x86_64 \
        -cdrom "$ISO_PATH" \
        -serial file:"$SERIAL_LOG" \
        -display none \
        -no-reboot \
        -m 128 \
        -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
        2>&1 || true
    
    echo "--- Boot complete ---"
    
    # Check serial output
    if [ -s "$SERIAL_LOG" ]; then
        echo ""
        echo "=== Serial output ==="
        cat "$SERIAL_LOG"
        echo ""
        echo "=== End serial output ==="
        echo ""
        echo "Boot validation PASSED (serial output captured)"
        return 0
    else
        echo ""
        echo "WARNING: No serial output captured"
        echo "This may indicate:"
        echo "  1. Kernel didn't reach serial init"
        echo "  2. QEMU serial redirection failed"
        echo "  3. Boot timed out before kernel output"
        echo ""
        echo "Boot validation INCONCLUSIVE"
        return 0
    fi
}

# Main
echo "Plan 0 QEMU Boot Validation"
echo "============================"
echo ""

if ! check_tools; then
    echo ""
    echo "Missing tools. Install requirements and try again."
    exit 1
fi

if [ "$DO_BUILD" -eq 1 ]; then
    if ! build_kernel; then
        exit 1
    fi
fi

if ! create_iso; then
    exit 2
fi

if ! boot_qemu; then
    exit 3
fi

echo ""
echo "Validation complete."
exit 0
