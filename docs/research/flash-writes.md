# Flash Write Feasibility on the TI-84 Plus CE Python Edition

**Date:** 2026-11-16
**Status:** Research complete; blocking issue identified

## Summary

Writing to flash on the TI-84 Plus CE Python Edition (hardware revision M+)
without calling TI code is **not feasible under the current project rules**.
The ASIC enforces a hardware privilege boundary: flash writes require unlocking
a protection gate via I/O ports, and those port writes are accepted only when
the CPU is executing code from a "privileged" address range within flash
(typically boot code and OS pages below the archive region). Code running from
user RAM at 0xD00000+ is classified as unprivileged by the hardware; any
attempt to write to the flash-control ports from that address triggers a
non-maskable interrupt (NMI) that resets the calculator.

This is a hard hardware restriction, not a software lock that the kernel could
work around. The project should **keep archive writes out of scope** unless a
future hardware exploit changes the picture.

## Flash Controller Details

### Flash Chip Identification

The TI-84 Plus CE has used two different flash chips across hardware revisions:

| Revision     | Flash Chip              | Interface | Capacity |
|-------------|------------------------|-----------|----------|
| Pre-M (A-L) | Winbond W29GL032C      | Parallel NOR (16-bit bus) | 4 MB |
| M+ (2019+)  | Winbond W25Q32JV(SIQ)  | Serial SPI (QSPI mode)   | 4 MB |

The Python Edition ships exclusively with revision M+ hardware, so only the
serial flash path matters for this project. The 2019 revision added a 2-way
set-associative cache between the CPU and flash, because the serial interface
is slower per byte than the old parallel bus. Cache hits cost 2-3 cycles;
misses cost approximately 197 cycles.

### Flash Controller Register Map

The flash controller is memory-mapped at **0xE10000** in the eZ80 address
space. CEmu's `core/flash.c` models two operating modes selected by
`asic.serFlash`:

**Serial mode (revision M+):** The controller presents a command interface at
offset 0x800 from the flash port base. Commands are written into a 16-byte
buffer (`0x00`-`0x0F`), with the opcode at offset `0x0F` triggering execution.
A data port at offset `0x100` carries payload bytes. Status is readable at
offset `0x18`. A mask register at `0x2C`-`0x2F` controls the address window.

The serial flash responds to standard Winbond W25Q SPI commands:

| Opcode | Operation              |
|--------|------------------------|
| 0x06   | Write Enable           |
| 0x04   | Write Disable          |
| 0x02   | Page Program (256 B)   |
| 0x32   | Quad Input Page Program|
| 0x20   | Sector Erase (4 KB)    |
| 0x52   | Block Erase (32 KB)    |
| 0xD8   | Block Erase (64 KB)    |
| 0xC7   | Chip Erase             |
| 0x9F   | JEDEC ID (returns 0xEF, 0x40, capacity) |

**Parallel mode (pre-M):** Uses AMD/Spansion-style command sequences written to
specific flash addresses (0xAAA/0x555 with values 0xAA/0x55). The standard
NOR flash command protocol applies:

- Byte program: AA, 55, A0, data
- Sector erase: AA, 55, 80, AA, 55, 30
- Chip erase: AA, 55, 80, AA, 55, 10

Both modes enforce **Individual Protection Bits (IPB)** and **DPB (Dynamic
Protection Bits)** per sector. A byte/sector operation succeeds only when both
`ipb & dpb == 1` for the target sector.

## Unlock Sequence

### Two-Stage Unlock

Flash writes require two independent unlock conditions to be satisfied
simultaneously. Both are tracked in the ASIC control register at I/O port
**0x28** (`flashUnlocked`):

1. **Protected ports must be unlocked first** (port **0x06**,
   `protectedPortsUnlocked`). Bit 2 of this register must be set. When
   protected ports are locked (bit 2 clear), bit 3 of the flash-unlock
   register is forcibly cleared.

2. **The flash unlock sequence must be detected.** The ASIC watches the CPU's
   instruction fetch stream for a specific 17-byte opcode sequence:

   ```
   F3 18 00 F3 F3 ED 7E ED 56 ED 39 28 ED 38 28 CB 57
   ```

   Decoded as eZ80 assembly, this is approximately:

   ```asm
   di              ; F3
   jr $+2          ; 18 00
   di              ; F3
   di              ; F3
   im 2            ; ED 7E  (or related IM instruction)
   im 1            ; ED 56
   out0 (28h), a   ; ED 39 28  (write port 0x28)
   in0 a, (28h)    ; ED 38 28  (read port 0x28)
   bit 2, a        ; CB 57     (test bit 2)
   ```

   When this sequence is detected AND the code is executing from a privileged
   address AND protected ports are already unlocked, the ASIC sets bit 3 of
   `flashUnlocked`.

3. **Both bits 2 and 3** of `flashUnlocked` must be set for
   `flash_unlocked()` to return true:
   ```c
   return (control.flashUnlocked & (3 << 2)) == (3 << 2);
   ```

