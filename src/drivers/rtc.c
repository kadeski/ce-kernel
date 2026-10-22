#include "rtc.h"
#include "hw.h"

void rtc_init(void)
{
}

void rtc_restore(void)
{
    /* Nothing to put back. Don't write the control register "just in case":
     * if TI-OS was still loading a new time when the kernel started, the
     * value read then has the load bit set, and writing it starts the load
     * again from the old values, so the clock would jump back. */
}

bool rtc_read(struct rtc_time *t)
{
    uint8_t running = RTC_CTRL_ENABLE | RTC_CTRL_LATCH;
    if ((RTC_CONTROL & running) != running)
        return false;

    /* The RTC updates all three registers at once, but the CPU reads them one
     * at a time: retry if the seconds changed in between, or an update could
     * pair 12:59's minutes with 13:00's hour. */
    uint8_t s;
    do {
        s = RTC_SECONDS;
        t->minutes = RTC_MINUTES;
        t->hours = RTC_HOURS;
    } while (s != RTC_SECONDS);
    t->seconds = s;
    return true;
}
