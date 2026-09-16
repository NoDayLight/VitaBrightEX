#!/usr/bin/env python3
import argparse
import hashlib
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path
from generate_expected import build as build_expected

C1_SHA = '740c6f7a345a9544dc0b9c79b38040baa663e6f8a8dbb27483d304b9901278bd'
CANON_SHA = '5dc12dfcae42a648dc093db831b661cc3068f2d70f199b03fb582ce301b188b5'
RGB = {'R','G','B'}
C2_IDS = ['D0-C2','B02-C2','D2-C2','B01-C2','B20-C2','B21-C2','N02-C2']
C1_FROZEN = {
    'E00': {'output':'R','input':'R','direction':'DECREASE','isolated':'YES'},
    'E01': {'output':'G','input':'NONE','direction':'DECREASE','isolated':'AMBIG'},
    'E02': {'output':'R','input':'B','direction':'INCREASE','isolated':'YES'},
    'E10': {'output':'G','input':'R','direction':'INCREASE','isolated':'YES'},
    'E11': {'output':'G','input':'G','direction':'DECREASE','isolated':'YES'},
    'E12': {'output':'G','input':'B','direction':'INCREASE','isolated':'YES'},
    'E20': {'output':'G','input':'NONE','direction':'DECREASE','isolated':'AMBIG'},
    'E21': {'output':'G','input':'NONE','direction':'DECREASE','isolated':'AMBIG'},
    'E22': {'output':'B','input':'NONE','direction':'DECREASE','isolated':'AMBIG'},
}

PRIMARY_KEYS = [('R','r_changed','r_appearance'),('G','g_changed','g_appearance'),('B','b_changed','b_appearance')]
CROSS_ADDED = {
    ('R','YELLOW'): 'G', ('R','MAGENTA'): 'B',
    ('G','YELLOW'): 'R', ('G','CYAN'): 'B',
    ('B','MAGENTA'): 'R', ('B','CYAN'): 'G',
}
SECONDARY = {frozenset(('R','G')):'YELLOW50', frozenset(('R','B')):'MAGENTA50', frozenset(('G','B')):'CYAN50'}
PRIMARY_APPEARANCE = {'R':'RED','G':'GREEN','B':'BLUE'}

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

def c1_decoder_path():
    here=Path(__file__).resolve().parent
    packaged=here/'campaign1'/'decode.py'
    if packaged.exists(): return packaged
    sibling=here.parent/'gate1f_ctm_semantics'/'decode.py'
    if sibling.exists(): return sibling
    raise FileNotFoundError('exact Campaign-1 decoder not found')

def validate_campaign1(path):
    path=Path(path)
    got=hashlib.sha256(path.read_bytes()).hexdigest()
    if got != C1_SHA: raise ValueError(f'Campaign-1 SHA mismatch: {got}')
    with tempfile.TemporaryDirectory() as td:
        j=Path(td)/'c1.json'
        cp=subprocess.run([sys.executable,str(c1_decoder_path()),str(path),'--json-out',str(j)],capture_output=True,text=True)
        if cp.returncode != 0:
            raise ValueError('Campaign-1 frozen decoder failed:\n'+cp.stdout+cp.stderr)
        if 'GATE1F_EVIDENCE_INVARIANTS=PASS' not in cp.stdout or 'GATE1F_DISCRIMINATOR_COMPLETE=PASS' not in cp.stdout:
            raise ValueError('Campaign-1 decoder did not reproduce frozen integrity/completion')
        data=json.loads(j.read_text())
    if data.get('evidence_sha256') != C1_SHA: raise ValueError('Campaign-1 decoder SHA mismatch')
    table=data['semantic_table']
    for r in range(3):
        for c in range(3):
            key=f'E{r}{c}'
            got={k:table[r][c][k] for k in ('output','input','direction','isolated')}
            if got != C1_FROZEN[key]: raise ValueError(f'Campaign-1 interpretation changed at {key}: {got}')
    if data.get('stage_c_allowed') is not False: raise ValueError('Campaign-1 Stage C disposition changed')
    return data

