// OpenCode OS Boot Stub
// Multiboot 1 compliant - works with GRUB Legacy/GRUB2

#include <stdint.h>

// ============================================================================
// Multiboot 1 Header
// ============================================================================

// Multiboot header - referenced by GRUB bootloader, not by code
// The __attribute__((used)) prevents "unused" warnings since GRUB reads this directly
__attribute__((section(".multiboot")))
__attribute__((aligned(4)))
__attribute__((used))
static const uint32_t multiboot_header[3] = {
    0x1BADB002,      // Magic
    0x00000000,      // Flags
    (uint32_t)(-(0x1BADB002 + 0x00000000))  // Checksum
};

// ============================================================================
// Stack in BSS
// ============================================================================

static uint8_t stack[16384] __attribute__((aligned(16)));

// ============================================================================
// Kernel entry
// ============================================================================

extern void kernel_main(uint32_t magic, uint32_t mbi_addr);

// ============================================================================
// Entry point called by GRUB
// ============================================================================

__attribute__((noreturn))
void _start(void) {
    uint32_t magic, mbi;

    // Capture GRUB-passed values from registers
    __asm__ volatile (
        "movl %%eax, %0\n\t"
        "movl %%ebx, %1"
        : "=r" (magic), "=r" (mbi)
        :
        : "memory"
    );

    // Set up stack
    __asm__ volatile (
        "movl %0, %%esp\n\t"
        "xorl %%ebp, %%ebp"
        :
        : "r" (stack + sizeof(stack))
        : "memory"
    );

    // Enter kernel
    kernel_main(magic, mbi);

    // Halt forever if kernel returns
    while (1) {
        __asm__ volatile ("cli; hlt");
    }
}
