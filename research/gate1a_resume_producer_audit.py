#!/usr/bin/env python3
from __future__ import annotations
import argparse,json,struct,hashlib
from pathlib import Path
from collections import defaultdict
from vita_elf_audit import VitaElf,Reachability
from topology_common import all_insns

# Gate-1A: intentionally scoped to the physically observed SceDisplay A/B/enable boundary.
DISPLAY_SHA='83ef39adf741a4bbbc1cec67d00e2594de1d17be0f5ea6cc77c935eae46065e5'
NIDS={'A':0x0FCBF457,'B':0xD64F4C6B,'ENABLE':0x0D7C02F7}
SEQUENCE_OWNER=0x81000C2C
GENERATOR=0x81000A2C
TABLE_VAS=(0x81009724,0x8100977C,0x810097D0,0x8100980C,0x81009970,0x810099AC)
A_WORDS=[0,0x202,0x3ff,0,0,0,0,0,0,0,0,0,0,0,0]
B_WORDS=[0,0,0x3ff,0,0x3ff,0,0x200,0,0,0,0x200,0,0,0,0x200]

def pack(words): return struct.pack('<15I',*words)
def import_stub_by_nid(imports,nid):
 for lib in imports:
  for f in lib['functions']:
   if f['nid']==nid:return {'library':lib['library_name'],'library_nid':lib['library_nid'],'stub_va':f['va'],'thumb':f['thumb']}
 return None

def read60(e,va):
 _,off=e.file_from_va(va); b=e.data[off:off+60]
 if len(b)!=60:raise SystemExit(f'short table at 0x{va:08X}')
 w=list(struct.unpack('<15I',b));return {'va':va,'sha256':hashlib.sha256(b).hexdigest(),'words':[f'0x{x:08X}' for x in w]}

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--display',type=Path,required=True);ap.add_argument('--json',type=Path,required=True);a=ap.parse_args()
 e=VitaElf(a.display)
 if e.sha256!=DISPLAY_SHA:raise SystemExit(f'SceDisplay hash drift {e.sha256}')
 imports=e.imports(); exports=e.exports(); r=Reachability(e,exports,imports)
 stubs={k:import_stub_by_nid(imports,n) for k,n in NIDS.items()}
 if any(v is None for v in stubs.values()):raise SystemExit(f'missing target imports {stubs}')
 calls={k:r.proven_calls_to(v['stub_va']) for k,v in stubs.items()}
 byfn=defaultdict(list)
 for kind,rows in calls.items():
  for x in rows:byfn[x['function_start']].append((x['call_va'],kind))
 producers=[]
 for fn,cr in sorted(byfn.items()):
  cfg=next(c for (s,t),c in r.functions.items() if s==fn)
  ins=[{'va':i.address,'text':f'{i.mnemonic} {i.op_str}'.strip()} for i in all_insns(cfg)]
  ordered=[{'call_va':va,'kind':kind} for va,kind in sorted(cr)]
  producers.append({'function_start':fn,'logical_end':cfg.logical_end,'boundary_sources':cfg.boundary_sources,'ordered_target_calls':ordered,'instructions':ins})
 def raw_hits(blob):
  out=[];start=0
  while True:
   p=e.data.find(blob,start)
   if p<0:return out
   vas=[]
   for ph in e.phdrs:
    if ph.p_offset<=p and p+len(blob)<=ph.p_offset+ph.p_filesz:vas.append(ph.p_vaddr+(p-ph.p_offset))
   out.append({'file_offset':p,'vas':vas});start=p+1
 physical={'A':{'sha256':hashlib.sha256(pack(A_WORDS)).hexdigest(),'words':[f'0x{x:08X}' for x in A_WORDS],'raw_blob_hits':raw_hits(pack(A_WORDS))},'B':{'sha256':hashlib.sha256(pack(B_WORDS)).hexdigest(),'words':[f'0x{x:08X}' for x in B_WORDS],'raw_blob_hits':raw_hits(pack(B_WORDS))}}
 tables=[read60(e,va) for va in TABLE_VAS]
 for t in tables:
  t['physical_match']='B' if t['sha256']==physical['B']['sha256'] else ('A' if t['sha256']==physical['A']['sha256'] else 'NONE')
 out={'schema':2,'display_sha256':e.sha256,'target_imports':stubs,'target_calls':calls,'producer_candidates':producers,'sequence_owner_callers':r.proven_calls_to(SEQUENCE_OWNER),'generator_callers':r.proven_calls_to(GENERATOR),'known_source_tables':tables,'physical_objects':physical}
 a.json.write_text(json.dumps(out,indent=2)+'\n')
 print('GATE1A_RESUME_PRODUCER_AUDIT')
 for k,v in stubs.items():print(f'{k}_STUB=0x{v["stub_va"]:08X} lib={v["library"]}')
 for k,v in calls.items():
  print(f'{k}_CALLS={len(v)}')
  for x in v:print(f'  fn=0x{x["function_start"]:08X} call=0x{x["call_va"]:08X}')
 print('FUNCTIONS_WITH_TARGET_CALLS')
 for p in producers:print(f'  fn=0x{p["function_start"]:08X} order={[x["kind"] for x in p["ordered_target_calls"]]} calls={[hex(x["call_va"]) for x in p["ordered_target_calls"]]}')
 print(f'SEQUENCE_OWNER=0x{SEQUENCE_OWNER:08X} CALLERS={[(hex(x["function_start"]),hex(x["call_va"])) for x in out["sequence_owner_callers"]]}')
 print(f'GENERATOR=0x{GENERATOR:08X} CALLERS={[(hex(x["function_start"]),hex(x["call_va"])) for x in out["generator_callers"]]}')
 for t in tables:print(f'TABLE 0x{t["va"]:08X} sha={t["sha256"]} physical_match={t["physical_match"]} words={t["words"]}')
 for k,v in physical.items():print(f'{k}_PHYSICAL_SHA={v["sha256"]} raw_blob_hits={v["raw_blob_hits"]}')

if __name__=='__main__':main()
