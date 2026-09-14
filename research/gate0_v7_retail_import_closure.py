#!/usr/bin/env python3
from __future__ import annotations
import argparse
import json
import re
from pathlib import Path

from vita_elf_audit import VitaElf

LINE = re.compile(
    r'^library=(\S+) library_nid=(0x[0-9A-F]{8}) '
    r'function_nid=(0x[0-9A-F]{8}) function_name=(\S+)$'
)

OLD_MODULEMGR_LIBRARY = 'SceModulemgrForKernel'
OLD_MODULEMGR_PAIR = (0xC445FA63, 0xD269F915)
DYNAMIC_MODULEMGR_PAIR = (0x92C9FFC2, 0xDAA90093)
SYSTEM_SW_VERSION_PAIR = (0xD4A60A52, 0x5182E212)
EXPECTED_TAIHEN_SOURCE = '309b3800bcb8ebbd5e4f5e5e920af3da3590b829'


def parse_manifest(path: Path):
    rows = []
    for lineno, raw in enumerate(path.read_text().splitlines(), 1):
        m = LINE.match(raw)
        if not m:
            raise SystemExit(f'{path}:{lineno}: malformed manifest line: {raw!r}')
        rows.append({
            'library_name': m.group(1),
            'library_nid': int(m.group(2), 16),
            'function_nid': int(m.group(3), 16),
            'function_name': m.group(4),
        })
    return rows


def classify_import(row):
    name = row['library_name']
    if name.startswith('Sce'):
        return 'SONY_RETAIL_IMPORT'
    if name.lower().startswith('taihen'):
        return 'TAIHEN_PROVIDED_IMPORT'
    return 'OTHER_EXPLICIT_PLUGIN_DEPENDENCY'


def export_rows(elf_dir: Path):
    rows = []
    modules = []
    parse_failures = []
    for path in sorted(elf_dir.glob('*.elf')):
        try:
            elf = VitaElf(path)
            module = elf.modinfo['name']
            modules.append({
                'provider_file': path.name,
                'provider_module': module,
                'elf_sha256': elf.sha256,
            })
            for lib in elf.exports():
                lnid = lib.get('library_nid')
                if lnid is None:
                    continue
                for fn in lib.get('functions', ()):
                    rows.append({
                        'provider_file': path.name,
                        'provider_module': module,
                        'provider_elf_sha256': elf.sha256,
                        'library_name': lib.get('library_name') or '<anonymous>',
                        'library_nid': int(lnid),
                        'function_nid': int(fn['nid']),
                        'va': int(fn['va']),
                    })
        except Exception as exc:
            parse_failures.append({'provider_file': path.name, 'error': str(exc)})
    return rows, modules, parse_failures


def one_hit(exports, pair):
    return [x for x in exports if (x['library_nid'], x['function_nid']) == pair]


