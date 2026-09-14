#!/usr/bin/env python3
from __future__ import annotations
import argparse
import json
import re
from pathlib import Path
from gate0_v7_import_manifest import load_names

LINE = re.compile(r'^library=(\S+) library_nid=(0x[0-9A-F]{8}) function_nid=(0x[0-9A-F]{8}) function_name=(\S+)$')
OLD_MODULEMGR = ('SceModulemgrForKernel', 0xC445FA63, 0xD269F915)
SYSTEM_SW_VERSION = ('SceModulemgrForDriver', 0xD4A60A52, 0x5182E212)
NEW_RUNTIME_LIBRARY = 0x92C9FFC2
NEW_RUNTIME_FUNCTION = 0xDAA90093


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
    ap.add_argument('--retail-modulemgr-json', required=True, type=Path)
    ap.add_argument('--report', required=True, type=Path)
    args = ap.parse_args()

    old = read_manifest(args.old)
    new = read_manifest(args.new)
    old_keys = {key(r) for r in old}
    new_keys = {key(r) for r in new}

    if OLD_MODULEMGR not in old_keys:
        raise SystemExit('frozen candidate manifest lacks expected 3.60 SceModulemgrForKernel/ksceKernelGetModuleInfo import')
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

    retail = json.loads(args.retail_modulemgr_json.read_text())
    if retail.get('firmware') != '3.65' or retail.get('module') != 'SceKernelModulemgr':
        raise SystemExit('retail Modulemgr evidence is not exact firmware 3.65 SceKernelModulemgr')
    sw = retail.get('system_sw_version_direct_import', {})
    retail_sw_proven = (
        sw.get('library_name') == SYSTEM_SW_VERSION[0]
        and sw.get('library_nid') == f'0x{SYSTEM_SW_VERSION[1]:08X}'
        and sw.get('function_nid') == f'0x{SYSTEM_SW_VERSION[2]:08X}'
        and sw.get('present') is True
        and sw.get('hit_count') == 1
    )

    n360 = load_names([args.db360])
    n363 = load_names([args.db363])
    classifications = []
    for lib, lnid, fnid, shown_name in new:
        k = (lib, lnid, fnid)
        if lib.lower().startswith('taihen'):
            cls = 'TAIHEN_PLUGIN_OWNED'
            name = shown_name
        elif k == SYSTEM_SW_VERSION:
            if not retail_sw_proven:
                raise SystemExit(
                    'remaining direct SceModulemgrForDriver/ksceKernelGetSystemSwVersion import '
                    'is not exported by exact retail 3.65 SceKernelModulemgr'
                )
            cls = 'RETAIL_365_PROVEN'
            name = 'ksceKernelGetSystemSwVersion'
        else:
            name360 = n360.get(k)
            name363 = n363.get(k)
            if not name360 or not name363 or name360 != name363:
                raise SystemExit(
                    f'remaining direct import lacks 3.60/3.63 stability proof: '
                    f'{lib} 0x{lnid:08X} 0x{fnid:08X} names=({name360},{name363})'
                )
            cls = 'FIRMWARE_STABLE_360_363_CORROBORATED'
            name = name360
        classifications.append((lib, lnid, fnid, name, cls))

    lines = [
        'MODULEMGR_RESOLUTION_REPORT=PASS',
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
        f'expected_repair_import_addition=library={resolver_import[0]} library_nid=0x{resolver_import[1]:08X} function_nid=0x{resolver_import[2]:08X}',
        'added_direct_firmware_imports=NONE',
        'unexpected_direct_import_delta=NONE',
        'remaining_direct_imports:',
    ]
    for lib, lnid, fnid, name, cls in classifications:
        lines.append(
            f'  library={lib} library_nid=0x{lnid:08X} '
            f'function_nid=0x{fnid:08X} function_name={name} classification={cls}'
        )
    args.report.write_text('\n'.join(lines) + '\n')
    print('\n'.join(lines))


if __name__ == '__main__':
    main()
