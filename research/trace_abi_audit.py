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

def _is_return(i):
 t=ins_text(i)
 return t.startswith('bx lr') or (t.startswith('pop ') and 'pc' in t) or t.startswith('mov pc, lr')

def _context(ins,idx,radius=4):
 lo=max(0,idx-radius);hi=min(len(ins),idx+radius+1)
 return [f"0x{x.address:08X}: {ins_text(x)}" for x in ins[lo:hi]]

def verify(function_name,c,wants):
 ins=list(all_insns(c));by={i.address:(n,i) for n,i in enumerate(ins)}
 failures=[]
 for va,spec in wants.items():
  expected=spec['instruction'];semantic=spec['semantic'];actual=by.get(va)
  actual_text=ins_text(actual[1]) if actual else '<NO_INSTRUCTION_AT_EXPECTED_VA>'
  if actual_text!=expected:
   failures.append((va,expected,semantic,actual_text,actual[0] if actual else None))
 if not failures:return
 end=max((i.address+i.size for i in ins),default=c.start)
 returns=[]
 for n,i in enumerate(ins):
  if _is_return(i):
   returns.append({'site':f'0x{i.address:08X}','context':_context(ins,n,4)})
 print('TRACE_ABI_MISMATCH_DIAGNOSTIC')
 print(f'function: {function_name}')
 print(f'function_boundary: 0x{c.start:08X}..0x{end:08X}')
 print('return_block_context:')
 if returns:
  for r in returns:
   print(f"  return_site {r['site']}")
   for line in r['context']:print('    '+line)
 else:print('  <NO_DECODED_RETURN_SITE>')
 for va,expected,semantic,actual_text,idx in failures:
  print('failed_invariant:')
  print(f'  function: {function_name}')
  print(f'  expected_va: 0x{va:08X}')
  print(f'  expected_semantic: {semantic}')
  print(f'  expected_instruction_string: {expected}')
  print(f'  actual_decoded_instruction: {actual_text}')
  print('  surrounding_plus_minus_4:')
  if idx is None:
   near=min(range(len(ins)),key=lambda n:abs(ins[n].address-va)) if ins else None
   if near is None:print('    <NO_INSTRUCTIONS>')
   else:
    for line in _context(ins,near,4):print('    '+line)
  else:
   for line in _context(ins,idx,4):print('    '+line)
  print('  diagnosis_classification: UNCLASSIFIED_PENDING_EXACT_PINNED_ELF_EVIDENCE')
 raise SystemExit(f'{function_name}: {len(failures)} ABI proof-harness invariant(s) failed; inspect diagnostic above before changing semantic assertions')

