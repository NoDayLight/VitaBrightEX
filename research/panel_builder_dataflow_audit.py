#!/usr/bin/env python3
from __future__ import annotations
import argparse,json
from pathlib import Path
from vita_elf_audit import VitaElf,Reachability,FunctionCFG,PT_LOAD
from topology_common import all_insns,ins_text

LCD_SHA='24752bb4c69f0c241701cab0364cde9cbe5c95128b6bfe02bf3263aad524246e'
BUILDER=0x81000B78; STATE=0x810020A0; WORK=0x81002020; SPLIT=0x29; DELAY=0x0D
CANDIDATES=(
 {'id':'selector_le_0x24','condition':'state+0x0C <= 0x24','root_va':0x81001AF8,'provenance_vas':[0x81000B84,0x81000B86,0x81000B8A,0x81000B8E,0x81000B92]},
 {'id':'selector_0x25_to_0x31','condition':'0x24 < state+0x0C <= 0x31','root_va':0x81001AC8,'provenance_vas':[0x81000B94,0x81000B98,0x81000B9C,0x81000BA0,0x81000BA4,0x81000BA8]},
 {'id':'selector_gt_0x31','condition':'state+0x0C > 0x31','root_va':0x81001B90,'provenance_vas':[0x81000B94,0x81000B9C,0x81000BA4,0x81000BA8]},)
EXPECTED={
 0x81000B7C:'movw r7, #0x20a0',0x81000B80:'movt r7, #0x8100',0x81000B84:'ldrh r3, [r7, #0xc]',
 0x81000B86:'cmp r3, #0x24',0x81000B8A:'movw r5, #0x1af8',0x81000B8E:'movt r5, #0x8100',
 0x81000B92:'bls #0x81000baa',0x81000B94:'movw r5, #0x1b90',0x81000B98:'movw r2, #0x1ac8',
 0x81000B9C:'movt r5, #0x8100',0x81000BA0:'movt r2, #0x8100',0x81000BA4:'cmp r3, #0x31',
 0x81000BA8:'mov r5, r2',0x81000BAA:'movw r4, #0x2020',0x81000BAE:'movt r4, #0x8100',
 0x81000BB2:'ldrb r3, [r5]',0x81000BB4:'cmp r3, #0x29',0x81000BB8:'adds r5, #2',
 0x81000BBA:'ldrb r6, [r5, #-0x1]',0x81000BBE:'cmp r3, #0xd',0x81000BC0:'strb r3, [r4], #2',
 0x81000BCA:'strb r6, [r4, #-0x1]',0x81000BCE:'beq #0x81000bb2',0x81000BD0:'cmp r6, #0',
 0x81000BDA:'add r5, r6',0x81000BDC:'add r4, r6',0x81000BDE:'blx #0x81001468',
 0x81000BE2:'ldrb r3, [r5]',0x81000BE4:'cmp r3, #0x29',0x81000BE8:'ldr r6, [r7, #0x2c]',
 0x81000C4E:'ldr r6, [r7, #0x28]',0x81000CAC:'adds r3, r5, #2'}
ROLES={0x81000B84:'bucket_selector_load',0x81000BAA:'working_buffer_pointer',0x81000BB2:'first_loop_command_load',
 0x81000BB4:'compare_0x29_before_copy',0x81000BB8:'source_plus_header',0x81000BBA:'length_load',
 0x81000BBE:'delay_test',0x81000BDA:'source_plus_payload',0x81000BDC:'dest_plus_payload',0x81000BDE:'payload_copy',
 0x81000BE8:'color_space_selector',0x81000C4E:'secondary_selector',0x81000CAC:'suffix_resumes_first_loop_source'}

def cfg_for(e):
 r=Reachability(e,e.exports(),e.imports(),extra_starts=(BUILDER,)); c=next((x for x in r.functions.values() if x.start==BUILDER),None)
 return c or FunctionCFG(e,BUILDER,True,r.import_stubs,boundary_index=r.boundaries,noreturn_stubs=r.noreturn_stubs)

