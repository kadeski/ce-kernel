"""Boot the bare ROM in headless CEmu, type shell commands, and check the output.

The screen is read back as text: the console draws an 8x8 font on a fixed grid,
so each 8x8 cell of a screenshot is matched against the kernel's own glyphs.

usage: smoke.py --rom bin/kernel.rom [--headless PATH] [--keep DIR]
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
        self.lines = ["run 1500"]
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


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--headless", default=shutil.which("cemu-headless") or "cemu-headless")
    parser.add_argument("--keep", type=Path, help="keep screenshots in this directory")
    args = parser.parse_args()

    shots = args.keep or Path(tempfile.mkdtemp(prefix="ce-kernel-smoke-"))
    shots.mkdir(parents=True, exist_ok=True)
    s = Script(shots)

    checks = []                         # (description, screenshot, predicate on screen text)

    def expect(desc, shot, pattern):
        checks.append((desc, shot, re.compile(pattern)))

    boot = s.screenshot()
    expect("status bar", boot, r"^ ce-kernel 0\.1\.0 +a \d+s$")
    expect("banner", boot, r"^type help and press ENTER$")
    expect("prompt", boot, r"^> $|^>$")

    expect("help lists commands", s.command("help"), r"^peek ADDR \[LEN\]$")
    expect("about names the loader", s.command("about"), r"^loader: bare ROM \(no TI code\)$")
    expect("mem shows the framebuffer", s.command("mem"), r"^screen  D40000-D52C00  76800$")
    expect("echo", s.command("echo hi there"), r"^hi there$")
    expect("peek reads flash", s.command("peek 30000 8"), r"^030000: [0-9A-F]{2}( [0-9A-F]{2}){7}$")
    expect("peek refuses I/O", s.command("peek e30000"), r"^only flash and RAM can be read$")
    expect("uptime", s.command("uptime"), r"^\d+\.\d{3} s$")
    expect("unknown command", s.command("xyzzy"), r"^unknown command: xyzzy$")
    expect("clear", s.command("clear"), r"^> $|^>$")

    s.type("keys")
    s.lines += ["keys enter", "keys down:9", "run 200"]
    expect("keys shows a held key", s.screenshot(), r"^5 \. \. \. # ")
    s.lines += ["keys up:9", "keys clear", "run 200"]

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
    cache = {}
    failed = 0
    for desc, shot, pattern in checks:
        text = cache.setdefault(shot, screen_text(shot, font))
        ok = any(pattern.search(line) for line in text)
        print(f"{'ok  ' if ok else 'FAIL'} {desc}")
        if not ok:
            failed += 1
            print("     screen was:\n" + "\n".join("     |" + l for l in text))
    if not args.keep:
        shutil.rmtree(shots)
    print(f"{len(checks) - failed}/{len(checks)} checks passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
