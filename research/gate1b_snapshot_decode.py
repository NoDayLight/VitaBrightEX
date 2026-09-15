#!/usr/bin/env python3
from __future__ import annotations
import argparse, hashlib, json, struct
from pathlib import Path

MAGIC = 0x42314756
BUNDLE_MAGIC = 0x32424756
VERSION = 4
FW_365 = 0x03650000
PLANE_SIZE = 152
SNAPSHOT_SIZE = 344
STATUS_SIZE = 64
BUNDLE_SIZE = 784
BUNDLE_HEAD = struct.Struct('<8I')
STATUS = struct.Struct('<16I')
SNAP_HEAD = struct.Struct('<10I')
PLANE_HEAD = struct.Struct('<8I')
A_CANONICAL_SHA256 = '2f9fd211d1d389611267070cfbc936063b0b790a59245243feb404ee3c00daf6'
B_CANONICAL_SHA256 = '5dc12dfcae42a648dc093db831b661cc3068f2d70f199b03fb582ce301b188b5'
EXPECTED_MMIO = {0: 0x280B2000, 1: 0x280B4000}
REASON = {
    0: 'NONE',
    1: 'PRE_SUSPEND_4000',
    2: 'POST_RESUME_100000',
}
PLANE_CONTROL_READ = 1 << 3
PLANE_ENABLE_STABLE = 1 << 4
PLANE_CANONICAL_A = 1 << 5
PLANE_CANONICAL_B = 1 << 6
OBSERVER_SYSEVENT = 1 << 0


def sha(b: bytes) -> str:
    return hashlib.sha256(b).hexdigest()


def csc_words(b: bytes) -> list[int]:
    if len(b) != 60:
        raise ValueError('CSC payload is not 0x3C bytes')
    return list(struct.unpack('<15I', b))


def parse_snapshot(raw: bytes, off: int) -> dict:
    h = SNAP_HEAD.unpack_from(raw, off)
    names = ('magic','version','struct_size','firmware_version','capture_sequence',
             'status_flags','lowio_modid','lowio_module_nid','lowio_segment_index',
             'capture_reason')
    out = dict(zip(names, h))
    out['capture_reason_name'] = REASON.get(out['capture_reason'], 'UNKNOWN')
    out['planes'] = []
    base = off + SNAP_HEAD.size
    for idx in range(2):
        poff = base + idx * PLANE_SIZE
        p = PLANE_HEAD.unpack_from(raw, poff)
        pn = ('plane','flags','state_address','mmio_base','live_csc_control',
              'enable_state_before','enable_state_after','private_control')
        row = dict(zip(pn, p))
        a = raw[poff + 32:poff + 92]
        b = raw[poff + 92:poff + 152]
        row['cache_a_sha256'] = sha(a)
        row['cache_b_sha256'] = sha(b)
        row['cache_a_canonical'] = row['cache_a_sha256'] == A_CANONICAL_SHA256
        row['cache_b_canonical'] = row['cache_b_sha256'] == B_CANONICAL_SHA256
        row['cache_a_words'] = csc_words(a)
        row['cache_b_words'] = csc_words(b)
        row['control_read'] = bool(row['flags'] & PLANE_CONTROL_READ)
        row['enable_stable'] = bool(row['flags'] & PLANE_ENABLE_STABLE)
        row['mmio_expected'] = row['mmio_base'] == EXPECTED_MMIO.get(row['plane'])
        out['planes'].append(row)
    return out


