#!/usr/bin/env python3
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
errors = []

OLED_NAMES = [
    "vitabright_lut.txt",
    "vitabright_lut_p4.txt",
    "vitabright_lut_p5.txt",
    "vitabright_lut_p6.txt",
]
LCD_NAME = "vitabright_lcd_lut.txt"
HEX = re.compile(r"^[0-9A-Fa-f]{2}$")


def fail(path: Path, message: str) -> None:
    errors.append(f"{path.relative_to(ROOT)}: {message}")


def data_lines(path: Path):
    for lineno, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#") or line.startswith(";"):
            continue
        yield lineno, line


def parse_oled(path: Path) -> bytes:
    rows = []
    for lineno, line in data_lines(path):
        fields = line.split()
        if len(fields) != 21:
            fail(path, f"line {lineno}: expected 21 hex bytes, got {len(fields)}")
            continue
        if any(not HEX.fullmatch(field) for field in fields):
            fail(path, f"line {lineno}: every field must be exactly two hexadecimal digits")
            continue
        rows.append(bytes(int(field, 16) for field in fields))

    if len(rows) != 17:
        fail(path, f"expected exactly 17 data rows, got {len(rows)}")
    blob = b"".join(rows)
    if len(blob) != 357:
        fail(path, f"expected 357 data bytes, got {len(blob)}")
    if blob and (all(v == 0 for v in blob) or all(v == 0xFF for v in blob)):
        fail(path, "degenerate all-00/all-FF table")
    return blob


def parse_lcd(path: Path) -> tuple[int, ...]:
    values = []
    for lineno, line in data_lines(path):
        if not line.isdecimal():
            fail(path, f"line {lineno}: expected an unsigned decimal byte")
            continue
        value = int(line, 10)
        if not 0 <= value <= 255:
            fail(path, f"line {lineno}: value {value} is outside 0..255")
            continue
        values.append(value)

    if len(values) != 17:
        fail(path, f"expected exactly 17 values, got {len(values)}")
    if any(b < a for a, b in zip(values, values[1:])):
        fail(path, "brightness values must be monotonic nondecreasing")
    return tuple(values)


def require_identical(source: Path, packaged: Path) -> None:
    if not packaged.is_file():
        fail(packaged, "packaged copy is missing")
        return
    if source.read_bytes() != packaged.read_bytes():
        fail(packaged, f"does not byte-match authoritative source {source.relative_to(ROOT)}")


for name in OLED_NAMES:
    source = ROOT / "oled" / "luts" / name
    packaged = ROOT / "release" / "ur0_tai" / name
    if not source.is_file():
        fail(source, "authoritative OLED LUT is missing")
        continue
    parse_oled(source)
    if packaged.is_file():
        parse_oled(packaged)
    require_identical(source, packaged)

lcd_source = ROOT / "lcd" / "luts" / LCD_NAME
lcd_packaged = ROOT / "release" / "ur0_tai" / LCD_NAME
if lcd_source.is_file():
    parse_lcd(lcd_source)
    if lcd_packaged.is_file():
        parse_lcd(lcd_packaged)
    require_identical(lcd_source, lcd_packaged)
else:
    fail(lcd_source, "authoritative LCD LUT is missing")

require_identical(ROOT / "vitabrightex.cfg", ROOT / "release" / "ur0_tai" / "vitabrightex.cfg")

if errors:
    for error in errors:
        print(f"ERROR: {error}", file=sys.stderr)
    raise SystemExit(1)

print("packaged LUT/config assets: OK")
