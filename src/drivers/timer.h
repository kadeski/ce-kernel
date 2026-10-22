#ifndef DRIVERS_TIMER_H
#define DRIVERS_TIMER_H

#include <stdint.h>

#define TIMER_HZ 32768

/* Takes over timer 1 as a free-running 32768 Hz counter starting at 0. */
void timer_init(void);
/* Gives timer 1 back in the state timer_init found it. */
void timer_restore(void);

uint32_t timer_ticks(void);
uint32_t timer_ms(void);
void timer_delay_ms(uint24_t ms);

#endif
