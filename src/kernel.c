/*
 * kernel.c - OpenCode OS main kernel
 *
 * Entry point after boot loader. Initializes all subsystems
 * and provides a simple interactive shell.
 */

#include <stdint.h>
#include <stddef.h>
#include "../include/vga.h"
#include "../include/gdt.h"
#include "../include/idt.h"
#include "../include/interrupts.h"
#include "../include/keyboard.h"

/*
 * Kernel entry point - called from boot_c.c
 * magic:  Multiboot magic number (should be 0x2BADB002)
 * mbi:    Pointer to Multiboot information structure
 */
void kernel_main(uint32_t magic, uint32_t mbi_addr) {
    // Initialize VGA console first
    terminal_initialize();

    // Print banner
    terminal_writestring("\n");
    terminal_writestring("  ____                _                _____ ____   _____ \n");
    terminal_writestring(" / __ \\___  ___ ___  | | ___  _ __ ___| ___/ ___| / ____|\n");
    terminal_writestring("| |  | / _ \\/ __/ _ \\| |/ _ \\| '__/ _ \\___ \\___ \\| |     \n");
    terminal_writestring("| |__| | __/ (_| (_) | | (_) | | | __/___) |__) | |___  \n");
    terminal_writestring(" \\____/ \\___|\\___\\___/|_|\\___/|_|  \\___|____/____/ \\_____|\n");
    terminal_writestring("\n");
    terminal_writestring("OpenCode OS v0.1 - x86 32-bit Protected Mode\n");
    terminal_writestring("Type 'help' for available commands.\n\n");

    // Validate multiboot
    if (magic != 0x2BADB002) {
        terminal_writestring("[FAIL] Invalid multiboot magic: 0x");
        terminal_writestring("System halted.\n");
        __asm__ volatile ("cli; hlt");
        return;
    }
    terminal_writestring("[OK]   Multiboot validated\n");

    // Initialize GDT (segmentation)
    terminal_writestring("[INIT] GDT... ");
    gdt_init();
    terminal_writestring("OK\n");

    // Initialize IDT (interrupts)
    terminal_writestring("[INIT] IDT... ");
    idt_init();
    terminal_writestring("OK\n");

    // Remap PIC and enable interrupts
    terminal_writestring("[INIT] PIC... ");
    pic_remap();
    terminal_writestring("OK\n");

    // Initialize keyboard
    terminal_writestring("[INIT] Keyboard... ");
    keyboard_init();
    terminal_writestring("OK\n");

    // Enable interrupts
    terminal_writestring("[INIT] Enabling interrupts... ");
    __asm__ volatile ("sti");
    terminal_writestring("OK\n\n");

    terminal_writestring("System ready. Press keys to interact.\n\n");
    terminal_writestring("osh> ");

    // Main loop - interactive shell
    char cmd_buffer[256];
    uint32_t cmd_pos = 0;

    while (1) {
        // Check for keyboard input
        char c = keyboard_getchar();
        if (c) {
            if (c == '\n') {
                // Execute command
                terminal_putchar('\n');
                cmd_buffer[cmd_pos] = '\0';

                // Simple command parser
                if (cmd_pos == 0) {
                    // Empty command
                } else if (cmd_buffer[0] == 'h' && cmd_buffer[1] == 'e' &&
                           cmd_buffer[2] == 'l' && cmd_buffer[3] == 'p') {
                    terminal_writestring("Commands:\n");
                    terminal_writestring("  help  - Show this help\n");
                    terminal_writestring("  clear - Clear screen\n");
                    terminal_writestring("  ver   - Show version\n");
                    terminal_writestring("  echo  - Echo text\n");
                    terminal_writestring("  halt  - Halt system\n");
                } else if (cmd_buffer[0] == 'c' && cmd_buffer[1] == 'l' &&
                           cmd_buffer[2] == 'e' && cmd_buffer[3] == 'a' &&
                           cmd_buffer[4] == 'r') {
                    terminal_initialize();
                } else if (cmd_buffer[0] == 'v' && cmd_buffer[1] == 'e' &&
                           cmd_buffer[2] == 'r') {
                    terminal_writestring("OpenCode OS v0.1\n");
                    terminal_writestring("Built: ");
                    terminal_writestring(__DATE__);
                    terminal_writestring(" ");
                    terminal_writestring(__TIME__);
                    terminal_writestring("\n");
                } else if (cmd_buffer[0] == 'h' && cmd_buffer[1] == 'a' &&
                           cmd_buffer[2] == 'l' && cmd_buffer[3] == 't') {
                    terminal_writestring("Halting...\n");
                    __asm__ volatile ("cli; hlt");
                } else if (cmd_buffer[0] == 'e' && cmd_buffer[1] == 'c' &&
                           cmd_buffer[2] == 'h' && cmd_buffer[3] == 'o' &&
                           cmd_buffer[4] == ' ') {
                    terminal_writestring(cmd_buffer + 5);
                    terminal_writestring("\n");
                } else {
                    terminal_writestring("Unknown command: ");
                    terminal_writestring(cmd_buffer);
                    terminal_writestring("\n");
                }

                cmd_pos = 0;
                terminal_writestring("osh> ");
            } else if (c == '\b') {
                // Backspace
                if (cmd_pos > 0) {
                    cmd_pos--;
                    terminal_putchar('\b');
                    terminal_putchar(' ');
                    terminal_putchar('\b');
                }
            } else if (cmd_pos < 255 && c >= ' ' && c <= '~') {
                // Printable character
                cmd_buffer[cmd_pos++] = c;
                terminal_putchar(c);
            }
        }

        // Halt CPU until next interrupt (saves power)
        __asm__ volatile ("hlt");
    }
}
