#!/usr/bin/env python3
from __future__ import annotations
import argparse,json
from collections import defaultdict
from pathlib import Path
from vita_elf_audit import VitaElf,Reachability,FunctionCFG
from topology_common import all_insns,ins_text,load_nid_names,best_name,import_stub_map,call_semantics,relevant_abs,function_parents

DISPLAY_SHA='83ef39adf741a4bbbc1cec67d00e2594de1d17be0f5ea6cc77c935eae46065e5'
LOWIO_SHA='f791cfe2c6db955deb9446c57bb72ce1870bde2c6c485cac363e343d5128f744'
IFTU_NIDS={0x0D7C02F7,0x0FCBF457,0x357EAE24,0x67E37EFC,0x7CE0C4DA,0xAF19FD85,0xC11F30B3,0xD64F4C6B,0xE6EE2C6B}
FOCUS_INTERNALS={
    0x81000A2C:'display_csc_builder',
    0x81007228:'color_state_helper_7228',
    0x8100724C:'color_state_helper_724c',
    0x810073C4:'brightness_state_helper_73c4',
    0x810073E8:'brightness_state_helper_73e8',
    0x81007410:'color_state_helper_7410',
}
FOCUS_EXPORTS={0x8D79D187:'ksceDisplaySetColorSpaceMode',0x9E3C6DC6:'ksceDisplaySetBrightness',0x19140ACD:'ksceDisplaySetInvertColors'}

def category(name,iftu_calls):
    n=(name or '').lower()
    if 'brightness' in n:return 'brightness'
    if any(x in n for x in ('colorspace','color_space','invertcolor','invert_colors')) or 'color' in n:return 'color/range'
    if any(x in n for x in ('framebuf','capture')):return 'framebuffer commit/capture'
    if any(x in n for x in ('enablehead','disablehead','owner','outputmode','viewport','scaleconf','mergeconf')):return 'display/head lifecycle'
    if 'vblank' in n:return 'vblank'
    if n.startswith('kscedisplayget'):return 'state getter'
    if n.startswith('kscedisplayset'):return 'state setter'
    if iftu_calls:return 'unknown display-pipeline relevant (IFTU)'
    return 'unknown/other'

