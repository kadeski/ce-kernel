#include "timer.h"
#include "hw.h"

static uint16_t saved_control;

void timer_init(void)
{
    saved_control = TMR_CONTROL;
    TMR_CONTROL = saved_control & ~(TMR1_ENABLE | TMR1_INT);
    TMR1_COUNTER = 0;
    TMR1_RELOAD = 0;
    TMR_CONTROL = (saved_control & ~TMR1_INT) | TMR1_ENABLE | TMR1_CRYSTAL | TMR1_COUNTUP;
}

void timer_restore(void)
{
    TMR_CONTROL = saved_control;
}

uint32_t timer_ticks(void)
{
    /* The CPU reads the 32-bit counter one byte at a time while it keeps
     * counting, so retry until two reads agree. */
    uint32_t a, b;
    do {
        a = TMR1_COUNTER;
        b = TMR1_COUNTER;
    } while (a != b);
    return a;
}

uint32_t timer_ms(void)
{
    uint32_t t = timer_ticks();
    return (t >> 15) * 1000 + (((t & 0x7FFF) * 1000) >> 15);
}

void timer_delay_ms(uint24_t ms)
{
    uint32_t start = timer_ticks();
    uint32_t wait = ((uint32_t)ms * TIMER_HZ + 999) / 1000;
    while (timer_ticks() - start < wait)
        ;
}
