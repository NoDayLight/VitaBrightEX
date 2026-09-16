#!/usr/bin/env python3
from __future__ import annotations
import argparse, hashlib, struct
from pathlib import Path

STATUS=384; CAPS=64; ACTION=388; STATUS_BUNDLE=448; ROLLBACK=396
CANON='5dc12dfcae42a648dc093db831b661cc3068f2d70f199b03fb582ce301b188b5'
MILD='090035115d31436fa114dfd54781c8184e17ade1e61326a90eac38b8a61faf50'
APPLIED=0; ROLLED_BACK=6
REAPPLY_CHAIN=3
TX_COMMITTED=10; TX_ROLLED_BACK=12
FAULT_INJECTED=1<<9

def s32(x): return x-(1<<32) if x&0x80000000 else x
def words(raw,off,n): return list(struct.unpack_from('<'+'I'*n,raw,off))
def sha15(v): return hashlib.sha256(struct.pack('<15I',*v)).hexdigest()

def plane(raw,off):
    h=words(raw,off,10); src=words(raw,off+40,15); fwd=words(raw,off+100,15)
    return dict(valid=h[0],natural=h[1],cls=h[2],applied=h[3],raw=s32(h[4]),mismatch=h[5],overflow=h[6],policy_fail=h[7],source_sha=sha15(src),forward_sha=sha15(fwd))

def status(raw,off):
    h=words(raw,off,16)
    return dict(size=h[0],abi=h[1],target=h[2],hook=h[3],hook_fail=h[4],requested=h[5],active=h[6],enabled=h[7],pending=h[8],reapply=h[9],last=s32(h[10]),validation=h[11],status_drop=h[12],tx_state=h[13],tx_fault=h[14],masks=h[15],planes=[plane(raw,off+64),plane(raw,off+224)])

def caps(raw,off=0):
    h=words(raw,off,16)
    return dict(size=h[0],abi=h[1],target_verified=h[2],matrix=h[3],immediate=h[4],reapply=h[5],gamma=h[6],additive=h[7],cct=h[8],saturation=h[9],channel=h[10])

def parse(path:Path):
    raw=path.read_bytes(); n=len(raw)
    if n==ACTION: return dict(kind='action',raw=raw,action=s32(words(raw,0,1)[0]),status=status(raw,4))
    if n==STATUS_BUNDLE: return dict(kind='status',raw=raw,caps=caps(raw),status=status(raw,64))
    if n==ROLLBACK:
        h=words(raw,0,3)
        return dict(kind='rollback',raw=raw,prepare=s32(h[0]),inject=s32(h[1]),action=s32(h[2]),status=status(raw,12))
    raise ValueError(f'unsupported bundle size {n}')

def get_status(x): return x['status']

def healthy(s,e):
    if s['size']!=STATUS or s['abi']!=1:e.append('bad status ABI')
    if s['target']!=1 or s['hook']!=1 or s['hook_fail']!=0:e.append('backend unhealthy')
    if s['reapply']!=REAPPLY_CHAIN:e.append(f'reapply mode={s["reapply"]}, expected chain-head(3)')
    if (s['masks'] & 0xffff)!=0:e.append(f'authority stale mask=0x{s["masks"]&0xffff:X}')
    if (s['masks']>>16)!=0:e.append(f'degraded plane mask=0x{s["masks"]>>16:X}')
    for i,p in enumerate(s['planes']):
        if p['valid']!=1:e.append(f'p{i}: authority invalid')
        if p['cls']!=1:e.append(f'p{i}: expected canonical identity authority class, got {p["cls"]}')
        if p['raw']<0:e.append(f'p{i}: Sony return {p["raw"]}')
        if p['mismatch'] or p['overflow'] or p['policy_fail']:e.append(f'p{i}: counters mismatch={p["mismatch"]} overflow={p["overflow"]} policy={p["policy_fail"]}')
        if p['source_sha']!=CANON:e.append(f'p{i}: authority source {p["source_sha"]}')

def previous_status(path):
    return get_status(parse(Path(path))) if path else None

def compare_natural_same(s,p,e):
    if p is None:return
    for i in (0,1):
        if s['planes'][i]['natural']!=p['planes'][i]['natural']:
            e.append(f'p{i}: internal transaction advanced natural generation {p["planes"][i]["natural"]}->{s["planes"][i]["natural"]}')

def compare_natural_advanced(s,p,e):
    if p is None:return
    for i in (0,1):
        if s['planes'][i]['natural']<=p['planes'][i]['natural']:
            e.append(f'p{i}: natural Sony replay did not advance natural generation')

def common_final(s,enabled,forward,last,tx,e):
    healthy(s,e)
    if s['enabled']!=enabled:e.append(f'enabled={s["enabled"]}, expected {enabled}')
    if s['pending']!=0:e.append(f'pending=0x{s["pending"]:X}')
    if s['last']!=last:e.append(f'last result={s["last"]}, expected {last}')
    if s['tx_state']!=tx:e.append(f'tx_state={s["tx_state"]}, expected {tx}')
    if s['requested']!=s['active']:e.append('requested != active')
    for i,p in enumerate(s['planes']):
        if p['applied']!=s['active']:e.append(f'p{i}: applied {p["applied"]} != active {s["active"]}')
        if p['forward_sha']!=forward:e.append(f'p{i}: forward {p["forward_sha"]}, expected {forward}')

