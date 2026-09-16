#!/usr/bin/env python3
from __future__ import annotations
import argparse, hashlib, struct
from pathlib import Path

MAGIC=0x49453156
VERSION=1
BUNDLE_SIZE=2748
MATRIX_STATUS_SIZE=384
OBSERVER_SIZE=1548
EVENT_SIZE=92
EVENT_CAP=16
ACTION_MILD=1
ACTION_RESET=2
CANONICAL_SHA='5dc12dfcae42a648dc093db831b661cc3068f2d70f199b03fb582ce301b188b5'
MILD_SHA='090035115d31436fa114dfd54781c8184e17ade1e61326a90eac38b8a61faf50'

def s32(x): return x-(1<<32) if x&0x80000000 else x
def u32s(raw,off,n): return list(struct.unpack_from('<'+'I'*n,raw,off))
def sha_words(w): return hashlib.sha256(struct.pack('<15I',*w)).hexdigest()

def parse_plane(raw,off):
    h=u32s(raw,off,10); src=u32s(raw,off+40,15); fwd=u32s(raw,off+100,15)
    return {'valid':h[0],'pristine':h[1],'class':h[2],'applied':h[3],'raw':s32(h[4]),
            'mismatch':h[5],'overflow':h[6],'policy_fail':h[7],
            'source':src,'forward':fwd,'source_sha':sha_words(src),'forward_sha':sha_words(fwd)}

def parse_matrix(raw,off):
    h=u32s(raw,off,16)
    return {'size':h[0],'abi':h[1],'target':h[2],'hook':h[3],'hook_fail':h[4],
            'requested':h[5],'active':h[6],'enabled':h[7],'pending':h[8],'reapply':h[9],
            'last_request':s32(h[10]),'validation_fail':h[11],'status_drop':h[12],
            'planes':[parse_plane(raw,off+64),parse_plane(raw,off+224)]}

def parse_event(raw,off):
    h=u32s(raw,off,8); words=u32s(raw,off+32,15)
    return {'seq':h[0],'node':h[1],'phase':h[2],'plane':s32(h[3]),'tid':h[4],
            'epoch':h[5],'valid':h[6],'raw':s32(h[7]),
            'source_sha':sha_words(words) if h[6] else None}

def parse_observer(raw,off):
    h=u32s(raw,off,19)
    return {'magic':h[0],'version':h[1],'flags':h[2],'owned':h[3],'fail':h[4],
            'export':h[5],'epoch':h[6],'tid':h[7],'runs':h[8],'concurrent':h[9],
            'overlap':h[10],'event_count':h[11],'attempted':h[12],
            'ret0':s32(h[13]),'ret1':s32(h[14]),
            'events':[parse_event(raw,off+76+i*EVENT_SIZE) for i in range(EVENT_CAP)]}

def parse(raw):
    if len(raw)!=BUNDLE_SIZE: raise ValueError(f'expected {BUNDLE_SIZE} bytes, got {len(raw)}')
    h=u32s(raw,0,12)
    if h[0]!=MAGIC or h[1]!=VERSION: raise ValueError('bad Gate-1E immediate bundle header')
    before=parse_matrix(raw,48)
    published=parse_matrix(raw,48+MATRIX_STATUS_SIZE)
    observer=parse_observer(raw,48+MATRIX_STATUS_SIZE*2)
    after=parse_matrix(raw,48+MATRIX_STATUS_SIZE*2+OBSERVER_SIZE)
    return {'action':h[2],'before_ret':s32(h[3]),'action_result':s32(h[4]),
            'published_ret':s32(h[5]),'replay_ret':s32(h[6]),'observer_ret':s32(h[7]),
            'after_ret':s32(h[8]),'before':before,'published':published,'observer':observer,'after':after}

def healthy(label,s,e):
    if s['size']!=MATRIX_STATUS_SIZE or s['abi']!=1:e.append(f'{label}: bad matrix ABI')
    if s['target']!=1 or s['hook']!=1 or s['hook_fail']!=0:e.append(f'{label}: Gate-1D backend unhealthy')

def plane_unchanged(a,b):
    return (a['pristine']==b['pristine'] and a['applied']==b['applied'] and
            a['source_sha']==b['source_sha'] and a['forward_sha']==b['forward_sha'] and
            a['mismatch']==b['mismatch'] and a['overflow']==b['overflow'] and a['policy_fail']==b['policy_fail'])

