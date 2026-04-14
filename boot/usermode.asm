; usermode.asm - User Mode Transition
;
; Jump from kernel mode (ring 0) to user mode (ring 3)

section .text
bits 64

global jump_to_user

; void jump_to_user(uint64_t entry, uint64_t stack)
; rdi = entry point
; rsi = stack pointer
jump_to_user:
    ; Set up user data segments
    mov ax, 0x23        ; USER_DS (GDT entry 4, RPL=3)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ; Set up user stack and entry point for iretq
    push 0x23           ; SS (user data)
    push rsi            ; RSP (user stack)
    pushfq              ; RFLAGS
    push 0x1B           ; CS (user code)
    push rdi            ; RIP (entry point)
    
    ; Enable interrupts in user mode
    or qword [rsp + 8], 0x200
    
    ; Clear registers
    xor rax, rax
    xor rbx, rbx
    xor rcx, rcx
    xor rdx, rdx
    xor rsi, rsi
    xor rdi, rdi
    xor rbp, rbp
    xor r8, r8
    xor r9, r9
    xor r10, r10
    xor r11, r11
    xor r12, r12
    xor r13, r13
    xor r14, r14
    xor r15, r15
    
    ; Jump to user mode
    iretq

section .note.GNU-stack noalloc noexec nowrite progbits
