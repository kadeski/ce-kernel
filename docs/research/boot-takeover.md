# Boot-code takeover feasibility on revision M+ / OS 5.8.1

Date: 2026-11-16

## Summary

A boot-code takeover on revision M+ TI-84 Plus CE hardware (which includes every
Python Edition) is not feasible today and there is no known path to one. The
revision M hardware moved from a parallel NOR flash chip (Winbond W29GL032C) to a
serial SPI flash chip (Winbond W25Q32JV or similar), changing the entire
write-command interface. TI also began using the W25Q-series status-register
block-protection bits to hardware-protect the boot-code sectors, which the earlier
parallel chip did not enforce. Combined with RSA-2048 signature verification of the
OS on every reset, there are now three independent barriers: you cannot write to the
boot-code flash sectors, you cannot forge a valid OS signature, and even if you
could patch the boot code, the signature check would reject any unsigned OS.

The project's current design -- a takeover kernel launched from arTIfiCE under
TI-OS -- remains the only viable approach. If a boot-code exploit for revision M+
ever appears, the kernel's loader-agnostic `kmain` entry point means a third loader
could be written without changing the kernel itself (as noted in ADR 0002 and
DESIGN.md). Until then, this should stay a watch item.

## Boot chain walkthrough

### Reset to boot code entry

The eZ80 CPU fetches its first instruction from address 0x000000 on reset. On the
TI-84 Plus CE, this is the start of the 4 MB flash (0x000000--0x3FFFFF). The reset
vector jumps to the boot-code entry point at 0x000658.

Boot code version is exposed through a jump table at fixed offsets in the first page:

| Entry            | Offset   | What it returns                         |
|------------------|----------|-----------------------------------------|
| GetBootVerMajor  | 0x000080 | 16-bit value: high byte major, low minor|
| GetBootVerMinor  | 0x00008C | 8-bit revision number                   |
| GetBootVerBuild  | 0x000090 | 16-bit build number                     |

