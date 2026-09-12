#!/usr/bin/env python3
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]

C_SOURCES = [
    ROOT / "main.c",
    ROOT / "color_space.c",
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
    forbid(path, "0x0FCBF457",
           f"{path.relative_to(ROOT)} reintroduces speculative IFTU NID 0x0FCBF457")

# The v1.4 core has no continuous worker, timer or periodic filesystem path.
for path in C_SOURCES:
    for symbol in (
        "ksceKernelCreateThread",
        "sceKernelCreateThread",
        "ksceKernelCreateTimer",
        "sceKernelCreateTimer",
        "ksceKernelDelayThread",
        "sceKernelDelayThread",
    ):
        forbid(path, symbol,
               f"{path.relative_to(ROOT)} introduces background worker primitive {symbol}")

# Persistent registry mutation is outside the handheld feature set. In
# particular rgb_range_mode must never become a PCH-2000 registry write.
for path in C_SOURCES:
    for symbol in ("ksceRegMgrSetKeyInt", "NID_REGMGR_SET_KEY_INT", "0x23B99BDE"):
        forbid(path, symbol,
               f"{path.relative_to(ROOT)} reintroduces persistent registry mutation ({symbol})")

# The OLED row structure is known, but direct arithmetic on driver-specific
# gamma register codes is still prohibited until their voltage transfer is
# independently established.
for symbol in ("normalise_white_point", "apply_color_bias", "apply_night_mode"):
    forbid(ROOT / "oled" / "hooks.c", symbol,
           f"OLED backend reintroduces unverified gamma-code transform {symbol}")

# Firmware support is fail-closed and raw injection is runtime-gated.
require(ROOT / "lcd" / "hooks.c", "lcd_validate_layout",
        "LCD backend lost runtime layout signature validation")
require(ROOT / "lcd" / "hooks.c", "lcd_stock_signature",
        "LCD backend lost stock table signature")
require(ROOT / "oled" / "hooks.c", "validate_layout",
        "OLED backend lost loaded-module offset/plausibility validation")
require(ROOT / "oled" / "hooks.c", "module_get_offset",
        "OLED backend no longer resolves inherited raw offset before injection")
for version in ("0x371", "0x372", "0x373", "0x374"):
    forbid(ROOT / "lcd" / "hooks.c", f"case {version}",
           f"LCD raw table support for {version} was added without a verified layout")
    forbid(ROOT / "oled" / "hooks.c", f"case {version}",
           f"OLED raw table support for {version} was added without a verified layout")

# Session colour-space writes must be reversible and verified with the matched
# getter on both panel backends. No set-only implementation is acceptable.
for nid in ("0x17F66722", "0xD40968FB", "0x4F8A1D4A", "0xDABBD9D3"):
    require(ROOT / "color_space.c", nid, f"color-space module lost verified export {nid}")
require(ROOT / "color_space.c", "g_original_mode",
        "color-space module no longer snapshots firmware-owned state")
require(ROOT / "color_space.c", "current = g_get_mode()",
        "color-space writes are no longer read-back verified")
require(ROOT / "main.c", "color_space_shutdown()",
        "module stop no longer restores session color-space state")

# Capability honesty / serialization are public v1.4 contracts.
require(ROOT / "status.c", "s.abi_version = 2", "status ABI is no longer v2")
require(ROOT / "screen_filter.c", "g_vbe_status.csc_filter = VBE_CAP_UNSUPPORTED",
        "CSC is no longer explicitly reported unsupported")
require(ROOT / "screen_filter.c", "g_vbe_status.transfer_lut = VBE_CAP_UNSUPPORTED",
        "nonlinear transfer LUT is no longer explicitly reported unsupported")
require(ROOT / "main.c", "state_lock_init()", "global state-transition lock initialization is missing")
require(ROOT / "main.c", "oled_reload_backend()",
        "generic reload bypasses OLED rollback-aware backend reload")
require(ROOT / "main.c", "lcd_reload_backend()",
        "generic reload bypasses LCD rollback-aware backend reload")
require(ROOT / "module.yml", "vitabrightColorSpaceGetMode",
        "color-space getter syscall is missing from generated ABI")
require(ROOT / "module.yml", "vitabrightColorSpaceSetMode",
        "color-space setter syscall is missing from generated ABI")

if errors:
    for error in errors:
        print(f"ERROR: {error}", file=sys.stderr)
    sys.exit(1)

print("pseudo-v1.4 source contract: OK")
