#ifndef KERNEL_EVENTS_H
#define KERNEL_EVENTS_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Timer events: callbacks that kernel_poll() runs once their time has come.
 * They run to completion on the caller's stack, so they must return quickly
 * and must not call kernel_poll() themselves. Each callback has at most one
 * event: scheduling it again replaces the old one.
 */

#define EVENT_MAX 8

/* Runs fn every ms milliseconds. Repeats keep their phase; runs missed while
 * nothing polled are skipped, not queued. Returns false if the table is full. */
bool event_every(uint24_t ms, void (*fn)(void));
/* Runs fn once, ms milliseconds from now. Returns false if the table is full. */
bool event_after(uint24_t ms, void (*fn)(void));
/* Removes fn's event, if it has one. */
void event_cancel(void (*fn)(void));

/*
 * Runs the timer events that are due and checks the ON key. Anything that
 * loops calls this on every pass and stops when it returns true. It returns
 * true once ON has been pressed and released again, so the same press isn't
 * seen twice.
 */
bool kernel_poll(void);

#endif
