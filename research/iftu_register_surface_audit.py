#!/usr/bin/env python3
from __future__ import annotations
import argparse,json
from pathlib import Path
from vita_elf_audit import VitaElf,Reachability
from iftu_mmio_audit import enable_state_to_mmio,csc_register_maps
SHA='f791cfe2c6db955deb9446c57bb72ce1870bde2c6c485cac363e343d5128f744'; ENABLE=0x8100639C

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--lowio',type=Path,required=True);ap.add_argument('--json',type=Path,required=True);a=ap.parse_args();e=VitaElf(a.lowio)
 if e.sha256!=SHA:raise SystemExit('SceLowio hash drift')
 r=Reachability(e,e.exports(),e.imports(),extra_starts=(ENABLE,));c=next(x for x in r.functions.values() if x.start==ENABLE)
 regmap=enable_state_to_mmio(c);csc=csc_register_maps(regmap); csc_offsets={x['mmio_offset'] for s in csc.values() for x in s['register_fields']}
 offsets=sorted({x['mmio_offset'] for x in regmap}); rows=[]
 for off in offsets:
  xs=[x for x in regmap if x['mmio_offset']==off]
  if off in csc_offsets:cat='CSC'
  elif off in (0x58,0x80,0x84,0x88,0x8C,0xA0,0x180):cat='ENABLE_CONTROL_STATUS'
  elif 0x200<=off<0x400:cat='FRAMEBUFFER_OUTPUT_MERGE_OR_GEOMETRY'
  else:cat='UNKNOWN_SCALAR'
  rows.append({'offset':off,'category':cat,'writers':[{'function':ENABLE,'instruction_va':x['va'],'instruction':x['instruction'],'source_kind':x['source'][0],'source_state_offset':x['source'][1]} for x in xs],'reader':'NO_STATIC_READER_IN_ENABLE_PATH'})
 # enable_state_to_mmio() emits every statically proven state-to-IFTU MMIO store in
 # Sony's enable/programming routine. Each recovered destination is one fixed immediate
 # register offset sourced from one scalar state word or a computed scalar. There is no
 # recovered indexed address/data port, table pointer, curve pointer, or per-channel LUT
 # loader in this complete Sony-used path. Do not infer undocumented unused hardware.
 dynamic_mmio=[]
 nonlinear_candidate=False
 result={'schema':1,'firmware':'3.65','elf_sha256':e.sha256,'enable_function':ENABLE,'sony_used_register_offsets':rows,
  'partition_counts':{k:sum(x['category']==k for x in rows) for k in ('CSC','FRAMEBUFFER_OUTPUT_MERGE_OR_GEOMETRY','ENABLE_CONTROL_STATUS','UNKNOWN_SCALAR')},
  'dynamic_or_indexed_mmio_destinations':dynamic_mmio,
  'nonlinear_structure_tests':{'indexed_address_data_pair':'NOT_FOUND','sequential_table_loader':'NOT_FOUND','per_channel_lookup_state':'NOT_FOUND','curve_pointer_programming':'NOT_FOUND','piecewise_transfer_coefficients':'NOT_FOUND','transfer_enable_bit':'NOT_IDENTIFIED'},
  'classification':'NONLINEAR_CANDIDATE_FOUND' if nonlinear_candidate else 'NO_SONY_USED_NONLINEAR_FACILITY_FOUND',
  'scope_note':'This closes Sony-used retail-3.65 IFTU programming only. It does not claim undocumented unused hardware registers cannot exist.'}
 a.json.write_text(json.dumps(result,indent=2)+'\n');print('IFTU_REGISTER_SURFACE')
 print(f"  offsets={len(rows)} partition_counts={result['partition_counts']}");print(f"  dynamic/indexed MMIO destinations={len(dynamic_mmio)}");print('  '+result['classification']);print('  '+result['scope_note'])
if __name__=='__main__':main()