def check_observer(x,phase,e):
    o=x['observer']
    if o['magic']!=0x45314756 or o['version']!=1:e.append('observer header invalid')
    if (o['flags']&0x7)!=0x7:e.append(f'observer readiness flags=0x{o["flags"]:X}')
    if o['flags']&0x8:e.append('observer event overflow')
    if o['owned']!=3 or o['fail']!=0:e.append(f'observer ownership/fail owned=0x{o["owned"]:X} fail=0x{o["fail"]:X}')
    if o['export']==0:e.append('patched export unresolved')
    expected_runs=1 if phase=='mild' else 2
    if o['runs']!=expected_runs:e.append(f'observer run count={o["runs"]}, expected {expected_runs}')
    if o['attempted']!=1:e.append('replay_attempted != 1')
    if o['ret0']!=0 or o['ret1']!=0:e.append(f'plane replay returns {o["ret0"]},{o["ret1"]}')
    if o['concurrent']!=0:e.append(f'concurrent events={o["concurrent"]}')
    if o['overlap']!=0:e.append(f'recursion/overlap={o["overlap"]}')
    if o['event_count']!=8:e.append(f'event_count={o["event_count"]}, expected 8')
    ev=o['events'][:min(o['event_count'],EVENT_CAP)]
    if [q['seq'] for q in ev]!=list(range(1,len(ev)+1)):e.append('event sequence not contiguous')
    if any(q['tid']!=o['tid'] for q in ev):e.append('event escaped control thread')
    if any(q['epoch']!=o['epoch'] for q in ev):e.append('event epoch mismatch')
    allowed={CANONICAL_SHA,MILD_SHA} if phase=='mild' else {CANONICAL_SHA}
    for plane in (0,1):
        pe=[q for q in ev if q['plane']==plane]
        if len(pe)!=4:e.append(f'p{plane}: expected 4 observer events, got {len(pe)}');continue
        stack=[]; counts={(1,1):0,(1,2):0,(2,1):0,(2,2):0}
        for q in pe:
            counts[(q['node'],q['phase'])]=counts.get((q['node'],q['phase']),0)+1
            if q['phase']==1:
                stack.append(q['node'])
                if not q['valid'] or q['source_sha'] not in allowed:e.append(f'p{plane}/node{q["node"]}: unexpected ENTER source {q["source_sha"]}')
            elif q['phase']==2:
                if not stack or stack.pop()!=q['node']:e.append(f'p{plane}: hook nesting violation')
                if q['raw']!=0:e.append(f'p{plane}/node{q["node"]}: return {q["raw"]}')
            else:e.append(f'p{plane}: unknown phase {q["phase"]}')
        if stack:e.append(f'p{plane}: unterminated hook stack')
        for node in (1,2):
            if counts.get((node,1),0)!=1 or counts.get((node,2),0)!=1:e.append(f'p{plane}/node{node}: not exactly once')
    if len(ev)==8 and [q['plane'] for q in ev[:4]]!=[0]*4:e.append('p0 events are not the first serial replay')
    if len(ev)==8 and [q['plane'] for q in ev[4:]]!=[1]*4:e.append('p1 events are not the second serial replay')

