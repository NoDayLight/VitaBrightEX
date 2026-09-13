#!/usr/bin/env python3
"""Prove the retail-3.65 r1 pointer path and compare it with pinned v1.3 source."""
from __future__ import annotations
import argparse,hashlib,re
from pathlib import Path
from vita_elf_audit import VitaElf,FunctionCFG,Reachability
EXPECTED_LOWIO_SHA256='f791cfe2c6db955deb9446c57bb72ce1870bde2c6c485cac363e343d5128f744'
TARGET_NID=0x0FCBF457

def norm(ins): return ins.mnemonic.lower(),ins.op_str.replace(' ','').lower()
def find_export(elf,nid):
    for lib in elf.exports():
        for f in lib['functions']:
            if f['nid']==nid: return lib,f
    raise SystemExit(f'target export 0x{nid:08X} not found')
def imap(cfg): return {x.address:x for b in cfg.blocks.values() for x in b.instructions}
def require(m,va,mn,frag):
    x=m.get(va)
    if x is None: raise SystemExit(f'required reachable instruction 0x{va:08X} missing')
    a,b=norm(x)
    if a!=mn or frag.replace(' ','').lower() not in b: raise SystemExit(f'unexpected 0x{va:08X}: {x.mnemonic} {x.op_str}')
    return x

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('--lowio',type=Path,required=True); ap.add_argument('--legacy-screen-filter',type=Path,required=True); a=ap.parse_args()
    elf=VitaElf(a.lowio)
    if elf.sha256!=EXPECTED_LOWIO_SHA256: raise SystemExit(f'unexpected SceLowio SHA-256 {elf.sha256}')
    lib,target=find_export(elf,TARGET_NID); reach=Reachability(elf,elf.exports(),elf.imports()); cfg=reach.functions.get((target['va'],target['thumb'])) or FunctionCFG(elf,target['va'],target['thumb'],reach.import_stubs); m=imap(cfg)
    require(m,0x81005D4E,'mov','r4,r1'); require(m,0x81005D72,'cbz','r4,#0x81005db8'); require(m,0x81005D74,'mov','r3,r4')
    load=m.get(0x81005D7E)
    if load is None or not norm(load)[0].startswith('ldr') or '[r3]' not in norm(load)[1]: raise SystemExit('reachable non-null r1-derived load missing')
    block=next((b for b in cfg.blocks.values() if any(x.address==0x81005D72 for x in b.instructions)),None)
    if block is None: raise SystemExit('CBZ block missing')
    vas=[x.address for x in block.instructions]; i=vas.index(0x81005D72)
    if i+1>=len(vas) or vas[i+1]!=0x81005D74: raise SystemExit('non-null fallthrough not contiguous')
    legacy=a.legacy_screen_filter.read_text(encoding='utf-8'); lsha=hashlib.sha256(legacy.encode()).hexdigest()
    decl=re.search(r'ksceIftuSetCscParams\)\(int\s+head,\s*int\s+fb_idx,\s*const\s+SceIftuCscParams\s*\*p\)',legacy,re.S)
    if decl is None: raise SystemExit('legacy fb_idx declaration not found')
    if 'ksceIftuSetCscParams(0, 1, &csc);' not in legacy and 'ksceIftuSetCscParams(0, 1, &CSC_IDENTITY);' not in legacy: raise SystemExit('legacy second argument 1 call not found')
    if 'ksceIftuSetCscParams(0, 0, &csc);' not in legacy and 'ksceIftuSetCscParams(0, 0, &CSC_IDENTITY);' not in legacy: raise SystemExit('legacy second argument 0 call not found')
    print('IFTU ABI evidence — retail 3.65'); print(f'  SceLowio sha256={elf.sha256}'); print(f"  library={lib['library_name']} libnid=0x{lib['library_nid']:08X}"); print(f"  function_nid=0x{TARGET_NID:08X} entry=0x{target['va']:08X} thumb={int(target['thumb'])}")
    print('  reachable dataflow:'); print('    0x81005D4E mov r4, r1'); print('    0x81005D72 cbz r4, 0x81005DB8'); print('    non-null fallthrough -> 0x81005D74 mov r3, r4'); print(f'    0x81005D7E {load.mnemonic} {load.op_str}'); print('  conclusion: r1 is pointer-like on the non-null path; r1=1 reaches a load through address 0x00000001')
    print('LEGACY V1.3 SOURCE EVIDENCE'); print(f'  screen_filter.c sha256={lsha}'); print('  declaration=(int head, int fb_idx, const SceIftuCscParams *p)'); print('  source calls second arguments 0 and 1, including the identity path')
    print('VERDICT'); print('  PROVEN_ABI_BUG: legacy fb_idx prototype contradicts retail-3.65 callee semantics'); print('  PROVEN_UNSAFE_PATH: legacy r1=1 reaches an r1-derived memory load'); print('  HANG_CAUSALITY: LIKELY_NOT_CONCLUSIVE_WITHOUT_FAULT_TRACE')
if __name__=='__main__': main()
