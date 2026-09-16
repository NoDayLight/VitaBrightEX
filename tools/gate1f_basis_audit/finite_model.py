#!/usr/bin/env python3
from __future__ import annotations
import argparse, hashlib, itertools, json, subprocess, sys, tempfile
from pathlib import Path

RGB = ('R', 'G', 'B')
C1_SHA = '740c6f7a345a9544dc0b9c79b38040baa663e6f8a8dbb27483d304b9901278bd'
R3_SHA = 'd7b31ffe209855ca3233f709e5d4c1815dac9fb7479abb7e32b429aa19803f80'
C1_CLEAR_IDS = ('E00', 'E02', 'E10', 'E11', 'E12')
PATCH_SUPPORT = {
    'R50': frozenset('R'), 'G50': frozenset('G'), 'B50': frozenset('B'),
    'RED75': frozenset('R'), 'GREEN75': frozenset('G'), 'BLUE75': frozenset('B'),
    'YELLOW50': frozenset(('R', 'G')), 'MAGENTA50': frozenset(('R', 'B')),
    'CYAN50': frozenset(('G', 'B')), 'GRAY50': frozenset(('R', 'G', 'B')),
    'BLACK': frozenset(),
}
FROZEN_C1 = {
    'E00': dict(output='R', input='R', direction='DECREASE', isolated='YES'),
    'E01': dict(output='G', input='NONE', direction='DECREASE', isolated='AMBIG'),
    'E02': dict(output='R', input='B', direction='INCREASE', isolated='YES'),
    'E10': dict(output='G', input='R', direction='INCREASE', isolated='YES'),
    'E11': dict(output='G', input='G', direction='DECREASE', isolated='YES'),
    'E12': dict(output='G', input='B', direction='INCREASE', isolated='YES'),
    'E20': dict(output='G', input='NONE', direction='DECREASE', isolated='AMBIG'),
    'E21': dict(output='G', input='NONE', direction='DECREASE', isolated='AMBIG'),
    'E22': dict(output='B', input='NONE', direction='DECREASE', isolated='AMBIG'),
}

def parse_pipe(path: Path):
    rows = []
    for no, line in enumerate(path.read_text().splitlines(), 1):
        if not line.strip():
            continue
        parts = line.split('|')
        row = {'_tag': parts[0], '_line': no}
        for field in parts[1:]:
            if '=' not in field:
                raise ValueError(f'{path}:{no}: malformed field {field!r}')
            k, v = field.split('=', 1)
            row[k] = v
        rows.append(row)
    return rows

def one(rows, tag, pid=None, required=True):
    xs = [r for r in rows if r['_tag'] == tag and (pid is None or r.get('id') == pid)]
    if not xs and not required:
        return None
    if len(xs) != 1:
        raise ValueError(f'expected one {tag}/{pid}, got {len(xs)}')
    return xs[0]

def c1_from_raw(path: Path, decoder: Path | None):
    got = hashlib.sha256(path.read_bytes()).hexdigest()
    if got != C1_SHA:
        raise ValueError(f'Campaign-1 SHA mismatch: {got}')
    if decoder is None:
        raise ValueError('--campaign1-decoder is required with --campaign1-raw')
    with tempfile.TemporaryDirectory() as td:
        out = Path(td) / 'c1.json'
        cp = subprocess.run([sys.executable, str(decoder), str(path), '--json-out', str(out)], capture_output=True, text=True)
        if cp.returncode:
            raise ValueError('Campaign-1 frozen decoder failed:\n' + cp.stdout + cp.stderr)
        data = json.loads(out.read_text())
    table = {}
    for r in range(3):
        for c in range(3):
            e = data['semantic_table'][r][c]
            table[f'E{r}{c}'] = {k: e[k] for k in ('output', 'input', 'direction', 'isolated')}
    if table != FROZEN_C1:
        raise ValueError('Campaign-1 decoded table drifted from frozen authority')
    return table, 'RAW_SHA_VERIFIED'

def c1_from_frozen(path: Path | None):
    if path:
        data = json.loads(path.read_text())
        if data.get('raw_evidence_sha256') != C1_SHA:
            raise ValueError('frozen Campaign-1 SHA metadata mismatch')
        table = {k: {x: v[x] for x in ('output', 'input', 'direction', 'isolated')}
                 for k, v in data['semantic_table'].items()}
        if table != FROZEN_C1:
            raise ValueError('frozen Campaign-1 table mismatch')
    else:
        table = FROZEN_C1
    return table, 'FROZEN_METADATA'

def relation(h, r, c):
    pin, pout, orientation = h['pin'], h['pout'], h['orientation']
    if orientation == 'normal':
        return pout[r], pin[c]
    return pout[c], pin[r]

def hypotheses():
    out = []
    i = 0
    for pin in itertools.permutations(RGB):
        for pout in itertools.permutations(RGB):
            for orientation in ('normal', 'transposed'):
                out.append({'id': f'H{i:02d}', 'pin': pin, 'pout': pout, 'orientation': orientation})
                i += 1
    return out

