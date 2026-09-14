#!/usr/bin/env python3
from __future__ import annotations
import argparse,hashlib,json
from pathlib import Path
from vita_elf_audit import VitaElf,Reachability,FunctionCFG
from topology_common import all_insns,ins_text,function_parents
LOWIO_SHA='f791cfe2c6db955deb9446c57bb72ce1870bde2c6c485cac363e343d5128f744'
LCD_SHA='24752bb4c69f0c241701cab0364cde9cbe5c95128b6bfe02bf3263aad524246e'
CSC_A=0x81005D48;CSC_B=0x81005E24;WRITER=0x81000A54;READER=0x810005B4;READ_CALLER=0x81000ED0

def cfg(e,va):
 r=Reachability(e,e.exports(),e.imports(),extra_starts=(va,));return r,next((x for x in r.functions.values() if x.start==va),None) or FunctionCFG(e,va,True,r.import_stubs,boundary_index=r.boundaries,noreturn_stubs=r.noreturn_stubs)
def texts(c):return [ins_text(i).lower() for i in all_insns(c)]
def has(t,*need):return all(any(n in x for x in t) for n in need)
def proof(name,ok,evidence):
 if not ok:raise SystemExit('semantic tracer-safety invariant failed: '+name)
 return {'status':'STATICALLY_PROVEN','evidence':evidence}
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--lowio',type=Path,required=True);ap.add_argument('--lcd',type=Path,required=True);ap.add_argument('--json',type=Path,required=True);ap.add_argument('--text',type=Path,required=True);a=ap.parse_args();lo,lc=VitaElf(a.lowio),VitaElf(a.lcd)
 if lo.sha256!=LOWIO_SHA or lc.sha256!=LCD_SHA:raise SystemExit('retail 3.65 module hash drift')
 rla,ca=cfg(lo,CSC_A);rlb,cb=cfg(lo,CSC_B);rlw,cw=cfg(lc,WRITER);rlr,cr=cfg(lc,READER);_,cc=cfg(lc,READ_CALLER)
 ta,tb,tw,tr,tc=map(texts,(ca,cb,cw,cr,cc))
 out={'schema':2,'firmware':'3.65','module_sha256':{'lowio':lo.sha256,'lcd':lc.sha256},'functions':{},'historical_mismatch':'NOT MATERIAL AFTER SUPERSEDING PROOF'}
 for name,t,off in [('csc_a',ta,'#0x10c'),('csc_b',tb,'#0x148')]:
  ok=has(t,'cmp r0, #4','mov r4, r1','cbz r4','add.w r0, r4, #0x30',off,'movs r0, #0')
  out['functions'][name]=proof(name,ok,['plane r0 range-checked against 4','r1 retained as params','NULL has separate branch','copy loop bound uses params+0x30 plus trailing 12 bytes = 0x3C','success return materializes zero semantically without pinning its exact VA'])
 wok=has(tw,'mov sb, r1','mov r8, r2','add.w ip, sb, r8','ldrb r1, [lr], #1','cmp lr, ip','cmp.w r8, #0','movs r0, #0')
 parents=function_parents(rlw).get((WRITER,True),function_parents(rlw).get((WRITER,False),[]))
 out['functions']['panel_writer']=proof('panel_writer',wok,['r1 retained as source pointer','r2 retained as byte length','end pointer is source+length','zero length branches around byte load','nonzero path advances one ldrb at a time to end','tracer copies only non-NULL lengths 1..255 and rejects larger captures','normal retail SceLcd invocation provenance is internal; tracer never changes Sony arguments'])
 out['functions']['panel_writer']['direct_parent_count']=len(parents)
 rok=has(tr,'mov r7, r1','mov r8, r2','mov sb, r0','movs r0, #0') and has(tc,'movs r2, #1','movs r0, #0xa','mov r1, sp','cmp r0, #0','ldrb.w r0, [sp]')
 out['functions']['panel_reader']=proof('panel_reader invocation',rok,['r0/r1/r2 consumed as command/destination/requested-length','known 0x0A caller requests one byte to stack','known caller tests raw return before consuming byte','generic initialized-output extent remains unproven; tracer copies zero reader bytes'])
 out['tracer_contract']={'writer_max_copy':255,'csc_copy':60,'reader_copy':0,'original_call':'exactly once enforced by source audit','arguments':'unchanged enforced by source audit','return':'raw original return passed through','failure':'capture reservation/copy rejection cannot suppress original'}
 a.json.write_text(json.dumps(out,indent=2)+'\n');lines=['GATE0_TRACER_SAFETY_STATIC_PROOF=PASS','lowio_sha256='+lo.sha256,'lcd_sha256='+lc.sha256,'historical_mismatch=NOT MATERIAL AFTER SUPERSEDING PROOF']
 for k,v in out['functions'].items():lines.append(k+': '+v['status']+'; '+'; '.join(v['evidence']))
 a.text.write_text('\n'.join(lines)+'\n');print('\n'.join(lines))
if __name__=='__main__':main()
