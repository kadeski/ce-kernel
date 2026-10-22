#include "events.h"
#include "../drivers/keypad.h"
#include "../drivers/timer.h"

#include <stddef.h>

/* ON has to stay up this long to count as released, so contact bounce isn't
 * taken for a second press. */
#define ON_RELEASE_TICKS (TIMER_HZ / 50)        /* 20 ms */

struct event {
    void (*fn)(void);                   /* NULL: free slot */
    uint32_t due;                       /* in timer ticks */
    uint32_t period;                    /* in ticks; 0 runs once */
};

static struct event events[EVENT_MAX];

/* The largest uint24_t ms is under 2^30 ticks, inside the 2^31 that due()
 * allows. The multiply is split so it can't overflow 32 bits. */
static uint32_t ms_to_ticks(uint24_t ms)
{
    return (uint32_t)(ms / 1000) * TIMER_HZ + (uint32_t)(ms % 1000) * TIMER_HZ / 1000;
}

/* A signed difference stays right when the 32-bit tick counter wraps (about
 * every 36 hours), as long as the deadline is less than 2^31 ticks away. */
static bool due(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

static bool schedule(void (*fn)(void), uint24_t ms, bool repeat)
{
    struct event *slot = NULL;
    for (uint8_t i = 0; i < EVENT_MAX; i++) {
        if (events[i].fn == fn) {
            slot = &events[i];
            break;
        }
        if (!events[i].fn && !slot)
            slot = &events[i];
    }
    if (!slot)
        return false;
    uint32_t ticks = ms_to_ticks(ms);
    slot->fn = fn;
    slot->due = timer_ticks() + ticks;
    slot->period = repeat ? ticks : 0;
    return true;
}

bool event_every(uint24_t ms, void (*fn)(void))
{
    return schedule(fn, ms, true);
}

bool event_after(uint24_t ms, void (*fn)(void))
{
    return schedule(fn, ms, false);
}

void event_cancel(void (*fn)(void))
{
    for (uint8_t i = 0; i < EVENT_MAX; i++)
        if (events[i].fn == fn)
            events[i].fn = NULL;
}

static void run_due(void)
{
    uint32_t now = timer_ticks();
    for (uint8_t i = 0; i < EVENT_MAX; i++) {
        struct event *e = &events[i];
        if (!e->fn || !due(now, e->due))
            continue;
        void (*fn)(void) = e->fn;
        /* The slot is updated before the call, so fn can reschedule or
         * cancel itself. A repeat skips the runs it missed. */
        if (e->period)
            e->due += ((now - e->due) / e->period + 1) * e->period;
        else
            e->fn = NULL;
        fn();
    }
}

bool kernel_poll(void)
{
    run_due();
    if (!keypad_on_pressed())
        return false;

    /* Wait for ON to come back up, keeping the timers running meanwhile. */
    uint32_t up_since = timer_ticks();
    for (;;) {
        run_due();
        uint32_t now = timer_ticks();
        if (keypad_on_pressed())
            up_since = now;
        else if (now - up_since >= ON_RELEASE_TICKS)
            return true;
    }
}
