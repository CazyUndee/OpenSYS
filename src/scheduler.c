/*
 * scheduler.c - Round-Robin Process Scheduler
 */

#include <stdint.h>
#include "../include/scheduler.h"
#include "../include/process.h"
#include "../include/kheap.h"
#include "../include/timer.h"

#define TIME_SLICE_MS 10

static process_t* ready_queue = 0;
static process_t* ready_queue_tail = 0;
static process_t* current_process = 0;
static int schedule_needed = 0;
static int time_slice_counter = 0;

void scheduler_init(void) {
    ready_queue = 0;
    ready_queue_tail = 0;
    current_process = 0;
    schedule_needed = 0;
}

void scheduler_add(process_t* proc) {
    proc->next = 0;
    proc->prev = ready_queue_tail;
    
    if (ready_queue_tail) {
        ready_queue_tail->next = proc;
    } else {
        ready_queue = proc;
    }
    ready_queue_tail = proc;
    
    proc->state = PROC_STATE_READY;
}

void scheduler_remove(process_t* proc) {
    if (proc->prev) {
        proc->prev->next = proc->next;
    } else {
        ready_queue = proc->next;
    }
    
    if (proc->next) {
        proc->next->prev = proc->prev;
    } else {
        ready_queue_tail = proc->prev;
    }
    
    proc->state = PROC_STATE_ZOMBIE;
}

process_t* scheduler_pick(void) {
    if (!ready_queue) return 0;
    
    /* Round-robin: take from front, move to back */
    process_t* proc = ready_queue;
    ready_queue = proc->next;
    
    if (ready_queue) {
        ready_queue->prev = 0;
    } else {
        ready_queue_tail = 0;
    }
    
    return proc;
}

void scheduler_tick(void) {
    if (!current_process) return;

    time_slice_counter++;
    
    /* Only reschedule after time slice expires */
    if (time_slice_counter >= TIME_SLICE_MS) {
        time_slice_counter = 0;
        current_process->cpu_time += TIME_SLICE_MS;
        schedule_needed = 1;
    }
}

void scheduler_reschedule(void) {
    schedule_needed = 1;
}

int scheduler_runnable_count(void) {
    int count = 0;
    process_t* p = ready_queue;
    while (p) {
        count++;
        p = p->next;
    }
    return count;
}

/* Get/set current process */
process_t* process_current(void) {
    return current_process;
}

void scheduler_set_current(process_t* proc) {
    current_process = proc;
    if (proc) {
        proc->state = PROC_STATE_RUNNING;
    }
}

int scheduler_needs_reschedule(void) {
    return schedule_needed;
}

void scheduler_clear_reschedule(void) {
    schedule_needed = 0;
}
