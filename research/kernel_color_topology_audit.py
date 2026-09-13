#!/usr/bin/env python3
from __future__ import annotations
import argparse,json,struct
from pathlib import Path
from capstone import Cs,CS_ARCH_ARM,CS_MODE_ARM,CS_MODE_THUMB
from capstone.arm import ARM_OP_IMM,ARM_OP_REG
from vita_elf_audit import VitaElf,PT_LOAD
from topology_common import load_nid_names,best_name

IFTU_NIDS={0x0D7C02F7,0x0FCBF457,0x357EAE24,0x67E37EFC,0x7CE0C4DA,0xAF19FD85,0xC11F30B3,0xD64F4C6B,0xE6EE2C6B}
LCD_NIDS={0x1A0A7519,0x5F4124AB,0x3A6D6AC3,0xE03E120B,0x17F66722,0x581D3A87,0xD40968FB,0x0C7E03D8}
DISPLAY_LIB='SceDisplayForDriver';IFTU_LIB='SceIftuForDriver';LCD_LIB='SceLcdForDriver'
PHYS={0xE5020000,0xE5021000,0xE5022000}
PRIORITY={'SceLowio','SceDisplay','SceLcd','ScePower','SceSysStateMgr','SceRegMgr','ScePervasive'}

def linear_mmio_halfword_refs(e):
 out=[]
 for p in e.phdrs:
  if p.p_type!=PT_LOAD or not (p.p_flags&1):continue
  blob=e.data[p.p_offset:p.p_offset+p.p_filesz]
  for mode in (CS_MODE_THUMB,CS_MODE_ARM):
   md=Cs(CS_ARCH_ARM,mode);md.detail=True;regs={}
   for i in md.disasm(blob,p.p_vaddr):
    ops=getattr(i,'operands',[]);m=i.mnemonic.lower()
    if m=='movw' and len(ops)>=2 and ops[0].type==ARM_OP_REG and ops[1].type==ARM_OP_IMM:regs[ops[0].reg]=ops[1].imm&0xffff
    elif m=='movt' and len(ops)>=2 and ops[0].type==ARM_OP_REG and ops[1].type==ARM_OP_IMM and ops[0].reg in regs:
     v=regs[ops[0].reg]|((ops[1].imm&0xffff)<<16);regs[ops[0].reg]=v
     if 0xE5020000<=v<0xE5030000:out.append({'va':i.address,'value':v,'mode':'thumb' if mode==CS_MODE_THUMB else 'arm'})
 return out[:128]

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--elf-dir',type=Path,required=True);ap.add_argument('--nid-db-root',type=Path,required=True);ap.add_argument('--json',type=Path,required=True);a=ap.parse_args()
 names=load_nid_names(a.nid_db_root);mods=[];elf_fail=[];import_fail=[]
 files=sorted(a.elf_dir.glob('*.elf'))
 for p in files:
  try:e=VitaElf(p)
  except Exception as ex:elf_fail.append({'file':p.name,'error':str(ex)});continue
  rel_imports=[]
  try: imports=e.imports()
  except Exception as ex:
   imports=[];import_fail.append({'file':p.name,'module':e.modinfo.get('name',''),'error':str(ex)})
  for lib in imports:
   if lib['library_name'] in (IFTU_LIB,DISPLAY_LIB,LCD_LIB) or any(f['nid'] in IFTU_NIDS|LCD_NIDS for f in lib['functions']):
    rel_imports.append({'library':lib['library_name'],'library_nid':lib['library_nid'],'functions':[{'nid':f['nid'],'name':best_name(names,f['nid'])} for f in lib['functions']]})
  raw=[]
  for v in PHYS:
   n=e.data.count(struct.pack('<I',v))
   if n:raw.append({'value':v,'count':n})
  code=linear_mmio_halfword_refs(e)
  module=e.modinfo.get('name','')
  if rel_imports or raw or code or module in PRIORITY:
   mods.append({'file':p.name,'module':module,'sha256':e.sha256,'relevant_imports':rel_imports,'raw_e502_constants':raw,'constructed_e502_constants':code,'import_table_decoded':not any(x['file']==p.name for x in import_fail)})
 result={'schema':2,'firmware':'3.65','scanned_elf_count':len(files),'elf_parse_failures':elf_fail,'import_table_decode_failures':import_fail,'relevant_modules':mods,'scope':'all 46 os0:/kd/*.skprx decrypted to ELF from pinned retail 3.65','interpretation':'An import-table decode failure for a non-target image does not erase raw/code MMIO scanning; target display modules remain exact-hash analyzable in dedicated audits.'}
 a.json.write_text(json.dumps(result,indent=2)+'\n')
 print('KERNEL_COLOR_TOPOLOGY')
 print(f"  scanned={result['scanned_elf_count']} elf_parse_failures={len(elf_fail)} import_decode_failures={len(import_fail)} relevant={len(mods)}")
 for f in import_fail:print(f"  IMPORT_TABLE_UNDECODABLE {f['file']} module={f['module']} reason={f['error']}")
 for m in mods:print(f"  {m['file']} module={m['module']} imports={[x['library'] for x in m['relevant_imports']]} raw_mmio={[hex(x['value']) for x in m['raw_e502_constants']]} code_mmio={[hex(x['value']) for x in m['constructed_e502_constants'][:8]]}")
if __name__=='__main__':main()
