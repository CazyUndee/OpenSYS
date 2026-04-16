/*
 * tss.c - Task State Segment
 */

#include <stdint.h>
#include "../include/tss.h"
#include "../include/kheap.h"

tss_t* tss = 0;

void tss_init(void) {
    tss = (tss_t*)kmalloc(sizeof(tss_t));
    if (!tss) return;
    
    /* Clear TSS */
    for (int i = 0; i < sizeof(tss_t); i++) {
        ((uint8_t*)tss)[i] = 0;
    }
    
    /* Set kernel stack */
    tss->rsp0 = 0;  /* Set by scheduler */
    
    /* Set IOPB offset (no I/O bitmap) */
    tss->iopb = sizeof(tss_t);
}

void tss_set_rsp0(uint64_t rsp0) {
    if (tss) {
        tss->rsp0 = rsp0;
    }
}
