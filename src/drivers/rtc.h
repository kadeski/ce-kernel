#ifndef DRIVERS_RTC_H
#define DRIVERS_RTC_H

#include <stdbool.h>
#include <stdint.h>

struct rtc_time {
    uint8_t hours;                      /* 0-23 */
    uint8_t minutes;
    uint8_t seconds;
};

/* The kernel only reads the real-time clock: TI-OS has started it (the bare
 * ROM's cold init does on the emulator), so these change and restore
 * nothing. */
void rtc_init(void);
void rtc_restore(void);

/* Reads the time of day. Returns false if the clock isn't running. */
bool rtc_read(struct rtc_time *t);

#endif
