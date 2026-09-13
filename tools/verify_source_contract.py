#!/usr/bin/env python3
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
errors = []


def text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def require(path: Path, needle: str, message: str) -> None:
    if needle not in text(path):
        errors.append(message)


def forbid(path: Path, needle: str, message: str) -> None:
    if needle in text(path):
        errors.append(message)


def require_before(path: Path, first: str, second: str, message: str) -> None:
    source = text(path)
    a = source.find(first)
    b = source.find(second)
    if a < 0 or b < 0 or a >= b:
        errors.append(message)


main = ROOT / "main.c"
oled = ROOT / "oled" / "hooks.c"
lcd = ROOT / "lcd" / "hooks.c"
oled_state = ROOT / "oled" / "state_core.c"
filter_policy = ROOT / "filter_policy.c"
filter_runtime = ROOT / "screen_filter.c"
editor = ROOT / "editor" / "app.c"
cmake = ROOT / "CMakeLists.txt"
validator = ROOT / "tools" / "validate_packaged_assets.py"
workflow = ROOT / ".github" / "workflows" / "display-pipeline-ci.yml"

for path in (main, ROOT / "color_space.c", filter_runtime, lcd, oled):
    forbid(path, "0x0FCBF457",
           f"{path.relative_to(ROOT)} reintroduces speculative IFTU mutation")
    for symbol in ("ksceKernelCreateThread", "sceKernelCreateThread",
                   "ksceKernelCreateTimer", "sceKernelCreateTimer",
                   "ksceKernelDelayThread", "sceKernelDelayThread"):
        forbid(path, symbol,
               f"{path.relative_to(ROOT)} introduces background polling/worker primitive {symbol}")
    for symbol in ("ksceRegMgrSetKeyInt", "NID_REGMGR_SET_KEY_INT", "0x23B99BDE"):
        forbid(path, symbol,
               f"{path.relative_to(ROOT)} reintroduces persistent registry mutation ({symbol})")
    forbid(path, "status_set_error(",
           f"{path.relative_to(ROOT)} bypasses explicit error-domain ownership")
    forbid(path, "status_clear_error()",
           f"{path.relative_to(ROOT)} reintroduces generic multi-domain clearing")

for path in (lcd, oled):
    forbid(path, "g_vbe_status.last_error",
           f"{path.relative_to(ROOT)} uses derived legacy summary as internal state")
    forbid(path, "(void)taiHookReleaseForKernel",
           f"{path.relative_to(ROOT)} ignores hook-release ownership failure")
    forbid(path, "(void)taiInjectReleaseForKernel",
           f"{path.relative_to(ROOT)} ignores injection-release ownership failure")
    require(path, "VBE_OWNERSHIP_DEGRADED",
            f"{path.relative_to(ROOT)} lost explicit uncertain ownership state")
    require(path, "vbe_txn_should_rollback",
            f"{path.relative_to(ROOT)} bypasses shared rollback legality")
    require(path, "vbe_txn_file_persistence_allowed",
            f"{path.relative_to(ROOT)} bypasses ACTIVE+FILE persistence eligibility")
    require(path, "VBE_ERR_RESOURCE_RELEASE",
            f"{path.relative_to(ROOT)} no longer reports incomplete teardown")

# LCD still owns source identity directly and therefore retains the explicit
# source-commit helper. OLED commits a complete VbeOledLutState instead.
require(lcd, "vbe_txn_commit_source",
        "LCD backend bypasses transactional source commit")

require(oled, "candidate->lut.runtime",
        "OLED hardware transaction no longer consumes derived runtime LUT")
require(oled, "taiInjectDataForKernel",
        "OLED runtime LUT no longer reaches taiHEN injection")
require(oled, "backend->lut.base",
        "OLED persistence no longer serializes authoritative base LUT")
require(oled, "lut_copy(snapshot, g_oled.lut.base)",
        "OLED GetLut no longer exposes authoritative base LUT")