def assert_exact(cfg):
 got={i.address:ins_text(i) for i in all_insns(cfg)}; bad=[]
 for va,want in EXPECTED.items():
  if got.get(va)!=want: bad.append({'va':va,'expected':want,'actual':got.get(va)})
 if bad: raise SystemExit('builder disassembly invariant failure: '+json.dumps(bad))
 return got

def classify(e,va):
 loads=[p for p in e.phdrs if p.p_type==PT_LOAD]
 for p in loads:
  fe=p.p_vaddr+p.p_filesz; me=p.p_vaddr+p.p_memsz
  if p.p_vaddr<=va<me:
   return {'relation':'FILE_BACKED_PT_LOAD' if va<fe else 'ZERO_FILL_OR_RUNTIME_WRITABLE_PT_LOAD','segment_index':p.index,'p_vaddr':p.p_vaddr,'p_filesz':p.p_filesz,'p_memsz':p.p_memsz,'file_end':fe,'mem_end':me,'offset':va-p.p_vaddr}
 prev=next((p for p in reversed(sorted(loads,key=lambda x:x.p_vaddr)) if p.p_vaddr+p.p_memsz<=va),None)
 nxt=next((p for p in sorted(loads,key=lambda x:x.p_vaddr) if p.p_vaddr>va),None)
 return {'relation':'PT_LOAD_GAP','segment_index':None,
  'previous':None if prev is None else {'segment_index':prev.index,'p_vaddr':prev.p_vaddr,'p_filesz':prev.p_filesz,'p_memsz':prev.p_memsz,'mem_end':prev.p_vaddr+prev.p_memsz,'distance_after_mem_end':va-(prev.p_vaddr+prev.p_memsz)},
  'next':None if nxt is None else {'segment_index':nxt.index,'p_vaddr':nxt.p_vaddr,'p_filesz':nxt.p_filesz,'p_memsz':nxt.p_memsz,'distance_before_vaddr':nxt.p_vaddr-va}}

def u8(e,va): _,o=e.file_from_va(va); return e.data[o]

def walk(e,root):
 va=root; headers=[]
 for n in range(1024):
  try: cmd=u8(e,va)
  except ValueError: return {'status':'SOURCE_LEFT_FILE_BACKED_STORAGE','stop_va':va,'boundary':classify(e,va),'header_count':len(headers),'headers':headers}
  if cmd==SPLIT:return {'status':'REACHED_SPLIT','split_va':va,'header_count':len(headers),'headers':headers}
  try: ln=u8(e,va+1)
  except ValueError:return {'status':'LENGTH_LEFT_FILE_BACKED_STORAGE','stop_va':va+1,'boundary':classify(e,va+1),'header_count':len(headers),'headers':headers}
  nv=va+2 if cmd==DELAY or ln==0 else va+2+ln
  headers.append({'index':n,'source_va':va,'command':cmd,'length':ln,'step':nv-va,'delay_header_only':cmd==DELAY})
  if nv<=va:return {'status':'NON_PROGRESSING','stop_va':va,'header_count':len(headers),'headers':headers}
  if cmd!=DELAY and ln:
   try:e.file_from_va(nv-1)
   except ValueError:return {'status':'PAYLOAD_LEFT_FILE_BACKED_STORAGE','stop_va':nv-1,'boundary':classify(e,nv-1),'header_count':len(headers),'headers':headers}
  va=nv
 return {'status':'GUARD_EXHAUSTED','stop_va':va,'header_count':len(headers),'headers':headers}

def classify_source(w):
 if w['status']=='REACHED_SPLIT':return 'STATIC_SOURCE','Exact first-loop stepping reaches 0x29 using file-backed bytes.'
 rel=w.get('boundary',{}).get('relation')
 if rel=='ZERO_FILL_OR_RUNTIME_WRITABLE_PT_LOAD':return 'RUNTIME_POPULATED_SOURCE','Exact stepping enters non-file-backed PT_LOAD memory; a bounded runtime producer proof is required.'
 if rel=='PT_LOAD_GAP':return 'UNRESOLVED','Exact assembly proves this direct root and stepping, but the candidate leaves every PT_LOAD before 0x29; Gate 0 must establish physical reachability before declaring the root or grammar wrong.'
 return 'UNRESOLVED','Static evidence does not close this candidate.'

