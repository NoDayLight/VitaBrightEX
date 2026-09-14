#!/usr/bin/env python3
from __future__ import annotations
import argparse,json
from pathlib import Path
from vita_elf_audit import VitaElf,Reachability,FunctionCFG
from topology_common import all_insns,ins_text
LOWIO_SHA='f791cfe2c6db955deb9446c57bb72ce1870bde2c6c485cac363e343d5128f744'
LCD_SHA='24752bb4c69f0c241701cab0364cde9cbe5c95128b6bfe02bf3263aad524246e'
CSC_A=0x81005D48;CSC_B=0x81005E24;WRITER=0x81000A54;READER=0x810005B4;READ_CALLER=0x81000ED0

def cfg(e,va):
 r=Reachability(e,e.exports(),e.imports(),extra_starts=(va,));return next((x for x in r.functions.values() if x.start==va),None) or FunctionCFG(e,va,True,r.import_stubs,boundary_index=r.boundaries,noreturn_stubs=r.noreturn_stubs)
def verify(c,wants):
 by={i.address:ins_text(i) for i in all_insns(c)}
 for va,w in wants.items():
  if by.get(va)!=w:raise SystemExit(f'ABI invariant 0x{va:08X}: {by.get(va)!r} != {w!r}')

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--lowio',type=Path,required=True);ap.add_argument('--lcd',type=Path,required=True);ap.add_argument('--json',type=Path,required=True);a=ap.parse_args()
 lo,lc=VitaElf(a.lowio),VitaElf(a.lcd)
 if lo.sha256!=LOWIO_SHA:raise SystemExit('lowio hash drift')
 if lc.sha256!=LCD_SHA:raise SystemExit('lcd hash drift')
 verify(cfg(lo,CSC_A),{0x81005D48:'cmp r0, #4',0x81005D4E:'mov r4, r1',0x81005E12:'movs r0, #0',0x81005E18:'mov.w r0, #0x700',0x81005E1C:'movt r0, #0x803f'})
 verify(cfg(lo,CSC_B),{0x81005E24:'cmp r0, #4',0x81005E2A:'mov r4, r1',0x81005F10:'movs r0, #0',0x81005F16:'mov.w r0, #0x700',0x81005F1A:'movt r0, #0x803f'})
 verify(cfg(lc,WRITER),{0x81000A60:'mov r5, r0',0x81000A62:'mov sb, r1',0x81000A64:'mov r8, r2',0x81000B36:'movs r0, #0',0x81000B60:'movw r0, #0xc03',0x81000B64:'movt r0, #0x803f',0x81000B6C:'movw r0, #0xc04',0x81000B70:'movt r0, #0x803f'})
 verify(cfg(lc,READER),{0x810005C0:'mov sb, r0',0x810005C2:'mov r7, r1',0x810005C4:'mov r8, r2',0x81000880:'movs r0, #0',0x81000A3C:'movw r0, #0xc03',0x81000A40:'movt r0, #0x803f',0x81000A48:'movw r0, #0xc04',0x81000A4C:'movt r0, #0x803f'})
 verify(cfg(lc,READ_CALLER),{0x81000EDC:'movs r2, #1',0x81000EDE:'movs r0, #0xa',0x81000EE2:'mov r1, sp',0x81000EE6:'bl #0x810005b4',0x81000EEA:'cmp r0, #0',0x81000EEE:'it ge',0x81000EF0:'ldrb.w r0, [sp]'})
 out={'schema':1,'firmware':'3.65','functions':{
  'csc_a':{'va':CSC_A,'abi':'int (int plane, const SceIftuCscParams *params)','return':'0 success; 0x803F0700 invalid-plane error','classification':'PROVEN_STATIC'},
  'csc_b':{'va':CSC_B,'abi':'int (int plane, const SceIftuCscParams *params)','return':'0 success; 0x803F0700 invalid-plane error','classification':'PROVEN_STATIC'},
  'panel_writer':{'va':WRITER,'abi':'int (unsigned command, const void *payload, unsigned length)','return':'0 success; 0x803F0C03 / 0x803F0C04 error returns','classification':'PROVEN_STATIC'},
  'panel_reader':{'va':READER,'abi':'int (unsigned command, void *payload, unsigned requested_length)','return':'status code: 0 success; 0x803F0C03 / 0x803F0C04 errors; NOT a byte count','payload_contract':'only command 0x0A requested_length=1 is proven consumed after return >=0; arbitrary trailing bytes remain non-authoritative','classification':'PROVEN_RETURN_CONVENTION_PARTIAL_PAYLOAD_CONTRACT'}},
  'trace_policy':'raw_return is always retained; SUCCESS/FAILURE labels are permitted only for these statically proven conventions'}
 a.json.write_text(json.dumps(out,indent=2)+'\n')
 print('TRACE_PRIVATE_ABI_RETURN_SEMANTICS')
 for n,x in out['functions'].items():print(f"  {n}: {x['abi']} ; {x['return']} ; {x['classification']}")
if __name__=='__main__':main()