require(oled, "g_oled.lut.transform",
        "OLED raw edit no longer preserves requested transform during rederivation")
require(oled, "previous.lut = g_oled.lut",
        "OLED replacement rollback snapshot no longer contains complete committed LUT state")
require(oled, "g_oled.lut = candidate->lut",
        "OLED transaction no longer atomically commits complete LUT/source/panel/transform state")
require(oled, "g_oled.lut.source",
        "OLED committed source identity no longer lives inside committed OLED state")
require(oled_state, "state->transform = *requested_transform",
        "OLED state no longer retains requested transform")
require(oled_state, "vbe_oled_transform_neutral(&state->applied_transform)",
        "OLED state no longer starts applied transform at neutral")
require(oled_state, "state->applied_transform = *requested_transform",
        "OLED supported transform no longer records applied truth")
require(oled_state, "panel_type != OLED_PANEL_4 && panel_type != OLED_PANEL_5",
        "OLED P4/P5 capability gate missing")

for split_truth in ("g_lcd_hooks_active", "static int g_active"):
    forbid(lcd, split_truth, f"LCD backend reintroduces split ownership flag {split_truth}")
    forbid(oled, split_truth, f"OLED backend reintroduces split ownership flag {split_truth}")

require(lcd, "lcd_validate_layout", "LCD runtime signature validation missing")
require(lcd, "lcd_stock_signature", "LCD stock signature missing")
require(oled, "validate_layout", "OLED layout plausibility validation missing")
require(oled, "module_get_offset", "OLED inherited offset is no longer resolved")
for version in ("0x371", "0x372", "0x373", "0x374"):
    forbid(lcd, f"case {version}", f"LCD unverified raw layout {version} returned")
    forbid(oled, f"case {version}", f"OLED unverified raw layout {version} returned")

for nid in ("0x17F66722", "0xD40968FB", "0x4F8A1D4A", "0xDABBD9D3"):
    require(ROOT / "color_space.c", nid, f"color-space module lost verified export {nid}")
require(ROOT / "color_space.c", "g_original_mode", "color-space original state snapshot missing")
require(ROOT / "color_space.c", "current = g_get_mode()", "color-space read-back verification missing")
require(main, "color_space_shutdown()", "module stop no longer restores color-space state")

require(main, "vbe_result_compose", "reload bypasses shared result-severity composition")
forbid(main, "keep_first_result", "obsolete first-nonzero result retention returned")
require(ROOT / "state_lock_core.c", "vbe_result_compose",
        "lock release result composition diverges from shared severity rule")
require(ROOT / "result_core.c", "if (stage_result < 0)",
        "negative runtime failures no longer dominate positive capability results")

require(main, "state_lock_begin_shutdown()", "runtime module stop no longer quiesces operations")
require(main, "state_lock_cancel_shutdown()", "failed runtime stop cannot return to resident state")
require(main, "state_lock_finish_shutdown()", "module stop bypasses confirmed mutex teardown")
require(main, "vbe_stop_can_unload", "module stop no longer uses explicit unload-safety accumulator")
require(main, "SCE_KERNEL_STOP_FAIL", "stop-critical failures no longer cancel unload")
require_before(main, "state_lock_init()", "config_load()",
               "authoritative config load occurs before synchronization commit boundary")
require_before(main, "state_lock_init()", "lcd_enable_hooks()",
               "LCD backend can initialize before synchronization commit boundary")
require_before(main, "state_lock_init()", "oled_enable_hooks()",
               "OLED backend can initialize before synchronization commit boundary")

require(ROOT / "config.c", "vbe_config_state_snapshot",
        "runtime config snapshot bypasses production-shared config state core")
require(ROOT / "config.c", "vbe_config_state_restore",
        "runtime config rollback bypasses production-shared config state core")
require(ROOT / "config.c", "vbe_source_identity_compiled(&g_config_source)",
        "compiled config source lost explicit no-path identity")
require(main, "config_restore(&previous_config)",
        "failed backend replacement no longer restores prior config/source identity")

