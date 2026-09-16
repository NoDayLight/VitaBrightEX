#!/usr/bin/env python3
import hashlib
import json
import struct

IDENTITY = [0,0,1023,0,1023,0,512,0,0,0,512,0,0,0,512]
FULL_TO_LIMITED = [64,64,940,64,940,64,439,0,0,0,439,0,0,0,439]
PAIRS = [(0,1),(0,2),(1,0),(1,2),(2,0),(2,1)]

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

def entry(pid, kind, row, col, value):
    m = matrix_identity()
    m[row * 3 + col] = value
    iw = IDENTITY[:6] + [raw(x) for x in m]
    rw = FULL_TO_LIMITED[:6] + [raw(round_away(439 * x)) for x in m]
    return {
        'id': pid,
        'kind': kind,
        'row': row,
        'col': col,
        'matrix_s3_9': m,
        'identity_forward_sha256': sha(iw),
        'full_to_limited_forward_sha256': sha(rw),
    }

def build():
    probes = []
    for i in range(3):
        probes.append(entry(f'D{i}', 'diagonal', i, i, 256))
    for r, c in PAIRS:
        probes.append(entry(f'B{r}{c}', 'positive_offdiag', r, c, 128))
    for r, c in PAIRS:
        probes.append(entry(f'N{r}{c}', 'negative_offdiag_candidate', r, c, -128))
    return {
        'format_version': 1,
        'baseline_identity_sha256': sha(IDENTITY),
        'baseline_full_to_limited_sha256': sha(FULL_TO_LIMITED),
        'probes': probes,
    }

if __name__ == '__main__':
    print(json.dumps(build(), indent=2, sort_keys=True))
