; boot64.asm - 64-bit Long Mode Bootstrap
;
; This is the entry point. We start in 16-bit real mode,
; switch to 32-bit protected mode, then to 64-bit long mode.

[BITS 16]

; Multiboot header (GRUB loads us in 32-bit protected mode)
; But we'll also support direct boot for flexibility

section .multiboot
align 8
mb_header:
    dd 0xE85250D6             ; Multiboot2 magic
    dd 0                       ; Architecture (i386)
    dd mb_header_end - mb_header
    dd -(0xE85250D6 + 0 + (mb_header_end - mb_header))
    
    ; Framebuffer tag (optional)
    align 8
    dw 5                       ; Type = framebuffer
    dw 0                       ; Flags
    dd 20                      ; Size
    dd 1024                    ; Width
    dd 768                     ; Height
    dd 32                      ; Depth
    
    ; End tag
    align 8
    dw 0
    dw 0
    dd 8
mb_header_end:

section .bss
align 16
stack_bottom:
    resb 65536                 ; 64KB stack
stack_top:

section .rodata
gdt64:
    dq 0                       ; Null descriptor
.code: equ $ - gdt64
    dq (1<<43) | (1<<44) | (1<<47) | (1<<53)  ; Code: exec, code, present, 64-bit
.data: equ $ - gdt64
    dq (1<<44) | (1<<47) | (1<<41)  ; Data: data, present, write
.pointer:
    dw $ - gdt64 - 1
    dq gdt64

section .text
global _start
extern kernel_main

_start:
    ; GRUB loads us in 32-bit protected mode with paging disabled
    ; We need to enable long mode ourselves
    
    ; Set up stack
    mov esp, stack_top
    
    ; Clear direction flag
    cld
    
    ; Save multiboot info
    push ebx
    push eax
    
    ; Check for CPUID
    call check_cpuid
    call check_long_mode
    
    ; Set up paging for long mode
    call setup_page_tables
    call enable_paging
    
    ; Load 64-bit GDT
    lgdt [gdt64.pointer]
    
    ; Jump to 64-bit code
    jmp gdt64.code:long_mode_start

check_cpuid:
    pushfd
    pop eax
    mov ecx, eax
    xor eax, 1 << 21
    push eax
    popfd
    pushfd
    pop eax
    push ecx
    popfd
    cmp eax, ecx
    je .no_cpuid
    ret
.no_cpuid:
    jmp $

check_long_mode:
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .no_long_mode
    mov eax, 0x80000001
    cpuid
    test edx, 1 << 29
    jz .no_long_mode
    ret
.no_long_mode:
    jmp $

setup_page_tables:
    ; Map PML4 at 0x1000
    mov eax, 0x2000           ; Address of PDPT
    or eax, 0b11              ; Present + writable
    mov [0x1000], eax         ; PML4[0] = PDPT
    
    ; Map PDPT
    mov eax, 0x3000           ; Address of PD
    or eax, 0b11
    mov [0x2000], eax         ; PDPT[0] = PD
    
    ; Map PD with 2MB pages
    mov eax, 0b10000011       ; Present + writable + huge page
    mov [0x3000], eax         ; PD[0] = 2MB page at 0x00000000
    
    ; Map second entry for higher half (-2GB)
    mov eax, 0x1000
    or eax, 0b11
    mov [0x1000 + 511*8], eax  ; PML4[511] = PDPT (recursive)
    
    ret

enable_paging:
    ; Set PML4 address
    mov eax, 0x1000
    mov cr3, eax
    
    ; Enable PAE
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax
    
    ; Set long mode bit
    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr
    
    ; Enable paging
    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax
    
    ret

[BITS 64]
long_mode_start:
    ; Load 64-bit data segment selectors
    mov ax, gdt64.data
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    ; Set up 64-bit stack
    mov rsp, stack_top
    mov rbp, 0
    
    ; Clear BSS
    extern bss_start, bss_end
    mov rdi, bss_start
    mov rcx, bss_end
    sub rcx, rdi
    rep stosb
    
    ; Call kernel main
    mov rdi, rax              ; First arg: magic
    mov rsi, rbx              ; Second arg: mbi
    call kernel_main
    
    ; Halt if kernel returns
.halt:
    cli
    hlt
    jmp .halt
