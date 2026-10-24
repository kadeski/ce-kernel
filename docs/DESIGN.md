# ce-kernel design

How the kernel works right now. Keep this current; history lives in git, and the reasons for the big decisions in [adr/](adr/).

ce-kernel is a **takeover kernel**: TI-OS starts it as an assembly program, and from then until exit the kernel drives the hardware itself and calls no TI code. A replacement OS can't be installed on revision M+ hardware, which includes every Python Edition ([ADR 0002](adr/0002-takeover-kernel.md)).

## Layers

```
src/tios/  rom/      loaders: fill in a boot_info and call kmain
  │
src/kernel/          kmain, the menu and its screens (ui, console, shell, calc, notes),
  │                  kernel_poll() and timer events, the archive reader, keymap, font
  ├── src/discrete/  the study tools, one section per file, listed by the menu
  ├── src/drivers/   the hardware: LCD, keypad, timer, real-time clock, register map
  └── src/core/      pure math: checked 64-bit integers, exact fractions, expressions
```

`src/core/` and `src/drivers/` are the bottom: they include nothing from the layers above them. Each layer has a rule:

- `src/core/` includes nothing outside itself but the C library: no hardware, no screen. It is plain C that could be built and tested anywhere.
- `src/drivers/` includes only `src/drivers/`. Each driver that changes hardware state saves it in `*_init` and puts it back in `*_restore`.
- `src/discrete/` builds on `src/kernel/`'s forms (`ui.h`) and `src/core/`. Each section is a table of tools, and `home[]` in `src/kernel/menu.c` lists the sections.
- Only the loaders know about TI-OS. Nothing else includes `ti/*` or `tice.h`.

`make` builds `bin/KERNEL.8xp` from everything under `src/`, with objects in `build/tios/`. `make rom` builds `bin/kernel.rom` from `src/` minus `src/tios/`, plus `rom/`, with objects in `build/rom/`. `rom/` stays outside `src/` because the toolchain's stock makefile compiles every file under `src/` into the TI-OS program. How this maps onto the shared project template is in [ADR 0001](adr/0001-standard-layout.md).

## Two loaders, one kernel

`kmain(const struct boot_info *)` is the only entry point. The loader says how it was started and whether there is anything to return to.

### TI-OS loader (real calculator)

Built with the toolchain's stock makefile and startup code (`crt0`). The program is about 84 KB, more than the 64 KB a TI-OS program variable can hold, so it is built compressed (`COMPRESSED = YES`): `KERNEL.8xp` stores about 44 KB, and the toolchain's extractor unpacks it into user memory before anything else runs. Unpacked, it needs that much free RAM. Before `main()`, crt0 turns off the run indicator and the USB timer and disables interrupts. After `main()` returns, it restores flash wait states, timer bits and TI-OS flags and redraws the home screen. Those calls are the handoff with TI-OS, not part of the kernel.

The kernel saves and restores everything it changes: LCD control and base address, timer 1, and the ON-key latch setting. It only reads the real-time clock. It waits for all keys to be released before returning so TI-OS doesn't see a stray key press. RAM is not cleared on exit.

Memory while running:

