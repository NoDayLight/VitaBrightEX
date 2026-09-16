#!/usr/bin/env python3
from __future__ import annotations
import argparse, hashlib, struct
from pathlib import Path

MAGIC=0x45314756
VERSION=1
BUNDLE_SIZE=2340
STATUS_SIZE=1548
MATRIX_STATUS_SIZE=384
EVENT_SIZE=92
EVENT_CAP=16
CANONICAL=[0,0,1023,0,1023,0,512,0,0,0,512,0,0,0,512]
CANONICAL_SHA='5dc12dfcae42a648dc093db831b661cc3068f2d70f199b03fb582ce301b188b5'

def sha_words(w): return hashlib.sha256(struct.pack('<15I',*w)).hexdigest()
def s32(x): return x-(1<<32) if x&0x80000000 else x

def u32s(raw,off,n): return list(struct.unpack_from('<'+'I'*n,raw,off))

def parse_plane(raw,off):
    h=u32s(raw,off,10)
    src=u32s(raw,off+40,15); fwd=u32s(raw,off+100,15)
    return {'valid':h[0],'pristine':h[1],'class':h[2],'applied':h[3],'raw_return':s32(h[4]),
            'mismatch':h[5],'overflow':h[6],'policy_fail':h[7],'source_fnv':h[8],'forward_fnv':h[9],
            'source':src,'forward':fwd,'source_sha':sha_words(src),'forward_sha':sha_words(fwd)}

def parse_matrix_status(raw,off):
    h=u32s(raw,off,16)
    return {'size':h[0],'abi':h[1],'target':h[2],'hook':h[3],'hook_fail':h[4],
            'requested':h[5],'active':h[6],'enabled':h[7],'pending':h[8],'reapply':h[9],
            'last_request':s32(h[10]),'validation_fail':h[11],'status_drop':h[12],
            'planes':[parse_plane(raw,off+64),parse_plane(raw,off+224)]}

def parse_event(raw,off):
    h=u32s(raw,off,8); words=u32s(raw,off+32,15)
    return {'seq':h[0],'node':h[1],'phase':h[2],'plane':s32(h[3]),'tid':h[4],
            'epoch':h[5],'source_valid':h[6],'raw_return':s32(h[7]),
            'source':words,'source_sha':sha_words(words) if h[6] else None}

def parse_observer(raw,off):
    h=u32s(raw,off,19)
    events=[parse_event(raw,off+76+i*EVENT_SIZE) for i in range(EVENT_CAP)]
    return {'magic':h[0],'version':h[1],'flags':h[2],'owned':h[3],'fail':h[4],
            'export':h[5],'epoch':h[6],'tid':h[7],'runs':h[8],'concurrent':h[9],
            'overlap':h[10],'event_count':h[11],'attempted':h[12],
            'ret0':s32(h[13]),'ret1':s32(h[14]),'events':events}

def parse(raw):
    if len(raw)!=BUNDLE_SIZE: raise ValueError(f'expected {BUNDLE_SIZE} bytes, got {len(raw)}')
    h=u32s(raw,0,6)
    if h[0]!=MAGIC or h[1]!=VERSION: raise ValueError('bad Gate-1E bundle header')
    before=parse_matrix_status(raw,24)
    obs=parse_observer(raw,24+MATRIX_STATUS_SIZE)
    after=parse_matrix_status(raw,24+MATRIX_STATUS_SIZE+STATUS_SIZE)
    return {'before_ret':s32(h[2]),'replay_ret':s32(h[3]),'observer_ret':s32(h[4]),'after_ret':s32(h[5]),
            'before':before,'observer':obs,'after':after}

