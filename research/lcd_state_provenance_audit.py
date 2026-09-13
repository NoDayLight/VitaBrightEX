#!/usr/bin/env python3
from __future__ import annotations
import argparse,json
from pathlib import Path
from capstone.arm import ARM_OP_IMM,ARM_OP_MEM,ARM_OP_REG
from vita_elf_audit import VitaElf,Reachability,FunctionCFG
from topology_common import all_insns,ins_text,load_nid_names,import_stub_map,call_semantics,absolute_constants,function_parents

LCD_SHA='24752bb4c69f0c241701cab0364cde9cbe5c95128b6bfe02bf3263aad524246e'
STATE=0x810020A0
FOCUS=(0x810003EC,0x8100057C,0x81000B78,0x81000ED0,0x81001340)

def rn(i,o):return i.reg_name(o.reg) if o.type==ARM_OP_REG else None

def focus(e,r,stubs,parents,va):
 c=next((x for x in r.functions.values() if x.start==va),None)
 if c is None:c=FunctionCFG(e,va,True,r.import_stubs,boundary_index=r.boundaries,noreturn_stubs=r.noreturn_stubs)
 m=c.compact(False)
 return {'start':c.start,'logical_end':c.logical_end,'parents':parents.get((c.start,c.thumb),[]),'termination_reason':m['termination_reason'],'calls':call_semantics(c,stubs),'absolute_constants':absolute_constants(e,c),'instructions':[{'va':i.address,'text':ins_text(i)} for i in all_insns(c)]}

def state_accesses(cfg):
 lo={};regs={};rows=[]
 for i in all_insns(cfg):
  ops=getattr(i,'operands',[]);m=i.mnemonic.lower()
  if m=='movw' and len(ops)>=2 and ops[0].type==ARM_OP_REG and ops[1].type==ARM_OP_IMM:
   d=rn(i,ops[0]);lo[d]=ops[1].imm&0xffff;regs[d]=('lo',lo[d]);continue
  if m=='movt' and len(ops)>=2 and ops[0].type==ARM_OP_REG and ops[1].type==ARM_OP_IMM:
   d=rn(i,ops[0])
   if d in lo:
    v=lo[d]|((ops[1].imm&0xffff)<<16);regs[d]=('state',0) if v==STATE else ('const',v)
   continue
  if m.startswith('mov') and len(ops)>=2 and ops[0].type==ARM_OP_REG and ops[1].type==ARM_OP_REG:regs[rn(i,ops[0])]=regs.get(rn(i,ops[1]),('unknown',None));continue
  if m.startswith('add') and len(ops)>=3 and ops[0].type==ARM_OP_REG and ops[1].type==ARM_OP_REG and ops[2].type==ARM_OP_IMM:
   d=rn(i,ops[0]);s=regs.get(rn(i,ops[1]));imm=int(ops[2].imm)
   if s and s[0] in ('state','state_ptr'):regs[d]=('state_ptr',s[1]+imm)
   continue
  if m.startswith(('ldr','ldrb','ldrh','ldrd','str','strb','strh','strd','stm')):
   mem=None
   for op in reversed(ops):
    if op.type==ARM_OP_MEM:mem=op;break
   if mem is not None:
    b=regs.get(i.reg_name(mem.mem.base));disp=int(mem.mem.disp)
    if b and b[0] in ('state','state_ptr'):
     off=b[1]+disp
     kind='write' if m.startswith(('str','stm')) else 'read'
     rows.append({'function':cfg.start,'va':i.address,'kind':kind,'offset':off,'width':m,'instruction':ins_text(i)})
 return rows

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--lcd',type=Path,required=True);ap.add_argument('--nid-db-root',type=Path,required=True);ap.add_argument('--json',type=Path,required=True);a=ap.parse_args()
 e=VitaElf(a.lcd)
 if e.sha256!=LCD_SHA:raise SystemExit('LCD hash drift')
 names=load_nid_names(a.nid_db_root);r=Reachability(e,e.exports(),e.imports(),extra_starts=FOCUS);stubs=import_stub_map(e,names);parents=function_parents(r)
 accesses=[]
 for c in r.functions.values():accesses.extend(state_accesses(c))
 ddb=[x for x in accesses if x['kind']=='write' and 0x08<=x['offset']<0x0E]
 ddb_reads=[x for x in accesses if x['kind']=='read' and 0x08<=x['offset']<0x0E]
 f={f'0x{x:08X}':focus(e,r,stubs,parents,x) for x in FOCUS}
 result={'schema':1,'firmware':'3.65','lcd_state_base':STATE,'state_accesses':accesses,'ddb_window_writers':ddb,'ddb_window_readers':ddb_reads,'focus':f,'interpretation':{'ddb_getter':'The public getter must be interpreted from its exact logical body; nearby state+0x0C is kept separate unless dataflow proves it belongs to the getter result.','controller_identity':'UNKNOWN until provenance reaches an official hardware read/board identity source; no unknown SPI probing is authorized.'}}
 a.json.write_text(json.dumps(result,indent=2)+'\n')
 print('LCD_STATE_PROVENANCE')
 print(f"  writes state+0x08..0x0D={len(ddb)}")
 for x in ddb:print(f"  writer=0x{x['function']:08X} insn=0x{x['va']:08X} offset=0x{x['offset']:X} {x['instruction']}")
 print(f"  reads state+0x08..0x0D={len(ddb_reads)}")
 for x in ddb_reads:print(f"  reader=0x{x['function']:08X} insn=0x{x['va']:08X} offset=0x{x['offset']:X} {x['instruction']}")
 print('FOCUS_0x81001340')
 x=f['0x81001340'];print(f"  logical=0x{x['start']:08X}..0x{x['logical_end']:08X} parents={x['parents']} calls={[(c.get('name'),hex(c['target'])) for c in x['calls']]}")

if __name__=='__main__':main()