Each entry starts with a `JP` instruction (opcode 0xC3) to the actual getter, which
loads constants and returns. On boot code 5.6.1, the version string
"5.6.1.0006" appears at 0x000d73. (These offsets come from the CEmu `bootver.c`
source and the existing ROM dump analysis on issue #5.)

### OS signature verification

The boot code validates the OS on every hard reset. The process, as described by
community reverse engineers:

1. **Hash**: SHA-256 of the OS image in flash.
2. **Verify**: The hash is checked against an RSA-2048 signature embedded in the OS
   certificate area, using a public key stored in the boot code itself.
3. **Accept or reject**: If the signature is valid, execution hands off to the OS
   entry point. If not, the calculator displays "The OS is invalid. Please install
   a new OS." (UI strings at 0x013d3a in the boot dump) and enters recovery mode,
   accepting only USB-connected OS transfers.

The signature verification routine is at approximately 0x006880, and the RSA-2048
public key material occupies 0x006906--0x006c80 (a 256-byte high-entropy structure
consistent with a 2048-bit modulus). The routine uses the eZ80's MLT multiply
instructions for big-integer arithmetic.

An anti-rollback version table at 0x024402 prevents installation of OS versions
older than a per-revision minimum. Boot code 5.1.5 introduced this: the minimum
allowed OS version is recorded in a certificate page of flash, and the boot code
refuses to start an older OS.

### Handoff to TI-OS

After validation succeeds, boot code jumps to the OS entry point. The OS then
initializes its own data structures, sets up the memory map for user programs, and
enters its main loop. From this point, TI-OS owns the hardware, though it delegates
Python execution to the SAMD21 coprocessor.

CEmu does not deeply emulate the boot chain. It requires a full ROM dump including
the boot code because the OS references code and data in the boot pages, but it does
not model the signature verification or boot-sector protection. The emulator's
`flash.c` handles SPI flash commands (erase, program, read) but does not implement
the status-register block protection bits -- write-enable is the only gate. This
means CEmu cannot be used to test boot-sector write protection behavior.

## Flash sector protection

### The chip change at revision M

This is the critical hardware change that broke all previous boot-code exploits.

**Pre-revision-M (hardware revisions A through L):**
- Flash chip: Winbond W29GL032C (parallel NOR, 4 MB)
- Command interface: AMD-compatible byte-mode commands (unlock cycles at 0xAAA and
  0x555, then the operation byte)
- Sector protection: The boot sector is nominally protected, but the protection
  was either not properly enabled or could be bypassed through the parallel
  command interface. Community developers found "at least two different ways to
  overwrite parts of the boot code" (commandz, Cemetech).

**Revision M and later (all Python Editions):**
- Flash chip: Winbond W25Q32JV or similar (serial SPI NOR, 4 MB)
- Command interface: SPI serial commands (opcode-based protocol over the SPI bus)
- Sector protection: Hardware block-protection via status register bits

### How W25Q32JV protection works

The Winbond W25Q32JV uses a status-register-based protection scheme with several
independent layers:

**Block Protect bits (BP0, BP1, BP2):** Select which portion of the flash array is
read-only (program and erase commands to protected regions are silently ignored).
The protected region can range from none to the entire array.

**Top/Bottom bit (TB):** Controls whether protection starts from the bottom
(address 0x000000, where boot code lives) or the top of the address space.

**Sector/Block bit (SEC):** Switches between block-granularity and
sector-granularity protection.

**Complement bit (CMP):** Inverts the protected region. For example, if BP bits
select the bottom 1/64 of flash with CMP=0, setting CMP=1 protects the other
63/64 instead.

**Status Register Protect (SRP) and /WP pin:** When SRP is set and the hardware
/WP pin is held low, the status register itself cannot be modified. This means the
protection settings are locked in hardware until /WP is released.

**Active Permanent Lock (SRL):** A special instruction sequence (available only on
application to Winbond, not in the public datasheet) permanently and irrevocably
locks protected regions. Once set, the data in those blocks behaves like
one-time-programmable memory.

### How TI uses this protection

TI's boot code (and/or the ASIC configuration) sets the status register to protect
the boot-code sectors at the bottom of flash. With TB=0 (bottom protection) and
appropriate BP bits, the first 64 KB or more of flash -- containing the entire boot
code -- is hardware-protected against writes. OS 5.6.1 is specifically identified as
the version where "TI now properly uses sector protection" (BootSwap README), which
is why BootSwap stopped working at that OS version even on pre-revision-M hardware.

On revision M+ hardware with the SPI flash chip, the protection is more robust:

1. The status register is almost certainly write-protected via SRP + /WP pin held
   low on the PCB, so software cannot clear the block-protect bits.
2. Even if the status register could be modified, the protection is likely set as
   non-volatile (power-cycle persistent) in the status register's non-volatile bits.
3. TI may use the Active Permanent Lock feature (which requires a special Winbond
   command not in the public datasheet) to make boot-sector protection truly
   irreversible.

The bottom line: on revision M+ hardware, boot-code sectors cannot be written from
software running on the eZ80, regardless of privilege level. This is enforced by the
flash chip's own hardware, not by software-only checks that could be bypassed.

### Can protection be bypassed from userland?

No. The SPI flash chip enforces protection independently of the eZ80 CPU. Even
native code running with full hardware access (as ce-kernel does) sends write
commands through the SPI interface, and the flash chip rejects writes to protected
sectors at the hardware level. The only ways to modify protected sectors would be:

- Physically desoldering the flash chip and reprogramming it externally (bricking
  risk, requires specialized equipment)
- Finding a hardware glitch or fault injection attack on the flash chip itself
  (no public research exists for this chip in this context)
- Exploiting a bug in the flash chip's SPI command parser (no such bug is publicly
  known for the W25Q32JV)

## History: Cauldron, BootSwap, and why they failed on revision M+

### BootSwap (commandblockguy, ~2019)

