#!/usr/bin/env python3
import argparse,hashlib,struct
from pathlib import Path
CANON='5dc12dfcae42a648dc093db831b661cc3068f2d70f199b03fb582ce301b188b5'; MILD='090035115d31436fa114dfd54781c8184e17ade1e61326a90eac38b8a61faf50'
def w(b,o,n): return list(struct.unpack_from('<'+'I'*n,b,o))
def s32(x): return x-(1<<32) if x&0x80000000 else x
def sh(v): return hashlib.sha256(struct.pack('<15I',*v)).hexdigest()
def status(b,o):
    h=w(b,o,16); ps=[]
    for po in (o+64,o+224):
        p=w(b,po,10); ps.append({'natural':p[1],'applied':p[3],'raw':s32(p[4]),'src':sh(w(b,po+40,15)),'fwd':sh(w(b,po+100,15))})
    return {'requested':h[5],'active':h[6],'enabled':h[7],'pending':h[8],'reapply':h[9],'last':s32(h[10]),'tx':h[13],'fault':h[14],'masks':h[15],'p':ps}
def parse(p):
    b=Path(p).read_bytes()
    if len(b)==388: return b,s32(w(b,0,1)[0]),status(b,4)
    if len(b)==460: return b,None,status(b,76)
    raise SystemExit('bad bundle size')
ap=argparse.ArgumentParser(); ap.add_argument('file'); ap.add_argument('--phase',required=True,choices=['mild','reset','natural-after-mild','final-reset']); ap.add_argument('--previous'); a=ap.parse_args()
b,act,s=parse(a.file); prev=parse(a.previous)[2] if a.previous else None; errs=[]
expected=MILD if a.phase in ('mild','natural-after-mild') else CANON; enabled=1 if a.phase in ('mild','natural-after-mild') else 0
if act is not None and act!=0: errs.append(f'action={act}')
if not (s['enabled']==enabled and s['pending']==0 and s['reapply']==3 and s['last']==0 and s['requested']==s['active'] and s['fault']==0 and s['masks']==0): errs.append('status mismatch')
for i,p in enumerate(s['p']):
    if p['applied']!=s['active'] or p['raw']<0 or p['src']!=CANON or p['fwd']!=expected: errs.append(f'p{i} mismatch')
    if prev:
        if a.phase=='natural-after-mild' and p['natural']<=prev['p'][i]['natural']: errs.append(f'p{i} natural did not advance')
        if a.phase!='natural-after-mild' and p['natural']!=prev['p'][i]['natural']: errs.append(f'p{i} internal replay advanced natural')
tag='VBE_RELEASE_'+a.phase.upper().replace('-','_')
print('FILE_SHA256='+hashlib.sha256(b).hexdigest()); print(f'POLICY=requested:{s["requested"]} active:{s["active"]} enabled:{s["enabled"]} pending:{s["pending"]} reapply:{s["reapply"]} last:{s["last"]}')
if errs: print(tag+'=FAIL'); [print('ERROR='+e) for e in errs]; raise SystemExit(2)
print(tag+'=PASS')