def record_cfg(disp,reach,stubs,parents,va,label):
    cfg=None;key=None
    for k,c in reach.functions.items():
        if c.start==va:cfg=c;key=k;break
    if cfg is None:
        cfg=FunctionCFG(disp,va,True,reach.import_stubs);key=(va,True)
    return {'label':label,'start':cfg.start,'instruction_count':cfg.instruction_count(),'parents':parents.get(key,[]),'calls':call_semantics(cfg,stubs),'absolute_state_or_mmio_constants':relevant_abs(disp,cfg),'instructions':[{'va':i.address,'text':ins_text(i)} for i in all_insns(cfg)]}

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--display',type=Path,required=True);ap.add_argument('--lowio',type=Path,required=True);ap.add_argument('--nid-db-root',type=Path,required=True);ap.add_argument('--json',type=Path,required=True);a=ap.parse_args()
    disp=VitaElf(a.display);low=VitaElf(a.lowio)
    if disp.sha256!=DISPLAY_SHA:raise SystemExit(f'SceDisplay hash drift {disp.sha256}')
    if low.sha256!=LOWIO_SHA:raise SystemExit(f'SceLowio hash drift {low.sha256}')
    names=load_nid_names(a.nid_db_root);reach=Reachability(disp,disp.exports(),disp.imports());stubs=import_stub_map(disp,names);parents=function_parents(reach)
    libs=[x for x in disp.exports() if x['library_name']=='SceDisplayForDriver']
    if len(libs)!=1:raise SystemExit(f'SceDisplayForDriver libraries={len(libs)}')
    exports=[];export_by_nid={}
    for f in libs[0]['functions']:
        cfg=reach.functions.get((f['va'],f['thumb'])) or FunctionCFG(disp,f['va'],f['thumb'],reach.import_stubs)
        calls=call_semantics(cfg,stubs);iftu=[c for c in calls if c.get('nid') in IFTU_NIDS or c.get('library')=='SceIftuForDriver']
        name=best_name(names,f['nid'])
        row={'nid':f['nid'],'name':name,'va':f['va'],'thumb':f['thumb'],'category':category(name,iftu),'instruction_count':cfg.instruction_count(),'calls':calls,'iftu_calls':iftu,'absolute_state_or_mmio_constants':relevant_abs(disp,cfg)}
        exports.append(row);export_by_nid[f['nid']]=(f,cfg,row)
    intern=[]
    for key,cfg in sorted(reach.functions.items()):
        calls=call_semantics(cfg,stubs);iftu=[c for c in calls if c.get('nid') in IFTU_NIDS or c.get('library')=='SceIftuForDriver']
        if not iftu:continue
        intern.append({'start':cfg.start,'mode':'thumb' if cfg.thumb else 'arm','instruction_count':cfg.instruction_count(),'iftu_calls':iftu,'parents':parents.get(key,[]),'absolute_state_or_mmio_constants':relevant_abs(disp,cfg),'instructions':[{'va':i.address,'text':ins_text(i)} for i in all_insns(cfg)] if len(iftu)>=2 else None})
    dual=[]
    for x in intern:
        nids={c.get('nid') for c in x['iftu_calls']}
        if 0x0FCBF457 in nids and 0xD64F4C6B in nids:dual.append(x['start'])
    glob=defaultdict(set)
    for x in exports:
        for c in x['absolute_state_or_mmio_constants']:
            v=c['value']
            if 0x81000000<=v<0x81200000:glob[v].add(x['name'] or f"0x{x['nid']:08X}")
    shared=[{'address':k,'exports':sorted(v)} for k,v in sorted(glob.items()) if len(v)>=2]
    focus={label:record_cfg(disp,reach,stubs,parents,va,label) for va,label in FOCUS_INTERNALS.items()}
    focus_exports={}
    for nid,label in FOCUS_EXPORTS.items():
        f,cfg,row=export_by_nid[nid]
        focus_exports[label]={'nid':nid,'va':f['va'],'calls':row['calls'],'absolute_state_or_mmio_constants':row['absolute_state_or_mmio_constants'],'instructions':[{'va':i.address,'text':ins_text(i)} for i in all_insns(cfg)]}
    color=focus_exports['ksceDisplaySetColorSpaceMode']
    color_targets=[c['target'] for c in color['calls'] if c.get('internal')]
    result={'schema':2,'firmware':'3.65','elf_sha256':{'SceDisplay':disp.sha256,'SceLowio':low.sha256},'SceDisplayForDriver_library_nid':libs[0]['library_nid'],'export_count':len(exports),'exports':sorted(exports,key=lambda x:x['va']),'iftu_reachable_internal_functions':intern,'dual_private_csc_setter_internal_candidates':dual,'shared_display_state_constants':shared,'focus_exports':focus_exports,'focus_internal_functions':focus,'official_color_space_path':{'export_nid':0x8D79D187,'export_va':export_by_nid[0x8D79D187][0]['va'],'internal_targets':color_targets,'direct_iftu_calls':color['calls'] and [c for c in color['calls'] if c.get('library')=='SceIftuForDriver'] or [],'interpretation':'Official SceDisplay color-space state path; do not conflate with ksceLcdSetDisplayColorSpaceMode until the internal helper dataflow is correlated.'},'evidence_notes':['semantic names are from pinned vita-headers db/360 and retail 3.65 NIDs are taken from the ELF','unknown functions are not named speculatively','absolute addresses are discovery/state-clustering evidence, not automatically hardware registers']}
    a.json.write_text(json.dumps(result,indent=2)+'\n')
    print('DISPLAY_FOR_DRIVER_TOPOLOGY')
    print(f'  retail export count={len(exports)}')
    for x in sorted(exports,key=lambda x:x['va']):
        cs=','.join(c.get('name') or f"0x{c.get('nid',0):08X}" for c in x['iftu_calls']) or '-'
        print(f"  0x{x['nid']:08X} {x['name'] or 'UNKNOWN'} -> 0x{x['va']:08X} [{x['category']}] iftu={cs}")
    print('DISPLAY_CSC_BUILDER_CANDIDATES')
    for va in dual:print(f'  internal 0x{va:08X} calls both private CSC setters')
    print(f"OFFICIAL_DISPLAY_COLOR_SPACE_API 0x8D79D187 -> 0x{export_by_nid[0x8D79D187][0]['va']:08X} internal_targets={[hex(x) for x in color_targets]}")
    for label,x in focus.items():print(f"  FOCUS {label} 0x{x['start']:08X} calls={[(c.get('name'),hex(c['target'])) for c in x['calls']]}")

if __name__=='__main__':main()