### Port 0x28 Write Behavior

Writing to port 0x28 does not simply store the value. The logic is:
```c
flashUnlocked = (flashUnlocked | 5) & byte;
```
Bits 0 and 2 take the written byte's values; other bits are AND-masked with
the existing value. This means bit 3 can only be **cleared** by a port write,
never set -- it can only be set by the hardware sequence detector.

### Automatic Re-Lock

Bit 3 of `flashUnlocked` is **automatically cleared** on the next instruction
fetch from an unprivileged address. This means flash is unlocked only for the
duration of a continuous run of privileged code. The moment the CPU fetches
from user RAM, the flash locks again.

## Privilege / Address Restrictions

This is the critical finding for the ce-kernel project.

### The Privilege Boundary

The ASIC maintains a 24-bit privilege boundary register at ports
**0x1D-0x1F** (`control.privileged`). On reset, this is set to **0xFFFFFF**
(all addresses privileged). The OS and boot code then lower it to protect
their own address ranges.

Code is considered **unprivileged** when:
- The program counter is **above** `control.privileged`, AND
- The program counter is **outside** the protected range
  (`protectedStart` to `protectedEnd`, ports 0x20-0x25)

The protected range defaults to **0xD1887C** for both start and end on reset,
which is within user RAM. In normal operation, the OS configures these
registers so that:
- Code in flash (0x000000 - somewhere in the OS) is privileged
- Code in user RAM (0xD00000+) is unprivileged

### Hardware Enforcement

The privilege check is enforced at the hardware level in the ASIC, not in
software. CEmu's `core/cpu.c` shows:

- **Port reads from unprivileged code:** Return 0 (silently fail)
- **Port writes from unprivileged code:** Set bit 2 of `protectionStatus`,
  log an error, and trigger `cpu_nmi()` -- a non-maskable interrupt that
  resets the calculator

This means the kernel (running from user RAM at 0xD00000+) **cannot**:
1. Write to port 0x06 to unlock protected ports
2. Write to port 0x28 to begin the flash unlock
3. Execute the flash unlock opcode sequence (it would be detected but rejected
   because `unprivileged_code()` returns true)
4. Write to ports 0x1D-0x1F to change the privilege boundary itself (these are
   also protected ports)

### The Circular Dependency

The protection creates an inescapable loop for unprivileged code:

1. To write flash, you need `flashUnlocked` bits 2+3 set
2. To set bit 3, you need the unlock sequence running from privileged code
3. To run privileged code, you need to be in the privileged address range
4. To change the privileged address range, you need to write to protected ports
5. To write to protected ports, you need to already be running privileged code

There is no entry point into this chain from user RAM.

### Z80 Mode: Not a Viable Bypass

One community source claims that the eZ80's Z80 emulation mode does not
enforce port protection. However:
- A developer attempting this approach on a real CE reported that it caused a
  system reset, "as if it wasn't running in Z80 mode"
- The OS interrupt handler checks the MB (MBASE) register and resets the
  device if it is not 0xD0
- OS 5.6.1 specifically patched the port unlock sequence used by community
  tools like Cesium
- No working Z80-mode bypass has been publicly demonstrated on revision M+
  hardware with OS 5.6.1+

## Sector Protection

### IPB and DPB System

Each flash sector has two protection bits:

- **IPB (Individual Protection Bit):** Can only be set (locked) by software,
  cleared only by erasing all IPBs at once. Provides persistent, one-way
  locking.
- **DPB (Dynamic Protection Bit):** Can be set or cleared by software.
  Provides temporary locking.

A sector is writable only when `ipb & dpb == 1` (both unlocked).

### Default Protection State (from CEmu)

On initialization:
- All **8 KB sectors** (below 0x10000, i.e., boot code): `ipb=0, dpb=1` --
  IPB-locked, not writable even if flash is unlocked
- All **64 KB sectors** (0x10000+): `ipb=1, dpb=1` -- writable when flash is
  unlocked, except `sector[1]` which has `ipb=0`
- The boot code sectors are IPB-protected, meaning their protection survives
  even a DPB clear

### OS 5.6.1 Changes

OS 5.6.1 introduced proper use of flash sector protection. Before this
version, TI relied mainly on the ASIC privilege system. The update:
- Patched the port unlock sequence used by Cesium and downgrade tools
- Properly enabled sector protection, which broke BootSwap on pre-M hardware
- Did NOT patch the arTIfiCE exploit (which uses a CabriJr app bug, not the
  port unlock)

### Boot Code Protection

The boot code occupies the 8 KB sectors at the bottom of flash. On revision M+
hardware:
- These sectors are IPB-locked at the hardware level
- The boot code cannot be modified without erasing all IPBs, which requires
  flash to be unlocked first (creating another circular dependency)
- BootSwap, the only tool that could modify boot code, does not work on
  revision M+ (different flash chip) or OS 5.6.1+ (sector protection enabled)

## Recommendation for the Project

