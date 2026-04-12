; interrupts.asm - Interrupt Service Routines
; Assembly stubs for all CPU exceptions and hardware IRQs

[BITS 32]

; Export all ISR/IRQ stubs
[GLOBAL isr0]   [GLOBAL isr1]   [GLOBAL isr2]   [GLOBAL isr3]
[GLOBAL isr4]   [GLOBAL isr5]   [GLOBAL isr6]   [GLOBAL isr7]
[GLOBAL isr8]   [GLOBAL isr9]   [GLOBAL isr10]  [GLOBAL isr11]
[GLOBAL isr12]  [GLOBAL isr13]  [GLOBAL isr14]  [GLOBAL isr15]
[GLOBAL isr16]  [GLOBAL isr17]  [GLOBAL isr18]  [GLOBAL isr19]
[GLOBAL isr20]  [GLOBAL isr21]  [GLOBAL isr22]  [GLOBAL isr23]
[GLOBAL isr24]  [GLOBAL isr25]  [GLOBAL isr26]  [GLOBAL isr27]
[GLOBAL isr28]  [GLOBAL isr29]  [GLOBAL isr30]  [GLOBAL isr31]

[GLOBAL irq0]   [GLOBAL irq1]   [GLOBAL irq2]   [GLOBAL irq3]
[GLOBAL irq4]   [GLOBAL irq5]   [GLOBAL irq6]   [GLOBAL irq7]
[GLOBAL irq8]   [GLOBAL irq9]   [GLOBAL irq10]  [GLOBAL irq11]
[GLOBAL irq12]  [GLOBAL irq13]  [GLOBAL irq14]  [GLOBAL irq15]

; External C handler functions
[EXTERN isr_handler]
[EXTERN irq_handler]

; Macro for ISRs without error code
%macro ISR_NO_ERR 1
isr%1:
    cli
    push byte 0         ; Dummy error code
    push byte %1        ; Interrupt number
    jmp isr_common_stub
%endmacro

; Macro for ISRs with error code
%macro ISR_ERR 1
isr%1:
    cli
    push byte %1        ; Interrupt number (error code already on stack)
    jmp isr_common_stub
%endmacro

; Macro for IRQs
%macro IRQ 2
irq%1:
    cli
    push byte 0         ; Dummy error code
    push byte %2        ; IRQ number (32+ for remapped IRQs)
    jmp irq_common_stub
%endmacro

; CPU Exceptions
ISR_NO_ERR 0    ; Divide by zero
ISR_NO_ERR 1    ; Debug
ISR_NO_ERR 2    ; NMI
ISR_NO_ERR 3    ; Breakpoint
ISR_NO_ERR 4    ; Overflow
ISR_NO_ERR 5    ; Bound range
ISR_NO_ERR 6    ; Invalid opcode
ISR_NO_ERR 7    ; Device not available
ISR_ERR 8       ; Double fault
ISR_NO_ERR 9    ; Coprocessor overrun
ISR_ERR 10      ; Invalid TSS
ISR_ERR 11      ; Segment not present
ISR_ERR 12      ; Stack fault
ISR_ERR 13      ; General protection fault
ISR_ERR 14      ; Page fault
ISR_NO_ERR 15   ; Reserved
ISR_NO_ERR 16   ; x87 FPU error
ISR_ERR 17      ; Alignment check
ISR_NO_ERR 18   ; Machine check
ISR_NO_ERR 19   ; SIMD FPU error
ISR_NO_ERR 20   ; Reserved
ISR_NO_ERR 21   ; Reserved
ISR_NO_ERR 22   ; Reserved
ISR_NO_ERR 23   ; Reserved
ISR_NO_ERR 24   ; Reserved
ISR_NO_ERR 25   ; Reserved
ISR_NO_ERR 26   ; Reserved
ISR_NO_ERR 27   ; Reserved
ISR_NO_ERR 28   ; Reserved
ISR_NO_ERR 29   ; Reserved
ISR_NO_ERR 30   ; Security exception
ISR_NO_ERR 31   ; Reserved

; Hardware IRQs (remapped to 32-47)
IRQ 0, 32       ; Timer
IRQ 1, 33       ; Keyboard
IRQ 2, 34       ; Cascade
IRQ 3, 35       ; COM2
IRQ 4, 36       ; COM1
IRQ 5, 37       ; LPT2
IRQ 6, 38       ; Floppy
IRQ 7, 39       ; LPT1
IRQ 8, 40       ; CMOS RTC
IRQ 9, 41       ; Free
IRQ 10, 42      ; Free
IRQ 11, 43      ; Free
IRQ 12, 44      ; PS/2 Mouse
IRQ 13, 45      ; FPU
IRQ 14, 46      ; Primary IDE
IRQ 15, 47      ; Secondary IDE

; Common ISR stub
isr_common_stub:
    pusha           ; Push all registers (edi, esi, ebp, esp, ebx, edx, ecx, eax)
    
    mov ax, ds      ; Save data segment
    push eax
    mov ax, es
    push eax
    mov ax, fs
    push eax
    mov ax, gs
    push eax
    
    mov ax, 0x10    ; Load kernel data segment (index 2)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    push esp        ; Push pointer to cpu_state structure
    call isr_handler
    pop esp
    
    pop eax         ; Restore segment registers
    mov gs, ax
    pop eax
    mov fs, ax
    pop eax
    mov es, ax
    pop eax
    mov ds, ax
    
    popa            ; Restore all registers
    add esp, 8      ; Clean up error code and interrupt number
    sti
    iret            ; Return from interrupt

; Common IRQ stub
irq_common_stub:
    pusha           ; Push all registers
    
    mov ax, ds      ; Save data segment
    push eax
    mov ax, es
    push eax
    mov ax, fs
    push eax
    mov ax, gs
    push eax
    
    mov ax, 0x10    ; Load kernel data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    push esp        ; Push pointer to cpu_state
    call irq_handler
    pop esp
    
    pop eax         ; Restore segment registers
    mov gs, ax
    pop eax
    mov fs, ax
    pop eax
    mov es, ax
    pop eax
    mov ds, ax
    
    popa            ; Restore all registers
    add esp, 8      ; Clean up error code and interrupt number
    sti
    iret            ; Return from interrupt
