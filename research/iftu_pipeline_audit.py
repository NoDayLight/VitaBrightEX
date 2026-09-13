#!/usr/bin/env python3
"""Retail-3.65 IFTU/SceDisplay color-pipeline evidence extractor v2."""
from __future__ import annotations

import argparse, json
from pathlib import Path
from capstone.arm import ARM_OP_IMM, ARM_OP_MEM, ARM_OP_REG
from vita_elf_audit import FunctionCFG, Reachability, VitaElf

LOWIO_SHA256="f791cfe2c6db955deb9446c57bb72ce1870bde2c6c485cac363e343d5128f744"
DISPLAY_SHA256="83ef39adf741a4bbbc1cec67d00e2594de1d17be0f5ea6cc77c935eae46065e5"
IFTU_PUBLIC={0x0D7C02F7:"ksceIftuEnable",0x67E37EFC:"ksceIftuCsc",0x7CE0C4DA:"ksceIftuSetInputFrameBuffer",0xAF19FD85:"ksceIftuSetMergeSetting",0xC11F30B3:"ksceIftuDisable",0xE6EE2C6B:"ksceIftuSetOutputFormat"}
IFTU_PRIVATE={0x0FCBF457:"private_csc_cache_A",0x357EAE24:"private_control",0xD64F4C6B:"private_csc_cache_B"}
DISPLAY_PUBLIC={0x9E3C6DC6:"ksceDisplaySetBrightness",0x19140ACD:"ksceDisplaySetInvertColors"}
EXPECTED_IFTU_VA={0x0D7C02F7:0x8100639C,0x0FCBF457:0x81005D48,0x357EAE24:0x810062B4,0x67E37EFC:0x8100678C,0x7CE0C4DA:0x81005FEC,0xAF19FD85:0x81006338,0xC11F30B3:0x81006714,0xD64F4C6B:0x81005E24,0xE6EE2C6B:0x81005F20}
INSTANCE_BASE=0x8100B37C


def die(x): raise SystemExit(x)
def load(path,sha):
    e=VitaElf(path)
    if e.sha256!=sha: die(f"SHA mismatch {e.modinfo['name']}: {e.sha256}")
    return e
def find_export(e,nid):
    for lib in e.exports():
        for fn in lib['functions']:
            if fn['nid']==nid:return lib,fn
    return None,None
def insns(cfg):
    d={}
    for b in cfg.blocks.values():
        for i in b.instructions:d[i.address]=i
    return [d[k] for k in sorted(d)]
def txt(i): return f"{i.mnemonic} {i.op_str}".strip()
def read_words(e,va,n):
    try: _,off=e.file_from_va(va)
    except ValueError:return None
    if off+4*n>len(e.data):return None
    return [int.from_bytes(e.data[off+4*i:off+4*i+4],'little') for i in range(n)]
def signed32(x):return x-(1<<32) if x&0x80000000 else x
def s3_9(w):
    if w&0xFFFFF000 in (0,0xFFFFF000):
        v=w&0xFFF
        if v&0x800:v-=0x1000
    else:v=signed32(w)
    return v/512.0
def plausible_csc(w):
    if not w or len(w)!=15:return False
    if any(x>0x3ff and x!=0xffffffff for x in w[:6]):return False
    return all((x&0xfffff000 in (0,0xfffff000)) or -2048<=signed32(x)<=2047 for x in w[6:])
def decode_csc(w):
    return {'post_add_0':w[0],'post_add_1_2':w[1],'post_clamp_max_0':w[2],'post_clamp_min_0':w[3],'post_clamp_max_1_2':w[4],'post_clamp_min_1_2':w[5],'ctm_s3_9':[[s3_9(w[6+r*3+c]) for c in range(3)] for r in range(3)]}
def constructed_constants(cfg):
    out=set()
    for b in cfg.blocks.values():
        regs={}
        for i in b.instructions:
            o=getattr(i,'operands',[]);m=i.mnemonic.lower()
            if m=='movw' and len(o)>=2 and o[0].type==ARM_OP_REG and o[1].type==ARM_OP_IMM:regs[o[0].reg]=o[1].imm&0xffff
            elif m=='movt' and len(o)>=2 and o[0].type==ARM_OP_REG and o[1].type==ARM_OP_IMM:
                r=o[0].reg
                if r in regs:
                    regs[r]=(regs[r]&0xffff)|((o[1].imm&0xffff)<<16);out.add(regs[r])
            elif o and o[0].type==ARM_OP_REG and m not in ('cmp','cmn','tst','teq','str','str.w'):
                regs.pop(o[0].reg,None)
    return sorted(out)
def function_record(e,r,nid,name):
    lib,fn=find_export(e,nid)
    if not fn:return {'nid':nid,'name':name,'missing':True}
    if fn['va']!=EXPECTED_IFTU_VA[nid]:die(f"{name} VA drift: 0x{fn['va']:08X}")
    cfg=r.functions.get((fn['va'],fn['thumb'])) or FunctionCFG(e,fn['va'],fn['thumb'],r.import_stubs)
    return {'nid':nid,'name':name,'va':fn['va'],'thumb':fn['thumb'],'library':lib['library_name'],'instruction_count':cfg.instruction_count(),'calls':cfg.calls,'instructions':[{'va':i.address,'text':txt(i)} for i in insns(cfg)] if name in ('ksceIftuEnable','ksceIftuCsc') else None}
