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

/* Called from timer interrupt */
void switch_timer_tick(void) {
    scheduler_tick();
    
    if (scheduler_needs_reschedule()) {
        switch_schedule();
    }
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
