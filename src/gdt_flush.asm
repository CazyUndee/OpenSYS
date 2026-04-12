; gdt_flush.asm - Load GDT and reload segment registers
;
; This is the most critical part of GDT initialization.
; A single mistake here = triple fault (instant reboot).
;
; Steps:
; 1. Load GDTR with lgdt instruction
; 2. Reload data segment registers (DS, ES, FS, GS, SS)
; 3. Far jump to reload CS (code segment)

[GLOBAL gdt_flush]

gdt_flush:
    ; Argument: pointer to gdt_ptr structure (6 bytes: limit + base)
    mov eax, [esp + 4]      ; Get argument from stack

    ; Load GDT pointer into GDTR register
    lgdt [eax]

    ; Reload data segment registers with kernel data selector (0x10)
    ; 0x10 = index 2 << 3 | RPL 0
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Reload code segment with far jump
    ; 0x08 = index 1 << 3 | RPL 0 (kernel code selector)
    ; The jump format is: jmp selector:offset
    jmp 0x08:.flush_cs

.flush_cs:
    ; Return to C code
    ret