**Drop flash write support from the project scope.** The hardware privilege
enforcement is a hard block that cannot be bypassed from user RAM without
either:

1. **Calling TI code:** The OS flash routines (`_WriteFlash`,
   `_EraseFlash`, etc. at known addresses) run from privileged flash and can
   unlock the flash controller. This directly violates the project's core
   "no TI code" rule.

2. **Exploiting a hardware bug:** Such as the Z80-mode port protection
   bypass, which has not been demonstrated to work on revision M+ hardware
   and may not exist.

3. **Physical hardware modification:** Desoldering the flash chip and
   programming it externally, as demonstrated by community members. This is
   outside the scope of a software project.

4. **Copying privileged code to RAM:** Even if you copied the unlock sequence
   from flash to RAM and ran it, the ASIC checks the *program counter
   address*, not the code content. The same bytes at 0xD00000 would fail
   where they succeed at 0x020000.

### Practical Alternatives

- **Keep the archive read-only.** The kernel can read archived variables
  (already implemented in `src/kernel/archive.c`) without any flash unlock.
- **Use RAM for all mutable state.** The kernel has full access to the
  0xD00000-0xD65800 RAM region. Persistent storage could use the RTC
  backup RAM or similar mechanisms.
- **Document the limitation.** Users who need to modify archived variables
  should do so through TI-OS before launching the kernel.

### If the Rule Were Relaxed

If the project were to allow a narrow exception to the "no TI code" rule for
flash writes specifically, the approach would be:

1. Call TI-OS flash routines by address (e.g., `_WriteFlash` at a known
   fixed address, `_EraseFlash`, `_WriteAByte`)
2. These routines handle the privilege escalation internally since they
   reside in privileged flash
3. The kernel would need to save/restore hardware state around these calls
   since TI routines may modify interrupt state, LCD settings, etc.

This is **not recommended** because it would undermine the bare ROM build
guarantee and couple the kernel to specific OS versions.

## Sources

### CEmu Emulator Source (CE-Programming/CEmu on GitHub, `master` branch)

- `core/flash.c` -- Flash controller emulation: command handling, serial/parallel modes, sector protection (IPB/DPB), SPI command set
- `core/flash.h` -- Flash state structure: `flashUnlocked`, command buffer, mask registers, cache
- `core/mem.c` -- Memory access handling: `detect_flash_unlock_sequence()` (17-byte sequence), privilege checks on writes, NMI on unprivileged port access, IPB/DPB enforcement on byte program and erase
- `core/control.c` -- Privilege boundary logic: `flash_unlocked()`, `protected_ports_unlocked()`, `unprivileged_code()`, port 0x06/0x28 register behavior, reset state (privileged=0xFFFFFF)
- `core/control.h` -- `control_state_t` fields: `privileged`, `protectedStart`, `protectedEnd`, `flashUnlocked`, `protectedPortsUnlocked`
- `core/cpu.c` -- Port I/O enforcement: unprivileged port reads return 0, unprivileged port writes trigger NMI reset

### Community Documentation

- CE toolchain hardware overview: https://ce-programming.github.io/toolchain/static/hardware.html
- Cemetech forum thread on flash chip identification (W29GL032C / W25Q32JV): https://cemetech.net/forum/viewtopic.php?p=283389
- Cemetech forum thread on flash unlock sequence (BOS project, boot rev 5.0.0 vs 5.1.5): https://cemetech.net/forum/viewtopic.php?p=288517
- Cemetech forum thread on CE port mapping and system calls: https://www.cemetech.net/forum/viewtopic.php?p=233244
- Cemetech forum thread on privilege bypass attempt via Z80 mode: https://cemetech.net/forum/viewtopic.php?p=274641
- Cemetech forum thread on OS 5.6.1 port unlock patch: https://www.cemetech.net/forum/viewtopic.php?p=291246
- Cemetech forum thread on flash chip replacement: https://www.cemetech.net/forum/viewtopic.php?p=312089
- Cemetech article on TI-84 CE semiconductor design (serial vs parallel flash analysis): https://www.cemetech.net/forum/viewtopic.php?p=286130
- Omnimaga thread on flash unlocking (privileged page requirement): https://www.omnimaga.org/asm-language/unlocking-flash/5/?wap2
- Palimpsest blog post on flash abuse (TI-83+/84+ context): https://palimpsest.is-a.dev/posts/abusing-the-flash/
- ThreadReader thread on TI calculator security history: https://threadreaderapp.com/thread/1124389329776926721.html
- Cemetech Z80 port listing (83+/84+ family, not CE-specific): https://learn.cemetech.net/index.php/Z80:Ports
- Wikipedia: TI-84 Plus CE series (hardware revision timeline): https://en.wikipedia.org/wiki/TI-84_Plus_CE_series

### Datasheets

- Winbond W25Q32JV SPI Flash (revision M+ flash chip): https://www.winbond.com/resource-files/w25q32jv%20spi%20revc%2008302016.pdf