def validate_snapshot(x: dict, *, published: bool) -> list[str]:
    errors = []
    if not published:
        return ['snapshot was not published']
    if x['magic'] != MAGIC: errors.append(f'bad snapshot magic 0x{x["magic"]:08X}')
    if x['version'] != VERSION: errors.append(f'bad snapshot version {x["version"]}')
    if x['struct_size'] != SNAPSHOT_SIZE: errors.append(f'bad snapshot size {x["struct_size"]}')
    if x['firmware_version'] != FW_365: errors.append(f'wrong snapshot firmware 0x{x["firmware_version"]:08X}')
    if [p['plane'] for p in x['planes']] != [0, 1]:
        errors.append(f'unexpected plane order {[p["plane"] for p in x["planes"]]}')
    for p in x['planes']:
        expected = EXPECTED_MMIO.get(p['plane'])
        if expected is None:
            errors.append(f'plane {p["plane"]}: no authorized runtime MMIO mapping')
        elif p['mmio_base'] != expected:
            errors.append(f'plane {p["plane"]}: runtime MMIO drift (0x{p["mmio_base"]:08X} != 0x{expected:08X})')
        if not p['control_read']: errors.append(f'plane {p["plane"]}: csc_control unread')
        if not p['enable_stable']:
            errors.append(f'plane {p["plane"]}: enable scalar unstable ({p["enable_state_before"]}->{p["enable_state_after"]})')
        if not p['cache_a_canonical']: errors.append(f'plane {p["plane"]}: A not canonical')
        if not p['cache_b_canonical']: errors.append(f'plane {p["plane"]}: B not canonical')
    return errors


def compare(a: dict, b: dict) -> dict:
    rows = []
    for pa, pb in zip(a['planes'], b['planes']):
        rows.append({
            'plane': pa['plane'],
            'control_pre': pa['live_csc_control'],
            'control_post': pb['live_csc_control'],
            'control_equal': pa['live_csc_control'] == pb['live_csc_control'],
            'mmio_pre': pa['mmio_base'],
            'mmio_post': pb['mmio_base'],
            'mmio_equal': pa['mmio_base'] == pb['mmio_base'],
            'cache_a_equal': pa['cache_a_sha256'] == pb['cache_a_sha256'],
            'cache_b_equal': pa['cache_b_sha256'] == pb['cache_b_sha256'],
            'private_control_pre': pa['private_control'],
            'private_control_post': pb['private_control'],
            'private_control_equal': pa['private_control'] == pb['private_control'],
            'enable_pre': pa['enable_state_after'],
            'enable_post': pb['enable_state_after'],
            'enable_equal': pa['enable_state_after'] == pb['enable_state_after'],
        })
    return {'planes': rows}


