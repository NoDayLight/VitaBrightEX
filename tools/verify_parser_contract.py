#!/usr/bin/env python3
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
errors = []

def need(path, text, message):
    if text not in (ROOT / path).read_text(encoding="utf-8"):
        errors.append(message)

def ban(path, text, message):
    if text in (ROOT / path).read_text(encoding="utf-8"):
        errors.append(message)

need("lcd/hooks.c", "vbe_lcd_lut_parser_feed", "LCD backend is not using shared streaming parser")
need("oled/parser.c", "vbe_oled_lut_parser_feed", "OLED backend is not using shared streaming parser")
ban("lcd/hooks.c", "char line[64]", "LCD fixed physical-line buffer returned")
ban("oled/parser.c", "PARSER_LINE_MAX", "OLED fixed physical-line buffer returned")
need("config.c", "if (comment) continue;", "config no longer streams long comments")
need("config.c", "if (too_long) continue;", "config can split overlong directives again")
need("tools/validate_packaged_assets.py", "lut_parser_host.c", "asset CI is not compiling production parser tests")
need("tests/lut_parser_host.c", "16384", "long-comment regression coverage missing")

lcd = (ROOT / "lcd/hooks.c").read_text(encoding="utf-8")
if "if (opened) {\n        if (ret == 0) path_copy(source, LCD_LUT_FILE1);\n        return ret;\n    }" not in lcd:
    errors.append("malformed preferred LCD source may fall through")
if "if (opened) {\n        if (ret == 0) path_copy(source, LCD_LUT_FILE2);\n        return ret;\n    }" not in lcd:
    errors.append("LCD fallback authority contract changed")
need("oled/parser.c", "if (present) return ret;", "malformed preferred OLED source may fall through")

if errors:
    for error in errors:
        print(f"ERROR: {error}", file=sys.stderr)
    raise SystemExit(1)
print("parser/runtime/CI contract: OK")
