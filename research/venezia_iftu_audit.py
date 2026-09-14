#!/usr/bin/env python3
from __future__ import annotations
import argparse,json,struct
from pathlib import Path
from vita_elf_audit import VitaElf,Reachability
from topology_common import absolute_constants
TARGET=0xE5020000

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--elf',type=Path,required=True);ap.add_argument('--json',type=Path,required=True);a=ap.parse_args();e=VitaElf(a.elf);r=Reachability(e,e.exports(),e.imports())
 raw=[];start=0;needle=struct.pack('<I',TARGET)
 while True:
  p=e.data.find(needle,start)
  if p<0:break
  raw.append(p);start=p+1
 code=[]
 for c in r.functions.values():
  for x in absolute_constants(e,c):
   if x['value']==TARGET:code.append({'function':c.start,'va':x['va'],'instruction':x['instruction']})
 imports=[{'library':lib['library_name'],'library_nid':lib['library_nid'],'nids':[f['nid'] for f in lib['functions']]} for lib in e.imports()]
 displayish=[x for x in imports if x['library'] and any(k in x['library'].lower() for k in ('display','iftu','lowio','lcd'))]
 classification='LIVE_MMIO_USE' if code else ('DATA_ONLY_CONSTANT_NO_DISPLAY_RELATION' if raw and not displayish else 'UNRESOLVED_DATA_REFERENCE')
 production='CLOSED_AS_NON_PRODUCTION_RELEVANT' if classification=='DATA_ONLY_CONSTANT_NO_DISPLAY_RELATION' else 'REQUIRES_FURTHER_EVIDENCE'
 out={'schema':1,'module':e.modinfo['name'],'elf_sha256':e.sha256,'target':TARGET,'raw_word_file_offsets':raw,'reachable_code_constructions':code,'display_or_iftu_imports':displayish,'classification':classification,'production_relevance':production}
 a.json.write_text(json.dumps(out,indent=2)+'\n');print('SCE_VENEZIA_IMAGE_IFTU');print(f"  module={out['module']} raw_words={len(raw)} code_refs={len(code)} displayish_imports={len(displayish)}");print(f"  {classification} / {production}")
if __name__=='__main__':main()