def pair_hex(pair):
    return f'0x{pair[0]:08X} / 0x{pair[1]:08X}'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--manifest', type=Path, required=True)
    ap.add_argument('--elf-dir', type=Path, required=True)
    ap.add_argument('--conversion-failures', type=Path, required=True)
    ap.add_argument('--pup-sha256', required=True)
    ap.add_argument('--os0-sha256', required=True)
    ap.add_argument('--taihen-source-sha', required=True)
    ap.add_argument('--index-json', type=Path, required=True)
    ap.add_argument('--closure-json', type=Path, required=True)
    ap.add_argument('--report', type=Path, required=True)
    args = ap.parse_args()

    if args.taihen_source_sha != EXPECTED_TAIHEN_SOURCE:
        raise SystemExit(
            f'taiHEN source drift: expected {EXPECTED_TAIHEN_SOURCE}, got {args.taihen_source_sha}'
        )

    imports = parse_manifest(args.manifest)
    exports, modules, elf_parse_failures = export_rows(args.elf_dir)
    conversion_failures = [
        x for x in args.conversion_failures.read_text().splitlines() if x.strip()
    ]

    direct_old = [
        x for x in imports
        if x['library_name'] == OLD_MODULEMGR_LIBRARY
        or (x['library_nid'], x['function_nid']) == OLD_MODULEMGR_PAIR
    ]
    if direct_old:
        raise SystemExit(f'OLD_MODULEMGR_DIRECT_IMPORT=PRESENT: {direct_old}')

    classified = []
    relevant_export_keys = set()
    sony_failure = []
    other_failure = []

    for imp in imports:
        cls = classify_import(imp)
        row = dict(imp)
        row['classification'] = cls
        if cls == 'SONY_RETAIL_IMPORT':
            pair = (imp['library_nid'], imp['function_nid'])
            hits = one_hit(exports, pair)
            relevant_export_keys.add(pair)
            row['retail_365_hits'] = len(hits)
            row['providers'] = [
                {
                    'provider_module': h['provider_module'],
                    'provider_file': h['provider_file'],
                    'export_library_name': h['library_name'],
                    'va': f'0x{h["va"]:08X}',
                    'provider_elf_sha256': h['provider_elf_sha256'],
                }
                for h in hits
            ]
            if len(hits) != 1:
                sony_failure.append({
                    'library_name': imp['library_name'],
                    'library_nid': f'0x{imp["library_nid"]:08X}',
                    'function_nid': f'0x{imp["function_nid"]:08X}',
                    'hit_count': len(hits),
                })
        elif cls == 'TAIHEN_PROVIDED_IMPORT':
            row['provider'] = 'taiHEN'
            row['taihen_source_sha'] = args.taihen_source_sha
        else:
            other_failure.append(row)
        classified.append(row)

    dynamic_hits = one_hit(exports, DYNAMIC_MODULEMGR_PAIR)
    relevant_export_keys.add(DYNAMIC_MODULEMGR_PAIR)
    if len(dynamic_hits) != 1:
        raise SystemExit(
            'MODULEMGR_DYNAMIC_TARGET=RETAIL_365_UNPROVEN '
            f'pair={pair_hex(DYNAMIC_MODULEMGR_PAIR)} hits={len(dynamic_hits)}'
        )
    if dynamic_hits[0]['provider_module'] != 'SceKernelModulemgr':
        raise SystemExit(
            'MODULEMGR_DYNAMIC_TARGET provider mismatch: '
            f'{dynamic_hits[0]["provider_module"]}'
        )

    sw_imports = [
        x for x in imports
        if (x['library_nid'], x['function_nid']) == SYSTEM_SW_VERSION_PAIR
    ]
    sw_hits = one_hit(exports, SYSTEM_SW_VERSION_PAIR)
    relevant_export_keys.add(SYSTEM_SW_VERSION_PAIR)
    if len(sw_imports) != 1 or len(sw_hits) != 1:
        raise SystemExit(
            'SYSTEM_SW_VERSION_DIRECT_IMPORT not uniquely proven: '
            f'import_hits={len(sw_imports)} retail_hits={len(sw_hits)}'
        )
    if sw_hits[0]['provider_module'] != 'SceKernelModulemgr':
        raise SystemExit(
            'SYSTEM_SW_VERSION provider mismatch: '
            f'{sw_hits[0]["provider_module"]}'
        )

    if other_failure:
        raise SystemExit(
            'unclassified explicit plugin dependencies: '
            + json.dumps(other_failure, sort_keys=True)
        )
    if sony_failure:
        suffix = ''
        if conversion_failures or elf_parse_failures:
            suffix = (
                f'; conversion_failures={conversion_failures!r} '
                f'elf_parse_failures={elf_parse_failures!r}'
            )
        raise SystemExit(
            'DIRECT_SONY_IMPORT_CLOSURE=FAIL '
            + json.dumps(sony_failure, sort_keys=True)
            + suffix
        )

    relevant_exports = [
        {
            **x,
            'library_nid': f'0x{x["library_nid"]:08X}',
            'function_nid': f'0x{x["function_nid"]:08X}',
            'va': f'0x{x["va"]:08X}',
        }
        for x in exports
        if (x['library_nid'], x['function_nid']) in relevant_export_keys
    ]

    index_doc = {
        'schema': 1,
        'firmware': '3.65',
        'pup_sha256': args.pup_sha256,
        'os0_sha256': args.os0_sha256,
        'scope': 'Candidate-9 direct Sony imports plus runtime Modulemgr target',
        'scanned_elf_modules': modules,
        'conversion_failures': conversion_failures,
        'elf_parse_failures': elf_parse_failures,
        'relevant_exports': relevant_exports,
    }
    args.index_json.write_text(json.dumps(index_doc, indent=2, sort_keys=True) + '\n')

    closure_doc = {
        'schema': 1,
        'firmware': '3.65',
        'direct_sony_import_closure': 'PASS',
        'old_modulemgr_direct_import': 'ABSENT',
        'modulemgr_dynamic_target': {
            'status': 'RETAIL_365_PROVEN',
            'library_nid': '0x92C9FFC2',
            'function_nid': '0xDAA90093',
            'provider_module': dynamic_hits[0]['provider_module'],
            'provider_file': dynamic_hits[0]['provider_file'],
            'va': f'0x{dynamic_hits[0]["va"]:08X}',
        },
        'system_sw_version_direct_import': {
            'status': 'RETAIL_365_PROVEN',
            'library_nid': '0xD4A60A52',
            'function_nid': '0x5182E212',
            'provider_module': sw_hits[0]['provider_module'],
            'provider_file': sw_hits[0]['provider_file'],
            'va': f'0x{sw_hits[0]["va"]:08X}',
        },
        'taihen_provider': {
            'status': 'PINNED',
            'source_sha': args.taihen_source_sha,
        },
        'imports': classified,
    }
    args.closure_json.write_text(json.dumps(closure_doc, indent=2, sort_keys=True) + '\n')

    lines = [
        'DIRECT_SONY_IMPORT_CLOSURE=PASS',
        'OLD_MODULEMGR_DIRECT_IMPORT=ABSENT',
        'MODULEMGR_DYNAMIC_TARGET=RETAIL_365_PROVEN '
        'library=0x92C9FFC2 function=0xDAA90093 '
        f'provider={dynamic_hits[0]["provider_module"]} va=0x{dynamic_hits[0]["va"]:08X}',
        'SYSTEM_SW_VERSION_DIRECT_IMPORT=RETAIL_365_PROVEN '
        'library=0xD4A60A52 function=0x5182E212 '
        f'provider={sw_hits[0]["provider_module"]} va=0x{sw_hits[0]["va"]:08X}',
        f'TAIHEN_PROVIDER_SOURCE={args.taihen_source_sha}',
    ]
    for row in classified:
        base = (
            f'IMPORT class={row["classification"]} '
            f'library={row["library_name"]} '
            f'library_nid=0x{row["library_nid"]:08X} '
            f'function_nid=0x{row["function_nid"]:08X} '
            f'function_name={row["function_name"]}'
        )
        if row['classification'] == 'SONY_RETAIL_IMPORT':
            provider = row['providers'][0]
            base += (
                f' provider={provider["provider_module"]}'
                f' provider_file={provider["provider_file"]}'
            )
        elif row['classification'] == 'TAIHEN_PROVIDED_IMPORT':
            base += ' provider=taiHEN'
        lines.append(base)
    lines.append(f'RETAIL_ELF_MODULES_SCANNED={len(modules)}')
    lines.append(f'RETAIL_CONVERSION_FAILURES={len(conversion_failures)}')
    lines.append(f'RETAIL_ELF_PARSE_FAILURES={len(elf_parse_failures)}')
    args.report.write_text('\n'.join(lines) + '\n')
    print('\n'.join(lines))


if __name__ == '__main__':
    main()
