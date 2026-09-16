#!/usr/bin/env python3
import argparse, hashlib, struct
from pathlib import Path
CANON='5dc12dfcae42a648dc093db831b661cc3068f2d70f199b03fb582ce301b188b5'
def words(b,o,n): return struct.unpack_from('<'+'I'*n,b,o)
def sha15(v): return hashlib.sha256(struct.pack('<15I',*v)).hexdigest()
ap=argparse.ArgumentParser(); ap.add_argument('file',type=Path); ap.add_argument('--expected-build-id',required=True); a=ap.parse_args()
raw=a.file.read_bytes(); assert len(raw)==460, len(raw)
bid=raw[:9].split(b'\0',1)[0].decode('ascii')
caps=words(raw,12,16); st=words(raw,76,16)
errs=[]
if bid!=a.expected_build_id: errs.append(f'build_id={bid}, expected {a.expected_build_id}')
if not (caps[0]==64 and caps[1]==1 and caps[2]==1 and caps[3]==1 and caps[4]==1 and caps[5]==3): errs.append('capabilities not release-ready')
if not (st[0]==384 and st[1]==1 and st[2]==1 and st[3]==1 and st[4]==0 and st[5]==1 and st[6]==1 and st[7]==0 and st[8]==0 and st[9]==3 and st[10]==0 and st[13]==0 and st[14]==0 and st[15]==0): errs.append('runtime is not fresh neutral chain-head state')
for i,o in enumerate((76+64,76+224)):
    p=words(raw,o,10); src=words(raw,o+40,15); fwd=words(raw,o+100,15)
    if not (p[0]==1 and p[2]==1 and p[3]==1 and p[4]==0 and p[5]==0 and p[6]==0 and p[7]==0): errs.append(f'p{i} unhealthy')
    if sha15(src)!=CANON or sha15(fwd)!=CANON: errs.append(f'p{i} not canonical identity')
print('FILE_SHA256='+hashlib.sha256(raw).hexdigest()); print('BUILD_ID='+bid); print(f'CAPS=matrix:{caps[3]} immediate:{caps[4]} reapply:{caps[5]}')
if errs:
    print('VBE_RELEASE_RUNTIME_PREFLIGHT=FAIL'); [print('ERROR='+e) for e in errs]; raise SystemExit(2)
print('VBE_RELEASE_RUNTIME_PREFLIGHT=PASS')
