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
run_host_test("source_authority_host", [
    "tests/source_authority_host.c",
    "source_authority.c",
])
run_host_test("config_source_state_host", [
    "tests/config_source_state_host.c",
    "config_state_core.c",
    "source_authority.c",
])
run_host_test("transaction_core_host", [
    "tests/transaction_core_host.c",
    "transaction_core.c",
    "source_authority.c",
])
run_host_test("result_core_host", [
    "tests/result_core_host.c",
    "result_core.c",
])
run_host_test("persistence_core_host", [
    "tests/persistence_core_host.c",
    "persistence_core.c",
])
run_host_test("state_lock_core_host", [
    "tests/state_lock_core_host.c",
    "state_lock_core.c",
    "result_core.c",
])
run_host_test("module_lifecycle_core_host", [
    "tests/module_lifecycle_core_host.c",
    "module_lifecycle_core.c",
    "state_lock_core.c",
    "result_core.c",
])
run_host_test("status_error_host", [
    "tests/status_error_host.c",
])
run_host_test("filter_policy_host", [
    "tests/filter_policy_host.c",
    "filter_policy.c",
    "filter_state_core.c",
])
run_host_test("filter_state_host", [
    "tests/filter_state_host.c",
    "filter_state_core.c",
])
run_host_test("oled_transform_host", [
    "tests/oled_transform_host.c",
    "oled/transform_core.c",
])
run_host_test("oled_state_host", [
    "tests/oled_state_host.c",
    "oled/state_core.c",
    "oled/transform_core.c",
    "source_authority.c",
])
run_host_test("affine_core_host", [
    "tests/affine_core_host.c",
    "affine_core.c",
])

if errors:
    for error in errors:
        print(f"ERROR: {error}", file=sys.stderr)
    raise SystemExit(1)

print("packaged assets + production contracts: OK")
