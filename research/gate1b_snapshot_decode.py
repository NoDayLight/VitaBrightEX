#!/usr/bin/env python3
from __future__ import annotations
import argparse, hashlib, json, struct
from pathlib import Path

MAGIC = 0x42314756
VERSION = 1
SIZE = 344
FW_365 = 0x03650000
PLANE_SIZE = 152
HEADER = struct.Struct('<10I')
PLANE_HEAD = struct.Struct('<8I')
A_CANONICAL_SHA256 = '2f9fd211d1d389611267070cfbc936063b0b790a59245243feb404ee3c00daf6'
B_CANONICAL_SHA256 = '5dc12dfcae42a648dc093db831b661cc3068f2d70f199b03fb582ce301b188b5'


def sha(b: bytes) -> str:
    return hashlib.sha256(b).hexdigest()


def csc_words(b: bytes) -> list[int]:
    if len(b) != 60:
        raise ValueError('CSC payload is not 0x3C bytes')
    return list(struct.unpack('<15I', b))


def parse_bytes(raw: bytes) -> dict:
    if len(raw) != SIZE:
        raise ValueError(f'wrong snapshot size: {len(raw)} != {SIZE}')
    h = HEADER.unpack_from(raw, 0)
    names = ('magic','version','struct_size','firmware_version','sequence','status_flags',
             'lowio_modid','lowio_module_nid','lowio_segment_index','reserved')
    out = dict(zip(names, h))
    if out['magic'] != MAGIC:
        raise ValueError(f'bad magic 0x{out["magic"]:08X}')
    if out['version'] != VERSION:
        raise ValueError(f'bad version {out["version"]}')
    if out['struct_size'] != SIZE:
        raise ValueError(f'bad struct_size {out["struct_size"]}')
    if out['firmware_version'] != FW_365:
        raise ValueError(f'wrong firmware 0x{out["firmware_version"]:08X}')

    out['planes'] = []
    base = HEADER.size
    for idx in range(2):
        off = base + idx * PLANE_SIZE
        p = PLANE_HEAD.unpack_from(raw, off)
        pn = ('plane','flags','state_address','mmio_base','live_csc_control',
              'enable_state_before','enable_state_after','private_control')
        row = dict(zip(pn, p))
        a = raw[off + 32:off + 92]
        b = raw[off + 92:off + 152]
        row['cache_a_sha256'] = sha(a)
        row['cache_b_sha256'] = sha(b)
        row['cache_a_canonical'] = row['cache_a_sha256'] == A_CANONICAL_SHA256
        row['cache_b_canonical'] = row['cache_b_sha256'] == B_CANONICAL_SHA256
        row['cache_a_words'] = csc_words(a)
        row['cache_b_words'] = csc_words(b)
        out['planes'].append(row)
    return out


def parse_file(path: Path) -> dict:
    out = parse_bytes(path.read_bytes())
    out['file'] = str(path)
    out['sha256'] = sha(path.read_bytes())
    return out


def fmt_snapshot(x: dict) -> str:
    lines = [
        f"FILE={x.get('file','<memory>')}",
        f"FILE_SHA256={x.get('sha256','-')}",
        f"SEQUENCE={x['sequence']}",
        f"STATUS_FLAGS=0x{x['status_flags']:08X}",
        f"LOWIO_MODID=0x{x['lowio_modid']:08X}",
        f"LOWIO_MODULE_NID=0x{x['lowio_module_nid']:08X}",
        f"LOWIO_SEGMENT={x['lowio_segment_index']}",
    ]
    for p in x['planes']:
        lines.extend([
            f"PLANE{p['plane']}_FLAGS=0x{p['flags']:08X}",
            f"PLANE{p['plane']}_STATE=0x{p['state_address']:08X}",
            f"PLANE{p['plane']}_MMIO=0x{p['mmio_base']:08X}",
            f"PLANE{p['plane']}_CSC_CONTROL=0x{p['live_csc_control']:08X}",
            f"PLANE{p['plane']}_ENABLE_BEFORE=0x{p['enable_state_before']:08X}",
            f"PLANE{p['plane']}_ENABLE_AFTER=0x{p['enable_state_after']:08X}",
            f"PLANE{p['plane']}_PRIVATE_CONTROL=0x{p['private_control']:08X}",
            f"PLANE{p['plane']}_A_SHA256={p['cache_a_sha256']} canonical={str(p['cache_a_canonical']).upper()}",
            f"PLANE{p['plane']}_B_SHA256={p['cache_b_sha256']} canonical={str(p['cache_b_canonical']).upper()}",
            f"PLANE{p['plane']}_A_WORDS=" + ','.join(f'0x{w:08X}' for w in p['cache_a_words']),
            f"PLANE{p['plane']}_B_WORDS=" + ','.join(f'0x{w:08X}' for w in p['cache_b_words']),
        ])
    return '\n'.join(lines)