BootSwap was a C library that could back up, restore, and modify the boot code on
the TI-84 Plus CE. Its primary use was disabling OS signature verification by
patching approximately nine bytes in the boot code. According to commandblockguy:
"there's no real benefit to having an entirely custom bootcode over a slightly
patched TI bootcode -- it's like nine bytes difference to disable all OS validation
checks."

**How it worked:** On pre-revision-M hardware with the parallel W29GL032C flash
chip, BootSwap could issue raw flash write commands to the boot-code sectors. The
sector protection on the W29GL032C was either not enabled by the boot code or could
be overridden through the parallel command interface. The tool wrote directly to the
boot-code region in flash, patching the validation routine.

**Why it failed on 5.6.1+:** OS 5.6.1 enabled proper sector protection on the flash
chip. The BootSwap README states: "does not run on OS 5.6.1, as TI now properly uses
sector protection." This was a software-side fix: even on the old parallel flash
chip, TI configured the chip's sector protection registers to lock the boot-code
sectors, and BootSwap could no longer issue writes to those addresses.

**Why it fails on revision M+:** The flash chip itself is different. The W25Q32JV's
SPI serial interface uses an entirely different command set from the W29GL032C's
parallel AMD-compatible commands. BootSwap's flash write routines are hardcoded for
the parallel chip and simply do not work on the SPI chip. Additionally, the SPI
chip's status-register protection is more robust than the parallel chip's sector
protection.

### Cauldron (Ne3s2-3p4, 2020)

Cauldron was a boot hack for the TI-83 Premium CE (the French-market equivalent of
the TI-84 Plus CE) that removed OS verification at boot and the minimum install
version check. It worked "on all pre revision M hardware."

**How it worked:** Similar to BootSwap, Cauldron patched the boot code in flash to
disable the signature verification routine. It targeted the TI-83 Premium CE
specifically but used the same general approach of writing to boot-code flash
sectors.

**Why it failed on revision M+:** Same reasons as BootSwap -- the flash chip change
and the properly-enabled sector protection. Cauldron's own README carried a warning:
"DO NOT USE. It may induce various fatal error in the toolchain."

### Common failure mode

Both tools exploited the same fundamental weakness: the pre-revision-M boot code
did not properly lock its own flash sectors against writes. On the old parallel
flash chip, the protection was either misconfigured or bypassable. Revision M
closed this by:

1. Switching to a SPI flash chip with a more robust protection model
2. Properly configuring the chip's hardware block protection
3. Using the /WP pin to lock the protection settings

These are hardware changes that cannot be reversed by software, which is why no
software-only patch has appeared for revision M+ in the six years since.

## Current state: known revision M+ vulnerabilities and research

### arTIfiCE v3 (September 2026)

arTIfiCE v3 was released on September 21, 2026, supporting TI-84 Plus CE OS 5.8.5.
It is the current jailbreak for running native code. Important context:

- arTIfiCE exploits a vulnerability in the OS, not in the boot code. It restores
  native assembly/C execution by exploiting an OS-level bug.
- It does not bypass, modify, or interact with flash sector protection.
- It does not modify the boot code or the OS in flash.
- OS updates break it: v2.1 covered OS 5.5--5.8.4, then OS 5.8.5 broke it and v3
  followed.

arTIfiCE is orthogonal to boot-code modification. It provides the ability to run
native code under TI-OS, which is what ce-kernel depends on, but it does not provide
a path to replacing the boot code or OS.

### No known revision M+ boot exploit

As of November 2026, no public boot-code exploit exists for revision M+ hardware.
Specific findings:

- **Cemetech forums:** No threads describe a working boot-code modification on
  revision M+ hardware. The most recent discussions (2024--2026) acknowledge that
  boot-code patching is not possible on current hardware.
- **TI-Planet:** No articles describe a revision M+ boot exploit.
- **Academic/security research:** No published papers on TI-84 CE boot-code
  vulnerabilities were found.
