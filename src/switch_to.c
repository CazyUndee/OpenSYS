/*
 * switch_to.c - Main Scheduler Entry Point
 */

#include <stdint.h>
#include "../include/process.h"
#include "../include/scheduler.h"
#include "../include/timer.h"

/* Assembly context switch */
extern void context_switch(uint64_t* old_ctx, uint64_t* new_ctx);

/* External scheduler functions */
extern process_t* process_current(void);
extern void scheduler_set_current(process_t* proc);
extern int scheduler_needs_reschedule(void);
extern void scheduler_clear_reschedule(void);
extern void process_check_sleepers(void);

/* Main scheduler - called from timer interrupt */
void schedule(void) {
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
        context_switch(0, (uint64_t*)&next->context);
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
    context_switch((uint64_t*)&current->context, (uint64_t*)&next->context);
}

/* Called from timer interrupt */
void scheduler_timer_tick(void) {
    scheduler_tick();
    
    if (scheduler_needs_reschedule()) {
        schedule();
    }
}
