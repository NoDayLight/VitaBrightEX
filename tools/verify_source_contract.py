#!/usr/bin/env python3
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]

C_SOURCES = [
    ROOT / "main.c",
    ROOT / "screen_filter.c",
    ROOT / "lcd" / "hooks.c",
    ROOT / "oled" / "hooks.c",
]

errors = []

def require(path: Path, needle: str, message: str) -> None:
    text = path.read_text(encoding="utf-8")
    if needle not in text:
        errors.append(message)


def forbid(path: Path, needle: str, message: str) -> None:
    text = path.read_text(encoding="utf-8")
    if needle in text:
        errors.append(message)


# The v1.3 crash candidate must never return to executable kernel source.
for path in C_SOURCES:
    forbid(path, "0x0FCBF457", f"{path.relative_to(ROOT)} reintroduces speculative IFTU NID 0x0FCBF457")

# The current v1.4 core must have no newly introduced continuous worker.
for path in C_SOURCES:
    for symbol in (
        "ksceKernelCreateThread",
        "sceKernelCreateThread",
        "ksceKernelCreateTimer",
        "sceKernelCreateTimer",
        "ksceKernelDelayThread",
        "sceKernelDelayThread",
    ):
        forbid(path, symbol, f"{path.relative_to(ROOT)} introduces continuous/background worker primitive {symbol}")

# Persistent registry mutation was explicitly removed from the LCD boot path.
for symbol in ("ksceRegMgrSetKeyInt", "NID_REGMGR_SET_KEY_INT", "0x23B99BDE"):
    forbid(ROOT / "lcd" / "hooks.c", symbol, f"LCD backend reintroduces persistent registry mutation ({symbol})")

# OLED panel bytes remain opaque: no old pseudo-RGB transforms.
for symbol in ("normalise_white_point", "apply_color_bias", "apply_night_mode"):
    forbid(ROOT / "oled" / "hooks.c", symbol, f"OLED backend reintroduces unverified panel-byte transform {symbol}")

# Firmware support is fail-closed and LCD raw injection is signature-gated.
require(ROOT / "lcd" / "hooks.c", "lcd_validate_layout", "LCD backend lost runtime layout signature validation")
require(ROOT / "lcd" / "hooks.c", "lcd_stock_signature", "LCD backend lost stock table signature")
for version in ("0x371", "0x372", "0x373", "0x374"):
    forbid(ROOT / "lcd" / "hooks.c", f"case {version}", f"LCD raw table support for {version} was added without a verified layout")
    forbid(ROOT / "oled" / "hooks.c", f"case {version}", f"OLED raw table support for {version} was added without a verified layout")

# Capability honesty / serialization are part of the public v1.4 contract.
require(ROOT / "status.c", "s.abi_version = 2", "status ABI is no longer v2")
require(ROOT / "screen_filter.c", "g_vbe_status.csc_filter = VBE_CAP_UNSUPPORTED", "CSC is no longer explicitly reported unsupported")
require(ROOT / "screen_filter.c", "g_vbe_status.transfer_lut = VBE_CAP_UNSUPPORTED", "nonlinear transfer LUT is no longer explicitly reported unsupported")
require(ROOT / "main.c", "state_lock_init()", "global state-transition lock initialization is missing")
require(ROOT / "main.c", "oled_reload_backend()", "generic reload bypasses OLED rollback-aware backend reload")
require(ROOT / "main.c", "lcd_reload_backend()", "generic reload bypasses LCD rollback-aware backend reload")

if errors:
    for error in errors:
        print(f"ERROR: {error}", file=sys.stderr)
    sys.exit(1)

print("pseudo-v1.4 source contract: OK")
