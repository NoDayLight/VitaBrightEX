#!/usr/bin/env python3
from __future__ import annotations
import argparse, json
from pathlib import Path
from vita_elf_audit import VitaElf, Reachability, FunctionCFG
from topology_common import all_insns, ins_text

DISPLAY_SHA = '83ef39adf741a4bbbc1cec67d00e2594de1d17be0f5ea6cc77c935eae46065e5'
LOWIO_SHA = 'f791cfe2c6db955deb9446c57bb72ce1870bde2c6c485cac363e343d5128f744'
GENERATOR = 0x81000A2C
SETUP_CALLER = 0x81000C2C
CSC_CALLER = 0x810023B8
RELEVANT_NIDS = {
    0x0D7C02F7: 'ksceIftuEnable',
    0x0FCBF457: 'private_csc_cache_A',
    0x357EAE24: 'private_control',
    0x67E37EFC: 'ksceIftuCsc',
    0x7CE0C4DA: 'ksceIftuSetInputFrameBuffer',
    0xAF19FD85: 'ksceIftuSetMergeSetting',
    0xC11F30B3: 'ksceIftuDisable',
    0xD64F4C6B: 'private_csc_cache_B',
    0xE6EE2C6B: 'ksceIftuSetOutputFormat',
}
TABLES = (0x81009724, 0x8100977C, 0x810097D0, 0x8100980C, 0x81009970, 0x810099AC)

def signed12(x):
    v = x & 0xFFF
    return v - 0x1000 if v & 0x800 else v

def read_words(elf, va, n=15):
    _, off = elf.file_from_va(va)
    return [int.from_bytes(elf.data[off + 4*i:off + 4*i + 4], 'little') for i in range(n)]

def decode_table(elf, va):
    w = read_words(elf, va)
    return {
        'va': va,
        'post_add_0': w[0], 'post_add_1_2': w[1],
        'clamp0': [w[3], w[2]], 'clamp12': [w[5], w[4]],
        'ctm_units': [[signed12(w[6 + 3*r + c]) for c in range(3)] for r in range(3)],
    }

def cfg_for(elf, reach, va):
    for (start, thumb), cfg in reach.functions.items():
        if start == va:
            return cfg
    return FunctionCFG(elf, va, True, reach.import_stubs, boundary_index=reach.boundaries, noreturn_stubs=reach.noreturn_stubs)

def import_map(elf):
    by_stub = {}
    rows = []
    for lib in elf.imports():
        for f in lib['functions']:
            rec = {'library': lib['library_name'], 'nid': f['nid'], 'stub': f['va'], 'name': RELEVANT_NIDS.get(f['nid'], f'unknown_{f["nid"]:08X}')}
            by_stub[f['va']] = rec
            if f['nid'] in RELEVANT_NIDS:
                rows.append(rec)
    return by_stub, sorted(rows, key=lambda x: x['stub'])

def named_calls(cfg, by_stub):
    out = []
    for c in cfg.calls:
        target = c.get('target')
        if target in by_stub:
            out.append({'call_va': c['va'], 'target': target, **by_stub[target], 'window': cfg.window_for_call(c['va'], before=48, after=8)})
    return out

def enumerate_formula_fits(matrix):
    # Exact S3.9 integer forms of Sony's two asymmetric BT.709-family inverse
    # matrices.  Labels are formula labels only; format context determines
    # whether these labels may be attached to hardware axes.
    refs = {
        'BT709_LIMITED_YCBCR_TO_RGB_QS39': [[596, 0, 917], [596, -109, -272], [596, 1081, 0]],
        'BT709_FULL_YCBCR_TO_RGB_QS39': [[512, 0, 806], [512, -95, -239], [512, 950, 0]],
    }
    import itertools
    fits = []
    for name, ref in refs.items():
        for rp in itertools.permutations(range(3)):
            for cp in itertools.permutations(range(3)):
                for tr in (False, True):
                    candidate = [[0]*3 for _ in range(3)]
                    for r in range(3):
                        for c in range(3):
                            rr, cc = (c, r) if tr else (r, c)
                            candidate[r][c] = ref[rp[rr]][cp[cc]]
                    if candidate == matrix:
                        fits.append({'formula': name, 'row_permutation': list(rp), 'column_permutation': list(cp), 'transposed': tr})
    return fits

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--display', type=Path, required=True)
    ap.add_argument('--lowio', type=Path, required=True)
    ap.add_argument('--json', type=Path, required=True)
    args = ap.parse_args()
    display, lowio = VitaElf(args.display), VitaElf(args.lowio)
    if display.sha256 != DISPLAY_SHA: raise SystemExit('Display SHA drift')
    if lowio.sha256 != LOWIO_SHA: raise SystemExit('Lowio SHA drift')
    starts = [GENERATOR, SETUP_CALLER, CSC_CALLER]
    reach = Reachability(display, display.exports(), display.imports(), extra_starts=starts)
    by_stub, relevant_imports = import_map(display)
    generator = cfg_for(display, reach, GENERATOR)
    setup = cfg_for(display, reach, SETUP_CALLER)
    csc = cfg_for(display, reach, CSC_CALLER)
    tables = [decode_table(display, va) for va in TABLES]
    for t in tables:
        t['formula_fits'] = enumerate_formula_fits(t['ctm_units'])
    result = {
        'schema': 1, 'firmware': '3.65', 'display_sha': display.sha256, 'lowio_sha': lowio.sha256,
        'relevant_imports': relevant_imports,
        'generator_calls': named_calls(generator, by_stub),
        'setup_calls': named_calls(setup, by_stub),
        'csc_calls': named_calls(csc, by_stub),
        'sony_tables': tables,
        'generator_instructions': [{'va': i.address, 'text': ins_text(i)} for i in all_insns(generator)],
    }
    args.json.write_text(json.dumps(result, indent=2) + '\n')
    print('GATE1F_BOUNDED_SONY_CSC_AUDIT')
    print('DISPLAY_SHA=' + display.sha256)
    print('LOWIO_SHA=' + lowio.sha256)
    print('RELEVANT_IMPORTS')
    for x in relevant_imports:
        print(f"  stub=0x{x['stub']:08X} nid=0x{x['nid']:08X} {x['name']}")
    for label, calls in [('GENERATOR', result['generator_calls']), ('SETUP', result['setup_calls']), ('CSC_CALLER', result['csc_calls'])]:
        print(label + '_IMPORT_CALLS')
        for x in calls:
            print(f"  call=0x{x['call_va']:08X} -> 0x{x['target']:08X} {x['name']}")
    print('ASYMMETRIC_TABLES')
    for t in tables:
        if any(t['ctm_units'][r][c] != (512 if r == c else 0) for r in range(3) for c in range(3)):
            print(f"  0x{t['va']:08X} post_add=({t['post_add_0']},{t['post_add_1_2']}) ctm={t['ctm_units']} fits={t['formula_fits']}")

if __name__ == '__main__':
    main()
