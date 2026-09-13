#!/usr/bin/env python3
from pathlib import Path
import hashlib
import sys

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "generated" / "oled_stock_signatures.h"
SOURCES = [
    ("p4", ROOT / "oled/luts/orig/vitabright_lut_4_orig.txt", "b360b7519e4e750ad5556ce264bf1aa72929cca5beaff249d404fd571fd27438"),
    ("p5", ROOT / "oled/luts/orig/vitabright_lut_5_orig.txt", "54d5e1bd18bf00ca50081467800495e5360835ee31a0807cfc4c767c19855572"),
    ("generic", ROOT / "oled/luts/orig/vitabright_lut_orig.txt", "268919f6d9da00993105efb718588714256cd56b35011b93353618a5d253e5c1"),
]

def parse(path):
    values = []
    rows = 0
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        tokens = line.split()
        if len(tokens) != 21:
            raise SystemExit(f"{path}: expected 21 bytes in row {rows}, got {len(tokens)}")
        row = [int(token, 16) for token in tokens]
        if any(value < 0 or value > 255 or len(token) != 2 for value, token in zip(row, tokens)):
            raise SystemExit(f"{path}: every item must be a two-digit byte")
        values.extend(row)
        rows += 1
    if rows != 17 or len(values) != 357:
        raise SystemExit(f"{path}: expected 17x21=357 bytes, got {rows} rows/{len(values)} bytes")
    return bytes(values)

def array(name, data):
    lines = [f"static const unsigned char {name}[LUT_SIZE] = {{"]
    for start in range(0, len(data), 12):
        chunk = data[start:start + 12]
        suffix = "," if start + 12 < len(data) else ""
        lines.append("    " + ", ".join(f"0x{value:02X}" for value in chunk) + suffix)
    lines.append("};")
    return "\n".join(lines)

def render():
    parsed = {}
    for key, path, expected_hash in SOURCES:
        data = parse(path)
        digest = hashlib.sha256(data).hexdigest()
        if digest != expected_hash:
            raise SystemExit(f"{path}: SHA-256 {digest} != research evidence {expected_hash}")
        parsed[key] = data
    return (
        '#pragma once\n#include "../oled/lut.h"\n\n'
        '/* Generated from oled/luts/orig by tools/generate_oled_stock_signatures.py.\n * Do not edit by hand. */\n'
        f'#define VBE_OLED_STOCK_P4_SHA256 "{SOURCES[0][2]}"\n'
        f'#define VBE_OLED_STOCK_P5_SHA256 "{SOURCES[1][2]}"\n'
        f'#define VBE_OLED_STOCK_GENERIC_SHA256 "{SOURCES[2][2]}"\n\n'
        + array("vbe_oled_stock_p4", parsed["p4"]) + "\n\n"
        + array("vbe_oled_stock_p5", parsed["p5"]) + "\n\n"
        + array("vbe_oled_stock_generic", parsed["generic"]) + "\n"
    )

def main():
    generated = render()
    if len(sys.argv) == 2 and sys.argv[1] == "--check":
        if not OUT.is_file() or OUT.read_text(encoding="utf-8") != generated:
            print(f"{OUT.relative_to(ROOT)} is stale", file=sys.stderr)
            return 1
        print("OLED stock-signature provenance: OK")
        return 0
    if len(sys.argv) != 1:
        print("usage: generate_oled_stock_signatures.py [--check]", file=sys.stderr)
        return 2
    sys.stdout.write(generated)
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