| Range | Use |
|---|---|
| `D1A87F`- | the program itself, unpacked (TI-OS user memory) |
| `D052C6`-`D13FD8` | `.bss` and free memory (TI-OS scratch area, the toolchain's default) |
| TI-OS stack | about 4 KB, so buffers go in `.bss`, not on the stack |
| `D40000`-`D52C00` | 8bpp framebuffer, 76,800 bytes |

### Bare ROM loader (emulator)

A complete 4 MB flash image: reset vector at 0, a model header at `0x020000` (CEmu won't load an image without it), code from `0x030000`. `rom/boot.c` does what TI's boot code and OS normally do: wake the LCD panel over SPI, program the LCD controller timing, set the keypad matrix size and scan timing, set the backlight, start the real-time clock. It then calls the same `kmain`.

It is linked only against the toolchain's pure `libc.a` and `libcrt.a`, never the `_os` variants, and `rom/check_no_os_calls.py` fails the build if any code calls or jumps below `0x030000`. So a passing ROM build is also proof that the kernel needs no TI code ([ADR 0003](adr/0003-bare-rom-build-proves-no-ti-code.md)).

That check rules out the toolchain's `float`, whose routines call the boot code, so `calc` and the tools work in exact fractions of 64-bit integers and show decimals by long division ([ADR 0005](adr/0005-exact-fractions-not-float.md)).

The bare ROM runs its code straight from flash and is slow in CEmu: the binomial tool with n = 20 takes 1.5 to 3 seconds there.

This image can't run on a real calculator: the boot code rejects it, and Python models need an extra SPI sequence because the ARM coprocessor shares the bus.

## Event loop

There are no threads ([ADR 0004](adr/0004-poll-with-interrupts-off.md)). The shell's loop, and the loop of any command that keeps running, call `kernel_poll()` (`src/kernel/events.h`) on every pass. It runs the timer events that are due, such as the status bar's redraw every second, and returns true once ON has been pressed and released, which tells the command to stop.

- Timer events are a fixed table of `EVENT_MAX` callbacks, each run once or repeating. They run to completion on the stack of whoever called `kernel_poll()`, so they must be short and must not call it themselves.
- Threads with their own stacks would not fit: the TI-OS build has about 4 KB of stack. Commands keep their state in static buffers, and a command that loops keeps its loop and calls `kernel_poll()` rather than being rewritten as callbacks.
- Deadlines are kept in timer ticks and compared with a signed difference, so they stay right when the 32-bit counter wraps (about every 36 hours at 32,768 Hz). Milliseconds would not: `timer_ms()` drops back to 0 at that point instead of wrapping at 2^32.
- A command that doesn't poll holds up the timer events until it returns. That is fine for commands that finish on their own, like `ls` or `peek`.

## Menu and study tools

`kmain` starts `menu_run()`, a numbered list of the tool sections, Notes, the calculator and the shell. Every screen is built from `src/kernel/ui.c`:

- **Lists** (`ui_list`): a digit picks an entry in either typing layer, or UP, DOWN and ENTER. CLEAR goes back.
- **Forms** (`ui_tool`): one field per line, typed with the shell's line editor. UP and DOWN move between fields, and ENTER reads them all and runs the tool, or first moves to a required field left empty. Number fields take expressions, so `1/6` or `0.8^6*0.2^4` work. The five keys under the screen are soft keys: Y= shows the steps, WINDOW the notes, GRAPH goes back.
- **Views** (`ui_view`): a scrolling list of lines from a callback, so a tool's steps (a 200-line buffer in `.bss`) and a notes file read in place from flash use the same code.

Each tool's `compute` function gets the parsed fields and prints with `ui_answer`, `ui_say`, `ui_check`, `ui_error` (the form) and `ui_step` (the steps). Their formatter is ui.c's own: the toolchain's `printf` might bring in float code. Answers are exact: `src/core/frac.h` keeps a reduced fraction of two `int64_t`, and every operation checks for overflow, so an answer too big to hold is an error instead of a wrong number. 64-bit math on the eZ80 is a runtime call per operation, with each value passed as three 24-bit words, which is why the tools take about half of the 84 KB.

The toolchain's clang 19 miscompiled one function that returned an `int64_t` straight from another call: it swapped the top two thirds of the value (the 24-bit middle and 16-bit top). That helper now returns through a pointer. The smoke test's inverse and RSA checks caught it.

**Notes.** TI-OS stores a Python file as an AppVar. Its data is the AppVar's 16-bit size, the magic `PYCD` or `PYSC`, metadata records (a 7-bit-per-byte length of 1 or 2 bytes, then that many bytes, the first being the record's type; type 1 is the file name), a `0` byte, and the script, maybe after a UTF-8 BOM and with CRLF line ends. That layout comes from tivars_lib_cpp's Python AppVar handler, not from TI, and hasn't been checked against a file from the calculator. The Notes entry lists archived Python files whose names end in `R`, notes made of `#` comment lines; the viewer drops each line's `# ` and shows lines starting `==` as headings. It indexes where the lines start and reads the text from flash, so a 241-line file costs 1.5 KB of RAM.

## Rules the kernel follows

- **No TI-OS calls.** Nothing under `src/core`, `src/drivers`, `src/kernel` or `src/discrete` may include `ti/*` or `tice.h` headers.
- **Polling, interrupts off.** IM 2 doesn't work on revision I and later hardware, so owning interrupts would mean running TI's handler at `0x38`. The kernel polls the keypad, timer and ON key instead ([ADR 0004](adr/0004-poll-with-interrupts-off.md)).
- **No SPI on real hardware.** On a calculator TI-OS has already set up the panel; switching to 8bpp is only an LCD controller register change. Panel SPI commands live in `rom/` only.
- **Only read plain memory.** `peek` refuses I/O ranges because reading some registers clears status bits.
- **Loops call `kernel_poll()`.** Anything that loops calls it on every pass and stops when it returns true. So ON stops a running command and returns to the prompt, the way Ctrl-C does, and a stuck command can be escaped without the reset button. At the prompt or in a tool, ON goes back to the menu; at the menu it exits to TI-OS (the bare ROM ignores it there).

## Growing

- **A tool** is a row in its section's `tools[]` table in `src/discrete/`. **A section** is a new file there, with an `extern` in `discrete.h` and a row in `home[]` in `src/kernel/menu.c`. The menu lists up to 9 entries, and it has 8.
- **A shell command** is a row in `commands[]` in `src/kernel/shell.c`.
- **A driver** is a file pair in `src/drivers/`, with its `*_init` and `*_restore` called from `kmain`.
- **Pure logic** with no screen or hardware goes in `src/core/`.
- **A third loader**, such as one for a boot-code exploit, is a folder beside `src/tios/` and `rom/` that fills in a `boot_info` and calls `kmain`.
- **`src/kernel/`** is past the template's guide of about 10 files. The next split is `src/ui/` for `console`, `font8x8`, `keymap` and `ui`. What is in the way: `ui.c` calls `notes_open()` for the WINDOW key, which would make `ui/` include a screen above it, and `tests/smoke.py` reads `src/kernel/font8x8.c` by path.

## Hardware notes

Register addresses are in `src/drivers/hw.h`, taken from the toolchain headers and checked against the CEmu core.

- **LCD:** ARM PL111 controller at `E30000`. It scans 240-pixel lines x 320 lines because the panel is mounted rotated; the panel's own settings make a row-major 320x240 framebuffer appear the right way up. 8bpp mode uses a 256-entry palette of 1555 colours (with the BGR bit set, red is bits 10-14).
- **Keypad:** controller at `F50000`. The kernel uses scan-once mode (as the toolchain's keypadc does) and keeps whatever row timing is already set. Seven 8-bit groups; key code = group x 8 + bit.
- **Timer:** timer 1 counts the 32,768 Hz crystal upwards. 32-bit reads are repeated until two agree, because the CPU reads it one byte at a time.
- **Real-time clock:** seconds, minutes and hours (0-23) at `F30000`, `F30004` and `F30008`, a 16-bit day count at `F3000C`, control at `F30020`. Control bit 0 makes it count and bit 7 makes it copy the count into the time registers (the toolchain calls it "unfreeze"); bits 1-5 enable its interrupts and bit 6 loads a new time. The time registers hold that copy, taken once a second about half a second after the count ticks, so they are never more than a second behind. The kernel reads seconds, minutes, hours and seconds again, and retries if the seconds changed, so an update between reads can't mix two times.
  - Reading has no side effects: the interrupt status at `F30034` is cleared only by writing 1s to it, and the hardware clears the load bit itself when a load finishes. This is from CEmu's model (`core/realclock.c`), not checked on hardware.
  - Writing control with bit 6 set starts a load from the load registers, which hold the last time that was set. So the kernel never writes control on a calculator, not even to restore it: if TI-OS was still loading a time when the kernel started, writing back the value read then would set the clock back ([ADR 0006](adr/0006-rtc-is-read-only.md)).
  - TI-OS 5.8.1 leaves control at `95` in CEmu (counting, copying, minute and day interrupts on). At reset it is 0 and the count is day 0, 00:00:00, so the bare ROM sets bits 0 and 7 and the clock counts from midnight.
  - The status bar shows 24-hour time, whatever TI-OS's 12/24-hour setting is.
- **Archive:** TI-OS keeps archived variables in the 64 KB flash sectors from `0C0000` up to `3B0000`; the sector at `3B0000` holds TI data even when the archive is empty. This was seen in CEmu with OS 5.8.1, not taken from TI documentation. A sector in use starts with `F0`; entries follow it and never cross into the next sector, so a variable too big for the space left starts a new sector. Each entry is: flag (`FC` valid, `F0` deleted), a 16-bit size of the rest, type, type2, version, the entry's own 24-bit address, name length, name, then the data as TI-OS stores it (programs and AppVars start with their own 16-bit size). The kernel only reads it, and skips any sector or entry it doesn't recognise. A real calculator also has flash apps, which the dumped ROM lacks, so how they sit in flash hasn't been checked.
- **ON key:** bit 0 of `F00020`. TI-OS may leave it latched (stays set after release), so the kernel turns latching off and restores the old setting on exit.
- **ARM coprocessor (Python models only):** a SAMD21 (48 MHz Cortex-M0+) running TI's Python firmware. It shares the SPI bus with the LCD panel, and bit 4 of control port `0x09` switches the bus between them. This comes from CEmu's `core/control.c`, which doesn't emulate the chip itself (it answers every transfer with a fixed byte), so nothing that talks to it can be tested in `make smoke`. Wikipedia and TI-Planet also describe a 115,200 baud UART link; which link carries the Python protocol is unknown, and the protocol isn't public. The kernel leaves port `0x09` alone: in CEmu, clearing its bit 2 resets the panel and one value puts the CPU to sleep.

## Testing

`make smoke` boots `bin/kernel.rom` in `cemu-headless`, works through the menu, a tool form per tool with worked examples, the shell and `calc`, and takes screenshots. `tests/smoke.py` reads each screenshot back as text by matching 8x8 cells against the kernel's own font, then checks the output with regular expressions.

`make smoke-tios` does the same for the TI-OS loader. It boots TI-OS from a ROM dumped from the user's own calculator, sends arTIfiCE, `KERNEL.8xp`, a small AppVar for the archive (twice, so the first copy is left behind as a deleted entry) and a Python notes file built in the tivars_lib_cpp layout, runs a TI-Basic program that sets TI-OS's clock to 13:45, and starts the kernel from arTIfiCE's launcher. The kernel's status bar must then show 13:45. Both tests hold a key in `keys` for 61 seconds, so the clock's minute must change while a command runs. It also opens the notes file from the Notes entry and from a tool's WINDOW key. After the shell checks, it checks that each way out (`2nd` `mode` and ON at the menu, `exit` in the shell) returns to the launcher and that TI-OS still computes and starts programs afterwards. TI-OS screens can't be read with the kernel's font, so they are compared pixel for pixel with screenshots from earlier in the same run, below the status bar (its battery icon animates). The ROM contains TI's code and arTIfiCE isn't ours, so neither is in the repo: the test takes both as paths and isn't part of `make smoke`.

One thing it can't check is the wait for key release on exit: arTIfiCE's launcher ignores a key still held when the kernel returns, so the test passes without the wait.

## Next steps

Planned work is tracked as GitHub issues with the [`roadmap` label](https://github.com/kadeski/ce-kernel/issues?q=label%3Aroadmap). Plans for work in progress go in [plans/](plans/).
