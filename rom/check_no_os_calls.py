"""Fail the ROM build if any code jumps into the boot code / TI-OS area.

The bare ROM has nothing below 0x030000 except the reset vector and the model
header, so a call or jump there (or an RST, which also lands there) means a
library routine expected TI's boot code or OS and would crash.

usage: check_no_os_calls.py KERNEL.ELF OBJDUMP
"""

import re
import subprocess
import sys

OWN_CODE_START = 0x030000
# Absolute branches only: jr/djnz are relative (+-128 bytes) and objdump prints
# their targets truncated to 16 bits.
BRANCH = re.compile(r"\b(call|jp)(\.\w+)?\s+(?:\w+,\s*)?(0x[0-9a-f]+|\$[0-9a-f]+)", re.I)
RST = re.compile(r"\brst\b", re.I)


def main() -> int:
    elf, objdump = sys.argv[1], sys.argv[2]
    listing = subprocess.run(
        [objdump, "-d", "-j", ".text", elf], check=True, capture_output=True, text=True
    ).stdout
    bad = []
    for line in listing.splitlines():
        if RST.search(line):
            bad.append(line.strip())
            continue
        m = BRANCH.search(line)
        if m:
            target = int(m.group(3).lstrip("$").removeprefix("0x"), 16)
            if target < OWN_CODE_START:
                bad.append(line.strip())
    if bad:
        print("error: ROM code reaches into the TI boot/OS area:", file=sys.stderr)
        for line in bad[:20]:
            print("  " + line, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
