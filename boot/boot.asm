; OpenCode OS Boot Loader
; Multiboot 2 compliant header

section .multiboot
align 8

multiboot_header_start:
    dd 0xe85250d6                   ; Multiboot 2 magic number
    dd 0                            ; Architecture (i386 protected mode)
    dd multiboot_header_end - multiboot_header_start  ; Header length
    dd -(0xe85250d6 + 0 + (multiboot_header_end - multiboot_header_start))  ; Checksum

    ; End tag (required)
    align 8
    dw 0                            ; Type (end tag)
    dw 0                            ; Flags
    dd 8                            ; Size
multiboot_header_end:

section .bss
align 16
stack_bottom:
    resb 16384                      ; 16KB stack
stack_top:

section .text
global _start
extern kernel_main

_start:
    ; GRUB leaves us in protected mode with these guarantees:
    ; - Protected mode is enabled
    ; - A20 gate is enabled
    ; - GDT is loaded (but we should set up our own)
    ; - IDT is not set up
    ; - Interrupts are disabled
    ; - EAX contains 0x36d76289 (multiboot 2 magic)
    ; - EBX contains pointer to multiboot info structure

    ; Set up stack immediately - CRITICAL
    mov esp, stack_top

    ; Store multiboot info for later
    push ebx                        ; Multiboot info pointer
    push eax                        ; Multiboot magic number

    ; Clear BSS section (optional but good practice)
    ; For now, skip - we know our BSS is already zeroed by GRUB

    ; Call kernel main
    call kernel_main

    ; If kernel returns, halt forever
.hang:
    cli
    hlt
    jmp .hang
