#!/usr/bin/env python3
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
errors = []

def text(path):
    return path.read_text(encoding="utf-8")

def require(path, needle, message):
    if needle not in text(path): errors.append(message)

def forbid(path, needle, message):
    if needle in text(path): errors.append(message)

def require_before(path, first, second, message):
    source = text(path); a = source.find(first); b = source.find(second)
    if a < 0 or b < 0 or a >= b: errors.append(message)

def function_body(path, signature):
    source = text(path); start = source.find(signature)
    if start < 0: return ""
    brace = source.find("{", start)
    if brace < 0: return ""
    depth = 0
    for i in range(brace, len(source)):
        if source[i] == "{": depth += 1
        elif source[i] == "}":
            depth -= 1
            if depth == 0: return source[start:i + 1]
    return ""

main = ROOT / "main.c"
oled = ROOT / "oled/hooks.c"
lcd = ROOT / "lcd/hooks.c"
color = ROOT / "color_space.c"
filter_runtime = ROOT / "screen_filter.c"
editor = ROOT / "editor/app.c"
cmake = ROOT / "CMakeLists.txt"
validator = ROOT / "tools/validate_packaged_assets.py"
workflow = ROOT / ".github/workflows/display-pipeline-ci.yml"
module = ROOT / "module.yml"

for path in (main, color, filter_runtime, lcd, oled):
    forbid(path, "0x0FCBF457", f"{path.relative_to(ROOT)} reintroduces private IFTU setter")
    for symbol in ("ksceKernelCreateThread", "sceKernelCreateThread",
                   "ksceKernelCreateTimer", "sceKernelCreateTimer",
                   "ksceKernelDelayThread", "sceKernelDelayThread"):
        forbid(path, symbol, f"{path.relative_to(ROOT)} introduces worker/polling primitive {symbol}")
    for symbol in ("ksceRegMgrSetKeyInt", "NID_REGMGR_SET_KEY_INT", "0x23B99BDE"):
        forbid(path, symbol, f"{path.relative_to(ROOT)} reintroduces RegMgr RGB-range mutation")

for path in (lcd, oled):
    require(path, "VBE_OWNERSHIP_DEGRADED", f"{path.relative_to(ROOT)} lost DEGRADED ownership")
    require(path, "vbe_txn_should_rollback", f"{path.relative_to(ROOT)} bypasses rollback legality")
    require(path, "vbe_txn_file_persistence_allowed", f"{path.relative_to(ROOT)} bypasses ACTIVE+FILE persistence gate")
    require(path, "VBE_ERR_RESOURCE_RELEASE", f"{path.relative_to(ROOT)} no longer diagnoses dirty release")

# Config is accepted request/provenance, independent of hardware commit state.
require(ROOT / "config.c", "config_load_candidate", "config candidate loader missing")
require(ROOT / "config.c", "config_commit_request", "explicit config request commit missing")
require(ROOT / "config.c", "vbe_config_request_commit", "config commit bypasses production-shared request core")
forbid(main, "config_snapshot", "reload retains mutate-then-restore config compensation")
forbid(main, "config_restore", "reload retains config rollback coupling to brightness")
require(main, "if (config_ret < 0)\n        return config_ret;", "malformed reload can reconcile hardware")
require_before(main, "config_commit_request(&candidate)", "oled_reload_backend()", "accepted config must commit before OLED reconciliation")
require_before(main, "config_commit_request(&candidate)", "lcd_reload_backend()", "accepted config must commit before LCD reconciliation")
require(ROOT / "config_request_core.c", "vbe_source_identity_copy", "config provenance commit is incomplete")

# DEGRADED backend ownership must stop subsequent hardware mutation.
require(main, "selected_backend_mutation_safe", "reload lacks authoritative backend mutation-safety query")
require(main, "color_space_apply_config()", "color-space reconciliation missing")
require(lcd, "lcd_backend_mutation_safe", "LCD ownership-safety query missing")
require(oled, "oled_backend_mutation_safe", "OLED ownership-safety query missing")

# Exact raw-layout authorization.
require(lcd, "lcd_stock_signature", "LCD exact stock signature missing")
require(lcd, "lcd_validate_layout", "LCD exact runtime authorization missing")
require(oled, "vbe_oled_stock_signature_match", "OLED exact stock authorization missing")
require(ROOT / "oled/layout_core.c", "vbe_oled_stock_p4", "OLED P4 stock signature not wired")
require(ROOT / "oled/layout_core.c", "vbe_oled_stock_p5", "OLED P5 stock signature not wired")
require(ROOT / "oled/layout_core.c", "vbe_oled_stock_generic", "OLED generic stock signature not wired")
require(ROOT / "generated/oled_stock_signatures.h", "VBE_OLED_STOCK_P4_SHA256", "generated OLED stock provenance missing")
start_body = function_body(oled, "static VbeTxnAttempt start_transaction")
if not start_body or start_body.find("validate_layout") < 0 or start_body.find("taiInjectDataForKernel") < 0 or start_body.find("validate_layout") >= start_body.find("taiInjectDataForKernel"):
    errors.append("OLED exact authorization does not precede raw injection")
for version in ("0x371", "0x372", "0x373", "0x374"):
    forbid(lcd, f"case {version}", f"LCD unsupported raw candidate {version} returned")
    forbid(oled, f"case {version}", f"OLED unsupported raw candidate {version} returned")