def authoritative(x,phase):
    e=[]; b=x['before']; p=x['published']; a=x['after']
    expected_action=ACTION_MILD if phase=='mild' else ACTION_RESET
    if x['action']!=expected_action:e.append(f'action kind={x["action"]}, expected {expected_action}')
    for k in ('before_ret','published_ret','observer_ret','after_ret'):
        if x[k]!=0:e.append(f'{k}={x[k]}')
    if x['action_result']!=1:e.append(f'action_result={x["action_result"]}, expected ACCEPTED_PENDING_REPLAY(1)')
    if x['replay_ret']!=0:e.append(f'replay_ret={x["replay_ret"]}')
    for label,s in [('before',b),('published',p),('after',a)]: healthy(label,s,e)
    if b['pending']!=0:e.append(f'before pending=0x{b["pending"]:X}')
    if p['requested']!=b['requested']+1 or p['active']!=b['active']+1:e.append('published policy generation did not advance exactly once')
    if p['requested']!=p['active']:e.append('published requested != active')
    if p['pending']!=3:e.append(f'published pending=0x{p["pending"]:X}, expected 0x3')
    if a['requested']!=p['requested'] or a['active']!=p['active']:e.append('replay changed policy generation')
    if a['pending']!=0:e.append(f'after pending=0x{a["pending"]:X}')
    expected_before_enabled=0 if phase=='mild' else 1
    expected_after_enabled=1 if phase=='mild' else 0
    if b['enabled']!=expected_before_enabled:e.append(f'before enabled={b["enabled"]}, expected {expected_before_enabled}')
    if p['enabled']!=expected_after_enabled or a['enabled']!=expected_after_enabled:e.append('published/after enabled state incorrect')
    before_forward=CANONICAL_SHA if phase=='mild' else MILD_SHA
    after_forward=MILD_SHA if phase=='mild' else CANONICAL_SHA
    for plane in (0,1):
        bp=b['planes'][plane]; pp=p['planes'][plane]; ap=a['planes'][plane]
        if not bp['valid'] or not pp['valid'] or not ap['valid']:e.append(f'p{plane}: invalid plane status')
        if bp['source_sha']!=CANONICAL_SHA or bp['forward_sha']!=before_forward:e.append(f'p{plane}: unexpected pre-action source/forward')
        if not plane_unchanged(bp,pp):e.append(f'p{plane}: publication changed applied hardware status before replay')
        if ap['pristine']!=bp['pristine']+1:e.append(f'p{plane}: pristine generation {bp["pristine"]}->{ap["pristine"]}, expected +1')
        if ap['applied']!=a['active']:e.append(f'p{plane}: applied generation {ap["applied"]} != active {a["active"]}')
        if ap['source_sha']!=CANONICAL_SHA or ap['forward_sha']!=after_forward:e.append(f'p{plane}: unexpected post-replay source/forward')
        if ap['raw']!=0:e.append(f'p{plane}: Sony raw return {ap["raw"]}')
        for key in ('mismatch','overflow','policy_fail'):
            if ap[key]!=bp[key]:e.append(f'p{plane}: {key} changed {bp[key]}->{ap[key]}')
    if a['planes'][0]['source_sha']!=a['planes'][1]['source_sha'] or a['planes'][0]['forward_sha']!=a['planes'][1]['forward_sha']:
        e.append('p0/p1 post-replay objects differ')
    check_observer(x,phase,e)
    return e

def main():
    ap=argparse.ArgumentParser();ap.add_argument('file',type=Path);ap.add_argument('--phase',choices=('mild','reset'),required=True);ap.add_argument('--authoritative',action='store_true');args=ap.parse_args()
    raw=args.file.read_bytes();x=parse(raw);o=x['observer']
    print('FILE='+str(args.file));print('FILE_SHA256='+hashlib.sha256(raw).hexdigest());print('SIZE='+str(len(raw)))
    print(f'ACTION={"MILD" if x["action"]==1 else "RESET" if x["action"]==2 else x["action"]} result={x["action_result"]} replay={x["replay_ret"]}')
    print(f'OBSERVER=flags:0x{o["flags"]:X} owned:0x{o["owned"]:X} fail:0x{o["fail"]:X} export:0x{o["export"]:08X} epoch:{o["epoch"]} runs:{o["runs"]} concurrent:{o["concurrent"]} overlap:{o["overlap"]} events:{o["event_count"]} ret:{o["ret0"]},{o["ret1"]}')
    for q in o['events'][:min(o['event_count'],EVENT_CAP)]:
        print(f'EVENT seq={q["seq"]} node={q["node"]} phase={"ENTER" if q["phase"]==1 else "EXIT" if q["phase"]==2 else q["phase"]} plane={q["plane"]} source_sha256={q["source_sha"]} raw_return={q["raw"]}')
    for label,s in [('BEFORE',x['before']),('PUBLISHED',x['published']),('AFTER',x['after'])]:
        print(f'{label}_POLICY=requested:{s["requested"]} active:{s["active"]} enabled:{s["enabled"]} pending:0x{s["pending"]:X}')
        for i,p in enumerate(s['planes']):
            print(f'{label}_P{i}=pristine:{p["pristine"]} applied:{p["applied"]} raw:{p["raw"]} mismatch:{p["mismatch"]} overflow:{p["overflow"]} policy_fail:{p["policy_fail"]} source_sha256={p["source_sha"]} forward_sha256={p["forward_sha"]}')
    errs=authoritative(x,args.phase)
    if errs:
        print('GATE1E_IMMEDIATE_'+args.phase.upper()+'=FAIL')
        for e in errs:print('ERROR='+e)
        if args.authoritative:raise SystemExit(2)
    else:print('GATE1E_IMMEDIATE_'+args.phase.upper()+'=PASS')
if __name__=='__main__':main()
