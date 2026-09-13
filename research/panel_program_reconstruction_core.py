#!/usr/bin/env python3
from __future__ import annotations
import argparse, hashlib, json, re
from collections import defaultdict
from pathlib import Path
from vita_elf_audit import VitaElf

LCD_SHA = '24752bb4c69f0c241701cab0364cde9cbe5c95128b6bfe02bf3263aad524246e'
BASE = {'low': 0x81001AF8, 'mid': 0x81001AC8, 'high': 0x81001B90}
COLOR = {0: 0x81001AE8, 1: 0x81001B70}
SECONDARY = {0: 0x81001B80, 1: 0x81001B20}
WORK = 0x81002020
SPLIT, END, DELAY = 0x29, 0xFF, 0x0D
DCS_HINTS = {0x0A: 'GET_POWER_MODE', 0x26: 'SET_GAMMA_CURVE',
             0x29: 'SET_DISPLAY_ON', 0x2D: 'WRITE_LUT'}

def byte(e, va):
    _, off = e.file_from_va(va)
    return e.data[off]

def payload(e, va, n):
    _, off = e.file_from_va(va)
    p = e.data[off:off+n]
    if len(p) != n:
        raise ValueError('payload exceeds file-backed data')
    return p

def raw_record(e, va):
    cmd, n = byte(e, va), byte(e, va + 1)
    if cmd == DELAY:
        return {'cmd': cmd, 'length': n, 'payload': b'', 'next': va + 2,
                'source_va': va}
    p = payload(e, va + 2, n) if n else b''
    return {'cmd': cmd, 'length': n, 'payload': p, 'next': va + 2 + n,
            'source_va': va}

def terminated_fragment(e, start, label):
    out, va = [], start
    for _ in range(1024):
        if byte(e, va) == END:
            return {'status': 'terminated', 'terminator_va': va, 'records': out}
        r = raw_record(e, va)
        r['fragment'] = label
        out.append(r)
        va = r['next']
    raise ValueError(f'{label} did not terminate from 0x{start:08X}')

def base_to_split(e, start):
    out, va = [], start
    for _ in range(1024):
        try:
            cmd = byte(e, va)
        except ValueError:
            return {'status': 'left_file_backed_data_before_split',
                    'stop_va': va, 'records': out}
        if cmd == SPLIT:
            return {'status': 'reached_split', 'split_va': va, 'records': out}
        try:
            r = raw_record(e, va)
        except ValueError:
            return {'status': 'record_left_file_backed_data_before_split',
                    'stop_va': va, 'records': out}
        r['fragment'] = 'base_prefix'
        out.append(r)
        if r['next'] <= va:
            return {'status': 'non_progressing_record', 'stop_va': va,
                    'records': out}
        va = r['next']
    return {'status': 'guard_exhausted', 'stop_va': va, 'records': out}

def suffix_from_split(e, split_va):
    out, va = [], split_va
    for _ in range(1024):
        if byte(e, va) == END:
            return {'status': 'terminated', 'terminator_va': va, 'records': out}
        r = raw_record(e, va)
        r['fragment'] = 'base_suffix'
        out.append(r)
        va = r['next']
    raise ValueError(f'suffix did not terminate from 0x{split_va:08X}')

def meta(r, index=None):
    d = {'command': r['cmd'], 'command_hex': f"0x{r['cmd']:02X}",
         'length': r['length'],
         'kind': 'delay' if r['cmd'] == DELAY else 'panel_write',
         'payload_sha256': hashlib.sha256(r['payload']).hexdigest(),
         'source_fragment': r['fragment'], 'source_va': r['source_va']}
    if index is not None:
        d['index'] = index
    return d

def stream_digest(rows):
    h = hashlib.sha256()
    for r in rows:
        h.update(bytes([r['cmd'], r['length']]))
        if r['cmd'] != DELAY:
            h.update(r['payload'])
    h.update(bytes([END]))
    return h.hexdigest()

def working(e, base, color, secondary):
    suffix = suffix_from_split(e, base['split_va'])
    col = terminated_fragment(e, COLOR[color], 'color')
    sec = terminated_fragment(e, SECONDARY[secondary], 'secondary')
    return base['records'] + col['records'] + sec['records'] + suffix['records']

def lcs_pairs(a, b):
    n, m = len(a), len(b)
    dp = [[0] * (m + 1) for _ in range(n + 1)]
    for i in range(n - 1, -1, -1):
        for j in range(m - 1, -1, -1):
            dp[i][j] = 1 + dp[i+1][j+1] if a[i]['cmd'] == b[j]['cmd'] \
                       else max(dp[i+1][j], dp[i][j+1])
    i = j = 0
    pairs = []
    while i < n and j < m:
        if a[i]['cmd'] == b[j]['cmd']:
            pairs.append((i, j)); i += 1; j += 1
        elif dp[i+1][j] >= dp[i][j+1]:
            i += 1
        else:
            j += 1
    return pairs

