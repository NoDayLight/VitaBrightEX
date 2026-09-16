#!/usr/bin/env python3
from __future__ import annotations
import argparse, json
from collections import defaultdict, deque
from pathlib import Path
from capstone.arm import ARM_OP_IMM, ARM_OP_MEM, ARM_OP_REG
from vita_elf_audit import VitaElf, Reachability, FunctionCFG
from topology_common import all_insns, ins_text, load_nid_names, import_stub_map, call_semantics
from affine_static_closure_audit import scan_plane_state_function

LOWIO_SHA='f791cfe2c6db955deb9446c57bb72ce1870bde2c6c485cac363e343d5128f744'
DISPLAY_SHA='83ef39adf741a4bbbc1cec67d00e2594de1d17be0f5ea6cc77c935eae46065e5'
NID_A=0x0FCBF457
NID_B=0xD64F4C6B
NID_ENABLE=0x0D7C02F7
NID_CSC=0x67E37EFC
NID_DISABLE=0xC11F30B3
GENERATOR=0x81000A2C
SEQUENCE_OWNER=0x81000C2C
LOWIO_B=0x81005E24
LOWIO_ENABLE=0x8100639C
LOWIO_CSC=0x8100678C
PLANE_BASE=0x8100B37C
PLANE_STRIDE=0x214


def find_export(e,nid):
    for lib in e.exports():
        for f in lib['functions']:
            if f['nid']==nid:return {'library':lib['library_name'], **f}
    return None

def find_import(e,nid):
    for lib in e.imports():
        for f in lib['functions']:
            if f['nid']==nid:return {'library':lib['library_name'], **f}
    return None

def cfg_for(e,r,va,thumb=True):
    for (_, _),cfg in r.functions.items():
        if cfg.start==va:return cfg
    return FunctionCFG(e,va,thumb,r.import_stubs,boundary_index=r.boundaries,noreturn_stubs=r.noreturn_stubs)

def rec(e,r,stubs,va):
    cfg=cfg_for(e,r,va)
    return {
      'start':cfg.start,'end':cfg.logical_end,'instruction_count':cfg.instruction_count(),
      'calls':call_semantics(cfg,stubs),
      'instructions':[{'va':i.address,'text':ins_text(i)} for i in all_insns(cfg)],
      'plane_state_refs':scan_plane_state_function(cfg),
    }

def callsites_to_va(r,target):
    out=[]
    for cfg in r.functions.values():
        for c in cfg.calls:
            if c['target']==target:
                out.append({'caller':cfg.start,'call_va':c['va'],'window':cfg.window_for_call(c['va'],before=80,after=20)})
    return sorted(out,key=lambda x:(x['caller'],x['call_va']))

def parents_map(r):
    p=defaultdict(list)
    starts={cfg.start for cfg in r.functions.values()}
    for cfg in r.functions.values():
        for c in cfg.calls:
            if c['target'] in starts:p[c['target']].append({'caller':cfg.start,'call_va':c['va']})
    return p

def ancestor_tree(r,start,depth=4):
    p=parents_map(r);seen={start};q=deque([(start,0)]);edges=[]
    while q:
        cur,d=q.popleft()
        if d>=depth:continue
        for x in p.get(cur,[]):
            edges.append({'callee':cur,'caller':x['caller'],'call_va':x['call_va'],'depth':d+1})
            if x['caller'] not in seen:
                seen.add(x['caller']);q.append((x['caller'],d+1))
    return edges

def mem_ops(cfg):
    out=[]
    for i in all_insns(cfg):
        m=i.mnemonic.lower();ops=getattr(i,'operands',[])
        if not (m.startswith('ldr') or m.startswith('str') or m.startswith('ldm') or m.startswith('stm')):continue
        row={'va':i.address,'instruction':ins_text(i)}
        for op in ops:
            if op.type==ARM_OP_MEM:
                row['base']=i.reg_name(op.mem.base);row['disp']=int(op.mem.disp);row['index']=i.reg_name(op.mem.index) if op.mem.index else None
        out.append(row)
    return out

