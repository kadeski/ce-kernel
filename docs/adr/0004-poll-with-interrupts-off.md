# 0004. Poll the hardware with interrupts off

Status: accepted
Date: 2026-10-21

## Context
On revision I and later hardware the CPU's vectored interrupt mode (IM 2) doesn't work
(CEmu: `asic.im2 = revision < ASIC_REV_I`), so every interrupt goes through the handler at
`0x38`, which is TI's code. Owning interrupts would break [0002](0002-takeover-kernel.md).

## Decision
Interrupts stay off. The kernel polls the keypad, timer and ON key. There are no threads:
anything that loops calls `kernel_poll()` (`src/kernel/events.h`) on every pass, which runs
the timer events that are due and returns true once ON has been pressed and released, and the
loop stops when it does.

## Consequences
- ON can stop any command that polls, the way Ctrl-C does, so a stuck command can be left
  without the reset button.
- A command that doesn't poll holds up the timer events (such as the status bar's clock) until
  it returns. That is fine for commands that finish on their own.
- Timer callbacks run to completion on the caller's stack, so they must be short and must
  never call `kernel_poll()`.
- Threads with their own stacks would not fit in the TI-OS build's 4 KB of stack anyway;
  commands keep their state in static buffers.
