/*
 * switch.c - Plan 0 Context Switch Manager
 *
 * Copyright (C) 2026 CazyUndee
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 * 
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "process.h"
#include "scheduler.h"
#include "timer.h"
#include <stdint.h>

/* Assembly context switch */
extern void context_switch(cpu_context_t* old_ctx, cpu_context_t* new_ctx);


/* Main scheduler - called from timer interrupt */
void switch_schedule(void) {
    /* Check sleeping processes */
    process_check_sleepers();
    
    /* Get current process */
    process_t* current = process_current();
    
    /* Get next process to run */
    process_t* next = scheduler_pick();
    
    if (!next) {
        /* Nothing to run, stick with current */
        if (current) {
            scheduler_clear_reschedule();
        }
        return;
    }
    
    if (!current) {
        /* No current process, just run next */
        scheduler_set_current(next);
        scheduler_clear_reschedule();
        
        /* Jump to new process */
        context_switch(0, &next->context);
        return;
    }
    
    /* Save current and switch to next */
    if (current->state == PROC_STATE_RUNNING) {
        current->state = PROC_STATE_READY;
        scheduler_add(current);
    }
    
    scheduler_set_current(next);
    scheduler_clear_reschedule();
    
    /* Perform context switch */
    context_switch(&current->context, &next->context);
}

/* Called from timer interrupt — performs preemptive context switch.
 *
 * When this function is called, the CPU has already pushed an interrupt
 * frame (RIP/CS/RFLAGS/[RSP/SS]) onto the current process's kernel stack,
 * and the ISR stub has pushed all general-purpose registers.
 *
 * The stack pointer at function entry points at the start of the interrupt
 * frame. We save it into the old process's context, then use the assembly
 * context_switch to swap to the new process's saved stack.
 */
void switch_timer_tick(void) {
    scheduler_tick();
    
    process_t* current = process_current();
    
    /* If no current process but there are runnable ones, force a schedule. */
    if (!current && scheduler_runnable_count() > 0) {
        scheduler_reschedule();
    }
    
    if (!scheduler_needs_reschedule()) return;
    
    process_t* next = scheduler_pick();
    
    if (!next) {
        scheduler_clear_reschedule();
        return;
    }
    
    /* Send EOI to the PIC BEFORE calling context_switch.  The assembly
     * context_switch performs its own iretq which replaces the ISR's
     * normal return path.  If we don't send EOI here, the PIC's
     * In-Service Register bit stays set and blocks ALL future timer
     * interrupts, hanging the system. */
    __asm__ volatile ("outb %0, $0x20" : : "a"((uint8_t)0x20));
    
    /* No current process — just switch directly */
    if (!current) {
        scheduler_set_current(next);
        scheduler_clear_reschedule();
        context_switch(0, &next->context);
        return;
    }
    
    /* Save current RSP into context. At this point, RSP points at the
     * interrupt frame (RIP/CS/RFLAGS) that the CPU pushed for IRQ0.
     * The assembly context_switch will save all GP registers at this RSP
     * and restore from the new process's context.rsp. */
    uint64_t saved_rsp;
    __asm__ volatile ("mov %%rsp, %[sp]" : [sp] "=r" (saved_rsp));
    current->context.rsp = saved_rsp;
    
    current->state = PROC_STATE_READY;
    scheduler_add(current);
    
    scheduler_set_current(next);
    scheduler_clear_reschedule();
    
    /* context_switch will restore next->context.rsp into RSP,
     * then use iretq to resume the new process at its saved RIP. */
    context_switch(&current->context, &next->context);
}

/* ===== PCID (Process-Context ID) Support ===== */

/* ASM PCID functions (from boot/switch_to_pcid.asm) */
extern int check_pcid_support(void);
extern void init_pcid_system(void);

/* PCID bitmap allocator - supports up to 4096 PCIDs */
#define PCID_MAX 4096
static uint64_t pcid_bitmap[PCID_MAX / 64];  /* 4096 bits */
static int pcid_initialized = 0;

/* Initialize PCID allocator - PCID 0 is reserved */
static void pcid_allocator_init(void) {
    for (int i = 0; i < PCID_MAX / 64; i++) {
        pcid_bitmap[i] = 0;
    }
    /* Reserve PCID 0 (kernel default) */
    pcid_bitmap[0] |= 1ULL;
    pcid_initialized = 1;
}

/* Allocate a PCID value */
uint16_t pcid_alloc(void) {
    if (!pcid_initialized) pcid_allocator_init();
    
    for (int i = 1; i < PCID_MAX; i++) {
        int word = i / 64;
        int bit = i % 64;
        if (!(pcid_bitmap[word] & (1ULL << bit))) {
            pcid_bitmap[word] |= (1ULL << bit);
            return (uint16_t)i;
        }
    }
    return 0;  /* All PCIDs exhausted */
}

/* Free a PCID value */
void pcid_free(uint16_t pcid) {
    if (pcid == 0 || pcid >= PCID_MAX) return;
    int word = pcid / 64;
    int bit = pcid % 64;
    pcid_bitmap[word] &= ~(1ULL << bit);
}

/* C wrapper for PCID system init - called from kernel_main */
int init_pcid_system_c(void) {
    init_pcid_system();
    
    int supported = check_pcid_support();
    if (supported) {
        pcid_allocator_init();
        return 0;  /* PCID supported */
    }
    return -1;  /* PCID not supported */
}

// Legacy compatibility shims.
// These thin wrappers bridge the old switch_* API to the current scheduler API.
// Caller: boot/interrupts.asm uses scheduler_timer_tick(); schedule() is a stable
// alias kept for ABI compatibility.  Do NOT remove without updating assembly.
void schedule(void) {
  switch_schedule();
}

void scheduler_timer_tick(void) {
  switch_timer_tick();
}
