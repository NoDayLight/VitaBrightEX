#!/usr/bin/env python3
from __future__ import annotations
import hashlib, json, struct

PRE = (0, 0, 0x3FF, 0, 0x3FF, 0)
IDENTITY = ((512, 0, 0), (0, 512, 0), (0, 0, 512))

def encode_s3_9_units(v: int) -> int:
    if not -2048 <= v <= 2047:
        raise ValueError(v)
    return v & 0xFFF

def object_record(matrix):
    coeff = [encode_s3_9_units(matrix[r][c]) for r in range(3) for c in range(3)]
    words = list(PRE) + coeff
    blob = struct.pack('<15I', *words)
    return {
        'matrix_s3_9_units': [list(row) for row in matrix],
        'words_u32_hex': [f'{w:08X}' for w in words],
        'sha256': hashlib.sha256(blob).hexdigest(),
        'size': len(blob),
    }

def knockout(c):
    m = [list(row) for row in IDENTITY]
    for r in range(3):
        m[r][c] = 0
    return tuple(tuple(row) for row in m)

def synth(r, c):
    m = [[0] * 3 for _ in range(3)]
    m[r][c] = 512
    return tuple(tuple(row) for row in m)

def subtract_slot(r, c):
    m = [list(row) for row in IDENTITY]
    m[r][c] -= 512
    return tuple(tuple(row) for row in m)

def safety(matrix):
    values = [x for row in matrix for x in row]
    assert all(-2048 <= x <= 2047 for x in values)
    nonnegative = all(x >= 0 for x in values)
    row_sums = [sum(row) / 512.0 for row in matrix]
    return {
        's3_9_representable': True,
        'nonnegative': nonnegative,
        'unit_cube_row_sum_max': row_sums,
        'all_nonnegative_outputs_within_0_1': (all(0.0 <= x <= 1.0 for x in row_sums) if nonnegative else None),
    }

def main():
    out = {
        'canonical_identity': object_record(IDENTITY),
        'column_knockout': {},
        'row_synthesis': {},
        'signed_candidates': {},
    }
    for c in range(3):
        m = knockout(c)
        out['column_knockout'][f'K{c}'] = {**object_record(m), 'safety': safety(m)}
    for r in range(3):
        for c in range(3):
            m = synth(r, c)
            out['row_synthesis'][f'S{r}{c}'] = {**object_record(m), 'safety': safety(m)}
    # Campaign-1 clear positive slots. The physical basis measurements decide
    # which one is suitable for the single final cancellation witness.
    for r, c in ((0, 2), (1, 0), (1, 2)):
        m = subtract_slot(r, c)
        out['signed_candidates'][f'I_MINUS_E{r}{c}'] = {**object_record(m), 'safety': safety(m)}
    print(json.dumps(out, indent=2, sort_keys=True))

if __name__ == '__main__':
    main()
