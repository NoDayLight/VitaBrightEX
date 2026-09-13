#!/usr/bin/env python3
from __future__ import annotations
import argparse,json
from pathlib import Path
from capstone.arm import ARM_OP_MEM
from vita_elf_audit import VitaElf,Reachability,FunctionCFG
from topology_common import all_insns,ins_text,load_nid_names,import_stub_map,call_semantics,absolute_constants,function_parents

LCD_SHA='24752bb4c69f0c241701cab0364cde9cbe5c95128b6bfe02bf3263aad524246e'
READ_FN=0x810005B4;WRITE_FN=0x81000A54;STATE_BASE=0x81002000
COLOR_PROGRAMS=(0x81001AE8,0x81001B70)
FOCUS={0x810003EC:'module_panel_init',0x81000B78:'panel_program_composer',0x81000ED0:'panel_status_read_caller',0x81000F88:'panel_program_step'}
SPI_NIDS={0xA85BF98A,0xDFD96BFC,0xE4B145AE,0x7B16F900};GPIO_NIDS={0xF6310435,0x129DF5AC,0xD454A584,0x372022A4};I2C_NIDS={0xCA94A759,0xD1D0A9A4,0x0A40B7BF}

def find_fn(reach,va):
 for (s,t),cfg in reach.functions.items():
  if s==va:return cfg
 return None

def call_windows(cfg,target,before=18,after=4):
 rows=[]
 for b in cfg.blocks.values():
  for idx,i in enumerate(b.instructions):
   for c in cfg.calls:
    if c['va']==i.address and c['target']==target:rows.append({'call_va':i.address,'function':cfg.start,'window':[{'va':x.address,'text':ins_text(x)} for x in b.instructions[max(0,idx-before):min(len(b.instructions),idx+after+1)]]})
 return rows

def store_halfword_candidates(reach):
 rows=[]
 for key,cfg in reach.functions.items():
  ins=all_insns(cfg)
  for idx,i in enumerate(ins):
   if not i.mnemonic.lower().startswith('strh'):continue
   ops=getattr(i,'operands',[])
   if len(ops)<2 or ops[1].type!=ARM_OP_MEM:continue
   disp=int(ops[1].mem.disp)
   if disp not in (8,10,12,14):continue
   rows.append({'function':cfg.start,'va':i.address,'disp':disp,'instruction':ins_text(i),'window':[{'va':x.address,'text':ins_text(x)} for x in ins[max(0,idx-8):idx+5]]})
 return rows