def authoritative(x,phase,prev):
    e=[];s=x['status']
    if phase=='neutral':
        if x['kind']!='status':e.append('neutral requires status bundle')
        else:
            c=x['caps']
            if c['size']!=64 or c['abi']!=1 or c['target_verified']!=1 or c['matrix']!=1 or c['immediate']!=1 or c['reapply']!=3:e.append('capability promotion invalid')
        common_final(s,0,CANON,APPLIED,0,e)
        if s['tx_fault']!=0:e.append(f'tx_fault=0x{s["tx_fault"]:X}')
    elif phase in ('mild','post-rollback-mild'):
        if x['kind']!='action':e.append('mild requires action bundle')
        elif x['action']!=APPLIED:e.append(f'action result={x["action"]}, expected APPLIED')
        common_final(s,1,MILD,APPLIED,TX_COMMITTED,e)
        if s['tx_fault']!=0:e.append(f'tx_fault=0x{s["tx_fault"]:X}')
        compare_natural_same(s,prev,e)
    elif phase in ('reset','final-reset'):
        if x['kind']!='action':e.append('reset requires action bundle')
        elif x['action']!=APPLIED:e.append(f'action result={x["action"]}, expected APPLIED')
        common_final(s,0,CANON,APPLIED,TX_COMMITTED,e)
        if s['tx_fault']!=0:e.append(f'tx_fault=0x{s["tx_fault"]:X}')
        compare_natural_same(s,prev,e)
    elif phase=='natural-after-mild':
        if x['kind']!='status':e.append('natural replay requires status bundle')
        common_final(s,1,MILD,APPLIED,TX_COMMITTED,e)
        compare_natural_advanced(s,prev,e)
    elif phase=='rollback':
        if x['kind']!='rollback':e.append('rollback requires rollback bundle')
        else:
            if x['prepare']!=APPLIED:e.append(f'prepare reset={x["prepare"]}')
            if x['inject']!=APPLIED:e.append(f'inject arm={x["inject"]}')
            if x['action']!=ROLLED_BACK:e.append(f'faulted SetRequest={x["action"]}, expected ROLLED_BACK(6)')
        common_final(s,0,CANON,ROLLED_BACK,TX_ROLLED_BACK,e)
        if (s['tx_fault'] & FAULT_INJECTED)==0:e.append('injected p1-abort fault flag absent')
        if s['tx_fault'] & ~FAULT_INJECTED:e.append(f'unexpected tx fault bits=0x{s["tx_fault"] & ~FAULT_INJECTED:X}')
        compare_natural_same(s,prev,e)
    else: raise ValueError(phase)
    return e

def main():
    ap=argparse.ArgumentParser();ap.add_argument('file',type=Path);ap.add_argument('--phase',required=True,choices=('neutral','mild','reset','natural-after-mild','rollback','post-rollback-mild','final-reset'));ap.add_argument('--previous',type=Path);ap.add_argument('--authoritative',action='store_true');a=ap.parse_args()
    x=parse(a.file);s=x['status'];prev=previous_status(a.previous)
    print('FILE='+str(a.file));print('FILE_SHA256='+hashlib.sha256(x['raw']).hexdigest());print('SIZE='+str(len(x['raw'])));print('KIND='+x['kind'])
    if x['kind']=='action':print('ACTION_RESULT='+str(x['action']))
    if x['kind']=='rollback':print(f'PREPARE_RESET={x["prepare"]} INJECT={x["inject"]} ACTION_RESULT={x["action"]}')
    if x['kind']=='status':print(f'CAPS=matrix:{x["caps"]["matrix"]} immediate:{x["caps"]["immediate"]} reapply:{x["caps"]["reapply"]}')
    print(f'POLICY=requested:{s["requested"]} active:{s["active"]} enabled:{s["enabled"]} pending:0x{s["pending"]:X} reapply:{s["reapply"]} last:{s["last"]}')
    print(f'TXN=state:{s["tx_state"]} faults:0x{s["tx_fault"]:X} stale:0x{s["masks"]&0xffff:X} degraded:0x{s["masks"]>>16:X}')
    for i,p in enumerate(s['planes']):print(f'P{i}=natural:{p["natural"]} class:{p["cls"]} applied:{p["applied"]} raw:{p["raw"]} mismatch:{p["mismatch"]} overflow:{p["overflow"]} policy_fail:{p["policy_fail"]} source_sha256={p["source_sha"]} forward_sha256={p["forward_sha"]}')
    errs=authoritative(x,a.phase,prev)
    tag='GATE1E_TRANSACTION_'+a.phase.upper().replace('-','_')
    if errs:
        print(tag+'=FAIL')
        for z in errs:print('ERROR='+z)
        if a.authoritative:raise SystemExit(2)
    else:print(tag+'=PASS')
if __name__=='__main__':main()