def parse_bundle_bytes(raw: bytes, authoritative: bool = False) -> dict:
    if len(raw) != BUNDLE_SIZE:
        raise ValueError(f'wrong bundle size: {len(raw)} != {BUNDLE_SIZE}')
    bh = BUNDLE_HEAD.unpack_from(raw, 0)
    bnames = ('magic','version','struct_size','status_size','snapshot_size',
              'snapshot_count','reserved0','reserved1')
    out = dict(zip(bnames, bh))
    if out['magic'] != BUNDLE_MAGIC: raise ValueError(f'bad bundle magic 0x{out["magic"]:08X}')
    if out['version'] != VERSION: raise ValueError(f'bad bundle version {out["version"]}')
    if out['struct_size'] != BUNDLE_SIZE: raise ValueError(f'bad bundle struct_size {out["struct_size"]}')
    if out['status_size'] != STATUS_SIZE: raise ValueError(f'bad status_size {out["status_size"]}')
    if out['snapshot_size'] != SNAPSHOT_SIZE: raise ValueError(f'bad snapshot_size {out["snapshot_size"]}')
    if out['snapshot_count'] != 2: raise ValueError(f'bad snapshot_count {out["snapshot_count"]}')

    st = STATUS.unpack_from(raw, BUNDLE_HEAD.size)
    snames = ('magic','version','firmware_version','status_flags','observer_mask',
              'required_observer_mask','missing_observer_mask','fail_mask','pre_event_count',
              'post_event_count','pre_published','post_published','reserved0','reserved1',
              'pre_reason','post_reason')
    out['status'] = dict(zip(snames, st))
    out['status']['pre_reason_name'] = REASON.get(out['status']['pre_reason'], 'UNKNOWN')
    out['status']['post_reason_name'] = REASON.get(out['status']['post_reason'], 'UNKNOWN')
    base = BUNDLE_HEAD.size + STATUS_SIZE
    out['snapshots'] = [
        parse_snapshot(raw, base),
        parse_snapshot(raw, base + SNAPSHOT_SIZE),
    ]
    out['comparison'] = compare(out['snapshots'][0], out['snapshots'][1])

    errors = []
    s = out['status']
    if s['magic'] != MAGIC: errors.append(f'bad status magic 0x{s["magic"]:08X}')
    if s['version'] != VERSION: errors.append(f'bad status version {s["version"]}')
    if s['firmware_version'] != FW_365: errors.append(f'wrong firmware 0x{s["firmware_version"]:08X}')
    if s['required_observer_mask'] != OBSERVER_SYSEVENT:
        errors.append(f'wrong required observer mask 0x{s["required_observer_mask"]:X}')
    if s['observer_mask'] != s['required_observer_mask']:
        errors.append('system-event observer ownership incomplete')
    if s['missing_observer_mask'] != 0:
        errors.append(f'missing observers 0x{s["missing_observer_mask"]:X}')
    if s['fail_mask'] != 0: errors.append(f'fail mask 0x{s["fail_mask"]:08X}')
    if s['pre_event_count'] < 1: errors.append('no pre-suspend 0x4000 event observed')
    if s['post_event_count'] < 1: errors.append('no post-resume 0x100000 event observed')
    if s['pre_reason'] != 1: errors.append(f'unexpected pre-suspend reason {s["pre_reason"]}')
    if s['post_reason'] != 2: errors.append(f'unexpected post-resume reason {s["post_reason"]}')
    errors.extend('pre-suspend: '+e for e in validate_snapshot(out['snapshots'][0], published=bool(s['pre_published'])))
    errors.extend('post-resume: '+e for e in validate_snapshot(out['snapshots'][1], published=bool(s['post_published'])))

    if authoritative:
        pre, post = out['snapshots']
        if pre['capture_reason'] != 1: errors.append('pre snapshot reason mismatch')
        if post['capture_reason'] != 2: errors.append('post snapshot reason mismatch')
        if pre['capture_sequence'] >= post['capture_sequence']:
            errors.append(f'capture sequence not increasing ({pre["capture_sequence"]}->{post["capture_sequence"]})')
        for row in out['comparison']['planes']:
            if not row['mmio_equal']:
                errors.append(f'plane {row["plane"]}: runtime MMIO changed across resume (0x{row["mmio_pre"]:08X}->0x{row["mmio_post"]:08X})')
            if not row['control_equal']:
                errors.append(f'plane {row["plane"]}: csc_control changed across resume (0x{row["control_pre"]:08X}->0x{row["control_post"]:08X})')
            if not row['cache_a_equal']: errors.append(f'plane {row["plane"]}: A cache changed across resume')
            if not row['cache_b_equal']: errors.append(f'plane {row["plane"]}: B cache changed across resume')
    out['authoritative_errors'] = errors
    out['authoritative_pass'] = not errors
    if authoritative and errors:
        raise ValueError('authoritative Gate-1B snapshot failed: ' + '; '.join(errors))
    return out


def parse_file(path: Path, authoritative: bool = False) -> dict:
    raw = path.read_bytes()
    out = parse_bundle_bytes(raw, authoritative=authoritative)
    out['file'] = str(path)
    out['sha256'] = sha(raw)
    return out


