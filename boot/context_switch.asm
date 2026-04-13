; context_switch.asm - Context Switching for x86_64
;
; void context_switch(cpu_context_t* old_ctx, cpu_context_t* new_ctx)

section .text
bits 64

global context_switch

context_switch:
    ; rdi = old_ctx (save current)
    ; rsi = new_ctx (restore new)

    ; Save old context
    test rdi, rdi
    jz .load_new

    mov [rdi + 0], rax
    mov [rdi + 8], rbx
    mov [rdi + 16], rcx
    mov [rdi + 24], rdx
    mov [rdi + 32], rsi
    mov [rdi + 40], rdi
    mov [rdi + 48], rbp
    mov [rdi + 56], r8
    mov [rdi + 64], r9
    mov [rdi + 72], r10
    mov [rdi + 80], r11
    mov [rdi + 88], r12
    mov [rdi + 96], r13
    mov [rdi + 104], r14
    mov [rdi + 112], r15

    ; Save RIP and flags (we'll return after restore)
    mov rax, [rsp]
    mov [rdi + 120], rax       ; rip
    add rsp, 8
    
    mov rax, [rsp + 8]         ; cs
    mov [rdi + 128], rax

    pushfq
    pop qword [rdi + 136]      ; rflags

    mov rax, rsp
    mov [rdi + 144], rax       ; rsp

    mov ax, ss
    mov [rdi + 152], ax        ; ss

.load_new:
    ; Load new context
    test rsi, rsi
    jz .done

    mov rax, [rsi + 0]
    mov rbx, [rsi + 8]
    mov rcx, [rsi + 16]
    mov rdx, [rsi + 24]
    mov rbp, [rsi + 48]
    mov r8, [rsi + 56]
    mov r9, [rsi + 64]
    mov r10, [rsi + 72]
    mov r11, [rsi + 80]
    mov r12, [rsi + 88]
    mov r13, [rsi + 96]
    mov r14, [rsi + 104]
    mov r15, [rsi + 112]

    ; Restore stack and jump
    mov rsp, [rsi + 144]       ; rsp
    push qword [rsi + 136]     ; rflags
    push qword [rsi + 128]     ; cs
    push qword [rsi + 120]     ; rip

    ; Restore remaining registers
    mov rsi, [rsi + 32]
    
    iretq

.done:
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
