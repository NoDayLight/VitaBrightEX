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

for path in ("config.c", "lcd/hooks.c", "oled/parser.c"):
    need(path, "vbe_source_evaluate", f"{path} bypasses shared source-authority evaluator")
ban("lcd/hooks.c", "int *opened", "LCD open-success flag authority model returned")
ban("oled/parser.c", "int *opened", "OLED open-success flag authority model returned")
ban("oled/parser.c", "int *was_present", "OLED presence flag authority model returned")
need("source_authority.c", "open_result == VBE_SCE_IO_ERROR_NOT_FOUND",
     "source authority no longer restricts fallback to project-owned not-found result")
ban("source_authority.h", "#define SCE_ERROR_ERRNO_ENOENT",
    "source authority pretends project compatibility knowledge is SDK-owned")
need("tools/validate_packaged_assets.py", "tests/source_authority_host.c",
     "source authority behavior is no longer host tested")

need("tools/validate_packaged_assets.py", "tests/config_parser_host.c", "config behavior is no longer host tested")
need("tests/lut_parser_host.c", "CR inside token rejected", "LCD CR-in-token regression missing")
need("tests/lut_parser_host.c", "OLED CR inside byte rejected", "OLED CR-in-byte regression missing")
need("tests/config_parser_host.c", "interior CR rejected", "config CR regression missing")
need("tests/config_parser_host.c", "suffix garbage rejected", "config numeric strictness regression missing")
need("tests/config_parser_host.c", "color-space alias pair uses last valid occurrence",
     "config alias precedence regression missing")

need("lcd/hooks.c", "VBE_OWNERSHIP_DEGRADED", "LCD backend lost explicit degraded ownership state")
need("oled/hooks.c", "VBE_OWNERSHIP_DEGRADED", "OLED backend lost explicit degraded ownership state")
for path in ("lcd/hooks.c", "oled/hooks.c"):
    need(path, "vbe_txn_should_rollback", f"{path} bypasses shared rollback legality policy")
    need(path, "VBE_ERR_RESOURCE_RELEASE", f"{path} no longer reports dirty ownership")
ban("lcd/hooks.c", "g_lcd_hooks_active", "LCD split active/ownership truth returned")
ban("oled/hooks.c", "g_active", "OLED split active/ownership truth returned")
need("main.c", "SCE_KERNEL_STOP_FAIL", "module stop can no longer refuse unsafe unload")
need("main.c", "state_lock_begin_shutdown", "module stop bypasses serialized stop lifecycle")
need("tools/validate_packaged_assets.py", "tests/transaction_core_host.c",
     "production transaction/ownership core is no longer host tested")
need("tools/validate_packaged_assets.py", "tests/persistence_core_host.c",
     "production persistence core is no longer host tested")

if errors:
    for error in errors:
        print(f"ERROR: {error}", file=sys.stderr)
    raise SystemExit(1)
print("parser/runtime/CI structural contract: OK")