def authoritative(x):
    e=[]; b=x['before']; a=x['after']; o=x['observer']
    for k in ('before_ret','observer_ret','after_ret'):
        if x[k]!=0:e.append(f'{k}={x[k]}')
    if x['replay_ret']!=0:e.append(f'replay_ret={x["replay_ret"]}')
    for label,s in [('before',b),('after',a)]:
        if s['size']!=MATRIX_STATUS_SIZE or s['abi']!=1:e.append(f'{label}: bad Gate-1D ABI')
        if s['target']!=1 or s['hook']!=1 or s['hook_fail']!=0:e.append(f'{label}: Gate-1D backend not healthy')
        if s['enabled']!=0 or s['pending']!=0:e.append(f'{label}: policy not neutral/stable')
    if o['magic']!=MAGIC or o['version']!=VERSION:e.append('observer header invalid')
    if (o['flags']&0x7)!=0x7:e.append(f'observer readiness flags=0x{o["flags"]:X}')
    if o['flags']&0x8:e.append('observer event overflow')
    if o['owned']!=3 or o['fail']!=0:e.append(f'observer ownership/fail owned=0x{o["owned"]:X} fail=0x{o["fail"]:X}')
    if o['export']==0:e.append('patched export entry unresolved')
    if o['runs']!=1 or o['attempted']!=1:e.append('identity replay not executed exactly once')
    if o['ret0']!=0 or o['ret1']!=0:e.append(f'plane returns {o["ret0"]},{o["ret1"]}')
    if o['concurrent']!=0:e.append(f'concurrent control events={o["concurrent"]}')
    if o['overlap']!=0:e.append(f'recursion/overlap={o["overlap"]}')
    if o['event_count']!=8:e.append(f'event_count={o["event_count"]}, expected 8')
    ev=o['events'][:min(o['event_count'],EVENT_CAP)]
    if [q['seq'] for q in ev]!=list(range(1,len(ev)+1)):e.append('event sequence not contiguous')
    if any(q['tid']!=o['tid'] for q in ev):e.append('event escaped control thread')
    if any(q['epoch']!=o['epoch'] for q in ev):e.append('event epoch mismatch')
    for plane in (0,1):
        pe=[q for q in ev if q['plane']==plane]
        if len(pe)!=4:e.append(f'p{plane}: expected four observer events, got {len(pe)}');continue
        stack=[]; counts={(1,1):0,(1,2):0,(2,1):0,(2,2):0}
        for q in pe:
            counts[(q['node'],q['phase'])]=counts.get((q['node'],q['phase']),0)+1
            if q['phase']==1:
                stack.append(q['node'])
                if not q['source_valid'] or q['source_sha']!=CANONICAL_SHA:e.append(f'p{plane}/node{q["node"]}: noncanonical identity source')
            elif q['phase']==2:
                if not stack or stack.pop()!=q['node']:e.append(f'p{plane}: hook nesting violation')
                if q['raw_return']!=0:e.append(f'p{plane}/node{q["node"]}: raw return {q["raw_return"]}')
            else:e.append(f'p{plane}: unknown phase {q["phase"]}')
        if stack:e.append(f'p{plane}: unterminated hook stack')
        for node in (1,2):
            if counts.get((node,1),0)!=1 or counts.get((node,2),0)!=1:e.append(f'p{plane}/node{node}: not exactly once')
    if len(ev)==8 and [q['plane'] for q in ev[:4]]!=[0]*4:e.append('p0 events not first serial call')
    if len(ev)==8 and [q['plane'] for q in ev[4:]]!=[1]*4:e.append('p1 events not second serial call')
    for plane in (0,1):
        bp=b['planes'][plane]; ap=a['planes'][plane]
        if not bp['valid'] or not ap['valid']:e.append(f'p{plane}: Gate-1D status invalid')
        if bp['source_sha']!=CANONICAL_SHA or bp['forward_sha']!=CANONICAL_SHA:e.append(f'p{plane}: pre source/forward not canonical')
        if ap['source_sha']!=CANONICAL_SHA or ap['forward_sha']!=CANONICAL_SHA:e.append(f'p{plane}: post source/forward not canonical')
        if ap['pristine']!=bp['pristine']+1:e.append(f'p{plane}: Gate-1D pristine generation delta {bp["pristine"]}->{ap["pristine"]}, expected +1')
        if ap['applied']!=b['active']:e.append(f'p{plane}: post applied generation {ap["applied"]} != neutral active {b["active"]}')
        if ap['raw_return']!=0:e.append(f'p{plane}: Gate-1D raw return {ap["raw_return"]}')
        for k in ('mismatch','overflow','policy_fail'):
            if ap[k]!=bp[k]:e.append(f'p{plane}: {k} changed {bp[k]}->{ap[k]}')
    return e

def main():
    ap=argparse.ArgumentParser();ap.add_argument('file',type=Path);ap.add_argument('--authoritative',action='store_true');a=ap.parse_args()
    raw=a.file.read_bytes();x=parse(raw);o=x['observer'];b=x['before'];z=x['after']
    print('FILE='+str(a.file));print('FILE_SHA256='+hashlib.sha256(raw).hexdigest());print('SIZE='+str(len(raw)))
    print(f'RETURNS=before:{x["before_ret"]} replay:{x["replay_ret"]} observer:{x["observer_ret"]} after:{x["after_ret"]}')
    print(f'OBSERVER=flags:0x{o["flags"]:X} owned:0x{o["owned"]:X} fail:0x{o["fail"]:X} export:0x{o["export"]:08X} epoch:{o["epoch"]} tid:{o["tid"]} runs:{o["runs"]} concurrent:{o["concurrent"]} overlap:{o["overlap"]} events:{o["event_count"]} ret:{o["ret0"]},{o["ret1"]}')
    for q in o['events'][:min(o['event_count'],EVENT_CAP)]:
        print(f'EVENT seq={q["seq"]} node={q["node"]} phase={"ENTER" if q["phase"]==1 else "EXIT" if q["phase"]==2 else q["phase"]} plane={q["plane"]} tid={q["tid"]} source_sha256={q["source_sha"]} raw_return={q["raw_return"]}')
    for label,s in [('BEFORE',b),('AFTER',z)]:
        print(f'{label}_POLICY=requested:{s["requested"]} active:{s["active"]} enabled:{s["enabled"]} pending:0x{s["pending"]:X}')
        for i,p in enumerate(s['planes']):
            print(f'{label}_P{i}=pristine:{p["pristine"]} applied:{p["applied"]} raw:{p["raw_return"]} mismatch:{p["mismatch"]} overflow:{p["overflow"]} policy_fail:{p["policy_fail"]} source_sha256={p["source_sha"]} forward_sha256={p["forward_sha"]}')
    errs=authoritative(x)
    if errs:
        print('GATE1E_CHAIN_IDENTITY=FAIL')
        for e in errs:print('ERROR='+e)
        if a.authoritative:raise SystemExit(2)
    else:print('GATE1E_CHAIN_IDENTITY=PASS')
if __name__=='__main__':main()