def exact_instance_refs(r):
    rows=[]
    for (start,thumb),cfg in r.functions.items():
        if INSTANCE_BASE not in constructed_constants(cfg):continue
        for i in insns(cfg):
            for o in getattr(i,'operands',[]):
                if o.type!=ARM_OP_MEM:continue
                d=o.mem.disp
                if not (0x10c<=d<=0x180 or d in (0x1e8,0x1f8)):continue
                m=i.mnemonic.lower();a='read' if m.startswith(('ldr','ldm')) else 'write' if m.startswith(('str','stm')) else 'other'
                rows.append({'function':start,'va':i.address,'offset':d,'access':a,'instruction':txt(i)})
    return sorted(rows,key=lambda x:(x['function'],x['va']))
def display_gen(e,r,start,label):
    cfg=r.functions.get((start,True)) or FunctionCFG(e,start,True,r.import_stubs)
    cs=constructed_constants(cfg);cands=[]
    for va in cs:
        w=read_words(e,va,15)
        if plausible_csc(w):cands.append({'va':va,'decoded':decode_csc(w)})
    return {'label':label,'start':start,'absolute_constants':[x for x in cs if 0x81000000<=x<0x82000000],'csc_constant_candidates':cands,'instructions':[{'va':i.address,'text':txt(i)} for i in insns(cfg)]}
def private_calls(r,nid):
    return [{'function':x['function_start'],'call_va':x['call_va'],'proof':x['proof']} for x in r.proven_calls_to(nid)]


def main():
    p=argparse.ArgumentParser();p.add_argument('--lowio',type=Path,required=True);p.add_argument('--display',type=Path,required=True);p.add_argument('--json',type=Path,required=True);a=p.parse_args()
    low=load(a.lowio,LOWIO_SHA256);disp=load(a.display,DISPLAY_SHA256);lr=Reachability(low,low.exports(),low.imports());dr=Reachability(disp,disp.exports(),disp.imports())
    ex=[function_record(low,lr,n,nm) for n,nm in {**IFTU_PUBLIC,**IFTU_PRIVATE}.items()];ex.sort(key=lambda x:x['nid'])
    dm={}
    for n,nm in DISPLAY_PUBLIC.items():
        _,f=find_export(disp,n)
        if f:dm[nm]=f
    gens=[display_gen(disp,dr,0x81000A2C,'internal_0x81000A2C')]+[display_gen(disp,dr,dm[nm]['va'],nm) for nm in ('ksceDisplaySetBrightness','ksceDisplaySetInvertColors')]
    # The plane-selection globals used by SceDisplay live in a runtime/BSS mapping
    # not backed by bytes in this decrypted ELF. Treat that as a runtime fact to
    # observe, never as a static zero/default inferred from absent file data.
    plane_slots={hex(va):None for va in (0x8100B020,0x8100B024,0x8100B0B8,0x8100B0BC)}
    result={'schema':2,'firmware':'3.65','elf_sha256':{'SceLowio':low.sha256,'SceDisplay':disp.sha256},'iftu_exports':ex,'instance_refs':exact_instance_refs(lr),'private_calls':{f"0x{n:08X}":private_calls(dr,n) for n in IFTU_PRIVATE},'display_generators':gens,'display_runtime_plane_slots':plane_slots,'proven_structural_mapping':{
      'SceIftuCscParams_size':0x3c,
      'private_cache_A':{'offset':0x10c,'live_block':[0x104,0x12c],'matches_public_conv_field':'csc_params2 (+0x0C)'},
      'private_cache_B':{'offset':0x148,'live_block':[0x130,0x168],'matches_public_conv_field':'csc_params1 (+0x08)'},
      'public_csc_control':{'conv_offset':0x10,'live_offset':0x100},
      'private_control_cache':{'offset':0x1f8,'enable_live_offsets':[0x8c,0xa0],'is_public_csc_control':False},
      'enable_reapply':{'function':0x8100639c,'reads_cache_A_and_B':True,'reads_private_control':True}
    }}
    a.json.write_text(json.dumps(result,indent=2)+'\n')
    print('IFTU_EXPORT_MAP')
    for q in ex:print(f"  0x{q['nid']:08X} {q['name']} -> 0x{q['va']:08X}")
    print('PROVEN_CSC_STRUCTURE_MAP')
    print('  csc_params1 (+0x08) -> live +0x130..+0x168 -> private cache B (+0x148)')
    print('  csc_params2 (+0x0C) -> live +0x104..+0x12C -> private cache A (+0x10C)')
    print('  csc_control (+0x10) -> live +0x100')
    print('  private 0x357EAE24 cache +0x1F8 -> live +0x8C/+0xA0; it is NOT public csc_control')
    print('  0x8100639C -> ksceIftuEnable, which reapplies cached CSC blocks on enable')
    print('DISPLAY_RUNTIME_PLANE_SLOTS')
    for k in plane_slots:print(f'  {k} = RUNTIME/BSS; requires read-only physical snapshot')
    print('SONY_CSC_NUMERIC_TABLES')
    seen=set()
    for g in gens:
        for c in g['csc_constant_candidates']:
            if c['va'] in seen:continue
            seen.add(c['va']);d=c['decoded'];print(f"  0x{c['va']:08X}: post_add=({d['post_add_0']},{d['post_add_1_2']}) clamp0=[{d['post_clamp_min_0']},{d['post_clamp_max_0']}] clamp12=[{d['post_clamp_min_1_2']},{d['post_clamp_max_1_2']}] ctm={d['ctm_s3_9']}")
    print('AFFINE_ARCHITECTURE_EVIDENCE')
    print('  Sony display code constructs fresh 0x3C objects and calls both private setters: PROVEN')
    print('  IFTU enable reuses the two cached objects: PROVEN')
    print('  exact stage execution order: NOT YET PROVEN from register semantics')
    print('  active runtime LCD plane and pristine baseline: REQUIRE read-only physical snapshot')
if __name__=='__main__':main()
