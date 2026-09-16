#!/usr/bin/env python3
import argparse
import hashlib
import json
import struct
from pathlib import Path
from generate_expected import build as build_expected

CANON_SHA = '5dc12dfcae42a648dc093db831b661cc3068f2d70f199b03fb582ce301b188b5'
RGB = {'R','G','B'}
BASIS_IDS = ['D0','D1','D2','B01','B02','B10','B12','B20','B21']

def parse_lines(path):
    rows=[]
    for no,line in enumerate(Path(path).read_text().splitlines(),1):
        if not line.strip(): continue
        parts=line.split('|')
        row={'_tag':parts[0], '_line':no}
        for p in parts[1:]:
            if '=' not in p: raise ValueError(f'line {no}: malformed field {p!r}')
            k,v=p.split('=',1); row[k]=v
        rows.append(row)
    return rows

def one(rows,tag,pid=None):
    xs=[r for r in rows if r['_tag']==tag and (pid is None or r.get('id')==pid)]
    if len(xs)!=1: raise ValueError(f'expected one {tag}/{pid}, got {len(xs)}')
    return xs[0]

def words(s):
    a=[int(x,16) for x in s.split(',')]
    if len(a)!=15: raise ValueError('expected 15 words')
    return a

def obj_sha(w): return hashlib.sha256(struct.pack('<15I',*w)).hexdigest()
def as_int(r,k): return int(r[k],0)