def status_errors(r,nat0,nat1,expected_forward_sha,enabled):
    errs=[]
    expected={
        'target':1,'hook_owned':1,'hook_fail':0,'pending':0,'reapply':3,'faults':0,'masks':0,
        'p0_valid':1,'p1_valid':1,'p0_nat':nat0,'p1_nat':nat1,'p0_class':1,'p1_class':1,
        'p0_raw':0,'p1_raw':0,'p0_mismatch':0,'p1_mismatch':0,'p0_overflow':0,'p1_overflow':0,
        'p0_policy_fail':0,'p1_policy_fail':0,'enabled':enabled,
    }
    for k,want in expected.items():
        if as_int(r,k)!=want: errs.append(f'{k}={r[k]} expected {want}')
    if obj_sha(words(r['p0_source']))!=CANON_SHA or obj_sha(words(r['p1_source']))!=CANON_SHA:
        errs.append('source object not canonical identity')
    if obj_sha(words(r['p0_forward']))!=expected_forward_sha or obj_sha(words(r['p1_forward']))!=expected_forward_sha:
        errs.append('forward object mismatch')
    return errs

def derive_cross(obs):
    if obs.get('confidence')!='CLEAR': return None
    yes=[]
    for src,ck,ak in PRIMARY_KEYS:
        v=obs.get(ck)
        if v=='YES': yes.append((src,obs.get(ak)))
        elif v!='NO': return None
    if len(yes)!=1: return None
    src,appearance=yes[0]
    out=CROSS_ADDED.get((src,appearance))
    if not out: return None
    return {'output':out,'input':src,'direction':'INCREASE','isolated':'YES','appearance':appearance}

def derive_diagonal(obs):
    if obs.get('confidence')!='CLEAR': return None
    yes=[]
    for src,ck,ak in PRIMARY_KEYS:
        v=obs.get(ck)
        if v=='YES': yes.append((src,obs.get(ak)))
        elif v!='NO': return None
    if len(yes)!=1: return None
    src,appearance=yes[0]
    if appearance not in ('DARK_BLACK','VERY_DARK'): return None
    return {'output':src,'input':src,'direction':'DECREASE','isolated':'YES','appearance':appearance}

def factorize(table):
    row_map=[]; col_map=[]; separable=True
    for r in range(3):
        vals={table[r][c]['output'] for c in range(3) if table[r][c]}
        if len(vals)!=1 or not vals.issubset(RGB): separable=False; row_map.append('?')
        else: row_map.append(next(iter(vals)))
    for c in range(3):
        vals={table[r][c]['input'] for r in range(3) if table[r][c]}
        if len(vals)!=1 or not vals.issubset(RGB): separable=False; col_map.append('?')
        else: col_map.append(next(iter(vals)))
    if any(table[r][c] is None for r in range(3) for c in range(3)): separable=False
    if separable and (set(row_map)!=RGB or set(col_map)!=RGB): separable=False
    return row_map,col_map,separable,(separable and row_map==col_map)

