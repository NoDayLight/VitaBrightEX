#!/usr/bin/env python3
from __future__ import annotations
import argparse,json
from pathlib import Path
from vita_elf_audit import VitaElf,Reachability
from topology_common import all_insns,ins_text,load_nid_names,import_stub_map,call_semantics,function_parents,absolute_constants

MODULES=('enum_wakeup.elf','sysstatemgr.elf')
RELEVANT_WORDS=('Display','Lcd','RegMgr','SysState','Power','Suspend','Resume','Wake','SysEvent','Iftu')

def module_record(path,names):
 e=VitaElf(path);ex=e.exports();im=e.imports();r=Reachability(e,ex,im);stubs=import_stub_map(e,names);parents=function_parents(r)
 imports=[]
 for lib in im:
  fs=[]
  for f in lib['functions']:
   n=names.get(f['nid'],[None])[0]
   if any(w.lower() in ((n or '')+' '+lib['library_name']).lower() for w in RELEVANT_WORDS):fs.append({'nid':f['nid'],'name':n,'stub_va':f['va']})
  if fs:imports.append({'library':lib['library_name'],'library_nid':lib['library_nid'],'functions':fs})
 targets={f['stub_va']:(lib['library'],f) for lib in imports for f in lib['functions']}
 funcs=[]
 for key,cfg in r.functions.items():
  calls=call_semantics(cfg,stubs);rel=[c for c in calls if c['target'] in targets]
  if rel:
   m=cfg.compact(False);funcs.append({'start':cfg.start,'logical_end':cfg.logical_end,'parents':parents.get(key,[]),'termination_reason':m['termination_reason'],'relevant_calls':rel,'all_calls':calls,'absolute_constants':absolute_constants(e,cfg),'instructions':[{'va':i.address,'text':ins_text(i)} for i in all_insns(cfg)]})
 return {'file':path.name,'module':e.modinfo['name'],'sha256':e.sha256,'imports':imports,'relevant_functions':sorted(funcs,key=lambda x:x['start'])}

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--elf-dir',type=Path,required=True);ap.add_argument('--nid-db-root',type=Path,required=True);ap.add_argument('--json',type=Path,required=True);a=ap.parse_args()
 names=load_nid_names(a.nid_db_root);mods=[]
 for n in MODULES:
  p=a.elf_dir/n
  if not p.exists():raise SystemExit(f'missing {n}')
  mods.append(module_record(p,names))
 result={'schema':1,'firmware':'3.65','modules':mods,'scope':'Targeted suspend/resume dependency edge only; no broad kernel rescan.'}
 a.json.write_text(json.dumps(result,indent=2)+'\n')
 print('DISPLAY_LIFECYCLE')
 for m in mods:
  print(f"  {m['file']} module={m['module']}")
  for lib in m['imports']:print(f"    import {lib['library']} {[(x['name'],hex(x['nid'])) for x in lib['functions']]}")
  for f in m['relevant_functions']:
   print(f"    fn=0x{f['start']:08X}..0x{f['logical_end']:08X} calls={[(c.get('name'),hex(c.get('nid',0)),hex(c['call_va'])) for c in f['relevant_calls']]}")

if __name__=='__main__':main()
