#!/usr/bin/env python3
from __future__ import annotations
import argparse
import json
import re
from pathlib import Path

from gate0_v7_import_manifest import load_names

LINE = re.compile(
    r'^library=(\S+) library_nid=(0x[0-9A-F]{8}) '
    r'function_nid=(0x[0-9A-F]{8}) function_name=(\S+)$'
)
OLD_MODULEMGR = ('SceModulemgrForKernel', 0xC445FA63, 0xD269F915)
SYSTEM_SW_VERSION = ('SceModulemgrForDriver', 0xD4A60A52, 0x5182E212)
NEW_RUNTIME_LIBRARY = 0x92C9FFC2
NEW_RUNTIME_FUNCTION = 0xDAA90093
CANDIDATE9_MANIFEST = Path(__file__).resolve().parent / 'observer-imports-candidate9-authorized.txt'


def read_manifest(path: Path):
    rows = []
    for n, raw in enumerate(path.read_text().splitlines(), 1):
        m = LINE.match(raw)
        if not m:
            raise SystemExit(f'{path}:{n}: malformed manifest line: {raw!r}')
        rows.append((m.group(1), int(m.group(2), 16), int(m.group(3), 16), m.group(4)))
    return rows


def key(row):
    return row[:3]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--old', required=True, type=Path)
    ap.add_argument('--new', required=True, type=Path)
    ap.add_argument('--db360', required=True, type=Path)
    ap.add_argument('--db363', required=True, type=Path)
    ap.add_argument('--retail-closure-json', required=True, type=Path)
    ap.add_argument('--report', required=True, type=Path)
    args = ap.parse_args()

    old = read_manifest(args.old)
    new = read_manifest(args.new)
    candidate9 = read_manifest(CANDIDATE9_MANIFEST)
    if new != candidate9:
        raise SystemExit(
            'Candidate-10 generated import manifest differs from the exact '
            'authorized Candidate-9 manifest:\n'
            f'candidate9={candidate9!r}\n'
            f'candidate10={new!r}'
        )
    old_keys = {key(r) for r in old}
    new_keys = {key(r) for r in new}

    if OLD_MODULEMGR not in old_keys:
        raise SystemExit(
            'frozen candidate manifest lacks expected failing '
            'SceModulemgrForKernel/ksceKernelGetModuleInfo import'
        )
    if any(r[0] == 'SceModulemgrForKernel' for r in new):
        raise SystemExit('repaired observer still directly imports SceModulemgrForKernel')

    baseline_without_modulemgr = old_keys - {OLD_MODULEMGR}
    added = new_keys - baseline_without_modulemgr
    removed = baseline_without_modulemgr - new_keys
    if removed:
        raise SystemExit(f'unexpected direct import removal: {sorted(removed)}')
    if len(added) != 1 or next(iter(added))[0] != 'taihenModuleUtils':
        raise SystemExit(f'unexpected direct import addition: {sorted(added)}')
    resolver_import = next(iter(added))

    closure = json.loads(args.retail_closure_json.read_text())
    if closure.get('firmware') != '3.65':
        raise SystemExit('retail import closure is not for exact firmware 3.65')
    if closure.get('direct_sony_import_closure') != 'PASS':
        raise SystemExit('DIRECT_SONY_IMPORT_CLOSURE is not PASS')
    if closure.get('old_modulemgr_direct_import') != 'ABSENT':
        raise SystemExit('OLD_MODULEMGR_DIRECT_IMPORT is not ABSENT')

    dynamic = closure.get('modulemgr_dynamic_target', {})
    if (
        dynamic.get('status') != 'RETAIL_365_PROVEN'
        or dynamic.get('library_nid') != f'0x{NEW_RUNTIME_LIBRARY:08X}'
        or dynamic.get('function_nid') != f'0x{NEW_RUNTIME_FUNCTION:08X}'
        or dynamic.get('provider_module') != 'SceKernelModulemgr'
    ):
        raise SystemExit('dynamic Modulemgr target lacks exact retail-3.65 proof')

    sw = closure.get('system_sw_version_direct_import', {})
    if (
        sw.get('status') != 'RETAIL_365_PROVEN'
        or sw.get('library_nid') != f'0x{SYSTEM_SW_VERSION[1]:08X}'
        or sw.get('function_nid') != f'0x{SYSTEM_SW_VERSION[2]:08X}'
        or sw.get('provider_module') != 'SceKernelModulemgr'
    ):
        raise SystemExit('SystemSwVersion direct import lacks exact retail-3.65 proof')
    if SYSTEM_SW_VERSION not in new_keys:
        raise SystemExit('expected SystemSwVersion direct import is absent from repaired observer')

    n360 = load_names([args.db360])
    n363 = load_names([args.db363])
    db_diagnostics = []
    for lib, lnid, fnid, shown_name in new:
        if not lib.startswith('Sce'):
            continue
        k = (lib, lnid, fnid)
        name360 = n360.get(k)
        name363 = n363.get(k)
        if name360 != name363:
            db_diagnostics.append(
                f'DB_DIAGNOSTIC discrepancy library={lib} '
                f'library_nid=0x{lnid:08X} function_nid=0x{fnid:08X} '
                f'db360={name360 or "ABSENT"} db363={name363 or "ABSENT"} '
                'authority=EXACT_RETAIL_365'
            )

    lines = [
        'MODULEMGR_RESOLUTION_REPORT=PASS',
        'CANDIDATE9_CANDIDATE10_IMPORTS=PASS',
        'candidate9_manifest_source=authorized_run_34907094632_artifact_10373365353',
        'physical_failure_observed=SceModulemgrForKernel unresolved',
        'old_direct_import_library=SceModulemgrForKernel',
        'old_direct_import_library_nid=0xC445FA63',
        'old_direct_import_function=ksceKernelGetModuleInfo',
        'old_direct_import_function_nid=0xD269F915',
        'target_firmware=3.65',
        'runtime_lookup_module=SceKernelModulemgr',
        f'runtime_lookup_library_nid=0x{NEW_RUNTIME_LIBRARY:08X}',
        f'runtime_lookup_function_nid=0x{NEW_RUNTIME_FUNCTION:08X}',
        'direct_SceModulemgrForKernel_imports_in_repaired_observer=0',
        f'expected_repair_import_addition=library={resolver_import[0]} '
        f'library_nid=0x{resolver_import[1]:08X} '
        f'function_nid=0x{resolver_import[2]:08X}',
        'DIRECT_SONY_IMPORT_CLOSURE=PASS',
        'OLD_MODULEMGR_DIRECT_IMPORT=ABSENT',
        'MODULEMGR_DYNAMIC_TARGET=RETAIL_365_PROVEN',
        'SYSTEM_SW_VERSION_DIRECT_IMPORT=RETAIL_365_PROVEN',
        'added_direct_firmware_imports=NONE',
        'unexpected_direct_import_delta=NONE',
        'db_policy=DIAGNOSTIC_ONLY_EXACT_RETAIL_365_IS_AUTHORITY',
    ]
    lines.extend(db_diagnostics)
    args.report.write_text('\n'.join(lines) + '\n')
    print('\n'.join(lines))


if __name__ == '__main__':
    main()