def diff(a, b):
    pairs, out, ai, bi = lcs_pairs(a, b), [], 0, 0
    for i, j in pairs + [(len(a), len(b))]:
        while ai < i:
            out.append({'classification': 'command_removed', 'mode0_index': ai,
                        'command': a[ai]['cmd'], 'length': a[ai]['length']}); ai += 1
        while bi < j:
            out.append({'classification': 'command_added', 'mode1_index': bi,
                        'command': b[bi]['cmd'], 'length': b[bi]['length']}); bi += 1
        if i < len(a):
            x, y = a[i], b[j]
            hx, hy = hashlib.sha256(x['payload']).hexdigest(), hashlib.sha256(y['payload']).hexdigest()
            kind = ('same_command_changed_length' if x['length'] != y['length'] else
                    ('same_command_changed_payload' if hx != hy else 'same_command_identical'))
            if kind != 'same_command_identical':
                out.append({'classification': kind, 'mode0_index': i,
                            'mode1_index': j, 'command': x['cmd'],
                            'length0': x['length'], 'length1': y['length'],
                            'payload0_sha256': hx, 'payload1_sha256': hy})
            ai, bi = i + 1, j + 1
    return out

def command_inventory(context_rows):
    inv = defaultdict(lambda: {'lengths': set(), 'occurrences': 0,
        'source_fragments': set(), 'ddb_buckets': set(),
        'color_space_modes': set(), 'secondary_states': set(),
        'payload_sha256': set()})
    max_payload = 0
    for ctx, rows in context_rows:
        for r in rows:
            if r['cmd'] == DELAY:
                continue
            q = inv[r['cmd']]
            q['lengths'].add(r['length']); q['occurrences'] += 1
            q['source_fragments'].add(r['fragment'])
            if ctx.get('ddb_bucket') is not None: q['ddb_buckets'].add(ctx['ddb_bucket'])
            if ctx.get('color_space_mode') is not None: q['color_space_modes'].add(ctx['color_space_mode'])
            if ctx.get('secondary_state') is not None: q['secondary_states'].add(ctx['secondary_state'])
            q['payload_sha256'].add(hashlib.sha256(r['payload']).hexdigest())
            max_payload = max(max_payload, r['length'])
    rows = []
    for cmd in sorted(inv):
        q = inv[cmd]
        rows.append({'command': cmd, 'command_hex': f'0x{cmd:02X}',
            'common_dcs_name_hint': DCS_HINTS.get(cmd),
            'lengths': sorted(q['lengths']), 'occurrences': q['occurrences'],
            'source_fragments': sorted(q['source_fragments']),
            'ddb_buckets': sorted(q['ddb_buckets']),
            'color_space_modes': sorted(q['color_space_modes']),
            'secondary_states': sorted(q['secondary_states']),
            'unique_payload_count': len(q['payload_sha256']),
            'payload_sha256': sorted(q['payload_sha256'])})
    return rows, max_payload

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--lcd', type=Path, required=True)
    ap.add_argument('--json', type=Path, required=True)
    args = ap.parse_args()
    e = VitaElf(args.lcd)
    if e.sha256 != LCD_SHA:
        raise SystemExit(f'SceLcd hash drift {e.sha256}')

    bases = {name: base_to_split(e, va) for name, va in BASE.items()}
    colors = {mode: terminated_fragment(e, va, 'color') for mode, va in COLOR.items()}
    secondaries = {sel: terminated_fragment(e, va, 'secondary') for sel, va in SECONDARY.items()}
    reconstructible = [name for name, data in bases.items()
                       if data['status'] == 'reached_split']

    variants, by, context_rows = [], {}, []
    for bucket in reconstructible:
        for secondary in (0, 1):
            for color in (0, 1):
                rows = working(e, bases[bucket], color, secondary)
                key = (bucket, secondary, color)
                by[key] = rows
                ctx = {'ddb_bucket': bucket, 'secondary_state': secondary,
                       'color_space_mode': color}
                context_rows.append((ctx, rows))
                variants.append({**ctx, 'record_count': len(rows),
                    'working_stream_sha256': stream_digest(rows),
                    'records': [meta(r, i) for i, r in enumerate(rows)]})

    deltas = []
    for bucket in reconstructible:
        for secondary in (0, 1):
            a0, a1 = by[(bucket, secondary, 0)], by[(bucket, secondary, 1)]
            deltas.append({'ddb_bucket': bucket, 'secondary_state': secondary,
                'mode0_sha256': stream_digest(a0), 'mode1_sha256': stream_digest(a1),
                'changes': diff(a0, a1)})

    fragment_rows = []
    for mode, data in colors.items():
        fragment_rows.append(({'color_space_mode': mode}, data['records']))
    for sel, data in secondaries.items():
        fragment_rows.append(({'secondary_state': sel}, data['records']))
    for bucket, data in bases.items():
        fragment_rows.append(({'ddb_bucket': bucket}, data['records']))
    inventory, observed_max = command_inventory(context_rows or fragment_rows)

    protocol = (Path(__file__).parent / 'diagnostics/iftu_csc_trace/trace_protocol.h').read_text()
    m = re.search(r'^\s*#define\s+VBE_TRACE_PAYLOAD_MAX\s+(\d+)(?:u|U)?\s*$',
                  protocol, re.M)
    if not m:
        raise SystemExit('cannot derive VBE_TRACE_PAYLOAD_MAX')
    trace_capacity = int(m.group(1))
    encoded_max = 255
    if trace_capacity < encoded_max:
        raise SystemExit(f'trace payload capacity {trace_capacity} < uint8 record maximum {encoded_max}')

    base_meta = {}
    for bucket, data in bases.items():
        base_meta[bucket] = {'root_va': BASE[bucket], 'status': data['status'],
            'split_va': data.get('split_va'), 'stop_va': data.get('stop_va'),
            'header_count': len(data['records']),
            'headers': [meta(r, i) for i, r in enumerate(data['records'])]}

    result = {
        'schema': 3, 'firmware': '3.65',
        'status': ('PROVEN_STATIC_RECONSTRUCTION' if len(reconstructible) == 3
                   else 'PARTIAL_STATIC_RECONSTRUCTION'),
        'builder_va': 0x81000B78, 'working_buffer_va': WORK,
        'record_format': {'terminator': END, 'delay_command': DELAY,
            'length_field_bits': 8, 'encoded_max_payload_length': encoded_max,
            'delay_encoding': '0x0D,length; no payload',
            'write_encoding': 'command,length,payload[length]'},
        'builder_semantics': {
            'base_prefix': 'exact loop: stop only when record-boundary command is 0x29; 0xFF is not terminal here',
            'color_fragment': 'copy records until command 0xFF',
            'secondary_fragment': 'copy records until command 0xFF',
            'base_suffix': 'resume at 0x29 and copy records until command 0xFF'},
        'base_bucket_analysis': base_meta,
        'reconstructible_buckets': reconstructible,
        'branch_space': {'ddb_buckets': {'low': 'state+0x0C <= 0x24',
            'mid': '0x24 < state+0x0C <= 0x31', 'high': 'state+0x0C > 0x31'},
            'color_space_modes': [0, 1], 'secondary_state': [0, 1]},
        'reconstructed_variant_count': len(variants),
        'observed_max_payload_length': observed_max,
        'trace_payload_capacity': trace_capacity,
        'trace_payload_capacity_assertion':
            f'PASS: {trace_capacity} >= uint8 record-format maximum {encoded_max}',
        'command_inventory': inventory,
        'standalone_fragments': {
            'color': {str(k): {'root_va': COLOR[k],
                'records': [meta(r, i) for i, r in enumerate(v['records'])]}
                for k, v in colors.items()},
            'secondary': {str(k): {'root_va': SECONDARY[k],
                'records': [meta(r, i) for i, r in enumerate(v['records'])]}
                for k, v in secondaries.items()}},
        'variants': variants, 'mode_deltas': deltas,
        'command_namespace_note':
            'Common MIPI-DCS names are hints only; matching bytes do not prove controller identity.',
        'correction_note':
            'Schema-1 incorrectly stopped the base-prefix walk at 0xFF. The verified builder stops that first loop only at 0x29. Buckets that do not reach 0x29 through exact file-backed record walking are no longer called valid composed programs; Gate-0 runtime state and traffic resolve the physical branch.',
        'artifact_policy':
            'No proprietary payload bytes are emitted; only command IDs, lengths, source VAs, dependence metadata, and SHA-256 metadata.'}
    args.json.write_text(json.dumps(result, indent=2) + '\n')

    print('PANEL_PROGRAM_RECONSTRUCTION')
    for bucket in ('low', 'mid', 'high'):
        x = base_meta[bucket]
        split = f"0x{x['split_va']:08X}" if x['split_va'] is not None else 'none'
        stop = f"0x{x['stop_va']:08X}" if x['stop_va'] is not None else 'none'
        print(f"  bucket={bucket} root=0x{x['root_va']:08X} status={x['status']} headers={x['header_count']} split={split} stop={stop}")
    print(f'  reconstructible_buckets={reconstructible} variants={len(variants)}')
    print(f'  observed_max_payload={observed_max}')
    print(f'  trace_capacity={trace_capacity} encoded_max={encoded_max} PASS')
    print('COMMAND_INVENTORY')
    for row in inventory:
        print(f"  {row['command_hex']} lengths={row['lengths']} occurrences={row['occurrences']} fragments={row['source_fragments']} dcs_hint={row['common_dcs_name_hint']}")

if __name__ == '__main__':
    main()
