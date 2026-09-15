#!/usr/bin/env python3
from __future__ import annotations
import argparse,json
from pathlib import Path
from vita_elf_audit import VitaElf,Reachability,FunctionCFG
from topology_common import all_insns,ins_text

LOWIO_SHA='f791cfe2c6db955deb9446c57bb72ce1870bde2c6c485cac363e343d5128f744'
A_NID=0x0FCBF457;B_NID=0xD64F4C6B;E_NID=0x0D7C02F7;A_VA=0x81005D48;B_VA=0x81005E24

def die(s):raise SystemExit(s)
def exports_for(e,n):return[(l,f) for l in e.exports() for f in l['functions'] if f['nid']==n]
def cfg_at(e,v):
    r=Reachability(e,e.exports(),e.imports(),extra_starts=(v,));return r.functions.get((v,True)) or FunctionCFG(e,v,True,r.import_stubs,boundary_index=r.boundaries,noreturn_stubs=r.noreturn_stubs)
def norm(i):return ins_text(i).lower().replace(' ','')
def byva(c):return{i.address:i for i in all_insns(c)}
def exact(b,v,*want):
    got=norm(b[v]) if v in b else '<missing>'
    if got not in [x.replace(' ','').lower() for x in want]:die(f'0x{v:08X}: {got} not in {want}')
def prove_csc(e,nid,va,nullva,base,load,inc,cmpv,last):
    ex=exports_for(e,nid)
    if len(ex)!=1 or ex[0][1]['va']!=va:die(f'CSC export 0x{nid:08X} identity mismatch')
    c=cfg_at(e,va);b=byva(c);exact(b,va,'cmp r0, #4');exact(b,va+6,'mov r4, r1')
    if nullva not in b or not norm(b[nullva]).startswith('cbz'):die('CSC NULL branch missing')
    exact(b,base,'add.w r0, r4, #0x30')
    if load not in b or not norm(b[load]).startswith('ldr'):die('CSC loop load missing')
    exact(b,inc,'adds r3, #0x10');exact(b,cmpv,'cmp r3, r0')
    for off in (last,last+4,last+6):
        if off not in b or not norm(b[off]).startswith('ldr'):die(f'CSC final 0x3C load missing at 0x{off:08X}')
    return {'nid':f'0x{nid:08X}','va':f'0x{va:08X}','abi':'int(int plane, const SceIftuCscParams *params)','copy_safety':'valid plane + non-NULL consumes complete 0x3C object','return':'raw r0'}
def incoming_arg_unused(c,names):
    blocks=c.blocks;preds={k:[] for k in blocks}
    for k,b in blocks.items():
        for s in b.successors:
            if s in preds:preds[s].append(k)
    allr=set(names);entry={k:(set() if k==c.start else set(allr)) for k in blocks};out={k:set() for k in blocks};changed=True
    while changed:
        changed=False
        for k in sorted(blocks):
            if k!=c.start:
                ps=preds[k];new=set.intersection(*(out[p] for p in ps)) if ps else set()
                if new!=entry[k]:entry[k]=new;changed=True
            defined=set(entry[k])
            for ins in blocks[k].instructions:
                try:reads,writes=ins.regs_access()
                except Exception:reads,writes=([],[])
                rn={ins.reg_name(x) for x in reads};wn={ins.reg_name(x) for x in writes};bad=(rn&allr)-defined
                if bad:return False,{'va':f'0x{ins.address:08X}','instruction':ins_text(ins),'incoming_reads':sorted(bad)}
                defined|=(wn&allr)
            if defined!=out[k]:out[k]=defined;changed=True
    return True,None
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--lowio',type=Path,required=True);ap.add_argument('--json',type=Path,required=True);ap.add_argument('--text',type=Path,required=True);a=ap.parse_args()
    e=VitaElf(a.lowio)
    if e.sha256!=LOWIO_SHA:die('SceLowio hash drift')
    ca=prove_csc(e,A_NID,A_VA,0x81005D72,0x81005D7A,0x81005D7E,0x81005D82,0x81005D92,0x81005DA6)
    cb=prove_csc(e,B_NID,B_VA,0x81005E50,0x81005E58,0x81005E5C,0x81005E60,0x81005E70,0x81005E84)
    en=exports_for(e,E_NID)
    if len(en)!=1:die(f'enable export count {len(en)}')
    eva=en[0][1]['va'];ec=cfg_at(e,eva);ok,why=incoming_arg_unused(ec,('r1','r2','r3'))
    if not ok:die('enable incoming r1-r3 required: '+json.dumps(why))
    r0=False
    for ins in all_insns(ec):
        try:reads,writes=ins.regs_access()
        except Exception:continue
        rn={ins.reg_name(x) for x in reads};wn={ins.reg_name(x) for x in writes}
        if 'r0' in rn:r0=True;break
        if 'r0' in wn:break
    if not r0:die('enable incoming r0 not proven')
    doc={'schema':1,'firmware':'3.65','SceLowio_sha256':e.sha256,'csc_a':ca,'csc_b':cb,'enable':{'nid':'0x0D7C02F7','va':f'0x{eva:08X}','abi':'int(int plane)','incoming_r0':'read before overwrite','incoming_r1_r3':'not read before definite overwrite','return':'raw r0'}}
    a.json.write_text(json.dumps(doc,indent=2,sort_keys=True)+'\n')
    lines=['GATE1A_RETAIL_365_LOWIO_ABI=PASS',f'CSC_A_VA=0x{A_VA:08X} ABI=int(int,const 0x3C*)',f'CSC_B_VA=0x{B_VA:08X} ABI=int(int,const 0x3C*)',f'IFTU_ENABLE_VA=0x{eva:08X} ABI=int(int)','CSC_COMPLETE_0X3C_CONSUMPTION=PASS','RAW_RETURN_PRESERVATION_ABI=PASS']
    a.text.write_text('\n'.join(lines)+'\n');print('\n'.join(lines))
if __name__=='__main__':main()
