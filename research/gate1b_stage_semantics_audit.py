#!/usr/bin/env python3
from __future__ import annotations
import argparse,json
from pathlib import Path
from capstone.arm import ARM_OP_IMM,ARM_OP_MEM,ARM_OP_REG
from vita_elf_audit import VitaElf,Reachability,FunctionCFG
from topology_common import all_insns,ins_text,function_parents

LOWIO_SHA='f791cfe2c6db955deb9446c57bb72ce1870bde2c6c485cac363e343d5128f744'
DISPLAY_SHA='83ef39adf741a4bbbc1cec67d00e2594de1d17be0f5ea6cc77c935eae46065e5'
NID_IFTU_CSC=0x67E37EFC
NID_IFTU_ENABLE=0x0D7C02F7
NID_A=0x0FCBF457
NID_B=0xD64F4C6B
NID_COLORSPACE=0x8D79D187
NID_BRIGHTNESS=0x9E3C6DC6
NID_INVERT=0x19140ACD
GENERATOR=0x81000A2C


def find_export(e,nid):
    for lib in e.exports():
        for f in lib['functions']:
            if f['nid']==nid:return f
    return None

def cfg_for(e,r,va):
    for (s,t),cfg in r.functions.items():
        if s==va:return cfg
    return FunctionCFG(e,va,True,r.import_stubs,boundary_index=r.boundaries,noreturn_stubs=r.noreturn_stubs)

def rec(cfg):
    return {'start':cfg.start,'end':cfg.logical_end,'instructions':[{'va':i.address,'text':ins_text(i)} for i in all_insns(cfg)]}

def callsites_to(r,target):
    out=[]
    for cfg in r.functions.values():
        for c in cfg.calls:
            if c['target']==target:
                out.append({'caller':cfg.start,'call_va':c['va'],'window':cfg.window_for_call(c['va'],before=64,after=12)})
    return sorted(out,key=lambda x:(x['caller'],x['call_va']))

def mem100_refs(r):
    out=[]
    for cfg in r.functions.values():
        for i in all_insns(cfg):
            ops=getattr(i,'operands',[])
            for op in ops:
                if op.type==ARM_OP_MEM and int(op.mem.disp)==0x100:
                    m=i.mnemonic.lower()
                    out.append({'function':cfg.start,'va':i.address,'access':'read' if m.startswith(('ldr','ldm')) else 'write' if m.startswith(('str','stm')) else 'other','instruction':ins_text(i)})
                    break
    return sorted(out,key=lambda x:(x['function'],x['va']))

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--lowio',type=Path,required=True);ap.add_argument('--display',type=Path,required=True);ap.add_argument('--json',type=Path,required=True);a=ap.parse_args()
    low=VitaElf(a.lowio);disp=VitaElf(a.display)
    if low.sha256!=LOWIO_SHA:raise SystemExit('Lowio hash drift')
    if disp.sha256!=DISPLAY_SHA:raise SystemExit('Display hash drift')
    lr=Reachability(low,low.exports(),low.imports(),extra_starts=[0x8100678C,0x8100639C,0x81005D48,0x81005E24])
    dr=Reachability(disp,disp.exports(),disp.imports(),extra_starts=[GENERATOR])
    ex={}
    for name,nid in [('ksceIftuCsc',NID_IFTU_CSC),('ksceIftuEnable',NID_IFTU_ENABLE),('A',NID_A),('B',NID_B)]:
        f=find_export(low,nid);ex[name]=f['va'] if f else None
    dex={}
    for name,nid in [('ksceDisplaySetColorSpaceMode',NID_COLORSPACE),('ksceDisplaySetBrightness',NID_BRIGHTNESS),('ksceDisplaySetInvertColors',NID_INVERT)]:
        f=find_export(disp,nid);dex[name]=f['va'] if f else None
    if ex!={'ksceIftuCsc':0x8100678C,'ksceIftuEnable':0x8100639C,'A':0x81005D48,'B':0x81005E24}:raise SystemExit(f'Lowio VA drift {ex}')
    if dex['ksceDisplaySetColorSpaceMode'] is None:raise SystemExit('missing color-space export')
    focus={'generator':rec(cfg_for(disp,dr,GENERATOR))}
    for n,v in dex.items():focus[n]=rec(cfg_for(disp,dr,v))
    csc_stub=None
    for lib in disp.imports():
        for f in lib['functions']:
            if f['nid']==NID_IFTU_CSC:csc_stub=f['va']
    if csc_stub is None:raise SystemExit('missing Display IFTU CSC stub')
    parents=function_parents(dr)
    result={'schema':1,'firmware':'3.65','lowio_sha':low.sha256,'display_sha':disp.sha256,'lowio_exports':ex,'display_exports':dex,'lowio_mem_disp_0x100_refs':mem100_refs(lr),'display_focus':focus,'display_iftu_csc_calls':callsites_to(dr,csc_stub),'generator_parents':parents.get(GENERATOR,[])}
    a.json.write_text(json.dumps(result,indent=2)+'\n')
    print('GATE1B_STAGE_SEMANTICS_EVIDENCE')
    print('LOWIO_EXPORTS')
    for k,v in ex.items():print(f'  {k}=0x{v:08X}')
    print('DISPLAY_EXPORTS')
    for k,v in dex.items():print(f'  {k}=0x{v:08X}')
    print('LOWIO_ALL_DISP_0x100_MEMORY_REFS')
    for x in result['lowio_mem_disp_0x100_refs']:print(f"  fn=0x{x['function']:08X} va=0x{x['va']:08X} {x['access']} {x['instruction']}")
    print('DISPLAY_IFTU_CSC_CALLS')
    for x in result['display_iftu_csc_calls']:print(f"  caller=0x{x['caller']:08X} call=0x{x['call_va']:08X}")
    print('GENERATOR_PARENTS')
    for x in result['generator_parents']:print(' ',x)

if __name__=='__main__':main()
