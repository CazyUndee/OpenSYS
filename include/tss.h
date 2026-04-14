/*
 * tss.h - Task State Segment
 */

#ifndef TSS_H
#define TSS_H

#include <stdint.h>

typedef struct {
    uint32_t reserved0;
    uint64_t rsp0;      /* Stack pointer for ring 0 */
    uint64_t rsp1;      /* Stack pointer for ring 1 */
    uint64_t rsp2;      /* Stack pointer for ring 2 */
    uint64_t reserved1;
    uint64_t ist[7];    /* Interrupt Stack Tables */
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb;      /* I/O Permission Base */
} __attribute__((packed)) tss_t;

void tss_init(void);
void tss_set_rsp0(uint64_t rsp0);
extern void load_tr(void);

#endif