def compare(a: dict, b: dict) -> dict:
    rows = []
    for pa, pb in zip(a['planes'], b['planes']):
        rows.append({
            'plane': pa['plane'],
            'control_equal': pa['live_csc_control'] == pb['live_csc_control'],
            'cache_a_equal': pa['cache_a_sha256'] == pb['cache_a_sha256'],
            'cache_b_equal': pa['cache_b_sha256'] == pb['cache_b_sha256'],
            'private_control_equal': pa['private_control'] == pb['private_control'],
            'enable_stable_both': pa['enable_state_before'] == pa['enable_state_after'] and pb['enable_state_before'] == pb['enable_state_after'],
        })
    return {'planes': rows}


def self_test() -> None:
    a_words = [0, 0x202, 0x3FF, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
    b_words = [0, 0, 0x3FF, 0, 0x3FF, 0, 0x200, 0, 0, 0, 0x200, 0, 0, 0, 0x200]
    a = struct.pack('<15I', *a_words)
    b = struct.pack('<15I', *b_words)
    assert sha(a) == A_CANONICAL_SHA256
    assert sha(b) == B_CANONICAL_SHA256
    raw = bytearray(SIZE)
    HEADER.pack_into(raw, 0, MAGIC, VERSION, SIZE, FW_365, 1, 0xF, 1, 2, 1, 0)
    for i in range(2):
        off = HEADER.size + i * PLANE_SIZE
        PLANE_HEAD.pack_into(raw, off, i, 0x1F, 0x90000000 + i * 0x214,
                             0xE5020000 + i * 0x1000, 1, 2, 2, 0x100)
        raw[off + 32:off + 92] = a
        raw[off + 92:off + 152] = b
    x = parse_bytes(bytes(raw))
    assert all(p['cache_a_canonical'] and p['cache_b_canonical'] for p in x['planes'])
    assert all(p['live_csc_control'] == 1 for p in x['planes'])
    print('GATE1B_SNAPSHOT_DECODER_SELFTEST=PASS')


def main() -> None:
    ap = argparse.ArgumentParser(description='Decode Gate-1B read-only CSC-control snapshots')
    ap.add_argument('files', nargs='*', type=Path)
    ap.add_argument('--json', action='store_true')
    ap.add_argument('--self-test', action='store_true')
    args = ap.parse_args()
    if args.self_test:
        self_test()
        return
    if not args.files:
        ap.error('provide one or two snapshot files, or --self-test')
    if len(args.files) > 2:
        ap.error('at most two snapshots are supported')
    xs = [parse_file(p) for p in args.files]
    if args.json:
        payload = {'snapshots': xs}
        if len(xs) == 2:
            payload['comparison'] = compare(xs[0], xs[1])
        print(json.dumps(payload, indent=2))
        return
    for i, x in enumerate(xs):
        if i:
            print()
        print(fmt_snapshot(x))
    if len(xs) == 2:
        print('\nCOMPARISON=' + json.dumps(compare(xs[0], xs[1]), sort_keys=True))


if __name__ == '__main__':
    main()
