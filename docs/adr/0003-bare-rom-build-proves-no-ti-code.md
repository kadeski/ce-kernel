# 0003. A bare ROM build proves the kernel calls no TI code

Status: accepted
Date: 2026-10-21

## Context
Under TI-OS, a call into TI's code works and nothing notices. A library routine from the
toolchain's `_os` variants, or its float routines, can bring one in without any change to the
kernel's own source. The promise of [0002](0002-takeover-kernel.md) needs a check that fails.

## Decision
Build the same kernel a second way, as a complete 4 MB flash image (`bin/kernel.rom`) that
boots in CEmu with no TI code in it at all. `rom/` holds its reset code, the cold hardware
setup TI's boot code and OS would do, and its linker script. It links only the toolchain's
pure `libc.a` and `libcrt.a`, and `rom/check_no_os_calls.py` fails the link if any `call`,
`jp` or `rst` targets an address below `0x030000`, where only the reset vector and the model
header are. `make smoke` and CI run the tests on this image.

## Consequences
- A change that makes the kernel need TI code fails `make rom` (and CI), not just a run on
  the calculator.
- Some toolchain features are out, starting with `float` ([0005](0005-exact-fractions-not-float.md)).
- `rom/` has to do the panel's SPI setup, LCD timing, keypad scan setup and clock start that
  TI-OS has already done on a calculator. Those commands live only in `rom/`.
- The image can't run on a calculator: the boot code rejects it, and Python models need an
  extra SPI sequence because the ARM coprocessor shares the bus.
- It runs its code from flash, which is slow in CEmu, so the smoke test waits longer for the
  heavier tools.
- The TI-OS loader's entry and exit path is only tested by `make smoke-tios`, which needs a
  ROM dumped from a calculator and so can't run in CI.
