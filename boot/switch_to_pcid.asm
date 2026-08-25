; switch_to_pcid.asm - Plan 0 PCID-Aware Context Switching
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

; PCID-aware context switch for Plan 0 Unified UI
; Preserves TLB entries for GUI and CLI processes

section .text

extern g_current_process
extern g_pcid_supported

; Process context structure
struc process_context
    .rbp:      resq 1
    .rsp:      resq 1
    .r15:      resq 1
    .r14:      resq 1
    .r13:      resq 1
    .r12:      resq 1
    .r11:      resq 1
    .r10:      resq 1
    .r9:       resq 1
    .r8:       resq 1
    .rdi:      resq 1
    .rsi:      resq 1
    .rdx:      resq 1
    .rcx:      resq 1
    .rbx:      resq 1
    .rax:      resq 1
    .rip:      resq 1
    .cr3:      resq 1      ; Page table base
    .pcid:     resq 1      ; Process Context ID
    .size:
endstruc

; Check if PCID is supported (called during init)
global check_pcid_support
check_pcid_support:
    push rbx
    push rcx
    push rdx
    
    mov eax, 7
    cpuid
    test ebx, (1 << 10)  ; INVPCID bit
    jnz .pcid_supported
    
    xor eax, eax
    jmp .done
    
.pcid_supported:
    mov eax, 1
    
.done:
    pop rdx
    pop rcx
    pop rbx
    ret

; PCID-aware context switch
; rdi = pointer to next process context
; rsi = pointer to current process context (can be NULL)
global switch_to_pcid
switch_to_pcid:
    ; Save current context if provided
    test rsi, rsi
    jz .skip_save
    
    ; Save general purpose registers
    mov [rsi + process_context.rax], rax
    mov [rsi + process_context.rbx], rbx
    mov [rsi + process_context.rcx], rcx
    mov [rsi + process_context.rdx], rdx
    mov [rsi + process_context.rsi], rsi
    mov [rsi + process_context.rdi], rdi
    mov [rsi + process_context.r8], r8
    mov [rsi + process_context.r9], r9
    mov [rsi + process_context.r10], r10
    mov [rsi + process_context.r11], r11
    mov [rsi + process_context.r12], r12
    mov [rsi + process_context.r13], r13
    mov [rsi + process_context.r14], r14
    mov [rsi + process_context.r15], r15
    
    ; Save stack pointers
    mov [rsi + process_context.rbp], rbp
    mov [rsi + process_context.rsp], rsp
    
    ; Save return address
    mov [rsi + process_context.rip], rdx
    
    ; Save current CR3 and PCID
    mov rax, cr3
    mov [rsi + process_context.cr3], rax
    
.skip_save:
    ; Load new context
    mov rax, [rdi + process_context.rax]
    mov rbx, [rdi + process_context.rbx]
    mov rcx, [rdi + process_context.rcx]
    mov rdx, [rdi + process_context.rdx]
    mov r8, [rdi + process_context.r8]
    mov r9, [rdi + process_context.r9]
    mov r10, [rdi + process_context.r10]
    mov r11, [rdi + process_context.r11]
    mov r12, [rdi + process_context.r12]
    mov r13, [rdi + process_context.r13]
    mov r14, [rdi + process_context.r14]
    mov r15, [rdi + process_context.r15]
    
    ; Load stack pointers
    mov rbp, [rdi + process_context.rbp]
    mov rsp, [rdi + process_context.rsp]
    
    ; PCID-aware page table switch
    mov rcx, [g_pcid_supported]
    test rcx, rcx
    jz .no_pcid
    
    ; PCID supported - use enhanced switching
    mov rax, [rdi + process_context.cr3]
    mov rbx, [rdi + process_context.pcid]
    
    ; Combine CR3 with PCID (PCID goes in bits 0-11 of CR3 when PCIDE=1)
    mov rcx, cr4
    or rcx, (1 << 4)  ; Set PCIDE bit
    mov cr4, rcx
    
    ; Set new CR3 with PCID
    or rax, rbx        ; Combine page table address with PCID
    mov cr3, rax
    
    jmp .context_loaded
    
.no_pcid:
    ; Fallback for CPUs without PCID
    mov rax, [rdi + process_context.cr3]
    mov cr3, rax
    
.context_loaded:
    ; Load return address and jump
    mov rax, [rdi + process_context.rip]
    jmp rax

; Fast switch for same PCID (just change CR3, keep PCID)
; rdi = new CR3 value
; rsi = PCID value
global switch_cr3_same_pcid
switch_cr3_same_pcid:
    mov rcx, [g_pcid_supported]
    test rcx, rcx
    jz .fallback
    
    ; PCID supported - preserve PCID
    mov rax, cr4
    or rax, (1 << 4)  ; Set PCIDE
    mov cr4, rax
    
    ; Combine CR3 with existing PCID
    mov rax, rdi
    or rax, rsi
    mov cr3, rax
    ret
    
.fallback:
    ; Full TLB flush for non-PCID systems
    mov cr3, rdi
    ret

; Initialize PCID system
global init_pcid_system
init_pcid_system:
    call check_pcid_support
    mov [g_pcid_supported], rax
    
    test rax, rax
    jnz .pcid_ok
    
    ; PCID not supported - print warning
    ; TODO: Add terminal output
    ret
    
.pcid_ok:
    ; PCID supported - set up initial contexts
    ; TODO: Initialize PCID allocator
    
    ret

; Data section
section .data
g_pcid_supported:
    dq 0
g_current_process:
    dq 0

section .note.GNU-stack noalloc noexec nowrite progbits