def S(instruction,semantic):return {'instruction':instruction,'semantic':semantic}

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--lowio',type=Path,required=True);ap.add_argument('--lcd',type=Path,required=True);ap.add_argument('--json',type=Path,required=True);a=ap.parse_args()
 lo,lc=VitaElf(a.lowio),VitaElf(a.lcd)
 if lo.sha256!=LOWIO_SHA:raise SystemExit('lowio hash drift')
 if lc.sha256!=LCD_SHA:raise SystemExit('lcd hash drift')
 verify('csc_a',cfg(lo,CSC_A),{
  0x81005D48:S('cmp r0, #4','plane argument is range-checked at entry'),
  0x81005D4E:S('mov r4, r1','CSC params pointer is retained'),
  0x81005E12:S('movs r0, #0','successful return materializes zero'),
  0x81005E18:S('mov.w r0, #0x700','invalid-plane error low half is materialized'),
  0x81005E1C:S('movt r0, #0x803f','invalid-plane error high half is materialized')})
 verify('csc_b',cfg(lo,CSC_B),{
  0x81005E24:S('cmp r0, #4','plane argument is range-checked at entry'),
  0x81005E2A:S('mov r4, r1','CSC params pointer is retained'),
  0x81005F10:S('movs r0, #0','successful return materializes zero'),
  0x81005F16:S('mov.w r0, #0x700','invalid-plane error low half is materialized'),
  0x81005F1A:S('movt r0, #0x803f','invalid-plane error high half is materialized')})
 verify('panel_writer',cfg(lc,WRITER),{
  0x81000A60:S('mov r5, r0','command argument is retained'),
  0x81000A62:S('mov sb, r1','payload pointer is retained'),
  0x81000A64:S('mov r8, r2','payload length is retained'),
  0x81000B36:S('movs r0, #0','successful return materializes zero'),
  0x81000B60:S('movw r0, #0xc03','first known error low half is materialized'),
  0x81000B64:S('movt r0, #0x803f','first known error high half is materialized'),
  0x81000B6C:S('movw r0, #0xc04','second known error low half is materialized'),
  0x81000B70:S('movt r0, #0x803f','second known error high half is materialized')})
 verify('panel_reader',cfg(lc,READER),{
  0x810005C0:S('mov sb, r0','command argument is retained'),
  0x810005C2:S('mov r7, r1','destination pointer is retained'),
  0x810005C4:S('mov r8, r2','requested length is retained'),
  0x81000880:S('movs r0, #0','one successful return block materializes zero'),
  0x81000A3C:S('movw r0, #0xc03','first known error low half is materialized'),
  0x81000A40:S('movt r0, #0x803f','first known error high half is materialized'),
  0x81000A48:S('movw r0, #0xc04','second known error low half is materialized'),
  0x81000A4C:S('movt r0, #0x803f','second known error high half is materialized')})
 verify('panel_reader_known_caller',cfg(lc,READ_CALLER),{
  0x81000EDC:S('movs r2, #1','known caller requests exactly one byte'),
  0x81000EDE:S('movs r0, #0xa','known caller issues command 0x0A'),
  0x81000EE2:S('mov r1, sp','known caller supplies stack destination'),
  0x81000EE6:S('bl #0x810005b4','known caller invokes private reader'),
  0x81000EEA:S('cmp r0, #0','known caller compares raw return with zero'),
  0x81000EEE:S('it ge','known caller accepts the non-negative branch'),
  0x81000EF0:S('ldrb.w r0, [sp]','known caller consumes initialized byte on accepted branch')})
 out={'schema':1,'firmware':'3.65','functions':{
  'csc_a':{'va':CSC_A,'abi':'int (int plane, const SceIftuCscParams *params)','return':'0 success; 0x803F0700 invalid-plane error','classification':'PROVISIONAL_LEGACY_ASSERTION_PENDING_SEMANTIC_HARNESS'},
  'csc_b':{'va':CSC_B,'abi':'int (int plane, const SceIftuCscParams *params)','return':'0 success; 0x803F0700 invalid-plane error','classification':'PROVISIONAL_LEGACY_ASSERTION_PENDING_SEMANTIC_HARNESS'},
  'panel_writer':{'va':WRITER,'abi':'int (unsigned command, const void *payload, unsigned length)','return':'0 success; 0x803F0C03 / 0x803F0C04 error returns','classification':'PROVISIONAL_LEGACY_ASSERTION_PENDING_SEMANTIC_HARNESS'},
  'panel_reader':{'va':READER,'abi':'int (unsigned command, void *payload, unsigned requested_length)','return':'legacy assertion: 0 success; 0x803F0C03 / 0x803F0C04 errors; known 0x0A/1 caller accepts >=0','payload_contract':'only command 0x0A requested_length=1 is proven consumed after return >=0; arbitrary trailing bytes remain non-authoritative','classification':'PROVISIONAL_REQUIRES_RETURN_RECONCILIATION'}},
  'trace_policy':'raw_return is always retained; no semantic assertion is changed by this diagnostic pass'}
 a.json.write_text(json.dumps(out,indent=2)+'\n')
 print('TRACE_PRIVATE_ABI_DIAGNOSTIC_BASELINE_MATCHED')
if __name__=='__main__':main()