def status_invariants(r, nat0, nat1, expected_forward=None, reset=False):
    errs=[]
    for k,want in [('pending',0),('reapply',3),('faults',0),('masks',0),('p0_raw',0),('p1_raw',0)]:
        if as_int(r,k)!=want: errs.append(f'{k}={r[k]} expected {want}')
    if as_int(r,'p0_nat')!=nat0 or as_int(r,'p1_nat')!=nat1: errs.append('natural generation changed')
    if as_int(r,'p0_class')!=1 or as_int(r,'p1_class')!=1: errs.append('baseline class is not canonical identity')
    if obj_sha(words(r['p0_source']))!=CANON_SHA or obj_sha(words(r['p1_source']))!=CANON_SHA: errs.append('source object not canonical identity')
    if reset:
        if as_int(r,'enabled')!=0: errs.append('reset left policy enabled')
        if obj_sha(words(r['p0_forward']))!=CANON_SHA or obj_sha(words(r['p1_forward']))!=CANON_SHA: errs.append('reset did not restore canonical forward object')
    else:
        if as_int(r,'enabled')!=1: errs.append('probe policy is not enabled')
        if expected_forward:
            if obj_sha(words(r['p0_forward']))!=expected_forward: errs.append('p0 forward hash mismatch')
            if obj_sha(words(r['p1_forward']))!=expected_forward: errs.append('p1 forward hash mismatch')
    return errs

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('evidence')
    ap.add_argument('--json-out')
    a=ap.parse_args()
    rows=parse_lines(a.evidence)
    exp=build_expected(); byid={p['id']:p for p in exp['probes']}
    failures=[]
    meta=one(rows,'GATE1F')
    caps=one(rows,'CAPS')
    src=one(rows,'SOURCE_CONFIRM')
    pre=one(rows,'PREFLIGHT','-')
    nb=one(rows,'NATURAL_BASELINE')
    complete=one(rows,'COMPLETE')
    if meta.get('expected_runtime')!='a637f54f': failures.append('runtime contract is not a637f54f')
    for k,want in [('matrix','1'),('immediate','1'),('reapply','3'),('channel_order','0'),('cct','0'),('saturation','0')]:
        if caps.get(k)!=want: failures.append(f'capability {k}={caps.get(k)} expected {want}')
    if src.get('mask')!='7': failures.append('framebuffer primary confirmation missing')
    nat0=int(nb['p0']); nat1=int(nb['p1'])
    failures += status_invariants(pre,nat0,nat1,reset=True)
    if complete.get('basis_probes')!='9': failures.append('basis campaign incomplete')
    signed_id=complete['signed_probe']
    if signed_id not in byid or not signed_id.startswith('N'): failures.append(f'invalid signed probe {signed_id}')

    observations={}
    semantic=[[None]*3 for _ in range(3)]
    for pid in BASIS_IDS + [signed_id]:
        spec=byid[pid]
        m=one(rows,'MATRIX',pid)
        got_matrix=[int(x) for x in m['m'].split(',')]
        if got_matrix!=spec['matrix_s3_9']: failures.append(f'{pid}: matrix mismatch')
        apply=one(rows,'APPLY',pid)
        post=one(rows,'POSTOBS',pid)
        reset=one(rows,'RESET',pid)
        if as_int(apply,'result')!=0: failures.append(f'{pid}: SetRequest result {apply["result"]}')
        if as_int(reset,'result')!=0: failures.append(f'{pid}: Reset result {reset["result"]}')
        failures += [f'{pid} APPLY: {x}' for x in status_invariants(apply,nat0,nat1,spec['identity_forward_sha256'])]
        failures += [f'{pid} POSTOBS: {x}' for x in status_invariants(post,nat0,nat1,spec['identity_forward_sha256'])]
        failures += [f'{pid} RESET: {x}' for x in status_invariants(reset,nat0,nat1,reset=True)]
        o=one(rows,'OBS',pid)
        observations[pid]={k:o[k] for k in ('patch','component','direction','isolated')}
        if pid in BASIS_IDS:
            semantic[spec['row']][spec['col']]={'output':o['component'],'input':o['patch'],'direction':o['direction'],'isolated':o['isolated']}

    if failures:
        print('GATE1F_EVIDENCE_INVARIANTS=FAIL')
        for f in failures: print('ERROR='+f)
        raise SystemExit(2)

    print('GATE1F_EVIDENCE_INVARIANTS=PASS')
    table_complete=True
    for r in range(3):
        for c in range(3):
            s=semantic[r][c]
            if s['output'] not in RGB or s['input'] not in RGB or s['isolated']!='YES': table_complete=False
            print(f'SEMANTIC_E{r}{c}={s["output"]}<-{s["input"]} direction={s["direction"]} isolated={s["isolated"]}')

    row_map=[]; col_map=[]; separable=table_complete
    for r in range(3):
        vals={semantic[r][c]['output'] for c in range(3)}
        if len(vals)!=1 or not vals.issubset(RGB): separable=False; row_map.append('?')
        else: row_map.append(next(iter(vals)))
    for c in range(3):
        vals={semantic[r][c]['input'] for r in range(3)}
        if len(vals)!=1 or not vals.issubset(RGB): separable=False; col_map.append('?')
        else: col_map.append(next(iter(vals)))
    if separable and (set(row_map)!=RGB or set(col_map)!=RGB): separable=False
    shared=separable and row_map==col_map

    direction_ok=True
    for pid in ['D0','D1','D2']:
        if observations[pid]['direction']!='DECREASE': direction_ok=False
    for pid in ['B01','B02','B10','B12','B20','B21']:
        if observations[pid]['direction']!='INCREASE': direction_ok=False
    positive_id='B'+signed_id[1:]
    po=observations[positive_id]; no=observations[signed_id]
    signed_ok=(po['patch']==no['patch'] and po['component']==no['component'] and
               po['isolated']=='YES' and no['isolated']=='YES' and
               po['direction']=='INCREASE' and no['direction']=='DECREASE')

    print('ROW_OUTPUT=['+','.join(row_map)+']')
    print('COL_INPUT=['+','.join(col_map)+']')
    print('SEPARABLE_RGB_BASIS='+('YES' if separable else 'NO'))
    print('SHARED_RGB_BASIS='+('YES' if shared else 'NO'))
    print('POSITIVE_DIRECTION_CONSISTENT='+('YES' if direction_ok else 'NO'))
    print('SIGNED_CTM='+('PASS' if signed_ok else 'FAIL'))
    print('GATE1F_DISCRIMINATOR_COMPLETE=PASS')
    print('GATE1F_STAGE_C_ALLOWED='+('YES' if separable and direction_ok and signed_ok else 'NO'))

    result={
        'evidence_sha256':hashlib.sha256(Path(a.evidence).read_bytes()).hexdigest(),
        'natural_generation':[nat0,nat1],
        'semantic_table':semantic,
        'row_output':row_map,
        'col_input':col_map,
        'separable_rgb_basis':separable,
        'shared_rgb_basis':shared,
        'positive_direction_consistent':direction_ok,
        'signed_probe':signed_id,
        'signed_ctm_verified':signed_ok,
        'stage_c_allowed':separable and direction_ok and signed_ok,
        'limitations':{'ctm_transfer_domain':'UNKNOWN','cct_supported':False,'saturation_supported':False,'additive_affine_supported':False,'gamma_transfer_supported':False},
    }
    if a.json_out: Path(a.json_out).write_text(json.dumps(result,indent=2,sort_keys=True)+'\n')

if __name__=='__main__': main()