- **BOS project:** The open-source BOS operating system explicitly lists Python
  Edition and revision M+ as unsupported.
- **Noti-ez80:** An open-source replacement boot ROM exists for emulator use, but
  its developer says it "will likely never be safely installable on real
  calculators."
- **TI discontinuation:** Reports indicate TI discontinued the TI-84 CE Python in
  2026 in favor of the TI-84 Evo. This may reduce future research interest in CE
  hardware.

### The "unspoken arrangement"

Cemetech developer commandz noted that the community and TI appear to have an
unspoken arrangement: community members do not release boot-code exploits (which
could enable cheating on standardized tests) while TI does not patch the
user-space exploits that enable hobbyist programming. This social dynamic may
suppress publication of any boot-code research that does exist.

## ARM coprocessor as a potential vector

### Hardware overview

The Python Edition includes an Atmel SAMD21E18A-U (ARM Cortex-M0+ at 48 MHz,
256 KB flash, 32 KB RAM) running TI's Python firmware. It shares the SPI bus
with the LCD panel: bit 4 of control port 0x09 switches the bus between the panel
and the coprocessor. The protocol between the eZ80 and the SAMD21 is described
as using CSI (Control Sequence Introducer) sequences with 6-bit encoding for
variable-length data, though this is not officially documented.

