#!/usr/bin/env python3
"""Deterministic retail-3.65 IFTU nonlinear/register-transfer audit v2.

This classifies the complete direct-call subgraph rooted at all nine IFTU exports.
It does not infer gamma from a table length. Candidates include looped memory streams,
MMIO-adjacent constants, indexed writes and piecewise-size hints; each candidate keeps
its exact derived instruction sequence for semantic review.
"""
from __future__ import annotations
import argparse,json
from collections import deque
from pathlib import Path
from capstone.arm import ARM_OP_IMM,ARM_OP_MEM,ARM_OP_REG
from vita_elf_audit import FunctionCFG,Reachability,VitaElf
from topology_common import all_insns,ins_text,absolute_constants

LOWIO_SHA256='f791cfe2c6db955deb9446c57bb72ce1870bde2c6c485cac363e343d5128f744'
IFTU_NIDS={0x0D7C02F7:'ksceIftuEnable',0x0FCBF457:'private_csc_cache_A',0x357EAE24:'private_control',0x67E37EFC:'ksceIftuCsc',0x7CE0C4DA:'ksceIftuSetInputFrameBuffer',0xAF19FD85:'ksceIftuSetMergeSetting',0xC11F30B3:'ksceIftuDisable',0xD64F4C6B:'private_csc_cache_B',0xE6EE2C6B:'ksceIftuSetOutputFormat'}
SIZE_HINTS={8,9,15,16,17,31,32,33,63,64,65,127,128,129,255,256,257,357,511,512,513,1024}

def find_export(e,n):
 for lib in e.exports():
  for f in lib['functions']:
   if f['nid']==n:return f
 return None

def rooted(reach,roots):
 q=deque(roots);seen=set()
 while q:
  k=q.popleft()
  if k in seen or k not in reach.functions:continue
  seen.add(k)
  for c in reach.functions[k].direct_callees:q.append(c)
 return seen

def summary(e,cfg):
 hints=[];back=[];mem=[];steps=[];indexed=[]
 for i in all_insns(cfg):
  ops=getattr(i,'operands',[]);m=i.mnemonic.lower()
  for op in ops:
   if op.type==ARM_OP_IMM and abs(int(op.imm)) in SIZE_HINTS:hints.append({'va':i.address,'value':int(op.imm),'instruction':ins_text(i)})
   if op.type==ARM_OP_MEM:
    mem.append({'va':i.address,'mnemonic':i.mnemonic,'disp':int(op.mem.disp),'writeback':bool(getattr(i,'writeback',False)),'instruction':ins_text(i)})
    if m.startswith('str') and (op.mem.index!=0 or bool(getattr(i,'writeback',False))):indexed.append({'va':i.address,'instruction':ins_text(i)})
  if m.startswith(('add','sub')):
   for op in ops[1:]:
    if op.type==ARM_OP_IMM and 0<abs(int(op.imm))<=32:steps.append({'va':i.address,'step':int(op.imm)*(1 if m.startswith('add') else -1),'instruction':ins_text(i)})
 for b in cfg.blocks.values():
  for s in b.successors:
   if s<=b.start:back.append({'block':b.start,'target':s,'instructions':[ins_text(x) for x in b.instructions]})
 absconst=absolute_constants(e,cfg);mmio=[x for x in absconst if 0xE0000000<=x['value']<0xF0000000]
 score=2*len(back)+sum(1 for x in mem if x['writeback'])+len(indexed)+min(3,len(hints))+min(3,len(mmio))
 return {'start':cfg.start,'mode':'thumb' if cfg.thumb else 'arm','instruction_count':cfg.instruction_count(),'calls':cfg.calls,'size_hints':hints,'backedges':back,'writeback_memory_ops':[x for x in mem if x['writeback']],'indexed_or_writeback_stores':indexed,'small_pointer_steps':steps,'mmio_constants':mmio,'candidate_score':score,'instructions':[{'va':i.address,'text':ins_text(i)} for i in all_insns(cfg)] if score else None}

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--lowio',type=Path,required=True);ap.add_argument('--json',type=Path,required=True);a=ap.parse_args();e=VitaElf(a.lowio)
 if e.sha256!=LOWIO_SHA256:raise SystemExit(f'hash drift {e.sha256}')
 reach=Reachability(e,e.exports(),e.imports());roots=[];emap=[]
 for n,nm in IFTU_NIDS.items():
  f=find_export(e,n)
  if not f:raise SystemExit(f'missing {nm}')
  roots.append((f['va'],f['thumb']));emap.append({'nid':n,'name':nm,'va':f['va']})
 graph=rooted(reach,roots);func=[summary(e,reach.functions[k]) for k in sorted(graph)];cand=[x for x in func if x['candidate_score']>=2];cand.sort(key=lambda x:(-x['candidate_score'],x['start']))
 result={'schema':2,'firmware':'3.65','elf_sha256':e.sha256,'iftu_exports':emap,'rooted_reachable_function_count':len(graph),'functions':func,'nonlinear_candidates':cand,'closure_rule':'No candidate is classified as nonlinear until source->MMIO register semantics establish a transfer/LUT function. Absence applies only to this IFTU-rooted graph.'}
 a.json.write_text(json.dumps(result,indent=2)+'\n')
 print('IFTU_NONLINEAR_TOPOLOGY')
 print(f'  functions={len(graph)} candidates={len(cand)}')
 for c in cand:print(f"  fn=0x{c['start']:08X} score={c['candidate_score']} loops={len(c['backedges'])} hints={[x['value'] for x in c['size_hints']]} mmio={[hex(x['value']) for x in c['mmio_constants']]}")
 print('  no gamma/LUT label is assigned by shape alone')
if __name__=='__main__':main()
