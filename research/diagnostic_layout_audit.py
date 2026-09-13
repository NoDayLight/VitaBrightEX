#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path

from vita_elf_audit import VitaElf, Reachability, FunctionCFG, PT_LOAD
from topology_common import all_insns, ins_text, absolute_constants, load_nid_names, import_stub_map, call_semantics

LOWIO_SHA = 'f791cfe2c6db955deb9446c57bb72ce1870bde2c6c485cac363e343d5128f744'
LCD_SHA = '24752bb4c69f0c241701cab0364cde9cbe5c95128b6bfe02bf3263aad524246e'

LOWIO_PLANE_ARRAY = 0x8100B37C
LOWIO_PLANE_STRIDE = 0x214
LOWIO_PLANE_COUNT = 5
LCD_PANEL_WRITER = 0x81000A54
LOWIO_DESCRIPTOR_USERS = (0x81005B08, 0x81003C78)


def segment_binding(e: VitaElf, va: int, size: int = 1):
    for index, p in enumerate(e.phdrs):
        if p.p_type != PT_LOAD:
            continue
        if p.p_vaddr <= va and va + size <= p.p_vaddr + p.p_memsz:
            return {
                'segment_index': index,
                'segment_vaddr': p.p_vaddr,
                'segment_offset': va - p.p_vaddr,
                'segment_filesz': p.p_filesz,
                'segment_memsz': p.p_memsz,
                'linked_va': va,
                'size': size,
                'file_backed': va + size <= p.p_vaddr + p.p_filesz,
            }
    raise SystemExit(f'VA 0x{va:08X}+0x{size:X} is not contained in a PT_LOAD segment of {e.modinfo["name"]}')


def focus_record(e, reach, stubs, va):
    cfg = next((x for x in reach.functions.values() if x.start == va), None)
    if cfg is None:
        cfg = FunctionCFG(e, va, True, reach.import_stubs,
                          boundary_index=reach.boundaries,
                          noreturn_stubs=reach.noreturn_stubs)
    return {
        'start': cfg.start,
        'logical_end': cfg.logical_end,
        'boundary_sources': cfg.boundary_sources,
        'termination_reason': cfg.compact(False)['termination_reason'],
        'calls': call_semantics(cfg, stubs),
        'absolute_constants': absolute_constants(e, cfg),
        'instructions': [{'va': i.address, 'text': ins_text(i)} for i in all_insns(cfg)],
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--lowio', type=Path, required=True)
    ap.add_argument('--lcd', type=Path, required=True)
    ap.add_argument('--nid-db-root', type=Path, required=True)
    ap.add_argument('--json', type=Path, required=True)
    a = ap.parse_args()

    low = VitaElf(a.lowio)
    lcd = VitaElf(a.lcd)
    if low.sha256 != LOWIO_SHA:
        raise SystemExit(f'SceLowio hash drift {low.sha256}')
    if lcd.sha256 != LCD_SHA:
        raise SystemExit(f'SceLcd hash drift {lcd.sha256}')

    names = load_nid_names(a.nid_db_root)
    lr = Reachability(low, low.exports(), low.imports(), extra_starts=LOWIO_DESCRIPTOR_USERS)
    ls = import_stub_map(low, names)

    plane_size = LOWIO_PLANE_STRIDE * LOWIO_PLANE_COUNT
    plane = segment_binding(low, LOWIO_PLANE_ARRAY, plane_size)
    writer = segment_binding(lcd, LCD_PANEL_WRITER, 2)

    # The diagnostic must resolve these by module segment + offset at runtime.
    # Never convert the linked 0x810xxxxx VA to a runtime address by assuming a
    # single relocation delta across segments.
    result = {
        'schema': 1,
        'firmware': '3.65',
        'elf_sha256': {'SceLowio': low.sha256, 'SceLcd': lcd.sha256},
        'lowio_plane_array': {
            **plane,
            'plane_stride': LOWIO_PLANE_STRIDE,
            'plane_count': LOWIO_PLANE_COUNT,
            'runtime_resolution': 'module_get_offset(KERNEL_PID, lowio_modid, segment_index, segment_offset)',
        },
        'lcd_panel_writer': {
            **writer,
            'thumb': True,
            'runtime_hook': 'taiHookFunctionOffsetForKernel(KERNEL_PID, ..., lcd_modid, segment_index, segment_offset, 1, ...)',
        },
        'lowio_descriptor_users': {
            f'0x{va:08X}': focus_record(low, lr, ls, va)
            for va in LOWIO_DESCRIPTOR_USERS
        },
        'safety': {
            'absolute_runtime_va_guessing': False,
            'mmio_reads': False,
            'mmio_writes': False,
            'software_cache_snapshot_only': True,
        },
    }
    a.json.write_text(json.dumps(result, indent=2) + '\n')

    print('DIAGNOSTIC_RUNTIME_LAYOUT')
    print('  Lowio plane array: linked=0x%08X seg=%d seg_vaddr=0x%08X off=0x%X size=0x%X file_backed=%s' %
          (plane['linked_va'], plane['segment_index'], plane['segment_vaddr'],
           plane['segment_offset'], plane_size, plane['file_backed']))
    print('  SceLcd panel writer: linked=0x%08X seg=%d seg_vaddr=0x%08X off=0x%X thumb=1' %
          (writer['linked_va'], writer['segment_index'], writer['segment_vaddr'], writer['segment_offset']))
    print('LOWIO_DESCRIPTOR_USERS')
    for va in LOWIO_DESCRIPTOR_USERS:
        x = result['lowio_descriptor_users'][f'0x{va:08X}']
        print(f"  0x{va:08X}..0x{x['logical_end']:08X} calls={[(c.get('name'), hex(c['target'])) for c in x['calls']]}")


if __name__ == '__main__':
    main()