def analyze_builder(e):
 if e.sha256!=LCD_SHA:raise SystemExit(f'SceLcd hash drift {e.sha256}')
 cfg=cfg_for(e); assert_exact(cfg); candidates=[]
 for s in CANDIDATES:
  w=walk(e,s['root_va']); kind,reason=classify_source(w)
  candidates.append({**s,'root_provenance':'DIRECT_IMMEDIATE_POINTER_CONSTRUCTED_IN_BUILDER','root_memory':classify(e,s['root_va']),'walk':w,'source_classification':kind,'classification_reason':reason,
   'producer_search':'NOT_APPLICABLE_PT_LOAD_GAP' if w.get('boundary',{}).get('relation')=='PT_LOAD_GAP' else ('NOT_APPLICABLE_STATIC_SOURCE' if kind=='STATIC_SOURCE' else 'REQUIRED_IF_RUNTIME_BACKED')})
 all_static=all(c['source_classification']=='STATIC_SOURCE' for c in candidates)
 return {'schema':1,'firmware':'3.65','elf_sha256':e.sha256,'builder_va':BUILDER,'builder_logical_end':cfg.logical_end,
  'semantic_result':'PROVEN_STATIC_RECONSTRUCTION_INPUTS' if all_static else 'INCOMPLETE_STATIC_RECONSTRUCTION_INPUTS','physical_branch':'UNKNOWN_UNTIL_GATE_0',
  'inputs':{'arguments':'No incoming argument controls selection before builder-owned register initialization; selectors are SceLcd state fields.','state_base':STATE,'bucket_selector':{'offset':0x0C,'width':2},'color_space_selector':{'offset':0x2C,'width':4,'mode0_root':0x81001AE8,'nonzero_root':0x81001B70},'secondary_selector':{'offset':0x28,'width':4,'zero_root':0x81001B80,'nonzero_root':0x81001B20},'working_buffer':WORK},
  'first_loop_semantics':{'command_load_va':0x81000BB2,'split_compare_va':0x81000BB4,'split_value':SPLIT,'split_compared_before_copy':True,'length_load_va':0x81000BBA,'source_header_increment':2,'delay_command':DELAY,'delay_semantics':'header copied; no payload copy; source/destination advance only by 2','length_transform':'subs 1; uxtb; adds 1 -> original uint8 length','source_step':'2 for 0x0D or zero length; otherwise 2+length','destination_step':'same record bytes copied to working buffer','ff_special_in_first_loop':False,'source_replaced_inside_first_loop':False,'payload_copy_target':'0x81001468'},
  'candidate_sources':candidates,'instructions':[{'va':i.address,'text':ins_text(i),'role':ROLES.get(i.address)} for i in all_insns(cfg)],
  'artifact_policy':'No proprietary payload bytes emitted; only instruction text, pointers, command/length metadata, and memory-layout classifications.'}

def print_summary(r):
 print('PANEL_BUILDER_DATAFLOW');print(f"  function=0x{BUILDER:08X}..0x{r['builder_logical_end']:08X}");print('  first_loop: stop=0x29 only; 0xFF has no special meaning; step=2 or 2+length')
 for c in r['candidate_sources']:
  w=c['walk']; stop=w.get('split_va',w.get('stop_va')); print(f"  {c['id']} root=0x{c['root_va']:08X} walk={w['status']} stop={('none' if stop is None else f'0x{stop:08X}')} classification={c['source_classification']}")
  if 'boundary' in w:print(f"    boundary={w['boundary']['relation']}")
 print(f"  semantic_result={r['semantic_result']}");print('  physical_branch=UNKNOWN_UNTIL_GATE_0')

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--lcd',type=Path,required=True);ap.add_argument('--json',type=Path,required=True);a=ap.parse_args();r=analyze_builder(VitaElf(a.lcd));a.json.write_text(json.dumps(r,indent=2)+'\n');print_summary(r)
if __name__=='__main__':main()
