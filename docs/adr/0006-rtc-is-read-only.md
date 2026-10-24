# 0006. The real-time clock is read-only on a calculator

Status: accepted
Date: 2026-10-21

## Context
The kernel's drivers save the hardware state they change in `*_init` and put it back in
`*_restore`. For the real-time clock that would mean writing its control register back on
exit. Writing control with bit 6 set starts a load from the load registers, which hold the
last time that was set. If TI-OS was still loading a time when the kernel started, writing
back the value read then would set the clock back. (This is from CEmu's model,
`core/realclock.c`, not checked on hardware.)

## Decision
On a calculator the kernel only reads the clock and never writes `RTC_CONTROL`, not even to
restore it. `rtc_init()` and `rtc_restore()` are deliberately empty. The bare ROM, where
nothing has started the clock, sets bits 0 and 7 in `rom/boot.c`.

## Consequences
- The kernel shows whatever time TI-OS set and can't set the clock itself.
- The save-and-restore rule still holds: on a calculator the clock driver changes no state,
  so it has nothing to restore.