def self_test():
    d={'r_changed':'YES','g_changed':'NO','b_changed':'NO','r_appearance':'DARK_BLACK','confidence':'CLEAR'}
    assert derive_diagonal(d)['output']=='R'
    b={'r_changed':'NO','g_changed':'NO','b_changed':'YES','b_appearance':'MAGENTA','confidence':'CLEAR'}
    assert derive_cross(b)=={'output':'R','input':'B','direction':'INCREASE','isolated':'YES','appearance':'MAGENTA'}
    table=[[None]*3 for _ in range(3)]
    for r,o in enumerate('RGB'):
        for c,i in enumerate('RGB'): table[r][c]={'output':o,'input':i,'direction':'INCREASE','isolated':'YES'}
    row,col,sep,shared=factorize(table)
    assert row==['R','G','B'] and col==['R','G','B'] and sep and shared
    print('GATE1F_CAMPAIGN2_DECODER_SELFTEST=PASS')

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('campaign1',nargs='?')
    ap.add_argument('campaign2',nargs='?')
    ap.add_argument('--json-out')
    ap.add_argument('--self-test',action='store_true')
    a=ap.parse_args()
    if a.self_test:
        self_test(); return
    if not a.campaign1 or not a.campaign2: ap.error('campaign1 and campaign2 evidence files are required')

    validate_campaign1(a.campaign1)
    rows=parse_lines(a.campaign2)
    exp=build_expected(); byid={p['id']:p for p in exp['probes']}
    failures=[]
    meta=one(rows,'GATE1F_C2'); caps=one(rows,'CAPS2'); src=one(rows,'SOURCE_CONFIRM2')
    pre=one(rows,'PREFLIGHT2','-'); nb=one(rows,'NATURAL_BASELINE2'); complete=one(rows,'COMPLETE2')
    if meta.get('expected_runtime')!='a637f54f': failures.append('runtime contract is not a637f54f')
    if meta.get('campaign1_sha')!=C1_SHA: failures.append('Campaign-1 prerequisite SHA mismatch in C2 log')
    if meta.get('observer')!='TEMPORAL_AB': failures.append('observer method is not TEMPORAL_AB')
    for k,want in [('matrix','1'),('immediate','1'),('reapply','3'),('channel_order','0'),('cct','0'),('saturation','0')]:
        if caps.get(k)!=want: failures.append(f'capability {k}={caps.get(k)} expected {want}')
    if src.get('mask')!='7': failures.append('Campaign-2 source confirmation missing')
    nat0=int(nb['p0']); nat1=int(nb['p1'])
    failures += ['PREFLIGHT2: '+x for x in status_errors(pre,nat0,nat1,CANON_SHA,0)]
    if complete.get('controls')!='PASS' or complete.get('targets')!='4' or complete.get('signed_probe')!='N02-C2':
        failures.append('Campaign-2 completion record invalid')
    if int(complete['natural_p0'])!=nat0 or int(complete['natural_p1'])!=nat1: failures.append('completion natural generation mismatch')

    observations={}
    for pid in C2_IDS:
        spec=byid[pid]
        m=one(rows,'MATRIX2',pid)
        got=[int(x) for x in m['m'].split(',')]
        if got!=spec['matrix_s3_9']: failures.append(f'{pid}: matrix mismatch')
        toggles=[r for r in rows if r.get('id')==pid and r['_tag'] in ('TOGGLE_PROBE','TOGGLE_NEUTRAL')]
        obs=one(rows,'SIGNED_OBS2' if pid=='N02-C2' else 'OBS2',pid)
        count=int(obs['toggle_count'])
        if count < 3 or count > 25 or count != len(toggles): failures.append(f'{pid}: invalid toggle count {count}/{len(toggles)}')
        for i,t in enumerate(toggles):
            want_tag='TOGGLE_PROBE' if i%2==0 else 'TOGGLE_NEUTRAL'
            if t['_tag']!=want_tag: failures.append(f'{pid}: toggle sequence broke at {i}')
            ef=spec['identity_forward_sha256'] if want_tag=='TOGGLE_PROBE' else CANON_SHA
            en=1 if want_tag=='TOGGLE_PROBE' else 0
            failures += [f'{pid} {want_tag}: {x}' for x in status_errors(t,nat0,nat1,ef,en)]
        if toggles and toggles[-1]['_tag']!='TOGGLE_PROBE': failures.append(f'{pid}: observer did not finish in PROBE state')
        post=one(rows,'POSTOBS2',pid); reset=one(rows,'RESET2',pid)
        failures += [f'{pid} POSTOBS2: {x}' for x in status_errors(post,nat0,nat1,spec['identity_forward_sha256'],1)]
        failures += [f'{pid} RESET2: {x}' for x in status_errors(reset,nat0,nat1,CANON_SHA,0)]
        observations[pid]=obs

    for pid in ('D0-C2','B02-C2'):
        ctrl=one(rows,'CONTROL2',pid)
        if ctrl.get('result')!='PASS': failures.append(f'{pid}: observer validation control failed')

    if failures:
        print('GATE1F_CAMPAIGN2_EVIDENCE_INVARIANTS=FAIL')
        for f in failures: print('ERROR='+f)
        raise SystemExit(2)
    print('GATE1F_CAMPAIGN1_FROZEN=PASS')
    print('GATE1F_CAMPAIGN2_EVIDENCE_INVARIANTS=PASS')

    d0=derive_diagonal(observations['D0-C2'])
    b02=derive_cross(observations['B02-C2'])
    controls_ok=(d0 is not None and d0['output']=='R' and d0['input']=='R' and
                 b02 is not None and b02['output']=='R' and b02['input']=='B')
    print('CAMPAIGN2_OBSERVER_VALIDATION='+('PASS' if controls_ok else 'FAIL'))

    derived={
        'E22': derive_diagonal(observations['D2-C2']),
        'E01': derive_cross(observations['B01-C2']),
        'E20': derive_cross(observations['B20-C2']),
        'E21': derive_cross(observations['B21-C2']),
    }
    for key in ('E22','E01','E20','E21'):
        v=derived[key]
        if v: print(f'CAMPAIGN2_{key}={v["output"]}<-{v["input"]} direction={v["direction"]} isolated={v["isolated"]}')
        else: print(f'CAMPAIGN2_{key}=UNRESOLVED')

    table=[[None]*3 for _ in range(3)]
    for key in ('E00','E02','E10','E11','E12'):
        r=int(key[1]); c=int(key[2]); table[r][c]=dict(C1_FROZEN[key])
    for key,v in derived.items():
        if v:
            r=int(key[1]); c=int(key[2]); table[r][c]={k:v[k] for k in ('output','input','direction','isolated')}
    row_map,col_map,separable,shared=factorize(table)

    positive_ok=all(table[r][c] is not None and table[r][c]['direction']=='INCREASE'
                    for r,c in ((0,1),(0,2),(1,0),(1,2),(2,0),(2,1)))
    setup=one(rows,'SIGNED_SETUP2')
    sobs=observations['N02-C2']
    signed_ok=False
    if b02:
        witness=SECONDARY.get(frozenset((b02['output'],b02['input'])))
        expected_app=PRIMARY_APPEARANCE[b02['input']]
        signed_ok=(setup.get('positive')=='B02-C2' and setup.get('input')==b02['input'] and setup.get('output')==b02['output'] and
                   setup.get('witness')==witness and int(setup.get('coefficient','0'))==-512 and
                   sobs.get('witness')==witness and sobs.get('changed')=='YES' and sobs.get('probe_appearance')==expected_app and sobs.get('confidence')=='CLEAR')
    stage_c=controls_ok and all(derived.values()) and separable and positive_ok and signed_ok

    print('ROW_OUTPUT=['+','.join(row_map)+']')
    print('COL_INPUT=['+','.join(col_map)+']')
    print('SEPARABLE_RGB_BASIS='+('YES' if separable else 'NO'))
    print('SHARED_RGB_BASIS='+('YES' if shared else 'NO'))
    print('POSITIVE_DIRECTION_CONSISTENT='+('YES' if positive_ok else 'NO'))
    print('SIGNED_CTM='+('YES' if signed_ok else 'NO'))
    print('GATE1F_STAGE_C_ALLOWED='+('YES' if stage_c else 'NO'))

    result={
        'campaign1_evidence_sha256':C1_SHA,
        'campaign1_backend_integrity':True,
        'campaign2_evidence_sha256':hashlib.sha256(Path(a.campaign2).read_bytes()).hexdigest(),
        'campaign2_natural_generation':[nat0,nat1],
        'campaign2_controls_pass':controls_ok,
        'campaign2_derived':derived,
        'combined_semantic_table':table,
        'row_output':row_map,
        'col_input':col_map,
        'separable_rgb_basis':separable,
        'shared_rgb_basis':shared,
        'positive_direction_consistent':positive_ok,
        'signed_ctm_verified':signed_ok,
        'stage_c_allowed':stage_c,
        'limitations':{'ctm_transfer_domain':'UNKNOWN','cct_supported':False,'saturation_supported':False,'additive_affine_supported':False,'gamma_transfer_supported':False},
    }
    if a.json_out: Path(a.json_out).write_text(json.dumps(result,indent=2,sort_keys=True)+'\n')

if __name__=='__main__': main()
