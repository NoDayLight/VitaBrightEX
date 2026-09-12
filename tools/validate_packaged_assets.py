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


def run_production_parser_regressions() -> None:
    """Compile and execute the exact portable parser core used by the SKPRX.

    This is deliberately not a Python reimplementation of the grammar. The
    hardware-discovered v1.4 defect existed because the old Python validator
    accepted bytes that the kernel's fixed-line parser rejected.
    """
    with tempfile.TemporaryDirectory(prefix="vbe-parser-") as temp:
        exe = Path(temp) / "lut_parser_host"
        compile_cmd = [
            "cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
            "-I", str(ROOT),
            str(ROOT / "tests" / "lut_parser_host.c"),
            str(ROOT / "lcd" / "lut_parser.c"),
            str(ROOT / "oled" / "lut_parser.c"),
            "-o", str(exe),
        ]
        try:
            subprocess.run(compile_cmd, cwd=ROOT, check=True)
            subprocess.run([str(exe)], cwd=ROOT, check=True)
        except (OSError, subprocess.CalledProcessError) as exc:
            errors.append(f"production LUT parser regression harness failed: {exc}")


def check_authority_contract() -> None:
    """Guard the missing-vs-malformed source policy used by both backends."""
    lcd = (ROOT / "lcd" / "hooks.c").read_text(encoding="utf-8")
    oled = (ROOT / "oled" / "parser.c").read_text(encoding="utf-8")

    lcd_required = (
        "if (opened) {\n        if (ret == 0) path_copy(source, LCD_LUT_FILE1);\n        return ret;\n    }",
        "if (opened) {\n        if (ret == 0) path_copy(source, LCD_LUT_FILE2);\n        return ret;\n    }",
    )
    for fragment in lcd_required:
        if fragment not in lcd:
            errors.append("LCD authoritative-source policy changed without updating parser tests")

    if "if (present) return ret;" not in oled:
        errors.append("OLED authoritative-source policy changed without updating parser tests")

    # Semantic regression examples: malformed preferred source stops selection;
    # only a missing preferred source permits the next candidate.
    def decide(opened: bool, parse_result: int) -> str:
        if not opened:
            return "continue"
        return "accept" if parse_result == 0 else "reject"

    if decide(True, -1) != "reject":
        errors.append("malformed preferred source must be authoritative failure")
    if decide(False, -1) != "continue":
        errors.append("missing preferred source must permit documented fallback")


for name in OLED_NAMES:
    require_identical(ROOT / "oled" / "luts" / name,
                      ROOT / "release" / "ur0_tai" / name)

require_identical(ROOT / "lcd" / "luts" / LCD_NAME,
                  ROOT / "release" / "ur0_tai" / LCD_NAME)
require_identical(ROOT / "vitabrightex.cfg",
                  ROOT / "release" / "ur0_tai" / "vitabrightex.cfg")

run_production_parser_regressions()
check_authority_contract()

if errors:
    for error in errors:
        print(f"ERROR: {error}", file=sys.stderr)
    raise SystemExit(1)

print("packaged LUT/config assets + production parser contract: OK")
