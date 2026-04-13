/*
 * timer.h - PIT Timer Driver
 */

#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

void timer_init(void);
void timer_handler(void);

uint64_t timer_get_ticks(void);
uint64_t timer_get_ms(void);

void timer_sleep(uint64_t ms);
void timer_sleep_busy(uint64_t ms);

#endif
