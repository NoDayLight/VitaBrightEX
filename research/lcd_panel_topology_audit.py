#!/usr/bin/env python3
from __future__ import annotations
import argparse,json
from pathlib import Path
from vita_elf_audit import VitaElf,Reachability,FunctionCFG
from topology_common import all_insns,ins_text,load_nid_names,import_stub_map,call_semantics,absolute_constants,function_parents

LCD_SHA='24752bb4c69f0c241701cab0364cde9cbe5c95128b6bfe02bf3263aad524246e'
READ_FN=0x810005B4
WRITE_FN=0x81000A54
STATE_BASE=0x81002000
COLOR_PROGRAMS=(0x81001AE8,0x81001B70)
SPI_NIDS={0xA85BF98A,0xDFD96BFC,0xE4B145AE,0x7B16F900}
GPIO_NIDS={0xF6310435,0x129DF5AC,0xD454A584,0x372022A4}
I2C_NIDS={0xCA94A759,0xD1D0A9A4,0x0A40B7BF}

def find_fn(reach,va):
 for (s,t),cfg in reach.functions.items():
  if s==va:return cfg
 return None

def call_windows(cfg,target,before=18,after=4):
 rows=[]
 for b in cfg.blocks.values():
  for idx,i in enumerate(b.instructions):
   for c in cfg.calls:
    if c['va']==i.address and c['target']==target:
     rows.append({'call_va':i.address,'function':cfg.start,'window':[{'va':x.address,'text':ins_text(x)} for x in b.instructions[max(0,idx-before):min(len(b.instructions),idx+after+1)]]})
 return rows

def state_writers(reach):
 rows=[]
 for key,cfg in reach.functions.items():
  hasbase=any(c['value']==STATE_BASE for c in absolute_constants(cfg.elf,cfg))
  if not hasbase:continue
  for i in all_insns(cfg):
   txt=ins_text(i).lower()
   if txt.startswith(('str','stm')) and any(x in txt for x in ('#8]','#0xa]','#0x2c]','#4]','#0x18]','#0x24]')):
    rows.append({'function':cfg.start,'va':i.address,'instruction':ins_text(i)})
 return rows

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--lcd',type=Path,required=True);ap.add_argument('--nid-db-root',type=Path,required=True);ap.add_argument('--json',type=Path,required=True);a=ap.parse_args()
 e=VitaElf(a.lcd)
 if e.sha256!=LCD_SHA:raise SystemExit(f'SceLcd hash drift {e.sha256}')
 names=load_nid_names(a.nid_db_root);reach=Reachability(e,e.exports(),e.imports());stubs=import_stub_map(e,names);parents=function_parents(reach)
 imports=[]
 for lib in e.imports():
  for f in lib['functions']:
   imports.append({'library':lib['library_name'],'library_nid':lib['library_nid'],'nid':f['nid'],'name':names.get(f['nid'],[None])[0],'stub_va':f['va']})
 bus=[x for x in imports if x['nid'] in SPI_NIDS|GPIO_NIDS|I2C_NIDS]
 read_callers=[];write_callers=[];bus_callers=[]
 for key,cfg in reach.functions.items():
  read_callers+=call_windows(cfg,READ_FN);write_callers+=call_windows(cfg,WRITE_FN)
  calls=call_semantics(cfg,stubs);rel=[c for c in calls if c.get('nid') in SPI_NIDS|GPIO_NIDS|I2C_NIDS]
  if rel:bus_callers.append({'function':cfg.start,'parents':parents.get(key,[]),'calls':rel,'absolute_constants':absolute_constants(e,cfg),'instructions':[{'va':i.address,'text':ins_text(i)} for i in all_insns(cfg)]})
 sw=state_writers(reach)
 read_cfg=find_fn(reach,READ_FN) or FunctionCFG(e,READ_FN,True,reach.import_stubs);write_cfg=find_fn(reach,WRITE_FN) or FunctionCFG(e,WRITE_FN,True,reach.import_stubs)
 selectors=[]
 for key,cfg in reach.functions.items():
  vals={c['value'] for c in absolute_constants(e,cfg)}
  if any(v in vals for v in COLOR_PROGRAMS):selectors.append({'function':cfg.start,'parents':parents.get(key,[]),'program_constants':sorted(v for v in vals if v in COLOR_PROGRAMS),'instructions':[{'va':i.address,'text':ins_text(i)} for i in all_insns(cfg)]})
 result={'schema':1,'firmware':'3.65','elf_sha256':e.sha256,'imports':imports,'panel_control_bus_imports':bus,'private_panel_read':{'va':READ_FN,'callers':read_callers,'instructions':[{'va':i.address,'text':ins_text(i)} for i in all_insns(read_cfg)]},'private_panel_write':{'va':WRITE_FN,'callers':write_callers,'instructions':[{'va':i.address,'text':ins_text(i)} for i in all_insns(write_cfg)]},'bus_callers':bus_callers,'state_writers':sw,'color_program_selectors':selectors,'transport_classification':{'panel_register_control':'Pervasive SPI + GPIO; PROVEN from imports and private bitstream writer lifecycle','pixel_transport':'separate; SceLcd imports no SceDsiForDriver functions','DCS_0x26_relevance':'not primary evidence: register control is not routed through the public DSI DCS ABI'},'notes':['large program records remain UNCLASSIFIED vendor panel payload until controller command/register semantics are proven','GetDDB is a cached getter; callers of the private panel-read function are the authoritative path to controller/DDB production']}
 a.json.write_text(json.dumps(result,indent=2)+'\n')
 print('LCD_PANEL_CONTROL_TOPOLOGY')
 for x in bus:print(f"  import {x['library']} {x['name'] or hex(x['nid'])} nid=0x{x['nid']:08X} stub=0x{x['stub_va']:08X}")
 print(f'PRIVATE_PANEL_READ 0x{READ_FN:08X} callers={len(read_callers)}')
 for x in read_callers:print(f"  caller=0x{x['function']:08X} call=0x{x['call_va']:08X}")
 print(f'PRIVATE_PANEL_WRITE 0x{WRITE_FN:08X} callers={len(write_callers)}')
 print('COLOR_PROGRAM_SELECTORS')
 for x in selectors:print(f"  fn=0x{x['function']:08X} programs={[hex(v) for v in x['program_constants']]}")
 print('TRANSPORT_CONCLUSION')
 print('  panel register/control plane = Pervasive SPI + GPIO (not SceDsiForDriver)')
 print('  201-byte/other large program payloads = UNCLASSIFIED until controller semantics are proven')
if __name__=='__main__':main()
