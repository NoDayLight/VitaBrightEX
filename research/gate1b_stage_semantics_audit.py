#!/usr/bin/env python3
from __future__ import annotations
import argparse,json
from pathlib import Path
from capstone.arm import ARM_OP_MEM
from vita_elf_audit import VitaElf,Reachability,FunctionCFG
from topology_common import all_insns,ins_text,function_parents,absolute_constants

LOWIO_SHA='f791cfe2c6db955deb9446c57bb72ce1870bde2c6c485cac363e343d5128f744'
DISPLAY_SHA='83ef39adf741a4bbbc1cec67d00e2594de1d17be0f5ea6cc77c935eae46065e5'
NID_IFTU_CSC=0x67E37EFC;NID_IFTU_ENABLE=0x0D7C02F7;NID_A=0x0FCBF457;NID_B=0xD64F4C6B
NID_COLORSPACE=0x8D79D187;NID_BRIGHTNESS=0x9E3C6DC6;NID_INVERT=0x19140ACD
GENERATOR=0x81000A2C
DISPLAY_MODE_ROUTINES=[0x8100724C,0x81007410]
DISPLAY_KNOWN_CALLERS=[0x81000C2C,0x81002C94,0x81002FF4]
LOWIO_EXTRA_WRITER=0x81007250
MODE_GLOBAL_RANGE=(0x8100B200,0x8100B300)

def find_export(e,nid):
    for lib in e.exports():
        for f in lib['functions']:
            if f['nid']==nid:return f
    return None

def cfg_for(e,r,va):
    for (s,t),cfg in r.functions.items():
        if s==va:return cfg
    return FunctionCFG(e,va,True,r.import_stubs,boundary_index=r.boundaries,noreturn_stubs=r.noreturn_stubs)

def rec(cfg):return {'start':cfg.start,'end':cfg.logical_end,'instructions':[{'va':i.address,'text':ins_text(i)} for i in all_insns(cfg)],'calls':cfg.calls}

def callsites_to(r,target):
    out=[]
    for cfg in r.functions.values():
        for c in cfg.calls:
            if c['target']==target:out.append({'caller':cfg.start,'call_va':c['va'],'window':cfg.window_for_call(c['va'],before=64,after=12)})
    return sorted(out,key=lambda x:(x['caller'],x['call_va']))

def mem100_refs(r):
    out=[]
    for cfg in r.functions.values():
        for i in all_insns(cfg):
            for op in getattr(i,'operands',[]):
                if op.type==ARM_OP_MEM and int(op.mem.disp)==0x100:
                    m=i.mnemonic.lower();out.append({'function':cfg.start,'va':i.address,'access':'read' if m.startswith(('ldr','ldm')) else 'write' if m.startswith(('str','stm')) else 'other','instruction':ins_text(i)});break
    return sorted(out,key=lambda x:(x['function'],x['va']))

def global_ref_functions(elf,r,lo,hi):
    out=[]
    for cfg in r.functions.values():
        hits=[x for x in absolute_constants(elf,cfg) if lo<=x['value']<hi]
        if hits:out.append({'function':cfg.start,'hits':hits,'body':rec(cfg)})
    return sorted(out,key=lambda x:x['function'])

def all_exports_at(e,va):
    out=[]
    for lib in e.exports():
        for f in lib['functions']:
            if f['va']==va:out.append({'library':lib['library_name'],'nid':f['nid']})
    return out

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--lowio',type=Path,required=True);ap.add_argument('--display',type=Path,required=True);ap.add_argument('--json',type=Path,required=True);a=ap.parse_args()
    low=VitaElf(a.lowio);disp=VitaElf(a.display)
    if low.sha256!=LOWIO_SHA:raise SystemExit('Lowio hash drift')
    if disp.sha256!=DISPLAY_SHA:raise SystemExit('Display hash drift')
    lr=Reachability(low,low.exports(),low.imports(),extra_starts=[0x8100678C,0x8100639C,0x81005D48,0x81005E24,LOWIO_EXTRA_WRITER])
    dr=Reachability(disp,disp.exports(),disp.imports(),extra_starts=[GENERATOR]+DISPLAY_MODE_ROUTINES+DISPLAY_KNOWN_CALLERS)
    ex={}
    for name,nid in [('ksceIftuCsc',NID_IFTU_CSC),('ksceIftuEnable',NID_IFTU_ENABLE),('A',NID_A),('B',NID_B)]:
        f=find_export(low,nid);ex[name]=f['va'] if f else None
    dex={}
    for name,nid in [('ksceDisplaySetColorSpaceMode',NID_COLORSPACE),('ksceDisplaySetBrightness',NID_BRIGHTNESS),('ksceDisplaySetInvertColors',NID_INVERT)]:
        f=find_export(disp,nid);dex[name]=f['va'] if f else None
    if ex!={'ksceIftuCsc':0x8100678C,'ksceIftuEnable':0x8100639C,'A':0x81005D48,'B':0x81005E24}:raise SystemExit(f'Lowio VA drift {ex}')
    if dex['ksceDisplaySetColorSpaceMode']!=0x810032E8:raise SystemExit(f'color-space export drift {dex}')
    focus={'generator':rec(cfg_for(disp,dr,GENERATOR)),'lowio_extra_writer':rec(cfg_for(low,lr,LOWIO_EXTRA_WRITER))}
    for n,v in dex.items():focus[n]=rec(cfg_for(disp,dr,v))
    for v in DISPLAY_MODE_ROUTINES:focus[f'display_internal_{v:08X}']=rec(cfg_for(disp,dr,v))
    for v in DISPLAY_KNOWN_CALLERS:focus[f'display_known_{v:08X}']=rec(cfg_for(disp,dr,v))
    csc_stub=None
    for lib in disp.imports():
        for f in lib['functions']:
            if f['nid']==NID_IFTU_CSC:csc_stub=f['va']
    if csc_stub is None:raise SystemExit('missing Display IFTU CSC stub')
    parents=function_parents(dr)
    refs=global_ref_functions(disp,dr,*MODE_GLOBAL_RANGE)
    result={'schema':3,'firmware':'3.65','lowio_sha':low.sha256,'display_sha':disp.sha256,'lowio_exports':ex,'display_exports':dex,'lowio_extra_writer_exports':all_exports_at(low,LOWIO_EXTRA_WRITER),'lowio_mem_disp_0x100_refs':mem100_refs(lr),'display_focus':focus,'display_iftu_csc_calls':callsites_to(dr,csc_stub),'generator_parents':parents.get(GENERATOR,[]),'display_mode_global_ref_functions':refs}
    a.json.write_text(json.dumps(result,indent=2)+'\n')
    print('GATE1B_STAGE_SEMANTICS_EVIDENCE')
    for k,v in ex.items():print(f'LOWIO {k}=0x{v:08X}')
    for k,v in dex.items():print(f'DISPLAY {k}=0x{v:08X}')
    print('LOWIO_EXTRA_WRITER_EXPORTS',result['lowio_extra_writer_exports'])
    for x in result['lowio_mem_disp_0x100_refs']:print(f"MMIO_100 fn=0x{x['function']:08X} va=0x{x['va']:08X} {x['access']} {x['instruction']}")
    for x in refs:
        vals=','.join(f"0x{h['value']:08X}@0x{h['va']:08X}" for h in x['hits'])
        print(f"DISPLAY_MODE_GLOBAL_REF fn=0x{x['function']:08X} {vals}")

if __name__=='__main__':main()