Some sources describe a 115,200 baud UART serial link instead of or in addition
to SPI. Which interface carries the Python protocol has not been definitively
established in public documentation. CEmu's `spi.c` notes that coprocessor
emulation is a stub: `null_spi_peek` returns a fixed byte 0xC3 ("Hack to make OS
5.7.0 happy without a coprocessor").

### Could the SAMD21 be an attack vector?

**Theory:** The SAMD21 shares the SPI bus with the main flash. If the coprocessor
could be made to issue flash write commands while the bus is switched to it, or if
the bus-switching logic has a race condition, the coprocessor might be able to write
to the eZ80's flash.

**Problems with this theory:**

1. **Bus isolation:** The SPI bus is multiplexed: the eZ80's flash and the SAMD21
   are on different sides of a bus switch controlled by port 0x09. Only one device
   is selected at a time. The flash chip's chip-select line is presumably
   deasserted when the bus is switched to the coprocessor.

2. **SAMD21 firmware is TI's:** The coprocessor runs TI's closed-source Python
   firmware. Replacing it would require either a JTAG/SWD connection (physical
   hardware access) or an exploit in TI's firmware. The SAMD21's SWD port may or
   may not be accessible on the PCB.

3. **No known firmware vulnerabilities:** No public security research targets the
   SAMD21 firmware on TI calculators. The coprocessor's attack surface is the
   serial protocol from the eZ80, which is narrow.

4. **Flash protection is chip-level:** Even if the SAMD21 could somehow issue
   commands on the flash chip's SPI bus, the flash chip's own block-protection
   would still reject writes to the boot-code sectors.

5. **Protocol is undocumented:** The communication protocol between the eZ80 and
   the SAMD21 is not publicly documented. Reverse engineering it would be a
   prerequisite for any coprocessor-based attack.

**Assessment:** The ARM coprocessor is not a realistic attack vector for boot-code
modification. The bus architecture isolates it from the flash chip, the flash chip's
hardware protection would block writes even if isolation failed, and no public
research has explored this path. However, the coprocessor itself is an interesting
target for other reasons -- running custom Python firmware, for example -- and any
vulnerabilities found in TI's SAMD21 firmware could have implications beyond boot
modification.

## Recommendation for the project

### Keep as a watch item

Boot-code takeover on revision M+ is not actionable today. The project should
continue with the current arTIfiCE-based loader approach.

### Things to monitor

1. **arTIfiCE updates.** The project depends on arTIfiCE for native code execution.
   OS updates that break arTIfiCE are the most immediate threat. Monitor Cemetech
   and the arTIfiCE GitHub for updates when TI releases new OS versions.

2. **TI-84 Evo.** If TI has truly discontinued the CE Python, the Evo may have a
   different architecture entirely. Research into CE boot-code exploits may slow,
   but the existing hardware population remains large.

3. **Flash chip research.** Any published vulnerability in the Winbond W25Q32JV's
   status-register protection, SPI command parser, or voltage-glitch susceptibility
   could change the calculus. Monitor embedded-security conferences and publications.

4. **SAMD21 research.** If someone reverse-engineers the TI Python firmware or finds
   a way to run custom code on the SAMD21, it opens new research directions, though
   not necessarily a boot-code path.

5. **Cemetech and TI-Planet forums.** These are where new exploits are published.
   The "unspoken arrangement" could end at any time, especially now that TI is
   discontinuing the CE line.

### Actionable paths

- **None today for boot-code takeover.** The barriers are hardware-level and no
  known avenue of research is close to bypassing them.
- **Third loader architecture is ready.** If an exploit appears, ce-kernel's design
  already supports a third loader alongside `src/tios/` and `rom/` that fills in a
  `boot_info` and calls `kmain`. No kernel changes would be needed.
- **Focus on the arTIfiCE path.** Improving the TI-OS loader, reducing its footprint,
  and ensuring compatibility with future arTIfiCE versions is where effort pays off.

## Sources

### Community forums and wikis
- Cemetech: Flash Chip on the TI-84+CE -- https://www.cemetech.net/forum/viewtopic.php?t=16282
- Cemetech: How to downgrade from OS 5.6.1 -- https://www.cemetech.net/forum/viewtopic.php?t=17520
- Cemetech: arTIfiCE v3 Released for TI-84 Plus CE OS 5.8.5 -- https://www.cemetech.net/forum/viewtopic.php?t=21410
- Cemetech: Noti-ez80 (open source boot ROM) -- https://www.cemetech.net/forum/viewtopic.php?p=285881
- Cemetech: CE Boot Code Versions -- https://www.cemetech.net/forum/viewtopic.php?t=15599
- Cemetech: ASM/C Removal Updates -- https://www.cemetech.net/forum/viewtopic.php?t=16652
- Cemetech: arTIfiCE v3 news -- https://www.cemetech.net/news/2026/9/1070
- CodeWalrus: TI-84 Plus CE downgrade protection -- https://codewalr.us/index.php?topic=1985.0
- WikiTI: 84PCE General Hardware Information -- https://wikiti.brandonw.net/index.php?title=Category:84PCE:General_Hardware_Information
- CE-Programming toolchain: Calculator Hardware Overview -- https://ce-programming.github.io/toolchain/static/hardware.html

### Source code and projects
- CEmu emulator source (flash.c, bootver.c, spi.c, cert.c) -- https://github.com/CE-Programming/CEmu
- arTIfiCE -- https://yvantt.github.io/arTIfiCE/
- BOS (open-source OS) -- https://github.com/beckadamtheinventor/BOS
- Noti-ez80 -- https://github.com/beckadamtheinventor/noti-ez80

### Datasheets and technical references
- Winbond W25Q32JV datasheet -- available via Mouser and DigiKey
- Winbond: Safeguarding Boot Code and Other Critical Data -- https://www.winbond.com/hq/support/online-learning/articles-item/safeguarding-boot-code-and-other-critical-data.html
- Siraben: TI-84 Plus OS Reverse Engineering (flash memory) -- https://siraben.github.io/ti84p-re/flash-memory.html

### Reverse engineering and teardowns
- iFixit: TI-84 Plus CE Teardown -- https://www.ifixit.com/Teardown/TI-84+Plus+CE+Teardown/118886
- Wikipedia: TI-84 Plus CE series -- https://en.wikipedia.org/wiki/TI-84_Plus_CE_series

### Project documents
- ce-kernel ADR 0002: A takeover kernel, not a replacement OS
- ce-kernel DESIGN.md: Hardware notes section
