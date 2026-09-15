#!/usr/bin/env python3
from __future__ import annotations
import argparse,json,struct,hashlib
from pathlib import Path
from collections import defaultdict
from vita_elf_audit import VitaElf,Reachability
from topology_common import all_insns

DISPLAY_SHA='83ef39adf741a4bbbc1cec67d00e2594de1d17be0f5ea6cc77c935eae46065e5'
NIDS={'A':0x0FCBF457,'B':0xD64F4C6B,'ENABLE':0x0D7C02F7}
SEQUENCE_OWNER=0x81000C2C
GENERATOR=0x81000A2C
B_CALL=0x81000AAA
A_CALL=0x81000AF2
ENABLE_CALLS=(0x81000CC0,0x81000CCA)
TABLE_VAS=(0x81009724,0x8100977C,0x810097D0,0x8100980C,0x81009970,0x810099AC)
A_WORDS=[0,0x202,0x3ff,0,0,0,0,0,0,0,0,0,0,0,0]
B_WORDS=[0,0,0x3ff,0,0x3ff,0,0x200,0,0,0,0x200,0,0,0,0x200]

def pack(words):return struct.pack('<15I',*words)
def import_stub_by_nid(imports,nid):
    for lib in imports:
        for f in lib['functions']:
            if f['nid']==nid:return {'library':lib['library_name'],'library_nid':lib['library_nid'],'stub_va':f['va'],'thumb':f['thumb']}
    return None

def read60(e,va):
    _,off=e.file_from_va(va);b=e.data[off:off+60]
    if len(b)!=60:raise SystemExit(f'short table at 0x{va:08X}')
    return {'va':va,'sha256':hashlib.sha256(b).hexdigest(),'words':[f'0x{x:08X}' for x in struct.unpack('<15I',b)]}

