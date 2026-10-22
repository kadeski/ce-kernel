"""Boot the kernel in headless CEmu, type shell commands, and check the output.

There is one mode per loader:

    smoke.py --rom bin/kernel.rom
        boots the bare ROM straight into the kernel.
    smoke.py --rom DUMP.rom --program bin/KERNEL.8xp --artifice arTIfiCE.8xp
        boots TI-OS from a ROM dumped from a calculator, sends both programs
        and an AppVar for the archive, starts the kernel from arTIfiCE's
        launcher and checks each way back.

The kernel's screen is read back as text: the console draws an 8x8 font on a
fixed grid, so each 8x8 cell of a screenshot is matched against the kernel's
own glyphs. TI-OS screens can't be read that way, so they are compared with
screenshots taken earlier in the same run.

usage: smoke.py --rom ROM [--program 8XP --artifice 8XP] [--headless PATH] [--keep DIR]
"""

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
COLS, ROWS = 40, 30

# Pixel rows of a TI-OS screen to compare. The status bar above them has an
# animated battery icon; on the home screen, the cursor blinks below them.
TIOS_ROWS = (30, 240)
HOME_ROWS = (30, 76)                    # the first entry and its answer

# Reverse of src/kernel/keymap.c: which physical key types each character.
ALPHA_KEYS = {
    "a": "math", "b": "apps", "c": "prgm",
    "d": "inv", "e": "sin", "f": "cos", "g": "tan", "h": "pow",
    "i": "sq", "j": "comma", "k": "lpar", "l": "rpar", "m": "div",
    "n": "log", "o": "7", "p": "8", "q": "9", "r": "mul",
    "s": "ln", "t": "4", "u": "5", "v": "6", "w": "sub",
    "x": "sto", "y": "1", "z": "2", " ": "0",
}
NUMBER_KEYS = {str(d): str(d) for d in range(10)} | {
    ".": "dot", ",": "comma", "+": "add", "-": "sub", "*": "mul", "/": "div",
    "^": "pow", "(": "lpar", ")": "rpar",
}


def appvar_8xv(name: str, text: bytes) -> bytes:
    """A TI-OS AppVar file with text as its contents, for sending to the archive."""
    data = len(text).to_bytes(2, "little") + text
    entry = (b"\x0d\x00" + len(data).to_bytes(2, "little") + b"\x15"
             + name.encode().ljust(8, b"\0") + b"\x00\x80"
             + len(data).to_bytes(2, "little") + data)
    head = b"**TI83F*\x1a\x0a\x00" + bytes(42) + len(entry).to_bytes(2, "little")
    return head + entry + (sum(entry) & 0xFFFF).to_bytes(2, "little")


def load_font() -> dict[tuple[int, ...], str]:
    """Glyph bitmaps from src/kernel/font8x8.c, keyed by their 8 row bytes."""
    text = (ROOT / "src/kernel/font8x8.c").read_text()
    rows = re.findall(r"\{ ((?:0x[0-9A-Fa-f]{2}, ){7}0x[0-9A-Fa-f]{2}) \}", text)
    assert len(rows) == 95, len(rows)
    font = {}
    for i, row in enumerate(rows):
        key = tuple(int(v, 16) for v in row.split(", "))
        font.setdefault(key, chr(0x20 + i))
    return font


def read_bmp(path: Path) -> list[list[tuple[int, int, int]]]:
    data = path.read_bytes()
    offset = int.from_bytes(data[10:14], "little")
    width = int.from_bytes(data[18:22], "little")
    height = int.from_bytes(data[22:26], "little", signed=True)
    stride = (width * 3 + 3) & ~3
    pixels = []
    for y in range(abs(height)):
        src_row = (abs(height) - 1 - y) if height > 0 else y
        base = offset + src_row * stride
        row = data[base : base + width * 3]
        pixels.append([tuple(row[x * 3 : x * 3 + 3]) for x in range(width)])
    return pixels


def screen_text(path: Path, font) -> list[str]:
    pixels = read_bmp(path)
    lines = []
    for r in range(ROWS):
        band = pixels[r * 8 : r * 8 + 8]
        # Background per text row, not per cell: dense glyphs like '#' have
        # more lit pixels than dark ones.
        bg = Counter(p for row in band for p in row).most_common(1)[0][0]
        line = []
        for c in range(COLS):
            cell = [row[c * 8 : c * 8 + 8] for row in band]
            bits = tuple(sum(1 << x for x in range(8) if row[x] != bg) for row in cell)
            line.append(font.get(bits, "~"))
        lines.append("".join(line).rstrip())
    return lines


