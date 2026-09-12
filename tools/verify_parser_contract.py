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


need("lcd/hooks.c", "vbe_lcd_lut_parser_feed", "LCD backend is not using production streaming parser")
need("oled/parser.c", "vbe_oled_lut_parser_feed", "OLED backend is not using production streaming parser")
need("config.c", "vbe_config_parser_feed", "config loader bypasses portable production parser")
need("lcd/lut_parser.c", "vbe_text_newline_feed", "LCD parser bypasses shared CRLF decoder")
need("oled/lut_parser.c", "vbe_text_newline_feed", "OLED parser bypasses shared CRLF decoder")
need("config_parser.c", "vbe_text_newline_feed", "config parser bypasses shared CRLF decoder")
ban("lcd/hooks.c", "char line[64]", "LCD fixed physical-line buffer returned")
ban("oled/parser.c", "PARSER_LINE_MAX", "OLED fixed physical-line buffer returned")
ban("config.c", "cfg_readline", "legacy config physical-line parser returned")

# Source authority must be shared executable production logic. These are only
# structural guards; tests/source_authority_host.c is the semantic proof.
for path in ("config.c", "lcd/hooks.c", "oled/parser.c"):
    need(path, "vbe_source_evaluate", f"{path} bypasses shared source-authority evaluator")
ban("lcd/hooks.c", "int *opened", "LCD open-success flag authority model returned")
ban("oled/parser.c", "int *opened", "OLED open-success flag authority model returned")
ban("oled/parser.c", "int *was_present", "OLED presence flag authority model returned")
need("source_authority.c", "open_result == SCE_ERROR_ERRNO_ENOENT",
     "source authority no longer restricts fallback to explicit ENOENT")
need("tools/validate_packaged_assets.py", "tests/source_authority_host.c",
     "source authority behavior is no longer host tested")

need("tools/validate_packaged_assets.py", "tests/config_parser_host.c", "config behavior is no longer host tested")
need("tests/lut_parser_host.c", "CR inside token rejected", "LCD CR-in-token regression missing")
need("tests/lut_parser_host.c", "OLED CR inside byte rejected", "OLED CR-in-byte regression missing")
need("tests/config_parser_host.c", "interior CR rejected", "config CR regression missing")
need("tests/config_parser_host.c", "suffix garbage rejected", "config numeric strictness regression missing")
need("tests/config_parser_host.c", "color-space alias pair uses last valid occurrence",
     "config alias precedence regression missing")

if errors:
    for error in errors:
        print(f"ERROR: {error}", file=sys.stderr)
    raise SystemExit(1)
print("parser/runtime/CI structural contract: OK")