def require(c,msg):
    if not c:raise SystemExit('PRODUCER_CONTRACT=FAIL '+msg)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--display',type=Path,required=True);ap.add_argument('--json',type=Path,required=True);a=ap.parse_args()
    e=VitaElf(a.display);require(e.sha256==DISPLAY_SHA,f'SceDisplay hash drift {e.sha256}')
    imports=e.imports();exports=e.exports();r=Reachability(e,exports,imports)
    stubs={k:import_stub_by_nid(imports,n) for k,n in NIDS.items()};require(all(stubs.values()),f'missing imports {stubs}')
    calls={k:r.proven_calls_to(v['stub_va']) for k,v in stubs.items()}
    byfn=defaultdict(list)
    for kind,rows in calls.items():
        for x in rows:byfn[x['function_start']].append((x['call_va'],kind))
    producers=[];insmaps={}
    for fn,cr in sorted(byfn.items()):
        cfg=next(c for (s,t),c in r.functions.items() if s==fn)
        ins=[{'va':i.address,'text':f'{i.mnemonic} {i.op_str}'.strip()} for i in all_insns(cfg)]
        insmaps[fn]={x['va']:x['text'] for x in ins}
        producers.append({'function_start':fn,'logical_end':cfg.logical_end,'boundary_sources':cfg.boundary_sources,'ordered_target_calls':[{'call_va':va,'kind':kind} for va,kind in sorted(cr)],'instructions':ins})
    require(GENERATOR in insmaps,'generator absent');require(SEQUENCE_OWNER in insmaps,'sequence owner absent')
    g=insmaps[GENERATOR];o=insmaps[SEQUENCE_OWNER]
    def ins(m,va,text):require(m.get(va)==text,f'0x{va:08X}: expected {text!r}, got {m.get(va)!r}')
    ins(g,0x81000AA8,'mov r1, sp');ins(g,B_CALL,'blx #0x81003a54')
    ins(g,0x81000AB2,'mov r5, fp');ins(g,0x81000AB4,'mov r4, sp');ins(g,0x81000AB8,'stm r4!, {r0, r1, r2, r3}');ins(g,0x81000ABC,'stm r4!, {r0, r1, r2, r3}');ins(g,0x81000AC0,'stm r4!, {r0, r1, r2, r3}');ins(g,0x81000AC6,'stm.w r4, {r0, r1, r2}');ins(g,0x81000AF0,'mov r1, sp');ins(g,A_CALL,'blx #0x810039f4')
    require(any(x['function_start']==GENERATOR and x['call_va']==B_CALL for x in calls['B']),'B generator callsite');require(any(x['function_start']==GENERATOR and x['call_va']==A_CALL for x in calls['A']),'A generator callsite')
    ins(o,0x81000C90,'mov sb, r4');ins(o,0x81000C92,'add.w fp, r4, #8');ins(o,0x81000C96,'ldr.w sl, [sb, #0x20]');ins(o,0x81000C9A,'add.w sb, sb, #4');ins(o,0x81000CB0,'mov r1, sl');ins(o,0x81000CB6,'bl #0x81000a2c');ins(o,0x81000CBA,'cmp sb, fp');ins(o,0x81000CBC,'bne #0x81000c96');ins(o,0x81000CBE,'ldr r0, [r4, #0x20]');ins(o,ENABLE_CALLS[0],'blx #0x810039e4');ins(o,0x81000CC8,'ldr r0, [r4, #0x24]');ins(o,ENABLE_CALLS[1],'blx #0x810039e4')
    require([x['call_va'] for x in calls['ENABLE'] if x['function_start']==SEQUENCE_OWNER]==list(ENABLE_CALLS),'enable callsites')
    physical={'A':{'sha256':hashlib.sha256(pack(A_WORDS)).hexdigest(),'words':[f'0x{x:08X}' for x in A_WORDS]},'B':{'sha256':hashlib.sha256(pack(B_WORDS)).hexdigest(),'words':[f'0x{x:08X}' for x in B_WORDS]}}
    tables=[read60(e,va) for va in TABLE_VAS]
    for t in tables:t['physical_match']='B' if t['sha256']==physical['B']['sha256'] else ('A' if t['sha256']==physical['A']['sha256'] else 'NONE')
    bvas=[t['va'] for t in tables if t['physical_match']=='B'];require(bvas==[0x81009724,0x810099AC],f'B table provenance {bvas}');require(not any(t['physical_match']=='A' for t in tables),'A unexpectedly raw-static; expected reconstructed object')
    contract={'generator':{'status':'MACHINE_ASSERTED','va':'0x81000A2C','B_callsite':'0x81000AAA','A_callsite':'0x81000AF2','B_argument':'sp stack/local','A_argument':'same sp stack/local after 60-byte overwrite/reconstruction'},'sequence_owner':{'status':'MACHINE_ASSERTED','va':'0x81000C2C','plane_iteration':'two 4-byte slots at state offsets +0x20/+0x24','generator_call':'0x81000CB6','enable_calls':['0x81000CC0','0x81000CCA'],'enable_after_generation':True},'physical_correlation':{'status':'MACHINE_ASSERTED','B_raw_static_vas':['0x81009724','0x810099AC'],'A_raw_static_blob':False}}
    out={'schema':3,'display_sha256':e.sha256,'target_imports':stubs,'target_calls':calls,'producer_contract':contract,'producer_candidates':producers,'known_source_tables':tables,'physical_objects':physical}
    a.json.write_text(json.dumps(out,indent=2)+'\n')
    print('GATE1A_PRODUCER_CONTRACT=PASS');print('GENERATOR=0x81000A2C MACHINE_ASSERTED');print('B_CALLSITE=0x81000AAA MACHINE_ASSERTED');print('A_CALLSITE=0x81000AF2 MACHINE_ASSERTED');print('STACK_LOCAL_PROVENANCE=MACHINE_ASSERTED');print('SAME_LOGICAL_LOCAL_RECONSTRUCTED=MACHINE_ASSERTED');print('SEQUENCE_OWNER=0x81000C2C MACHINE_ASSERTED');print('TWO_PLANE_SLOTS=MACHINE_ASSERTED');print('ENABLE_AFTER_AB_GENERATION=MACHINE_ASSERTED');print('B_STATIC_PHYSICAL_CORRELATION=MACHINE_ASSERTED')
if __name__=='__main__':main()
