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


def require_before(path: Path, first: str, second: str, message: str) -> None:
    text = path.read_text(encoding="utf-8")
    first_pos = text.find(first)
    second_pos = text.find(second)
    if first_pos < 0 or second_pos < 0 or first_pos >= second_pos:
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
    forbid(path, "status_set_error(",
           f"{path.relative_to(ROOT)} bypasses explicit error-domain ownership")
    forbid(path, "status_clear_error()",
           f"{path.relative_to(ROOT)} reintroduces multi-domain generic clearing")

for path in (ROOT / "lcd" / "hooks.c", ROOT / "oled" / "hooks.c"):
    forbid(path, "g_vbe_status.last_error",
           f"{path.relative_to(ROOT)} uses derived legacy summary as internal state")
    forbid(path, "g_vbe_status.last_error_detail",
           f"{path.relative_to(ROOT)} uses derived legacy detail as internal state")
    forbid(path, "(void)taiHookReleaseForKernel",
           f"{path.relative_to(ROOT)} ignores hook-release ownership failure")
    forbid(path, "(void)taiInjectReleaseForKernel",
           f"{path.relative_to(ROOT)} ignores injection-release ownership failure")
    forbid(path, "(void)state_lock_release()",
           f"{path.relative_to(ROOT)} ignores synchronization ownership failure")
    require(path, "VBE_OWNERSHIP_DEGRADED",
            f"{path.relative_to(ROOT)} lost explicit dirty-ownership state")
    require(path, "vbe_txn_should_rollback",
            f"{path.relative_to(ROOT)} bypasses shared rollback legality")
    require(path, "vbe_txn_commit_source",
            f"{path.relative_to(ROOT)} bypasses transactional source commit")
    require(path, "vbe_txn_file_persistence_allowed",
            f"{path.relative_to(ROOT)} bypasses shared persistence eligibility")
    require(path, "VBE_ERR_RESOURCE_RELEASE",
            f"{path.relative_to(ROOT)} no longer reports incomplete teardown")

for split_truth in ("g_lcd_hooks_active", "static int g_active"):
    forbid(ROOT / "lcd" / "hooks.c", split_truth,
           f"LCD backend reintroduces split ownership flag {split_truth}")
    forbid(ROOT / "oled" / "hooks.c", split_truth,
           f"OLED backend reintroduces split ownership flag {split_truth}")

for path in (ROOT / "status.c", ROOT / "color_space.c", ROOT / "screen_filter.c"):
    forbid(path, "(void)state_lock_release()",
           f"{path.relative_to(ROOT)} ignores synchronization ownership failure")
    require(path, "state_lock_release_result",
            f"{path.relative_to(ROOT)} bypasses operation+unlock result composition")

for symbol in ("normalise_white_point", "apply_color_bias", "apply_night_mode"):
    forbid(ROOT / "oled" / "hooks.c", symbol,
           f"OLED backend reintroduces unverified gamma-code transform {symbol}")

require(ROOT / "lcd" / "hooks.c", "lcd_validate_layout", "LCD runtime signature validation missing")
require(ROOT / "lcd" / "hooks.c", "lcd_stock_signature", "LCD stock signature missing")
require(ROOT / "lcd" / "hooks.c", "vbe_source_identity_compiled(&candidate->source)",
        "LCD compiled fallback no longer carries explicit COMPILED source identity")
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
require(ROOT / "main.c", "state_lock_begin_shutdown()", "runtime module stop no longer quiesces operations")
require(ROOT / "main.c", "state_lock_cancel_shutdown()", "failed runtime stop cannot return to resident state")
require(ROOT / "main.c", "state_lock_finish_shutdown()", "module stop bypasses confirmed mutex teardown")
require(ROOT / "main.c", "vbe_stop_can_unload", "module stop no longer uses explicit unload-safety accumulator")
require(ROOT / "main.c", "SCE_KERNEL_STOP_FAIL", "stop-critical failures no longer cancel unload")
require(ROOT / "main.c", "vbe_module_stop_mode",
        "module stop no longer distinguishes inert startup from runtime teardown")
require(ROOT / "main.c", "g_module_lifecycle = VBE_MODULE_RUNTIME",
        "successful synchronization creation no longer commits runtime lifecycle")
require(ROOT / "main.c", "g_module_lifecycle = VBE_MODULE_INERT",
        "confirmed normal stop no longer returns module lifecycle to inert")
forbid(ROOT / "main.c", "state_lock_lifecycle() == VBE_LOCK_ABSENT",
       "module stop special-cases ABSENT without the module lifecycle contract")
forbid(ROOT / "main.c", "g_vbe_status.state_lock =",
       "main.c duplicates synchronization status ownership from state_lock.c")
