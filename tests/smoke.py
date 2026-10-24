"""Boot the kernel in headless CEmu, use its menu, tools and shell, and check the output.

There is one mode per loader:

    smoke.py --rom bin/kernel.rom
        boots the bare ROM straight into the kernel.
    smoke.py --rom DUMP.rom --program bin/KERNEL.8xp --artifice arTIfiCE.8xp
        boots TI-OS from a ROM dumped from a calculator, sends both programs,
        an AppVar and a Python notes file for the archive, sets TI-OS's clock,
        starts the kernel from arTIfiCE's launcher and checks each way back.

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


def python_appvar(name: str, script: str) -> bytes:
    """A Python file's AppVar data as tivars_lib_cpp lays it out: magic, a
    filename record (its length, type 1, the name), a 0, then the script."""
    record = bytes([1 + len(name), 0x01]) + name.encode()
    return b"PYCD" + record + b"\x00" + script.replace("\n", "\r\n").encode()


def ti_var(type_id: int, name: str, body: bytes, archived: bool) -> bytes:
    """A TI-OS variable file (.8xv, .8xp) holding body, for send-file."""
    data = len(body).to_bytes(2, "little") + body
    entry = (b"\x0d\x00" + len(data).to_bytes(2, "little") + bytes([type_id])
             + name.encode().ljust(8, b"\0") + b"\x00" + (b"\x80" if archived else b"\x00")
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
        self.alpha = False              # the menu is in digit mode
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

    def tool(self, section: str, number: str, fields, alpha=(), then=(), wait=1500):
        """From the menu, open a tool, fill in its fields (those numbered in
        alpha start in alpha lock), press ENTER, wait ms for the answer and
        screenshot it. With then, also run those lines and screenshot again.
        Ends back at the menu. The bare ROM runs from flash, which CEmu makes
        slow, so the heavier tools need a longer wait."""
        self.lines += [f"keys {section}", "run 500", f"keys {number}", "run 500"]
        for i, text in enumerate(fields):
            self.alpha = i in alpha     # each field sets its own typing layer
            if text:
                self.type(text)
            if i + 1 < len(fields):
                self.lines.append("keys down")
        self.lines += ["keys enter", f"run {wait}"]
        shot = self.screenshot()
        if then:
            self.lines += list(then)
            shot = (shot, self.screenshot())
        self.lines += ["keys 2nd,mode", "run 300"]
        self.alpha = False
        return shot

    def shell(self) -> Path:
        """From the menu, open the shell, which is in alpha lock."""
        self.lines += ["keys 8", "run 300"]
        self.alpha = True
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
        self.alpha = False              # each run starts on the menu again
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

    def expect_changed(desc, shot, earlier, pattern):
        """pattern matches both kernel screens, and its first group differs."""
        checks.append((desc, shot, (earlier, re.compile(pattern, re.MULTILINE))))

    status_bar = r"^ ce-kernel 0\.3\.0 +[a1] (\d\d:\d\d)$"
    alpha_bar = r"^ ce-kernel 0\.3\.0 +a \d\d:\d\d$"
    digit_bar = r"^ ce-kernel 0\.3\.0 +1 \d\d:\d\d$"
    menu = r"^ Discrete math\n  1 Number theory$"
    if tios:
        # An AppVar, so the prgm menu doesn't change. The first copy is
        # overwritten, which leaves a deleted entry before the second.
        old_appvar, appvar = shots / "SMOKE-old.8xv", shots / "SMOKE.8xv"
        old_appvar.write_bytes(ti_var(0x15, "SMOKE", b"old", archived=True))
        appvar.write_bytes(ti_var(0x15, "SMOKE", b"hello archive", archived=True))
        # A Python notes file like NUMR.py, long enough
        # that opening it at a heading scrolls.
        filler = "".join(f"# line {i}\n" for i in range(30))
        notes = shots / "NUMR.8xv"
        notes.write_bytes(ti_var(0x15, "NUMR", python_appvar(
            "NUMR", "# notes\n" + filler + "# == INVERSE ==\n# exists iff gcd(x,N)=1\n" + filler),
            archived=True))
        # A TI-Basic program, setTime(13,45,0), to give TI-OS's clock a time
        # the kernel must show. Its name sorts after arTIfiCE's and KERNEL in
        # the prgm menu.
        set_time, comma, rparen = b"\xef\x01", b"\x2b", b"\x11"     # tokens; digits are ASCII
        clock = shots / "ZCLOCK.8xp"
        clock.write_bytes(ti_var(0x05, "ZCLOCK", set_time + b"13" + comma + b"45" + comma + b"0" + rparen,
                                 archived=False))
        s.lines += [
            "run 8000",                 # TI-OS boots to "RAM Cleared"
            f"send-file {args.artifice}",
            f"send-file {args.program}",
            f"send-file archive {old_appvar}",
            f"send-file archive {appvar}",
            f"send-file archive {notes}",
            f"send-file {clock}",
            "keys clear,prgm", "run 500", "keys up,enter", "run 500", "keys enter", "run 500",
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
    if tios:
        expect("the status bar shows TI-OS's time", boot, r" 13:45$")
    else:
        expect("the bare ROM's clock starts at 00:00", boot, r" 00:00$")
    # The key that started the kernel (ENTER in arTIfiCE) may still be down;
    # it must not open the first entry.
    expect("the kernel starts on the menu, no stray key", boot, menu)
    expect("the menu is in digit mode", boot, digit_bar)

    # Each tool's answer, on worked examples.
    expect("div and mod rounds down", s.tool("1", "1", ["-457", "22"]), r"^ q = -21    r = 5$")
    gcd, steps = s.tool("1", "2", ["178", "20"], then=["keys yequ", "run 500"])
    expect("gcd remainders", gcd, r"^ remainders: 18 2 0$")
    expect("gcd and lcm", gcd, r"^ gcd = 2    lcm = 1780$")
    expect("Y= shows the steps", steps, r"^ 178 mod 20 = 18$")
    expect("inverse", s.tool("1", "3", ["33", "13"]), r"^ 13\^-1 mod 33 = 28$")
    expect("ones digit", s.tool("1", "4", ["3", "902", "10"]), r"^ 3\^902 mod 10 = 9$")
    expect("the exponent's bits", s.tool("1", "4", ["17", "21", "5"]),
           r"^ 17\^21 = 17\^16 \* 17\^4 \* 17$")
    factors = s.tool("1", "5", ["360"])
    expect("prime factors", factors, r"^ 360 = 2\^3 \* 3\^2 \* 5$")
    expect("divisor count", factors, r"^ divisors: \(3\+1\)\(2\+1\)\(1\+1\) = 24$")
    expect("binary", s.tool("1", "6", ["10011001", "2"]), r"^ 10011001 in base 2 = 153$")
    expect("RSA d, p = 11", s.tool("1", "7", ["11", "17", "3"]), r"^ d = 107$")
    expect("RSA d, p = 17", s.tool("1", "7", ["17", "23", "7"]), r"^ d = 151$")
    expect("A=1 code", s.tool("1", "8", ["happy birthday"], alpha=(0,)),
           r"^ joined: 8116162529182084125$")
    expect("LCG", s.tool("1", "9", ["7", "4", "9", "3"]), r"^ x0\.\.: 3 7 8 6 ")

    expect("nPr", s.tool("2", "1", ["12", "3"]), r"^ P\(12,3\) = 1320$")
    expect("multiset, at least one", s.tool("2", "2", ["10", "4", "1"]), r"^ C\(9,3\) = 84$")
    expect("multiset", s.tool("2", "2", ["7", "3"]), r"^ C\(9,2\) = 36$")
    expect("multinomial", s.tool("2", "3", ["1,4,4,2"]), r"^ 11!/\(1!4!4!2!\) = 34650$")
    expect("pigeonhole", s.tool("2", "5", ["26", "", "2"]), r"^ 27 items force 2 in a box$")
    expect("inclusion-exclusion", s.tool("2", "6", ["8", "10", "6"]),
           r"^ \|AuB\| = 8 \+ 10 - 6 = 12$")
    lex = s.tool("2", "7", ["4,5,2,3,1,6", "4,5,2,3,6,1"])
    expect("lex order", lex, r"^ a < b at place 5: 1 < 6$")
    expect("next permutation", lex, r"^ next: \(4,5,2,3,6,1\)$")
    s.lines += ["keys 2", "run 500", "keys 8", "run 500"]
    for answer in "221122":             # no, no, yes, yes, no, no
        s.lines += [f"keys {answer}", "run 500"]
    expect("the decision tree ends at a formula", s.screenshot(),
           r"^ P\(n,k\) = n!/\(n-k\)!\n  1 nCr, nPr, n!$")
    s.lines += ["keys 2nd,mode", "run 300"]

    expect("iterate a+n", s.tool("3", "1", ["a+n", "", "4", "", "5"]), r"^ f\(5\) = 19$")
    expect("iterate 3a", s.tool("3", "1", ["3a", "1", "3", "", "4"]), r"^ f\(4\) = 81$")
    expect("closed form, c1 = 2", s.tool("3", "2", ["2", "120", "0", "1"]),
           r"^ f\(n\) = 1/22\*12\^n - 1/22\*\(-10\)\^n$")
    expect("closed form, c1 = 3", s.tool("3", "2", ["3", "-2", "1", "2"]), r"^ f\(n\) = 2\^n$")
    sums = s.tool("3", "3", ["10"])
    expect("sum of k", sums, r"^ 1\+2\+\.\.\.\+n = 55$")
    expect("sum of k^3", sums, r"^ 1\^3\+\.\.\.\+n\^3 = 3025$")

    expect("Bayes, P(X|F) = 1", s.tool("4", "1", ["1", "0.01", "0.015"]), r"^ P\(F\|X\) = 200/497 ~ 0\.4024$")
    expect("Bayes, P(X|F) = 0.08", s.tool("4", "1", ["0.08", "0.4", "0.01"]), r"^ P\(F\|X\) = 16/19 ~ 0\.8421$")
    expect("and, or", s.tool("4", "2", ["1/2", "1/3"]), r"^ P\(AuB\) = 2/3 ~ 0\.6667$")
    expect("binomial count", s.tool("4", "3", ["20", "1/4", "5"], wait=6000), r"^ E\[X\] = np = 5$")
    expect("tosses", s.tool("4", "4", ["1/6", "", "1/2"], wait=3000), r"^ n = 4: 671/1296 ~ 0\.5177$")
    expect("at least once", s.tool("4", "4", ["1/10", "2"]), r"^ P = 19/100 = 0\.19$")
    expect("expected value", s.tool("4", "5", ["1,2,3,4,5,6"]), r"^ E\[X\] = 7/2 = 3\.5$")

    expect("bits", s.tool("5", "1", ["33"]), r"^ bits: floor\(log2 n\) \+ 1 = 6$")
    expect("sort counts", s.tool("5", "2", ["10"]), r"^ selection: 45 compares, always$")
    expect("master theorem, merge sort", s.tool("5", "3", ["2", "2", "1"]), r"^ Theta\(n\^1 log n\)$")
    expect("master theorem, leaves", s.tool("5", "3", ["3", "2", "1"]),
           r"^ Theta\(n\^log2\(3\)\) ~ n\^1\.58$")

    s.lines += ["keys 6", "run 500"]
    if tios:
        s.lines += ["keys 1", "run 500"]
        expect("Notes shows the archived NUMR.py", s.screenshot(), r"^ NUMR\n notes\n line 0$")
        s.lines += ["keys right", "run 500"]
        expect("RIGHT pages down", s.screenshot(), r"^ line 25\n")
        s.lines += ["keys clear", "run 200", "keys clear", "run 300"]
        _, notes_shot = s.tool("1", "3", ["33", "13"], then=["keys window", "run 500"])
        expect("WINDOW opens a tool's notes at its heading", notes_shot,
               r"^ NUMR   == INVERSE ==\n == INVERSE ==\n exists iff gcd\(x,N\)=1$")
    else:
        expect("Notes with an empty archive", s.screenshot(), r"^ No notes in the archive\.$")
        s.lines += ["keys clear", "run 300"]
    expect("CLEAR goes back to the menu", s.screenshot(), menu)
    s.lines += ["keys 5", "run 500", "keys 1", "run 500", "keys clear", "run 500"]
    expect("CLEAR in a form goes back to its section", s.screenshot(),
           r"^ Algorithms\n  1 bits and binary search$")
    s.lines += ["keys clear", "run 500", "keys 7", "run 500"]
    expect("entry 7 opens the calculator", s.command("1+1"),
           r"^fractions; MATH lists the functions\n= 1\+1\n2$")
    s.lines += ["keys clear", "run 500"]
    expect("CLEAR leaves the calculator for the menu", s.screenshot(), menu)

    banner = s.shell()
    expect("entry 8 opens the shell", banner, r"^type help and press ENTER\n>")
    expect("the shell is in alpha lock", banner, alpha_bar)
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
    held = s.screenshot()
    expect("keys shows a held key", held, r"^5 \. \. \. # ")
    # The bar shows only HH:MM, so wait for the minute to change.
    s.lines.append("run 61000")
    expect_changed("the status bar's clock runs during keys", s.screenshot(), held, status_bar)
    s.lines += ["keys up:9", "keys clear", "run 200"]

    # ON stops the command, and only the command: a press seen again at the
    # prompt would go back to the menu.
    s.type("keys")
    s.lines += ["keys enter", "run 200", "key on", "run 200"]
    expect("ON aborts keys", s.command("echo back"), r"^back$")

    # calc switches to digit mode, and each way out puts the shell back in
    # alpha lock. Checks match the expression line with its result, on a
    # cleared screen: keys leaves its matrix drawn at the bottom.
    s.type("clear")
    s.lines += ["keys enter", "run 200"]
    def enter_calc() -> Path:
        shot = s.command("calc")
        s.alpha = False
        return shot

    def leave_calc(keys: str):
        s.lines += [keys, "run 200"]
        s.alpha = True

    expect("calc starts in digit mode", enter_calc(), digit_bar)
    expect("calc multiplies", s.command("12*34"), r"^= 12\*34\n408$")
    s.lines.append("keys up")
    expect("calc recalls the last expression", s.command("+1"), r"^= 12\*34\+1\n409$")
    expect("calc: * and / before + and -", s.command("1+2*3-8/4"), r"^= 1\+2\*3-8/4\n5$")
    expect("calc: / is exact", s.command("-7/2*100"), r"^= -7/2\*100\n-350$")
    expect("calc shows a fraction and its decimal", s.command("7/2"), r"^= 7/2\n7/2 = 3\.5$")
    expect("calc: ^ before unary minus, right to left", s.command("-2^3^2"), r"^= -2\^3\^2\n-512$")
    expect("calc: parentheses", s.command("(1+2)*(3+4)"), r"^= \(1\+2\)\*\(3\+4\)\n21$")
    expect("calc reaches 2^63-1", s.command("2^62-1+2^62"), r"^= 2\^62-1\+2\^62\n9223372036854775807$")
    expect("calc overflow", s.command("2^63"), r"^= 2\^63\noverflow$")
    expect("calc divide by zero", s.command("7/0"), r"^= 7/0\ndivide by zero$")
    expect("calc negative exponent", s.command("2^-1"), r"^= 2\^-1\n1/2 = 0\.5$")
    expect("calc reads decimals exactly", s.command("1.5"), r"^= 1\.5\n3/2 = 1\.5$")
    expect("calc missing )", s.command("(1+2"), r"^= \(1\+2\nmissing \)$")
    expect("calc syntax error", s.command("1+*2"), r"^= 1\+\*2\nsyntax error$")
    expect("calc functions", s.command("ncr(12,5)*ncr(8,4)"), r"^= ncr\(12,5\)\*ncr\(8,4\)\n55440$")
    expect("calc", s.command("1-(9/10)^2"), r"^= 1-\(9/10\)\^2\n19/100 = 0\.19$")
    expect("calc: div rounds down", s.command("div(-457,22)"), r"^= div\(-457,22\)\n-21$")
    expect("calc: mod is never negative", s.command("mod(-457,22)"), r"^= mod\(-457,22\)\n5$")
    s.lines += ["keys math", "run 200", "keys 1", "run 200"]
    expect("MATH types a function's name", s.command("12,3)"), r"^= ncr\(12,3\)\n220$")
    s.type("99")
    leave_calc("keys clear,clear")
    shot = s.command("echo cleared")
    expect("CLEAR on an empty line leaves calc", shot, r"^=\n> echo cleared\ncleared$")
    expect("leaving calc restores alpha lock", shot, alpha_bar)
    enter_calc()
    leave_calc("keys 2nd,mode")
    expect("2nd mode leaves calc, not the shell", s.command("echo quit"), r"^> echo quit\nquit$")
    enter_calc()
    leave_calc("key on")
    expect("ON leaves calc, not the shell", s.command("echo on"), r"^> echo on\non$")

    # ON and 2nd mode at the prompt go back to the menu.
    s.lines += ["key on", "run 300"]
    s.alpha = False
    expect("ON at the prompt goes to the menu", s.screenshot(), menu)
    s.shell()
    s.lines += ["keys 2nd,mode", "run 300"]
    s.alpha = False
    expect("2nd mode at the prompt goes to the menu", s.screenshot(), menu)

    if tios:
        # Each way out goes back to arTIfiCE's launcher, and the next run
        # starts fresh.
        s.lines += ["keys 2nd,mode", "run 1500"]
        expect_same("2nd mode at the menu returns to arTIfiCE", s.screenshot(), launcher)
        expect("a new run starts on the menu", s.start_kernel(), menu)
        s.lines += ["key on", "run 1500"]
        expect_same("ON at the menu returns to arTIfiCE", s.screenshot(), launcher)
        s.start_kernel()
        s.shell()
        expect_same("exit returns to arTIfiCE", s.command("exit"), launcher)

        # Quitting arTIfiCE leaves a TI-OS that still computes and runs programs.
        s.lines += ["keys mode", "run 1500", "keys clear,1,add,1,enter", "run 500"]
        expect_same("TI-OS home screen works", s.screenshot(), home, HOME_ROWS)
        expect_same("arTIfiCE starts again", s.start_artifice(), launcher)
        again = s.start_kernel()
        expect("the kernel starts again", again, menu)
        # keys alone took over a minute, so the clock has moved on from 13:45.
        expect("TI-OS's clock kept running through each exit", again, r" 13:(4[6-9]|5\d)$")
    else:
        # ON exits on a calculator; the bare ROM has nowhere to go and must ignore it.
        s.lines += ["key on", "run 200", "key on", "run 200"]
        expect("ON is ignored at the menu", s.screenshot(), menu)
        s.shell()
        expect("exit halts the bare ROM", s.command("exit"), r"^halted; nothing to return to$")
    s.lines.append("quit")

    proc = subprocess.run(
        [args.headless, "--rom", str(args.rom)],
        input="\n".join(s.lines) + "\n",
        capture_output=True,
        text=True,
        timeout=900,
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
        elif isinstance(test[1], re.Pattern):
            earlier, pattern = test
            before = pattern.search("\n".join(screen_text(earlier, font)))
            after = pattern.search("\n".join(text))
            ok = bool(before and after) and before.group(1) != after.group(1)
        else:
            ref, (top, bottom) = test
            ok = read_bmp(shot)[top:bottom] == read_bmp(ref)[top:bottom]
        print(f"{'ok  ' if ok else 'FAIL'} {desc}")
        if not ok:
            failed += 1
            if isinstance(test, tuple) and isinstance(test[1], re.Pattern):
                print(f"     earlier screen {test[0].name} was:\n"
                      + "\n".join("     |" + l for l in screen_text(test[0], font)))
            elif not isinstance(test, re.Pattern):
                hint = "" if args.keep else "; rerun with --keep DIR to see both"
                print(f"     differs from {ref.name} in pixel rows {top}-{bottom - 1}{hint}")
            print("     screen was:\n" + "\n".join("     |" + l for l in text))
    if not args.keep:
        shutil.rmtree(shots)
    print(f"{len(checks) - failed}/{len(checks)} checks passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