def direct_b_transaction(cfg):
    # Exact instruction stream + conservative classifications. The detailed
    # semantic word/register table is derived separately by iftu_mmio_audit.py.
    refs=scan_plane_state_function(cfg)
    return {
      'cache_writes':[x for x in refs if x['kind']=='plane_write' and 0x148<=x['offset']<0x184],
      'mmio_writes':[x for x in refs if x['kind']=='mmio_write' and 0x130<=x['offset']<=0x168],
      'all_state_refs':refs,
      'memory_ops':mem_ops(cfg),
    }

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--lowio',type=Path,required=True);ap.add_argument('--display',type=Path,required=True);ap.add_argument('--nid-db-root',type=Path,required=True);ap.add_argument('--json',type=Path,required=True);a=ap.parse_args()
    low=VitaElf(a.lowio);disp=VitaElf(a.display)
    if low.sha256!=LOWIO_SHA:raise SystemExit('Lowio hash drift')
    if disp.sha256!=DISPLAY_SHA:raise SystemExit('Display hash drift')
    names=load_nid_names(a.nid_db_root)
    extra_low=[LOWIO_B,LOWIO_ENABLE,LOWIO_CSC]
    extra_disp=[GENERATOR,SEQUENCE_OWNER]
    lr=Reachability(low,low.exports(),low.imports(),extra_starts=extra_low)
    dr=Reachability(disp,disp.exports(),disp.imports(),extra_starts=extra_disp)
    ls=import_stub_map(low,names);ds=import_stub_map(disp,names)
    exports={name:find_export(low,nid) for name,nid in [('A',NID_A),('B',NID_B),('ENABLE',NID_ENABLE),('CSC',NID_CSC),('DISABLE',NID_DISABLE)]}
    if exports['B']['va']!=LOWIO_B or exports['ENABLE']['va']!=LOWIO_ENABLE or exports['CSC']['va']!=LOWIO_CSC:raise SystemExit('Lowio export VA drift')
    imports={name:find_import(disp,nid) for name,nid in [('A',NID_A),('B',NID_B),('ENABLE',NID_ENABLE),('CSC',NID_CSC)]}
    bcfg=cfg_for(low,lr,LOWIO_B)
    result={
      'schema':1,'firmware':'3.65','lowio_sha256':low.sha256,'display_sha256':disp.sha256,
      'exports':exports,'display_imports':imports,
      'lowio_B':rec(low,lr,ls,LOWIO_B),
      'lowio_ENABLE':rec(low,lr,ls,LOWIO_ENABLE),
      'lowio_CSC':rec(low,lr,ls,LOWIO_CSC),
      'display_generator':rec(disp,dr,ds,GENERATOR),
      'display_sequence_owner':rec(disp,dr,ds,SEQUENCE_OWNER),
      'B_transaction':direct_b_transaction(bcfg),
      'display_B_callsites':callsites_to_va(dr,imports['B']['va']) if imports['B'] else [],
      'display_A_callsites':callsites_to_va(dr,imports['A']['va']) if imports['A'] else [],
      'display_ENABLE_callsites':callsites_to_va(dr,imports['ENABLE']['va']) if imports['ENABLE'] else [],
      'generator_ancestors':ancestor_tree(dr,GENERATOR,5),
      'sequence_owner_ancestors':ancestor_tree(dr,SEQUENCE_OWNER,5),
    }
    a.json.write_text(json.dumps(result,indent=2)+'\n')
    print('GATE1E_STATIC_ATTACK_SURFACE')
    print(f"B_EXPORT=0x{exports['B']['va']:08X} DISPLAY_B_STUB=0x{imports['B']['va']:08X}")
    print('B_CALLSITES='+','.join(f"0x{x['caller']:08X}@0x{x['call_va']:08X}" for x in result['display_B_callsites']))
    print('GENERATOR_ANCESTORS='+','.join(f"0x{x['caller']:08X}->0x{x['callee']:08X}" for x in result['generator_ancestors']))
    print('B_SETTER_CALLS='+str([(x.get('name'),hex(x['target'])) for x in result['lowio_B']['calls']]))
    print('B_SETTER_CACHE_WRITES='+str([(hex(x['va']),hex(x['offset'])) for x in result['B_transaction']['cache_writes']]))
    print('B_SETTER_MMIO_WRITES='+str([(hex(x['va']),hex(x['offset'])) for x in result['B_transaction']['mmio_writes']]))
    print('B_SETTER_INSTRUCTION_COUNT='+str(result['lowio_B']['instruction_count']))
    print('CSC_CALLS='+str([(x.get('name'),hex(x['target'])) for x in result['lowio_CSC']['calls']]))
    print('ENABLE_CALLS='+str([(x.get('name'),hex(x['target'])) for x in result['lowio_ENABLE']['calls']]))
if __name__=='__main__':main()
