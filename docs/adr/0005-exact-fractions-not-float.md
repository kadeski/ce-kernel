# 0005. Exact fractions instead of floating point

Status: accepted
Date: 2026-10-24

## Context
The toolchain's `float` routines call the boot code (`0x0270`-`0x0290` in toolchain v15.0),
so the ROM check of [0003](0003-bare-rom-build-proves-no-ti-code.md) rejects them. The
toolchain's `printf` family may pull them in too. A 64-bit `long double` from libsoftfloat is
pure and would work once added to the ROM link, but its arithmetic alone about doubles the
kernel's size. 32- and 64-bit integer arithmetic is pure.

## Decision
The calculator and the study tools work in exact fractions: `src/core/frac.h` keeps a reduced
fraction of two `int64_t`, and every operation that can overflow goes through the checked
helpers in `src/core/imath.h`. Decimals are shown by long division. Text is formatted by
`ui.c`'s own printf-style formatter, never the `printf` family.

## Consequences
- Answers are exact, and a result too big to hold is an error message, never a wrong number.
- Numbers go up to 2^63-1. Irrational results need special handling (the master theorem tool
  uses a fixed-point `log2`).
- 64-bit math is a runtime call per operation on the eZ80, which makes each tool roughly 1 KB
  and the tools about half of the kernel's size.