require(filter_policy, "out.attempt_domains = 0",
        "generic filter policy unexpectedly enables an unowned hardware mutation path")
require(filter_policy, "out.unsupported_domains = out.requested_domains",
        "generic unsupported-domain truth no longer follows requested domains")
forbid(filter_runtime, "NID_DISPLAY_INVERT_COLORS",
       "production filter runtime reintroduced setter-only invert mutation")
forbid(filter_runtime, "ksceDisplaySetInvertColors",
       "production filter runtime reintroduced setter-only invert mutation")
require(filter_runtime, "vbe_filter_state_begin_request",
        "filter runtime bypasses requested/committed state core")
require(ROOT / "module.yml", "vitabrightFilterGetState",
        "generated syscall ABI is missing additive GetState")

require(editor, "vitabrightFilterGetState", "editor does not consume additive filter state ABI")
require(editor, "filter_state.requested", "editor invert edit does not clone requested filter parameters")
forbid(editor, "p.cct = CCT_DEFAULT", "editor silently erases requested CCT while editing invert")
require(editor, "unsupported_domains", "editor does not expose unsupported filter-domain truth")
require(editor, "failed_domains", "editor does not expose failed filter-domain truth")
require(editor, "vitabrightOledPersistLut()", "editor bypasses authoritative OLED persistence")
require(editor, "vitabrightLcdPersistBrightnessValues()", "editor bypasses authoritative LCD persistence")
for forbidden in ("fopen(", "vitabright_lut_p4.txt", "vitabright_lcd_lut.txt"):
    forbid(editor, forbidden, f"editor reintroduces direct/guessed LUT persistence ({forbidden})")

require(cmake, "result_core.c", "shared result-composition core is not linked")
require(cmake, "config_state_core.c", "config snapshot/restore core is not linked")
require(cmake, "oled/state_core.c", "OLED ownership state core is not linked")
forbid(cmake, "affine_core.c", "unsupported affine engine is dead code in production SKPRX")
for core in ("source_authority.c", "transaction_core.c", "persistence_core.c",
             "state_lock_core.c", "module_lifecycle_core.c", "filter_policy.c"):
    require(cmake, core, f"production shared core is not linked: {core}")

for test_source in ("tests/result_core_host.c", "tests/config_source_state_host.c",
                    "tests/oled_state_host.c", "tests/filter_policy_host.c",
                    "tests/filter_state_host.c"):
    require(validator, test_source, f"aggregate semantic suite does not execute {test_source}")
require(workflow, "tools/generate_cct_table.py --check", "CI does not verify CCT provenance")
require(workflow, "tests/oled_state_host.c", "CI display-core gate does not execute OLED state regression")
require(workflow, "runs-on: ubuntu-24.04", "display CI runner is mutable instead of ubuntu-24.04")
require(workflow, "actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1",
        "display CI checkout action is not pinned")
require(workflow, "c83b88a54ec13372515eeb8b3bdca4b10c36d721",
        "display CI vdpm revision is not pinned")

require(ROOT / "status.c", "s.abi_version = 2", "status ABI v2 changed unexpectedly")
require(ROOT / "status.c", "vitabrightGetDiagnostics", "per-domain diagnostics syscall missing")
require(ROOT / "status_error_core.h", "SYNC > BRIGHTNESS > CONFIG > COLOR_SPACE > FILTER > INPUT",
        "legacy summary precedence is no longer explicit")
require(cmake, 'set(VBE_FTP_UR0 "${VBE_FTP_ROOT}//ur0:")',
        "FTP ur0 root is not canonical absolute VitaShell syntax")
require(cmake, 'set(VBE_FTP_UX0 "${VBE_FTP_ROOT}//ux0:")',
        "FTP ux0 root is not canonical absolute VitaShell syntax")

if errors:
    for error in errors:
        print(f"ERROR: {error}", file=sys.stderr)
    sys.exit(1)
print("pseudo-v1.4 source contract: OK")