class Script:
    """Builds a cemu-headless command script, tracking the ALPHA lock state."""

    def __init__(self, shots: Path):
        self.lines = []
        self.alpha = True               # the shell starts in alpha lock
        self.shots = shots
        self.count = 0

    def type(self, text: str):
        keys = []
        for ch in text:
            want_alpha = ch in ALPHA_KEYS
            if not want_alpha and ch not in NUMBER_KEYS:
                raise ValueError(f"no key for {ch!r}")
            if want_alpha != self.alpha:
                keys.append("alpha")
                self.alpha = want_alpha
            keys.append(ALPHA_KEYS[ch] if want_alpha else NUMBER_KEYS[ch])
        self.lines.append("keys " + ",".join(keys))

    def command(self, text: str) -> Path:
        self.type(text)
        self.lines.append("keys enter")
        self.lines.append("run 400")
        return self.screenshot()

    def screenshot(self) -> Path:
        self.count += 1
        path = self.shots / f"{self.count:02d}.bmp"
        self.lines.append(f"screenshot {path}")
        return path

    def start_artifice(self) -> Path:
        """From the TI-OS home screen, run arTIfiCE and screenshot its launcher.

        RAM holds only TI-Basic programs, so the prgm menu doesn't ask which
        kind, and arTIfiCE's program sorts first."""
        self.lines += ["keys clear,prgm", "run 500", "keys enter", "run 500", "keys enter", "run 2000"]
        return self.screenshot()

    def start_kernel(self) -> Path:
        """From arTIfiCE's launcher, run KERNEL, its only entry."""
        self.lines += ["keys enter", "run 1500"]
        self.alpha = True               # each run starts in alpha lock again
        return self.screenshot()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--rom", required=True, type=Path,
                        help="bin/kernel.rom, or with --program a ROM dumped from a calculator")
    parser.add_argument("--program", type=Path, help="bin/KERNEL.8xp, to test the TI-OS loader")
    parser.add_argument("--artifice", type=Path, help="arTIfiCE's .8xp for the ROM's OS version")
    parser.add_argument("--headless", default=shutil.which("cemu-headless") or "cemu-headless")
    parser.add_argument("--keep", type=Path, help="keep screenshots in this directory")
    args = parser.parse_args()
    tios = args.program is not None
    if tios != (args.artifice is not None):
        parser.error("--program and --artifice go together")

    shots = args.keep or Path(tempfile.mkdtemp(prefix="ce-kernel-smoke-"))
    shots.mkdir(parents=True, exist_ok=True)
    s = Script(shots)

    checks = []                         # (description, screenshot, test)

    def expect(desc, shot, pattern):
        """The kernel's screen matches pattern, with ^ and $ at each line."""
        checks.append((desc, shot, re.compile(pattern, re.MULTILINE)))

    def expect_same(desc, shot, ref, rows=TIOS_ROWS):
        """A TI-OS screen matches one from earlier in the run."""
        checks.append((desc, shot, (ref, rows)))

    status_bar = r"^ ce-kernel 0\.1\.0 +a \d+s$"
    if tios:
        # An AppVar, so the prgm menu still holds only arTIfiCE and KERNEL.
        # The first copy is overwritten, which leaves a deleted entry before
        # the second.
        old_appvar, appvar = shots / "SMOKE-old.8xv", shots / "SMOKE.8xv"
        old_appvar.write_bytes(appvar_8xv("SMOKE", b"old"))
        appvar.write_bytes(appvar_8xv("SMOKE", b"hello archive"))
        s.lines += [
            "run 8000",                 # TI-OS boots to "RAM Cleared"
            f"send-file {args.artifice}",
            f"send-file {args.program}",
            f"send-file archive {old_appvar}",
            f"send-file archive {appvar}",
            "keys clear,1,add,1,enter",
            "run 500",
        ]
        home = s.screenshot()
        launcher = s.start_artifice()
        boot = s.start_kernel()
    else:
        s.lines.append("run 1500")
        boot = s.screenshot()
    expect("status bar", boot, status_bar)
    expect("banner", boot, r"^type help and press ENTER$")
    # The key that started the kernel (ENTER in arTIfiCE) may still be down.
    expect("one prompt, no stray key", boot, r"^type help and press ENTER\n>\n\n")

    expect("help lists commands", s.command("help"), r"^peek ADDR \[LEN\]$")
    loader = r"TI-OS \(asm program\)" if tios else r"bare ROM \(no TI code\)"
    expect("about names the loader", s.command("about"), rf"^loader: {loader}$")
    expect("mem shows the framebuffer", s.command("mem"), r"^screen  D40000-D52C00  76800$")
    expect("echo", s.command("echo hi there"), r"^hi there$")
    expect("peek reads flash", s.command("peek 30000 8"), r"^030000: [0-9A-F]{2}( [0-9A-F]{2}){7}$")
    expect("peek refuses I/O", s.command("peek e30000"), r"^only flash and RAM can be read$")
    if tios:
        expect("ls lists the archived AppVar", s.command("ls"), r"^appvar +SMOKE +15 +[0-9A-F]{6}$")
        # Its size word (13), then "hello archive".
        expect("cat shows its data", s.command("cat smoke"),
               r"^[0-9A-F]{6}: 0D 00 68 65 6C 6C 6F 20\n[0-9A-F]{6}: 61 72 63 68 69 76 65$")
    else:
        expect("ls finds no archive in the bare ROM", s.command("ls"), r"^0 files$")
    expect("cat of a missing file", s.command("cat nosuch"), r"^not in archive: nosuch$")
    expect("uptime", s.command("uptime"), r"^\d+\.\d{3} s$")
    expect("unknown command", s.command("xyzzy"), r"^unknown command: xyzzy$")
    expect("clear", s.command("clear"), r"^> $|^>$")

    s.type("keys")
    s.lines += ["keys enter", "keys down:9", "run 200"]
    expect("keys shows a held key", s.screenshot(), r"^5 \. \. \. # ")
    s.lines += ["keys up:9", "keys clear", "run 200"]

    if tios:
        # Each way out goes back to arTIfiCE's launcher, and the next run
        # starts fresh.
        s.type("1")                     # leave the shell in digit mode
        s.lines += ["keys clear,2nd,mode", "run 1500"]
        expect_same("2nd mode returns to arTIfiCE", s.screenshot(), launcher)
        expect("a new run starts in alpha lock", s.start_kernel(), status_bar)
        s.lines += ["key on", "run 1500"]
        expect_same("ON returns to arTIfiCE", s.screenshot(), launcher)
        s.start_kernel()
        expect_same("exit returns to arTIfiCE", s.command("exit"), launcher)

        # Quitting arTIfiCE leaves a TI-OS that still computes and runs programs.
        s.lines += ["keys mode", "run 1500", "keys clear,1,add,1,enter", "run 500"]
        expect_same("TI-OS home screen works", s.screenshot(), home, HOME_ROWS)
        expect_same("arTIfiCE starts again", s.start_artifice(), launcher)
        expect("the kernel starts again", s.start_kernel(), status_bar)
    else:
        # ON exits on a calculator; the bare ROM has nowhere to go and must ignore it.
        s.lines += ["key on", "run 200"]
        expect("ON is ignored by the bare ROM", s.command("echo still here"), r"^still here$")

        expect("exit halts the bare ROM", s.command("exit"), r"^halted; nothing to return to$")
    s.lines.append("quit")

    proc = subprocess.run(
        [args.headless, "--rom", str(args.rom)],
        input="\n".join(s.lines) + "\n",
        capture_output=True,
        text=True,
        timeout=300,
    )
    errors = [l for l in proc.stdout.splitlines() if l.startswith("ERR")]
    if proc.returncode != 0 or errors:
        print(proc.stdout, proc.stderr, sep="\n", file=sys.stderr)
        return 1

    font = load_font()
    failed = 0
    for desc, shot, test in checks:
        text = screen_text(shot, font)
        if isinstance(test, re.Pattern):
            ok = test.search("\n".join(text)) is not None
        else:
            ref, (top, bottom) = test
            ok = read_bmp(shot)[top:bottom] == read_bmp(ref)[top:bottom]
        print(f"{'ok  ' if ok else 'FAIL'} {desc}")
        if not ok:
            failed += 1
            if not isinstance(test, re.Pattern):
                hint = "" if args.keep else "; rerun with --keep DIR to see both"
                print(f"     differs from {ref.name} in pixel rows {top}-{bottom - 1}{hint}")
            print("     screen was:\n" + "\n".join("     |" + l for l in text))
    if not args.keep:
        shutil.rmtree(shots)
    print(f"{len(checks) - failed}/{len(checks)} checks passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