def fmt_bundle(x: dict) -> str:
    s = x['status']
    lines = [
        f"FILE={x.get('file','<memory>')}",
        f"FILE_SHA256={x.get('sha256','-')}",
        f"STATUS_FLAGS=0x{s['status_flags']:08X}",
        f"OBSERVERS=owned:0x{s['observer_mask']:X} required:0x{s['required_observer_mask']:X} missing:0x{s['missing_observer_mask']:X}",
        f"FAIL_MASK=0x{s['fail_mask']:08X}",
        f"EVENTS=pre_4000:{s['pre_event_count']} post_100000:{s['post_event_count']}",
        f"PUBLISHED=pre:{s['pre_published']} post:{s['post_published']}",
        f"REASONS=pre:{s['pre_reason_name']} post:{s['post_reason_name']}",
    ]
    for label, snap in zip(('PRE_SUSPEND','POST_RESUME'), x['snapshots']):
        lines.append(f"{label}_CAPTURE_SEQUENCE={snap['capture_sequence']}")
        lines.append(f"{label}_REASON={snap['capture_reason_name']}")
        for p in snap['planes']:
            lines.extend([
                f"{label}_P{p['plane']}_FLAGS=0x{p['flags']:08X}",
                f"{label}_P{p['plane']}_MMIO=0x{p['mmio_base']:08X} expected={str(p['mmio_expected']).upper()}",
                f"{label}_P{p['plane']}_CSC_CONTROL=0x{p['live_csc_control']:08X}",
                f"{label}_P{p['plane']}_ENABLE={p['enable_state_before']}->{p['enable_state_after']} stable={str(p['enable_stable']).upper()}",
                f"{label}_P{p['plane']}_PRIVATE_CONTROL=0x{p['private_control']:08X}",
                f"{label}_P{p['plane']}_A_SHA256={p['cache_a_sha256']} canonical={str(p['cache_a_canonical']).upper()}",
                f"{label}_P{p['plane']}_B_SHA256={p['cache_b_sha256']} canonical={str(p['cache_b_canonical']).upper()}",
            ])
    lines.append('COMPARISON=' + json.dumps(x['comparison'], sort_keys=True))
    lines.append('AUTHORITATIVE=' + ('PASS' if x['authoritative_pass'] else 'FAIL'))
    if x['authoritative_errors']:
        lines.append('AUTHORITATIVE_ERRORS=' + json.dumps(x['authoritative_errors']))
    return '\n'.join(lines)


def self_test() -> None:
    a_words = [0, 0x202, 0x3FF, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
    b_words = [0, 0, 0x3FF, 0, 0x3FF, 0, 0x200, 0, 0, 0, 0x200, 0, 0, 0, 0x200]
    a = struct.pack('<15I', *a_words)
    b = struct.pack('<15I', *b_words)
    assert sha(a) == A_CANONICAL_SHA256
    assert sha(b) == B_CANONICAL_SHA256
    raw = bytearray(BUNDLE_SIZE)
    BUNDLE_HEAD.pack_into(raw, 0, BUNDLE_MAGIC, VERSION, BUNDLE_SIZE, STATUS_SIZE, SNAPSHOT_SIZE, 2, 0, 0)
    STATUS.pack_into(raw, BUNDLE_HEAD.size, MAGIC, VERSION, FW_365, 0x1F, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 2)
    base = BUNDLE_HEAD.size + STATUS_SIZE
    full_flags = 0x7F
    for si, reason in enumerate((1,2)):
        off = base + si * SNAPSHOT_SIZE
        SNAP_HEAD.pack_into(raw, off, MAGIC, VERSION, SNAPSHOT_SIZE, FW_365, si+1, 0x1F, 1, 2, 1, reason)
        for i in range(2):
            poff = off + SNAP_HEAD.size + i * PLANE_SIZE
            PLANE_HEAD.pack_into(raw, poff, i, full_flags, 0x90000000 + i * 0x214,
                                 EXPECTED_MMIO[i], 1, 2, 2, 0x100)
            raw[poff + 32:poff + 92] = a
            raw[poff + 92:poff + 152] = b
    x = parse_bundle_bytes(bytes(raw), authoritative=True)
    assert x['authoritative_pass']
    assert all(p['cache_a_canonical'] and p['cache_b_canonical'] for snap in x['snapshots'] for p in snap['planes'])
    assert all(p['mmio_expected'] for snap in x['snapshots'] for p in snap['planes'])
    assert all(p['live_csc_control'] == 1 for snap in x['snapshots'] for p in snap['planes'])
    print('GATE1B_SNAPSHOT_DECODER_SELFTEST=PASS')


def main() -> None:
    ap = argparse.ArgumentParser(description='Decode Gate-1B v4 runtime-mapped CSC-control bundle')
    ap.add_argument('file', nargs='?', type=Path)
    ap.add_argument('--json', action='store_true')
    ap.add_argument('--authoritative', action='store_true')
    ap.add_argument('--self-test', action='store_true')
    args = ap.parse_args()
    if args.self_test:
        self_test()
        return
    if args.file is None:
        ap.error('provide the capture bundle, or --self-test')
    x = parse_file(args.file, authoritative=args.authoritative)
    if args.json:
        print(json.dumps(x, indent=2))
    else:
        print(fmt_bundle(x))


if __name__ == '__main__':
    main()
