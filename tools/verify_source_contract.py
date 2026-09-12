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
    if needle not in path.read_text(encoding="utf-8"):
        errors.append(message)


def forbid(path: Path, needle: str, message: str) -> None:
    if needle in path.read_text(encoding="utf-8"):
        errors.append(message)


for path in C_SOURCES:
    forbid(path, "0x0FCBF457",
           f"{path.relative_to(ROOT)} reintroduces speculative IFTU NID")
    for symbol in (
        "ksceKernelCreateThread", "sceKernelCreateThread",
        "ksceKernelCreateTimer", "sceKernelCreateTimer",
        "ksceKernelDelayThread", "sceKernelDelayThread",
    ):
        forbid(path, symbol,
               f"{path.relative_to(ROOT)} introduces background worker primitive {symbol}")
    for symbol in ("ksceRegMgrSetKeyInt", "NID_REGMGR_SET_KEY_INT", "0x23B99BDE"):
        forbid(path, symbol,
               f"{path.relative_to(ROOT)} reintroduces persistent registry mutation ({symbol})")

for symbol in ("normalise_white_point", "apply_color_bias", "apply_night_mode"):
    forbid(ROOT / "oled" / "hooks.c", symbol,
           f"OLED backend reintroduces unverified gamma-code transform {symbol}")

require(ROOT / "lcd" / "hooks.c", "lcd_validate_layout", "LCD runtime signature validation missing")
require(ROOT / "lcd" / "hooks.c", "lcd_stock_signature", "LCD stock signature missing")
require(ROOT / "oled" / "hooks.c", "validate_layout", "OLED layout plausibility validation missing")
require(ROOT / "oled" / "hooks.c", "module_get_offset", "OLED inherited offset is no longer resolved")
for version in ("0x371", "0x372", "0x373", "0x374"):
    forbid(ROOT / "lcd" / "hooks.c", f"case {version}", f"LCD unverified raw layout {version} returned")
    forbid(ROOT / "oled" / "hooks.c", f"case {version}", f"OLED unverified raw layout {version} returned")

for nid in ("0x17F66722", "0xD40968FB", "0x4F8A1D4A", "0xDABBD9D3"):
    require(ROOT / "color_space.c", nid, f"color-space module lost verified export {nid}")
require(ROOT / "color_space.c", "g_original_mode", "color-space original state snapshot missing")
require(ROOT / "color_space.c", "current = g_get_mode()", "color-space read-back verification missing")
require(ROOT / "main.c", "color_space_shutdown()", "module stop no longer restores color-space state")

require(ROOT / "status.c", "s.abi_version = 2", "status ABI v2 changed unexpectedly")
require(ROOT / "status.c", "vitabrightGetDiagnostics", "per-domain diagnostics syscall missing")
require(ROOT / "status.c", "status_clear_error_domain", "scoped error clearing missing")
require(ROOT / "screen_filter.c", "g_vbe_status.csc_filter = VBE_CAP_UNSUPPORTED", "CSC truthfulness missing")
require(ROOT / "screen_filter.c", "g_vbe_status.transfer_lut = VBE_CAP_UNSUPPORTED", "transfer LUT truthfulness missing")
require(ROOT / "main.c", "state_lock_init()", "state-transition lock initialization missing")
require(ROOT / "main.c", "oled_reload_backend()", "generic reload bypasses OLED backend")
require(ROOT / "main.c", "lcd_reload_backend()", "generic reload bypasses LCD backend")
require(ROOT / "main.c", "VBE_ERROR_DOMAIN_CONFIG", "startup config error ownership missing")

for symbol in (
    "vitabrightGetDiagnostics", "vitabrightGetBuildId",
    "vitabrightColorSpaceGetMode", "vitabrightColorSpaceSetMode",
    "vitabrightOledPersistLut", "vitabrightLcdPersistBrightnessValues",
):
    require(ROOT / "module.yml", symbol, f"generated ABI is missing {symbol}")

root_cmake = ROOT / "CMakeLists.txt"
require(root_cmake, 'set(VBE_FTP_UR0 "${VBE_FTP_ROOT}//ur0:")',
        "FTP ur0 root is not canonical absolute VitaShell syntax")
require(root_cmake, 'set(VBE_FTP_UX0 "${VBE_FTP_ROOT}//ux0:")',
        "FTP ux0 root is not canonical absolute VitaShell syntax")
for relative in (":1337/ur0:/", ":1337/ux0:/"):
    forbid(root_cmake, relative, f"relative Vita FTP path returned: {relative}")
require(root_cmake, "VBE_BUILD_ID", "plugin build identity is not generated")
require(ROOT / "editor" / "CMakeLists.txt", "VBE_BUILD_ID", "editor build identity is not generated")

editor = ROOT / "editor" / "app.c"
require(ROOT / "editor" / "CMakeLists.txt", "add_executable(vitabrightex-editor.elf app.c)",
        "editor build no longer targets audited source")
require(editor, "vitabrightOledPersistLut()", "editor bypasses authoritative OLED persistence")
require(editor, "vitabrightLcdPersistBrightnessValues()", "editor bypasses authoritative LCD persistence")
require(editor, "LCD LUT entry %d/%d: %u", "editor mislabels LUT cursor")
require(editor, "Build plugin=%s editor=%s", "editor no longer exposes plugin/editor provenance")
require(editor, "vitabrightGetDiagnostics", "editor no longer exposes error domains")
for forbidden in ("fopen(", "vitabright_lut_p4.txt", "vitabright_lcd_lut.txt"):
    forbid(editor, forbidden,
           f"editor reintroduces direct/guessed LUT persistence ({forbidden})")

if errors:
    for error in errors:
        print(f"ERROR: {error}", file=sys.stderr)
    sys.exit(1)
print("pseudo-v1.4 source contract: OK")
