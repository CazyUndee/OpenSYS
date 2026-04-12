; interrupts.asm - Interrupt Service Routines
; Assembly stubs for all CPU exceptions and hardware IRQs

[BITS 32]

; External C handler functions
extern isr_handler
extern irq_handler

; Common ISR stub - saves state and calls C handler
global isr_common_stub
isr_common_stub:
    pusha               ; Push edi,esi,ebp,esp,ebx,edx,ecx,eax
    push ds
    push es
    push fs
    push gs
    
    mov ax, 0x10        ; Load kernel data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    push esp            ; Pass pointer to stack
    call isr_handler
    pop esp
    
    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8          ; Clean up error code and int number
    sti
    iret

; Common IRQ stub - same as ISR but handles PIC EOI
global irq_common_stub
irq_common_stub:
    pusha
    push ds
    push es
    push fs
    push gs
    
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    push esp
    call irq_handler
    pop esp
    
    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8
    sti
    iret

; CPU Exception handlers (ISR 0-31)
; Macro for exceptions without error code
%macro ISR_NOERR 1
global isr%1
isr%1:
    cli
    push dword 0        ; Dummy error code
    push dword %1       ; Interrupt number
    jmp isr_common_stub
%endmacro

; Macro for exceptions with error code
%macro ISR_ERR 1
global isr%1
isr%1:
    cli
    push dword %1       ; Interrupt number (error code already pushed by CPU)
    jmp isr_common_stub
%endmacro

; Macro for IRQ handlers
%macro IRQ_HANDLER 2
global irq%1
irq%1:
    cli
    push dword 0        ; Dummy error code
    push dword %2       ; IRQ number (remapped to 32+)
    jmp irq_common_stub
%endmacro

; CPU Exceptions
ISR_NOERR 0             ; Divide by zero
ISR_NOERR 1             ; Debug
ISR_NOERR 2             ; NMI
ISR_NOERR 3             ; Breakpoint
ISR_NOERR 4             ; Overflow
ISR_NOERR 5             ; Bound range
ISR_NOERR 6             ; Invalid opcode
ISR_NOERR 7             ; Device not available
ISR_ERR   8             ; Double fault (has error code)
ISR_NOERR 9             ; Coprocessor overrun
ISR_ERR   10            ; Invalid TSS
ISR_ERR   11            ; Segment not present
ISR_ERR   12            ; Stack fault
ISR_ERR   13            ; General protection fault
ISR_ERR   14            ; Page fault
ISR_NOERR 15            ; Reserved
ISR_NOERR 16            ; x87 FPU error
ISR_ERR   17            ; Alignment check
ISR_NOERR 18            ; Machine check
ISR_NOERR 19            ; SIMD FPU error
ISR_NOERR 20            ; Reserved
ISR_NOERR 21            ; Reserved
ISR_NOERR 22            ; Reserved
ISR_NOERR 23            ; Reserved
ISR_NOERR 24            ; Reserved
ISR_NOERR 25            ; Reserved
ISR_NOERR 26            ; Reserved
ISR_NOERR 27            ; Reserved
ISR_NOERR 28            ; Reserved
ISR_NOERR 29            ; Reserved
ISR_NOERR 30            ; Security exception
ISR_NOERR 31            ; Reserved

; Hardware IRQs (remapped to vectors 32-47)
IRQ_HANDLER 0, 32       ; Timer
IRQ_HANDLER 1, 33       ; Keyboard
IRQ_HANDLER 2, 34       ; Cascade
IRQ_HANDLER 3, 35       ; COM2
IRQ_HANDLER 4, 36       ; COM1
IRQ_HANDLER 5, 37       ; LPT2
IRQ_HANDLER 6, 38       ; Floppy
IRQ_HANDLER 7, 39       ; LPT1
IRQ_HANDLER 8, 40       ; CMOS RTC
IRQ_HANDLER 9, 41       ; Free
IRQ_HANDLER 10, 42      ; Free
IRQ_HANDLER 11, 43      ; Free
IRQ_HANDLER 12, 44      ; PS/2 Mouse
IRQ_HANDLER 13, 45      ; FPU
IRQ_HANDLER 14, 46      ; Primary IDE
IRQ_HANDLER 15, 47      ; Secondary IDE

; Add .note.GNU-stack section to prevent executable stack warning
section .note.GNU-stack noalloc noexec nowrite progbits