def cfg_record(e,reach,stubs,parents,va,label):
 cfg=find_fn(reach,va) or FunctionCFG(e,va,True,reach.import_stubs);key=next((k for k,c in reach.functions.items() if c is cfg),(va,True))
 return {'label':label,'start':cfg.start,'parents':parents.get(key,[]),'calls':call_semantics(cfg,stubs),'absolute_constants':absolute_constants(e,cfg),'instructions':[{'va':i.address,'text':ins_text(i)} for i in all_insns(cfg)]}

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--lcd',type=Path,required=True);ap.add_argument('--nid-db-root',type=Path,required=True);ap.add_argument('--json',type=Path,required=True);a=ap.parse_args();e=VitaElf(a.lcd)
 if e.sha256!=LCD_SHA:raise SystemExit(f'SceLcd hash drift {e.sha256}')
 names=load_nid_names(a.nid_db_root);reach=Reachability(e,e.exports(),e.imports());stubs=import_stub_map(e,names);parents=function_parents(reach)
 imports=[]
 for lib in e.imports():
  for f in lib['functions']:imports.append({'library':lib['library_name'],'library_nid':lib['library_nid'],'nid':f['nid'],'name':names.get(f['nid'],[None])[0],'stub_va':f['va']})
 bus=[x for x in imports if x['nid'] in SPI_NIDS|GPIO_NIDS|I2C_NIDS]
 read_callers=[];write_callers=[];bus_callers=[]
 for key,cfg in reach.functions.items():
  read_callers+=call_windows(cfg,READ_FN);write_callers+=call_windows(cfg,WRITE_FN);calls=call_semantics(cfg,stubs);rel=[c for c in calls if c.get('nid') in SPI_NIDS|GPIO_NIDS|I2C_NIDS]
  if rel:bus_callers.append({'function':cfg.start,'parents':parents.get(key,[]),'calls':rel,'absolute_constants':absolute_constants(e,cfg),'instructions':[{'va':i.address,'text':ins_text(i)} for i in all_insns(cfg)]})
 read_cfg=find_fn(reach,READ_FN) or FunctionCFG(e,READ_FN,True,reach.import_stubs);write_cfg=find_fn(reach,WRITE_FN) or FunctionCFG(e,WRITE_FN,True,reach.import_stubs)
 selectors=[]
 for key,cfg in reach.functions.items():
  vals={c['value'] for c in absolute_constants(e,cfg)}
  if any(v in vals for v in COLOR_PROGRAMS):selectors.append({'function':cfg.start,'parents':parents.get(key,[]),'program_constants':sorted(v for v in vals if v in COLOR_PROGRAMS),'instructions':[{'va':i.address,'text':ins_text(i)} for i in all_insns(cfg)]})
 focus={label:cfg_record(e,reach,stubs,parents,va,label) for va,label in FOCUS.items()}
 half=store_halfword_candidates(reach)
 result={'schema':2,'firmware':'3.65','elf_sha256':e.sha256,'imports':imports,'panel_control_bus_imports':bus,'private_panel_read':{'va':READ_FN,'callers':read_callers,'instructions':[{'va':i.address,'text':ins_text(i)} for i in all_insns(read_cfg)]},'private_panel_write':{'va':WRITE_FN,'callers':write_callers,'instructions':[{'va':i.address,'text':ins_text(i)} for i in all_insns(write_cfg)]},'bus_callers':bus_callers,'color_program_selectors':selectors,'focus':focus,'ddb_halfword_store_candidates':half,'status_probe':{'caller':0x81000ED0,'command':0x0A,'receive_length':1,'classification':'PROVEN_ARGUMENTS; command semantic remains controller-dependent','standard_dcs_correspondence':'0x0A is GET_POWER_MODE in MIPI DCS, but Sony sends this panel-control traffic through its private SPI transport, so DCS semantic equivalence is not yet proven'},'program_composition':{'builder':0x81000B78,'base_sources_by_DDB_state':['0x81001AF8 when state+0x0C <= 0x24','0x81001AC8 when 0x24 < state+0x0C <= 0x31','0x81001B90 when state+0x0C > 0x31'],'base_split_command':0x29,'color_sources_by_state_2C':{'0':0x81001AE8,'nonzero':0x81001B70},'third_sources_by_state_28':{'0':0x81001B80,'nonzero':0x81001B20},'construction_order':['base records before command 0x29','selected color-space source through 0xFF sentinel','selected state+0x28 source through 0xFF sentinel','base records from command 0x29 through 0xFF sentinel'],'working_buffer':0x81002020},'transport_classification':{'panel_register_control':'Pervasive SPI + GPIO; PROVEN from imports and private bitstream writer lifecycle','pixel_transport':'separate; SceLcd imports no SceDsiForDriver functions','DCS_0x26_relevance':'secondary hypothesis: public SceDsi DCS ABI is not the panel-control transport used by SceLcd'},'notes':['source-table roots overlap byte ranges; they must be interpreted through Sony program-composition control flow, not independently parsed to EOF','large payloads remain UNCLASSIFIED vendor panel data until controller command/register semantics are proven','GetDDB returns cached state+0x08/+0x0A; this audit now searches stores to those halfword offsets separately from the command-0x0A status read']}
 a.json.write_text(json.dumps(result,indent=2)+'\n')
 print('LCD_PANEL_CONTROL_TOPOLOGY')
 for x in bus:print(f"  import {x['library']} {x['name'] or hex(x['nid'])} nid=0x{x['nid']:08X} stub=0x{x['stub_va']:08X}")
 print(f'PRIVATE_PANEL_READ 0x{READ_FN:08X} callers={len(read_callers)}')
 for x in read_callers:print(f"  caller=0x{x['function']:08X} call=0x{x['call_va']:08X}")
 print('STATUS_PROBE command=0x0A receive_length=1 (semantic controller-dependent)')
 print('PROGRAM_COMPOSITION base -> color-space -> state28 -> base-tail, working_buffer=0x81002020')
 print(f'DDB_HALFWORD_STORE_CANDIDATES count={len(half)}')
 for x in half:print(f"  fn=0x{x['function']:08X} insn=0x{x['va']:08X} disp=0x{x['disp']:X} {x['instruction']}")
if __name__=='__main__':main()