# OLED base/runtime/requested/applied and committed hook policy truth.
require(oled, "candidate->lut.runtime", "OLED hardware no longer consumes runtime LUT")
require(oled, "backend->lut.base", "OLED persistence no longer writes authoritative base")
require(oled, "lut_copy(snapshot, g_oled.lut.base)", "OLED GetLut no longer exposes base")
require(oled, "g_oled.lut.transform", "raw OLED edit no longer preserves requested transform")
require(oled, "previous.lut = g_oled.lut", "OLED rollback no longer snapshots complete LUT state")
require(oled, "previous.dim_policy_enabled", "OLED rollback no longer snapshots committed dim policy")
require(oled, "g_oled.dim_policy_enabled = candidate->dim_policy_enabled", "OLED candidate dim policy is not committed transactionally")
hook_body = function_body(oled, "int hook_ksceOledSetBrightness")
if not hook_body or "g_config" in hook_body:
    errors.append("OLED runtime brightness hook consumes requested config instead of committed backend state")
require(ROOT / "oled/state_core.c", "applied_transform", "OLED applied-transform truth missing")
require(oled, "vitabrightOledGetTransformState", "OLED transform-state syscall implementation missing")
require(module, "vitabrightOledGetTransformState", "OLED transform-state syscall export missing")
require(editor, "vitabrightOledGetTransformState", "editor does not consume OLED transform-state ABI")

# Manual warm profile is explicit register-domain state; legacy night stays separate.
for key in ("oled_warm_enabled", "oled_warm_first_row", "oled_warm_r_offset", "oled_warm_g_offset", "oled_warm_b_offset"):
    require(ROOT / "config_parser.c", key, f"canonical manual warm key missing: {key}")
require(oled, "g_config.night_mode_enabled", "legacy night compatibility request no longer reported")

# Color-space observation cannot synthesize restoration ownership.
forbid(color, "g_last_mode", "dead/unsafe g_last_mode observation state remains")
require(color, "vbe_color_space_ownership_note_write", "color-space writes do not create explicit ownership")
require(color, "vbe_color_space_ownership_observe", "color-space readback does not use production-shared ownership semantics")
require(main, "color_space_shutdown()", "module stop no longer restores owned color-space state")

# Generic unsafe domains remain unsupported and editor exposes no fake invert action.
forbid(filter_runtime, "NID_DISPLAY_INVERT_COLORS", "setter-only invert mutation returned")
forbid(filter_runtime, "ksceDisplaySetInvertColors", "setter-only invert mutation returned")
forbid(editor, "toggle_invert", "editor presents unsupported invert as an action")
forbid(editor, "SCE_CTRL_CROSS", "editor binds an unsupported invert action")
require(editor, "unsupported_domains", "editor no longer exposes generic unsupported-domain truth")

# Result, stop, and production linkage invariants.
require(main, "vbe_result_compose", "reload bypasses shared severity composition")
forbid(main, "keep_first_result", "first-nonzero result retention returned")
require(main, "state_lock_begin_shutdown()", "stop no longer quiesces operations")
require(main, "state_lock_finish_shutdown()", "stop bypasses confirmed lock teardown")
require(main, "vbe_stop_can_unload", "stop no longer uses unload-safety accumulator")
require_before(main, "state_lock_init()", "config_load_candidate", "config I/O occurs before synchronization commit boundary")

for core in ("result_core.c", "config_request_core.c", "color_space_state_core.c",
             "source_authority.c", "transaction_core.c", "persistence_core.c",
             "state_lock_core.c", "module_lifecycle_core.c", "filter_policy.c",
             "oled/state_core.c", "oled/transform_core.c", "oled/layout_core.c"):
    require(cmake, core, f"production shared core not linked: {core}")
forbid(cmake, "config_state_core.c", "obsolete config snapshot/restore core remains linked")
forbid(cmake, "affine_core.c", "unsupported affine future engine is linked into SKPRX")

for test_source in ("tests/result_core_host.c", "tests/config_source_state_host.c",
                    "tests/color_space_state_host.c", "tests/oled_layout_host.c",
                    "tests/oled_transform_host.c", "tests/oled_state_host.c",
                    "tests/filter_policy_host.c", "tests/filter_state_host.c"):
    require(validator, test_source, f"aggregate semantic suite does not execute {test_source}")
require(workflow, "tools/generate_cct_table.py --check", "CI does not verify CCT provenance")
require(workflow, "tools/generate_oled_stock_signatures.py --check", "CI does not verify OLED stock provenance")
require(workflow, "runs-on: ubuntu-24.04", "display CI runner is mutable")
require(workflow, "actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1", "display checkout action is not pinned")
require(workflow, "c83b88a54ec13372515eeb8b3bdca4b10c36d721", "display vdpm revision is not pinned")

require(ROOT / "status.c", "s.abi_version = 2", "status ABI v2 changed")
require(cmake, 'set(VBE_FTP_UR0 "${VBE_FTP_ROOT}//ur0:")', "FTP ur0 root lost absolute VitaShell syntax")
require(cmake, 'set(VBE_FTP_UX0 "${VBE_FTP_ROOT}//ux0:")', "FTP ux0 root lost absolute VitaShell syntax")

if errors:
    for error in errors: print(f"ERROR: {error}", file=sys.stderr)
    raise SystemExit(1)
print("pseudo-v1.4 final vertical source contract: OK")
