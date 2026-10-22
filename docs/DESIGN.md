# ce-kernel design

## Goal and constraint

The goal is open-source system software that owns a TI-84 Plus CE Python Edition. The constraint is the boot code: it only starts an OS signed with TI's RSA-2048 key.

- Boot-code patches (Cauldron, BootSwap) work only on hardware before revision M with boot code 5.3.6 or older, and BootSwap stopped working on OS 5.6.1+ because TI now uses flash sector protection.
- Every Python Edition is revision M or later. The open-source BOS project lists the Python Edition and revision M+ as unsupported for that reason.
- What is still possible: running native code from TI-OS. OS 5.5+ removed assembly programs, and the arTIfiCE jailbreak restores them (v2.1 for OS 5.5 to 5.8.4, v3 for 5.8.5).

So ce-kernel is a **takeover kernel**: TI-OS starts it as an assembly program, and from then until exit the kernel drives the hardware itself and calls no TI code. If a boot-code exploit for revision M+ appears, a third loader can put the same kernel in place of TI-OS.

## Two loaders, one kernel

```
src/kernel, src/drivers   shared; never call TI-OS or the boot code
src/tios/main.c           TI-OS loader  -> bin/KERNEL.8xp
rom/                      bare ROM loader -> bin/kernel.rom (CEmu only)
```

`kmain(const struct boot_info *)` is the only entry point. The loader says how it was started and whether there is anything to return to.

### TI-OS loader (real calculator)

Built with the toolchain's stock makefile and startup code (`crt0`). Before `main()`, crt0 turns off the run indicator and the USB timer and disables interrupts. After `main()` returns, it restores flash wait states, timer bits and TI-OS flags and redraws the home screen. Those calls are the handoff with TI-OS, not part of the kernel.

The kernel saves and restores everything it changes: LCD control and base address, timer 1, and the ON-key latch setting. It only reads the real-time clock. It waits for all keys to be released before returning so TI-OS doesn't see a stray key press. RAM is not cleared on exit.

Memory while running:

