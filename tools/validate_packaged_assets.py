#!/usr/bin/env python3
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
errors = []

OLED_NAMES = [
    "vitabright_lut.txt",
    "vitabright_lut_p4.txt",
    "vitabright_lut_p5.txt",
    "vitabright_lut_p6.txt",
]
LCD_NAME = "vitabright_lcd_lut.txt"


def fail(path: Path, message: str) -> None:
    try:
        name = path.relative_to(ROOT)
    except ValueError:
        name = path
    errors.append(f"{name}: {message}")


def require_identical(source: Path, packaged: Path) -> None:
    if not source.is_file():
        fail(source, "authoritative source is missing")
        return
    if not packaged.is_file():
        fail(packaged, "packaged copy is missing")
        return
    if source.read_bytes() != packaged.read_bytes():
        fail(packaged, f"does not byte-match authoritative source {source.relative_to(ROOT)}")


def run_host_test(name: str, sources: list[str]) -> None:
    with tempfile.TemporaryDirectory(prefix=f"vbe-{name}-") as temp:
        exe = Path(temp) / name
        command = [
            "cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
            "-I", str(ROOT),
            *[str(ROOT / source) for source in sources],
            "-o", str(exe),
        ]
        try:
            subprocess.run(command, cwd=ROOT, check=True)
            subprocess.run([str(exe)], cwd=ROOT, check=True)
        except (OSError, subprocess.CalledProcessError) as exc:
            errors.append(f"{name} host regression failed: {exc}")


for name in OLED_NAMES:
    require_identical(ROOT / "oled" / "luts" / name,
                      ROOT / "release" / "ur0_tai" / name)

require_identical(ROOT / "lcd" / "luts" / LCD_NAME,
                  ROOT / "release" / "ur0_tai" / LCD_NAME)
require_identical(ROOT / "vitabrightex.cfg",
                  ROOT / "release" / "ur0_tai" / "vitabrightex.cfg")

run_host_test("lut_parser_host", [
    "tests/lut_parser_host.c",
    "text_stream.c",
    "lcd/lut_parser.c",
    "oled/lut_parser.c",
])
run_host_test("config_parser_host", [
    "tests/config_parser_host.c",
    "text_stream.c",
    "config_parser.c",
])
run_host_test("status_error_host", [
    "tests/status_error_host.c",
])

if errors:
    for error in errors:
        print(f"ERROR: {error}", file=sys.stderr)
    raise SystemExit(1)

print("packaged assets + production parser/status contracts: OK")
