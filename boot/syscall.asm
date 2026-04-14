; syscall.asm - System Call Interface
;
; User programs call syscalls via int 0x80
; Arguments: rax = syscall number, rdi, rsi, rdx, r10, r8, r9 = args
; Returns: rax = result

section .text
bits 64

extern syscall_handler

global syscall_entry

syscall_entry:
    ; Save user context
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    
    ; Call C handler
    mov rcx, rdx      ; Third arg
    mov rdx, rsi      ; Second arg
    mov rsi, rdi      ; First arg
    mov rdi, rax      ; Syscall number
    call syscall_handler
    
    ; Restore user context
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    
    iretq

section .note.GNU-stack noalloc noexec nowrite progbits
