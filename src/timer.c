/*
 * timer.c - PIT (Programmable Interval Timer) Driver
 */

#include <stdint.h>

#define PIT_CHANNEL0 0x40
#define PIT_CHANNEL1 0x41
#define PIT_CHANNEL2 0x42
#define PIT_COMMAND  0x43

#define PIT_FREQ     1193182
#define TARGET_FREQ  1000  /* 1000 Hz = 1ms per tick */

static volatile uint64_t system_ticks = 0;

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* Called from IRQ0 */
void timer_handler(void) {
    system_ticks++;
}

void timer_init(void) {
    uint32_t divisor = PIT_FREQ / TARGET_FREQ;
    
    /* Channel 0, mode 3 (square wave), binary */
    outb(PIT_COMMAND, 0x36);
    
    /* Set divisor */
    outb(PIT_CHANNEL0, divisor & 0xFF);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);
    
    system_ticks = 0;
}

uint64_t timer_get_ticks(void) {
    return system_ticks;
}

uint64_t timer_get_ms(void) {
    return system_ticks;
}

void timer_sleep(uint64_t ms) {
    uint64_t target = system_ticks + ms;
    while (system_ticks < target) {
        __asm__ volatile ("hlt");
    }
}

void timer_sleep_busy(uint64_t ms) {
    uint64_t target = system_ticks + ms;
    while (system_ticks < target) {
        __asm__ volatile ("pause");
    }
}
