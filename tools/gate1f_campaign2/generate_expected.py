#!/usr/bin/env python3
import hashlib
import json
import struct

IDENTITY = [0,0,1023,0,1023,0,512,0,0,0,512,0,0,0,512]
FULL_TO_LIMITED = [64,64,940,64,940,64,439,0,0,0,439,0,0,0,439]
C1_RAW_SHA256 = '740c6f7a345a9544dc0b9c79b38040baa663e6f8a8dbb27483d304b9901278bd'

# Formal 50%-intensity witnesses are expressed in S3.9-like 0..512 component units.
PROBES = [
    ('D0-C2',  'control_diagonal',          0, 0,    0, [256,0,0]),
    ('B02-C2', 'control_positive_offdiag', 0, 2,  512, [0,0,256]),
    ('D2-C2',  'target_diagonal',           2, 2,    0, [0,0,256]),
    ('B01-C2', 'target_positive_offdiag',  0, 1,  512, [0,256,0]),
    ('B20-C2', 'target_positive_offdiag',  2, 0,  512, [256,0,0]),
    ('B21-C2', 'target_positive_offdiag',  2, 1,  512, [0,256,0]),
    ('N02-C2', 'signed_negative',           0, 2, -512, [256,0,256]),
]

def raw(v):
    return v & 0xFFF

def sha(words):
    return hashlib.sha256(struct.pack('<15I', *words)).hexdigest()

def round_away(n):
    sign = -1 if n < 0 else 1
    return sign * ((abs(n) + 256) // 512)

def matrix_identity():
    m = [0] * 9
    m[0] = m[4] = m[8] = 512
    return m

def apply_to_rgb50(m, v):
    out = []
    for r in range(3):
        n = sum(m[r*3+c] * v[c] for c in range(3))
        if n % 512:
            raise ValueError('formal witness produced non-integral host result')
        out.append(n // 512)
    return out

def entry(pid, kind, row, col, value, witness):
    m = matrix_identity()
    m[row * 3 + col] = value
    iw = IDENTITY[:6] + [raw(x) for x in m]
    rw = FULL_TO_LIMITED[:6] + [raw(round_away(439 * x)) for x in m]
    out = apply_to_rgb50(m, witness)
    in_range = all(0 <= x <= 512 for x in out)
    if not in_range:
        raise ValueError(f'{pid}: formal witness leaves [0,1] range: {out}')
    return {
        'id': pid,
        'kind': kind,
        'row': row,
        'col': col,
        'coefficient_s3_9': value,
        'matrix_s3_9': m,
        'formal_witness_rgb50': witness,
        'formal_witness_expected_ordinary_rgb50': out,
        'formal_witness_in_range': in_range,
        'identity_forward_words': [f'{x:08X}' for x in iw],
        'identity_forward_sha256': sha(iw),
        'full_to_limited_forward_words': [f'{x:08X}' for x in rw],
        'full_to_limited_forward_sha256': sha(rw),
    }

def build():
    probes = [entry(*p) for p in PROBES]
    return {
        'format_version': 2,
        'campaign': 2,
        'campaign1_raw_sha256': C1_RAW_SHA256,
        'base_identity_sha256': sha(IDENTITY),
        'baseline_full_to_limited_sha256': sha(FULL_TO_LIMITED),
        'stimulus_policy': {
            'cross_term_numerator': 512,
            'diagonal_zero_numerator': 0,
            'signed_cross_numerator': -512,
            'formal_component_50_numerator': 256,
            'range_min_numerator': 0,
            'range_max_numerator': 512,
        },
        'probes': probes,
    }

if __name__ == '__main__':
    print(json.dumps(build(), indent=2, sort_keys=True))