def c1_contradictions(h, table):
    bad = []
    for eid in C1_CLEAR_IDS:
        e = table[eid]
        if e['isolated'] != 'YES' or e['output'] not in RGB or e['input'] not in RGB:
            continue
        r, c = int(eid[1]), int(eid[2])
        predicted_output, predicted_input = relation(h, r, c)
        if predicted_output != e['output'] or predicted_input != e['input']:
            bad.append(f'{eid}: predicts {predicted_output}<-{predicted_input}, observed {e["output"]}<-{e["input"]}')
    return bad

def r3_observations(path: Path | None):
    if path is None:
        return {}, 'NOT_SUPPLIED'
    got = hashlib.sha256(path.read_bytes()).hexdigest()
    rows = parse_pipe(path)
    meta = one(rows, 'GATE1F_C2')
    if meta.get('format') != '2' or meta.get('observer') != 'TEMPORAL_AB_EXPLICIT_COMMIT':
        raise ValueError('not Campaign-2 R3 format=2 evidence')
    obs = {}
    for pid in ('D0-C2', 'B02-C2'):
        row = one(rows, 'OBS2', pid, required=False)
        if row:
            obs[pid] = row
    return obs, ('RAW_SHA_VERIFIED' if got == R3_SHA else f'RAW_SHA_OTHER:{got}')

def changed_map(obs):
    return {
        'R50': obs.get('r_changed'), 'G50': obs.get('g_changed'), 'B50': obs.get('b_changed'),
        'YELLOW50': obs.get('yellow_changed'), 'MAGENTA50': obs.get('magenta_changed'),
        'CYAN50': obs.get('cyan_changed'), 'GRAY50': obs.get('gray_changed'),
    }

def r3_contradictions(h, observations):
    bad = []
    for pid, slot in (('D0-C2', (0, 0)), ('B02-C2', (0, 2))):
        obs = observations.get(pid)
        if not obs or obs.get('confidence') != 'CLEAR':
            continue  # AMBIG observations never falsify a hypothesis.
        _, input_component = relation(h, *slot)
        for patch, answer in changed_map(obs).items():
            # Conservative support-topology rule only. A patch lacking this
            # source component is mathematically invariant under a one-slot
            # perturbation. A clear NO where the component is present is NOT a
            # contradiction because visibility, clipping and transfer placement
            # remain unknown.
            if answer == 'YES' and input_component not in PATCH_SUPPORT[patch]:
                bad.append(f'{pid}/{patch}: clear CHANGED but patch support lacks predicted input {input_component}')
    return bad

def main():
    ap = argparse.ArgumentParser(description='Gate-1F finite permutation/orientation replay')
    ap.add_argument('--campaign1-raw', type=Path)
    ap.add_argument('--campaign1-decoder', type=Path)
    ap.add_argument('--campaign1-frozen', type=Path)
    ap.add_argument('--campaign2-r3', type=Path)
    ap.add_argument('--json-out', type=Path)
    args = ap.parse_args()

    if args.campaign1_raw:
        c1, c1_source = c1_from_raw(args.campaign1_raw, args.campaign1_decoder)
    else:
        c1, c1_source = c1_from_frozen(args.campaign1_frozen)
    r3, r3_source = r3_observations(args.campaign2_r3)

    records = []
    for h in hypotheses():
        c1_bad = c1_contradictions(h, c1)
        r3_bad = r3_contradictions(h, r3)
        status = 'SURVIVES' if not c1_bad and not r3_bad else 'ELIMINATED'
        rec = {**h, 'c1_clear_contradictions': c1_bad, 'r3_clear_contradictions': r3_bad, 'status': status}
        records.append(rec)
        print(f'HYPOTHESIS {h["id"]}')
        print('P_in  = [' + ','.join(h['pin']) + ']')
        print('P_out = [' + ','.join(h['pout']) + ']')
        print('orientation = ' + h['orientation'])
        print(f'C1 clear contradictions = {len(c1_bad)}')
        for x in c1_bad:
            print('  C1_CONTRADICTION ' + x)
        print(f'R3 clear contradictions = {len(r3_bad)}')
        for x in r3_bad:
            print('  R3_CONTRADICTION ' + x)
        print('status = ' + status + '\n')

    survivors = [r for r in records if r['status'] == 'SURVIVES']
    print('C1_EVIDENCE_SOURCE=' + c1_source)
    print('R3_EVIDENCE_SOURCE=' + r3_source)
    print('INITIAL_HYPOTHESES=72')
    print('SURVIVING_HYPOTHESES=' + str(len(survivors)))
    for r in survivors:
        print(f'SURVIVOR={r["id"]} P_in=[{",".join(r["pin"])}] P_out=[{",".join(r["pout"])}] orientation={r["orientation"]}')
    if args.json_out:
        args.json_out.write_text(json.dumps({
            'initial_hypotheses': 72,
            'c1_source': c1_source,
            'r3_source': r3_source,
            'hypotheses': records,
            'survivors': [r['id'] for r in survivors],
        }, indent=2) + '\n')

if __name__ == '__main__':
    main()