require_before(ROOT / "main.c", "state_lock_init()", "config_load()",
               "authoritative config load occurs before synchronization commit boundary")
require_before(ROOT / "main.c", "state_lock_init()", "lcd_enable_hooks()",
               "LCD backend can initialize before synchronization commit boundary")
require_before(ROOT / "main.c", "state_lock_init()", "oled_enable_hooks()",
               "OLED backend can initialize before synchronization commit boundary")
require_before(ROOT / "main.c", "state_lock_init()", "color_space_apply_config()",
               "color-space mutation can begin before synchronization commit boundary")
require_before(ROOT / "main.c", "state_lock_init()", "screen_filter_apply(",
               "filter mutation can begin before synchronization commit boundary")
require_before(ROOT / "main.c", "vbe_module_stop_mode", "state_lock_begin_shutdown()",
               "module stop enters runtime shutdown before classifying inert/runtime lifecycle")

require(ROOT / "status.c", "s.abi_version = 2", "status ABI v2 changed unexpectedly")
require(ROOT / "status.c", "vitabrightGetDiagnostics", "per-domain diagnostics syscall missing")
require(ROOT / "status.c", "status_recovery_result", "domain-aware rollback recovery primitive missing")
require(ROOT / "status_error_core.h", "SYNC > BRIGHTNESS > CONFIG > COLOR_SPACE > FILTER > INPUT",
        "legacy summary precedence is no longer explicit")
require(ROOT / "filter_policy.c", "VBE_RESULT_UNSUPPORTED",
        "unsupported filter request no longer uses named capability result")
require(ROOT / "filter_policy.c", "policy.error = VBE_ERR_NONE",
        "unsupported filter policy became a runtime error")
require(ROOT / "screen_filter.c", "vbe_filter_request_policy",
        "screen filter bypasses production shared unsupported policy")
require(ROOT / "main.c", "state_lock_init()", "state-transition lock initialization missing")
require(ROOT / "main.c", "vitabright_reload_locked", "shared reload orchestrator missing")
require(ROOT / "main.c", "oled_reload_backend()", "generic reload bypasses OLED backend")
require(ROOT / "main.c", "lcd_reload_backend()", "generic reload bypasses LCD backend")
for backend in (ROOT / "lcd" / "hooks.c", ROOT / "oled" / "hooks.c"):
    forbid(backend, "config_load()",
           f"{backend.relative_to(ROOT)} improperly owns config reload lifecycle")

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
for core in ("source_authority.c", "transaction_core.c", "persistence_core.c",
             "state_lock_core.c", "module_lifecycle_core.c", "filter_policy.c"):
    require(root_cmake, core, f"production shared core is not linked: {core}")
require(ROOT / "editor" / "CMakeLists.txt", "VBE_BUILD_ID", "editor build identity is not generated")

for path in (ROOT / "source_authority.h", ROOT / "source_authority.c"):
    forbid(path, "#define SCE_ERROR_ERRNO_ENOENT",
           "project reintroduced SDK-looking ENOENT ownership")
require(ROOT / "source_authority.h", "VBE_SCE_IO_ERROR_NOT_FOUND",
        "project-owned Vita not-found compatibility constant missing")

validator = ROOT / "tools" / "validate_packaged_assets.py"
for test_source in (
    "tests/transaction_core_host.c",
    "tests/persistence_core_host.c",
    "tests/state_lock_core_host.c",
    "tests/module_lifecycle_core_host.c",
):
    require(validator, test_source, f"production semantic host suite not executed: {test_source}")

editor = ROOT / "editor" / "app.c"
require(ROOT / "editor" / "CMakeLists.txt", "add_executable(vitabrightex-editor.elf app.c)",
        "editor build no longer targets audited source")
require(editor, "vitabrightOledPersistLut()", "editor bypasses authoritative OLED persistence")
require(editor, "vitabrightLcdPersistBrightnessValues()", "editor bypasses authoritative LCD persistence")
require(editor, "LCD LUT entry %d/%d: %u", "editor mislabels LUT cursor")
require(editor, "Build plugin=%s editor=%s", "editor no longer exposes plugin/editor provenance")
require(editor, "vitabrightGetDiagnostics", "editor no longer exposes error domains")
require(editor, "r == VBE_RESULT_UNSUPPORTED", "editor collapses unsupported capability into generic failure")
require(editor, "VBE_RESULT_NO_FILE_SOURCE", "editor hides compiled-source persistence semantics")
for forbidden in ("fopen(", "vitabright_lut_p4.txt", "vitabright_lcd_lut.txt"):
    forbid(editor, forbidden,
           f"editor reintroduces direct/guessed LUT persistence ({forbidden})")

if errors:
    for error in errors:
        print(f"ERROR: {error}", file=sys.stderr)
    sys.exit(1)
print("pseudo-v1.4 source contract: OK")
