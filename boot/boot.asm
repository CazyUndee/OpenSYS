; Plan 0 v0.4.0 - Bootstrap
; 
; Copyright (C) 2026 CazyUndee
; 
; This program is free software: you can redistribute it and/or modify
; it under the terms of the GNU Affero General Public License as published by
; the Free Software Foundation, either version 3 of the License, or
; (at your option) any later version.
; 
; This program is distributed in the hope that it will be useful,
; but WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
; GNU Affero General Public License for more details.
; 
; You should have received a copy of the GNU Affero General Public License
; along with this program.  If not, see <https://www.gnu.org/licenses/>.

; boot.asm - Plan 0 64-bit Long Mode Bootstrap
;
; GRUB loads us in 32-bit protected mode. We need to:
; 1. Setup identity-mapped paging
; 2. Enable long mode
; 3. Jump to 64-bit Plan 0 kernel

section .multiboot
align 4
mb_header:
    dd 0x1BADB002              ; 0: magic
    dd 1 << 16                 ; 4: flags (a.out kludge)
    dd -(0x1BADB002 + (1 << 16)) ; 8: checksum
    dd mb_header               ; 12: header_addr
    dd 0x100000                ; 16: load_addr
    dd 0                       ; 20: load_end_addr (0 = entire file)
    dd 0                       ; 24: bss_end_addr (0 = no extra BSS)
    dd _start                  ; 28: entry_addr
mb_header_end:

section .bss
align 16
stack_bottom:
    resb 65536              ; 64KB stack
stack_top:

align 4096
pml4:
    resb 4096
pdpt:
    resb 4096
pd:
    resb 4096

section .rodata
gdt64:
    dq 0                    ; Null descriptor
.code: equ $ - gdt64
    dq (1<<43) | (1<<44) | (1<<47) | (1<<53)  ; Code: exec, code, present, 64-bit
.data: equ $ - gdt64
    dq (1<<44) | (1<<47) | (1<<41)           ; Data: data, present, write
.pointer:
    dw $ - gdt64 - 1
    dq gdt64

section .boot
bits 32
global _start
extern kernel_main
extern bss_start
extern kernel_end

_start:
    ; Set up stack
    mov esp, stack_top

    ; Clear direction flag
    cld

    ; Zero the BSS (all zero-initialized statics) before any C code runs.
    ; The multiboot header declares bss_end_addr = 0, so neither QEMU nor
    ; GRUB zeroes it, and the raw-loaded ELF debug sections overwrite it.
    ; Without this, every static variable in the kernel starts as garbage
    ; (this caused the keyboard ring-buffer garbage and the original
    ; 32->64 transition crash from garbage page tables).
    mov edi, bss_start
    xor eax, eax
    mov ecx, kernel_end
    sub ecx, edi
    shr ecx, 2
    rep stosd

    ; Save multiboot info FIRST (before any register modifications!)
    ; EBX = multiboot info pointer, EAX = magic (0x2BADB002)
    push ebx
    push eax

    ; Check for CPUID and long mode
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
    ; Zero the three page-table pages (pml4, pdpt, pd) so no stale
    ; entries remain in the uninitialized BSS. Only the entries below
    ; are populated; a stale present bit in any other slot would make
    ; the CPU walk garbage on address translation.
    mov edi, pml4
    xor eax, eax
    mov ecx, (4096 * 3) / 4
    rep stosd

    ; Map PML4[0] -> PDPT
    mov eax, pdpt
    or eax, 0b11
    mov [pml4], eax

    ; Map PDPT[0] -> PD
    mov eax, pd
    or eax, 0b11
    mov [pdpt], eax

    ; Map PD[0] -> 2MB huge page at 0x00000000 (low 2MB)
    mov eax, 0b10000011
    mov [pd], eax

    ; Map PD[1] -> 2MB huge page at 0x00200000 (kernel code/data)
    mov eax, 0x00200000 | 0b10000011
    mov [pd + 8], eax

    ret

enable_paging:
    ; Set PML4 address
    mov eax, pml4
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

bits 64
long_mode_start:
    ; Retrieve multiboot info from stack
    ; Stack layout: [rsp]=magic (4B), [rsp+4]=mbi (4B)
    xor rdi, rdi
    mov edi, [rsp]         ; rdi = magic (zero-extended from 32-bit push)
    xor rsi, rsi
    mov esi, [rsp + 4]     ; rsi = multiboot info pointer

    ; Set up clean 64-bit stack
    mov rsp, stack_top
    mov rbp, 0

    ; Load 64-bit data segment selectors
    mov ax, gdt64.data
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Call kernel main (rdi = magic, rsi = mbi)
    call kernel_main

    ; Halt if kernel returns
.halt:
    cli
    hlt
    jmp .halt

section .note.GNU-stack noalloc noexec nowrite progbits
