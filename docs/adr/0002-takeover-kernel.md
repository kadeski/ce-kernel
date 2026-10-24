# 0002. A takeover kernel, not a replacement OS

Status: accepted
Date: 2026-10-21

## Context
The goal is open-source system software that owns a TI-84 Plus CE Python Edition. The boot
code only starts an OS signed with TI's RSA-2048 key.

- Boot-code patches (Cauldron, BootSwap) work only on hardware before revision M with boot
  code 5.3.6 or older, and BootSwap stopped working on OS 5.6.1+ because TI now uses flash
  sector protection.
- Every Python Edition is revision M or later. The open-source BOS project lists the Python
  Edition and revision M+ as unsupported for that reason.
- Native code can still run from TI-OS. OS 5.5+ removed assembly programs, and the arTIfiCE
  jailbreak restores them (v2.1 for OS 5.5 to 5.8.4, v3 for 5.8.5).

## Decision
TI-OS starts the kernel as an assembly program (`KERNEL.8xp`, launched from arTIfiCE). From
then until exit the kernel drives the hardware itself and calls no TI code. The kernel's only
entry point, `kmain(const struct boot_info *)`, doesn't depend on how it was started, so if a
boot-code exploit for revision M+ appears, a third loader can put the same kernel in place of
TI-OS.

## Consequences
- The calculator needs arTIfiCE, and an OS update can break it, so the OS must not be updated.
- The kernel saves every piece of hardware state it changes and restores it on exit, and waits
  for keys to be released so TI-OS doesn't see a stray press.
- It lives in TI-OS's memory map: user RAM for the program, about 4 KB of stack, and a 64 KB
  limit on a program variable, which is why `KERNEL.8xp` is stored compressed.
- It can't own interrupts without running TI's handler ([0004](0004-poll-with-interrupts-off.md)).
- "Calls no TI code" needs a check of its own ([0003](0003-bare-rom-build-proves-no-ti-code.md)).