| Range | Use |
|---|---|
| `D1A87F`- | the program itself (TI-OS user memory) |
| `D052C6`-`D13FD8` | `.bss` and free memory (TI-OS scratch area, the toolchain's default) |
| TI-OS stack | about 4 KB, so buffers go in `.bss`, not on the stack |
| `D40000`-`D52C00` | 8bpp framebuffer, 76,800 bytes |

### Bare ROM loader (emulator)

A complete 4 MB flash image: reset vector at 0, a model header at `0x020000` (CEmu won't load an image without it), code from `0x030000`. `rom/boot.c` does what TI's boot code and OS normally do: wake the LCD panel over SPI, program the LCD controller timing, set the keypad matrix size and scan timing, set the backlight, start the real-time clock. It then calls the same `kmain`.

It is linked only against the toolchain's pure `libc.a` and `libcrt.a`, never the `_os` variants, and `rom/check_no_os_calls.py` fails the build if any code calls or jumps below `0x030000`. So a passing ROM build is also proof that the kernel needs no TI code.

That check rules out the toolchain's `float`: its routines call the boot code (`0x0270`-`0x0290` in toolchain v15.0). 32- and 64-bit integer arithmetic is pure, so `calc` uses 32-bit integers and checks each step in 64 bits. A 64-bit `long double` from the toolchain's libsoftfloat also works once it is added to the ROM link, but its arithmetic alone about doubles the kernel's size.

This image can't run on a real calculator: the boot code rejects it, and Python models need an extra SPI sequence because the ARM coprocessor shares the bus.

## Event loop

There are no threads. The shell's loop, and the loop of any command that keeps running, call `kernel_poll()` (`src/kernel/events.h`) on every pass. It runs the timer events that are due, such as the status bar's redraw every second, and returns true once ON has been pressed and released, which tells the command to stop.

- Timer events are a fixed table of `EVENT_MAX` callbacks, each run once or repeating. They run to completion on the stack of whoever called `kernel_poll()`, so they must be short and must not call it themselves.
- Threads with their own stacks would not fit: the TI-OS build has about 4 KB of stack. Commands keep their state in static buffers, and a command that loops keeps its loop and calls `kernel_poll()` rather than being rewritten as callbacks.
- Deadlines are kept in timer ticks and compared with a signed difference, so they stay right when the 32-bit counter wraps (about every 36 hours at 32,768 Hz). Milliseconds would not: `timer_ms()` drops back to 0 at that point instead of wrapping at 2^32.
- A command that doesn't poll holds up the timer events until it returns. That is fine for commands that finish on their own, like `ls` or `peek`.

## Rules the kernel follows

- **No TI-OS calls.** Nothing under `src/kernel` or `src/drivers` may include `ti/*` or `tice.h` headers.
- **Polling, interrupts off.** On revision I and later hardware the CPU's vectored interrupt mode (IM 2) doesn't work (CEmu: `asic.im2 = revision < ASIC_REV_I`), so every interrupt goes through TI's handler at `0x38`. Owning interrupts would mean running TI code, so the kernel polls the keypad, timer and ON key instead.
- **No SPI on real hardware.** On a calculator TI-OS has already set up the panel; switching to 8bpp is only an LCD controller register change. Panel SPI commands live in `rom/` only.
- **Only read plain memory.** `peek` refuses I/O ranges because reading some registers clears status bits.
- **Loops call `kernel_poll()`.** Anything that loops calls it on every pass and stops when it returns true. So ON stops a running command and returns to the prompt, the way Ctrl-C does, and a stuck command can be escaped without the reset button. At the prompt, ON exits to TI-OS (the bare ROM ignores it there).

## Hardware notes

Register addresses are in `src/drivers/hw.h`, taken from the toolchain headers and checked against the CEmu core.

- **LCD:** ARM PL111 controller at `E30000`. It scans 240-pixel lines x 320 lines because the panel is mounted rotated; the panel's own settings make a row-major 320x240 framebuffer appear the right way up. 8bpp mode uses a 256-entry palette of 1555 colours (with the BGR bit set, red is bits 10-14).
- **Keypad:** controller at `F50000`. The kernel uses scan-once mode (as the toolchain's keypadc does) and keeps whatever row timing is already set. Seven 8-bit groups; key code = group x 8 + bit.
- **Timer:** timer 1 counts the 32,768 Hz crystal upwards. 32-bit reads are repeated until two agree, because the CPU reads it one byte at a time.
- **Real-time clock:** seconds, minutes and hours (0-23) at `F30000`, `F30004` and `F30008`, a 16-bit day count at `F3000C`, control at `F30020`. Control bit 0 makes it count and bit 7 makes it copy the count into the time registers (the toolchain calls it "unfreeze"); bits 1-5 enable its interrupts and bit 6 loads a new time. The time registers hold that copy, taken once a second about half a second after the count ticks, so they are never more than a second behind. The kernel reads seconds, minutes, hours and seconds again, and retries if the seconds changed, so an update between reads can't mix two times.
  - Reading has no side effects: the interrupt status at `F30034` is cleared only by writing 1s to it, and the hardware clears the load bit itself when a load finishes. This is from CEmu's model (`core/realclock.c`), not checked on hardware.
  - Writing control with bit 6 set starts a load from the load registers, which hold the last time that was set. So the kernel never writes control on a calculator, not even to restore it: if TI-OS was still loading a time when the kernel started, writing back the value read then would set the clock back.
  - TI-OS 5.8.1 leaves control at `95` in CEmu (counting, copying, minute and day interrupts on). At reset it is 0 and the count is day 0, 00:00:00, so the bare ROM sets bits 0 and 7 and the clock counts from midnight.
  - The status bar shows 24-hour time, whatever TI-OS's 12/24-hour setting is.
- **Archive:** TI-OS keeps archived variables in the 64 KB flash sectors from `0C0000` up to `3B0000`; the sector at `3B0000` holds TI data even when the archive is empty. This was seen in CEmu with OS 5.8.1, not taken from TI documentation. A sector in use starts with `F0`; entries follow it and never cross into the next sector, so a variable too big for the space left starts a new sector. Each entry is: flag (`FC` valid, `F0` deleted), a 16-bit size of the rest, type, type2, version, the entry's own 24-bit address, name length, name, then the data as TI-OS stores it (programs and AppVars start with their own 16-bit size). The kernel only reads it, and skips any sector or entry it doesn't recognise. A real calculator also has flash apps, which the dumped ROM lacks, so how they sit in flash hasn't been checked.
- **ON key:** bit 0 of `F00020`. TI-OS may leave it latched (stays set after release), so the kernel turns latching off and restores the old setting on exit.

## Testing

`make smoke` boots `bin/kernel.rom` in `cemu-headless`, types commands and takes screenshots. `tests/smoke.py` reads each screenshot back as text by matching 8x8 cells against the kernel's own font, then checks the output with regular expressions.

`make smoke-tios` does the same for the TI-OS loader. It boots TI-OS from a ROM dumped from the user's own calculator, sends arTIfiCE, `KERNEL.8xp` and a small AppVar for the archive (twice, so the first copy is left behind as a deleted entry), runs a TI-Basic program that sets TI-OS's clock to 13:45, and starts the kernel from arTIfiCE's launcher. The kernel's status bar must then show 13:45. Both tests hold a key in `keys` for 61 seconds, so the clock's minute must change while a command runs. After the shell checks, it checks that each way out (`exit`, `2nd` `mode`, ON) returns to the launcher and that TI-OS still computes and starts programs afterwards. TI-OS screens can't be read with the kernel's font, so they are compared pixel for pixel with screenshots from earlier in the same run, below the status bar (its battery icon animates). The ROM contains TI's code and arTIfiCE isn't ours, so neither is in the repo: the test takes both as paths and isn't part of `make smoke`.

One thing it can't check is the wait for key release on exit: arTIfiCE's launcher ignores a key still held when the kernel returns, so the test passes without the wait.

## Next steps

Planned work is tracked as GitHub issues with the [`roadmap` label](https://github.com/kadeski/ce-kernel/issues?q=label%3Aroadmap).
