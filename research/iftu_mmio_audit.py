#!/usr/bin/env python3
from __future__ import annotations
import argparse,json,struct
from pathlib import Path
from capstone.arm import ARM_OP_IMM,ARM_OP_MEM,ARM_OP_REG,ARM_REG_PC
from vita_elf_audit import VitaElf,Reachability,FunctionCFG,PT_LOAD
from topology_common import all_insns,ins_text,load_nid_names,import_stub_map,call_semantics,absolute_constants,function_parents

LOWIO_SHA='f791cfe2c6db955deb9446c57bb72ce1870bde2c6c485cac363e343d5128f744'
IFTU={0x0D7C02F7:'ksceIftuEnable',0x0FCBF457:'private_csc_cache_A',0x357EAE24:'private_control',0x67E37EFC:'ksceIftuCsc',0x7CE0C4DA:'ksceIftuSetInputFrameBuffer',0xAF19FD85:'ksceIftuSetMergeSetting',0xC11F30B3:'ksceIftuDisable',0xD64F4C6B:'private_csc_cache_B',0xE6EE2C6B:'ksceIftuSetOutputFormat'}
PHYS_HINTS={0xE5020000:'SceIftu0RegA',0xE5021000:'SceIftu0RegB',0xE5022000:'SceIftuc0Reg'}
CSC_FIELDS=['post_add_0','post_add_1_2','post_clamp_max_0','post_clamp_min_0','post_clamp_max_1_2','post_clamp_min_1_2','ctm00','ctm01','ctm02','ctm10','ctm11','ctm12','ctm20','ctm21','ctm22']

def find_export(e,nid):
 for lib in e.exports():
  for f in lib['functions']:
   if f['nid']==nid:return f
 return None

def va_for_file_offset(e,o):
 for p in e.phdrs:
  if p.p_type==PT_LOAD and p.p_offset<=o<p.p_offset+p.p_filesz:return p.p_vaddr+(o-p.p_offset)
 return None

def read_words(e,va,n):
 try:_,o=e.file_from_va(va)
 except ValueError:return None
 if o+4*n>len(e.data):return None
 return [int.from_bytes(e.data[o+4*i:o+4*i+4],'little') for i in range(n)]
def raw_word_refs(e,value):
 b=struct.pack('<I',value);out=[];start=0
 while True:
  i=e.data.find(b,start)
  if i<0:break
  out.append({'file_offset':i,'va':va_for_file_offset(e,i)});start=i+1
 return out

def regn(ins,op): return ins.reg_name(op.reg) if op.type==ARM_OP_REG else None

def enable_state_to_mmio(cfg):
    regs={};stack={};out=[];mmio_reg=None
    for ins in all_insns(cfg):
        if not (0x810063D4<=ins.address<0x81006624):continue
        ops=getattr(ins,'operands',[]);m=ins.mnemonic.lower();regs.setdefault('r4',('state_base',0))
        if m.startswith('add') and len(ops)>=3 and ops[0].type==ARM_OP_REG and ops[1].type==ARM_OP_REG and ops[2].type==ARM_OP_IMM:
            d,s=regn(ins,ops[0]),regn(ins,ops[1]);imm=int(ops[2].imm)
            if s=='r4':regs[d]=('state_ptr',imm)
        elif m.startswith('ldr') and len(ops)>=2 and ops[0].type==ARM_OP_REG and ops[1].type==ARM_OP_MEM:
            d=regn(ins,ops[0]);base=ins.reg_name(ops[1].mem.base);disp=int(ops[1].mem.disp)
            if ins.address==0x810063D8:regs[d]=('mmio_base',0);mmio_reg=d
            elif base=='r4':regs[d]=('state',disp)
            elif base=='sp' and disp in stack:regs[d]=stack[disp]
            else:regs.pop(d,None)
        elif m.startswith('ldrd') and len(ops)>=3 and ops[0].type==ARM_OP_REG and ops[1].type==ARM_OP_REG and ops[2].type==ARM_OP_MEM:
            d0,d1=regn(ins,ops[0]),regn(ins,ops[1]);base=ins.reg_name(ops[2].mem.base);disp=int(ops[2].mem.disp)
            if base=='r4':regs[d0]=('state',disp);regs[d1]=('state',disp+4)
            else:regs.pop(d0,None);regs.pop(d1,None)
        elif m.startswith('ldm') and len(ops)>=2 and ops[0].type==ARM_OP_REG:
            base=regn(ins,ops[0]);src=regs.get(base)
            if src and src[0]=='state_ptr':
                off=src[1]
                for op in ops[1:]:
                    if op.type==ARM_OP_REG:regs[regn(ins,op)]=('state',off);off+=4
        elif m.startswith('str') and len(ops)>=2 and ops[0].type==ARM_OP_REG and ops[1].type==ARM_OP_MEM:
            s=regn(ins,ops[0]);base=ins.reg_name(ops[1].mem.base);disp=int(ops[1].mem.disp)
            if base=='sp':
                if s in regs:stack[disp]=regs[s]
            elif base==mmio_reg and mmio_reg is not None:
                out.append({'va':ins.address,'mmio_offset':disp,'source':regs.get(s,('computed_or_constant',None)),'instruction':ins_text(ins)})
        if ops and ops[0].type==ARM_OP_REG and not m.startswith(('ldr','ldrd','ldm','add','str')) and m not in ('cmp','cmn','tst','teq'):regs.pop(regn(ins,ops[0]),None)
    return out

def private_setter_record(e,reach,nid,name,stubs):
    f=find_export(e,nid);cfg=reach.functions.get((f['va'],f['thumb'])) or FunctionCFG(e,f['va'],f['thumb'],reach.import_stubs)
    return {'nid':nid,'name':name,'va':f['va'],'calls':call_semantics(cfg,stubs),'absolute_constants':absolute_constants(e,cfg),'instructions':[{'va':i.address,'text':ins_text(i)} for i in all_insns(cfg)]}

def csc_register_maps(regmap):
    by_state={x['source'][1]:x['mmio_offset'] for x in regmap if x['source'][0]=='state'}
    def stage(cache,public_field,expected):
        rows=[]
        for word_index,regoff in expected:
            state=cache+4*word_index
            if by_state.get(state)!=regoff:raise SystemExit(f'CSC register-map drift state=0x{state:X}: {by_state.get(state)} != 0x{regoff:X}')
            rows.append({'word_index':word_index,'field':CSC_FIELDS[word_index],'cache_offset':state,'mmio_offset':regoff})
        return {'public_conv_field':public_field,'cache_base':cache,'register_fields':rows,'not_programmed_fields':[CSC_FIELDS[i] for i in range(15) if i not in {w for w,_ in expected}]}
    a_order=[0,1]+list(range(6,15));b_order=[0,1]+list(range(6,15))+list(range(2,6))
    a=stage(0x10C,'csc_params2',[(w,0x104+4*i) for i,w in enumerate(a_order)])
    b=stage(0x148,'csc_params1',[(w,0x130+4*i) for i,w in enumerate(b_order)])
    return {'stage_A_private_0FCBF457':a,'stage_B_private_D64F4C6B':b}

def data_table_xrefs(e,reach,lo,hi):
    out=[]
    for (start,thumb),cfg in reach.functions.items():
        for ins in all_insns(cfg):
            ops=getattr(ins,'operands',[]);m=ins.mnemonic.lower();target=None
            if m.startswith('ldr') and len(ops)>=2 and ops[1].type==ARM_OP_MEM and ops[1].mem.base==ARM_REG_PC:
                target=((ins.address+4)&~3)+int(ops[1].mem.disp)
            elif m.startswith('add') and len(ops)>=3 and ops[1].type==ARM_OP_REG and ops[1].reg==ARM_REG_PC and ops[2].type==ARM_OP_IMM:
                target=((ins.address+4)&~3)+int(ops[2].imm)
            if target is not None and lo<=target<hi:out.append({'function':start,'va':ins.address,'target_va':target,'instruction':ins_text(ins)})
    return out

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--lowio',type=Path,required=True);ap.add_argument('--nid-db-root',type=Path,required=True);ap.add_argument('--json',type=Path,required=True);a=ap.parse_args()
 e=VitaElf(a.lowio)
 if e.sha256!=LOWIO_SHA:raise SystemExit(f'SceLowio hash drift {e.sha256}')
 names=load_nid_names(a.nid_db_root);reach=Reachability(e,e.exports(),e.imports());stubs=import_stub_map(e,names);parents=function_parents(reach)
 exports=[]
 for nid,name in IFTU.items():
  f=find_export(e,nid)
  if not f:raise SystemExit(f'missing {name}')
  cfg=reach.functions.get((f['va'],f['thumb'])) or FunctionCFG(e,f['va'],f['thumb'],reach.import_stubs)
  exports.append({'nid':nid,'name':name,'va':f['va'],'instruction_count':cfg.instruction_count(),'calls':call_semantics(cfg,stubs),'absolute_constants':absolute_constants(e,cfg)})
 enable_f=find_export(e,0x0D7C02F7);enable_cfg=reach.functions[(enable_f['va'],enable_f['thumb'])];regmap=enable_state_to_mmio(enable_cfg);cscmaps=csc_register_maps(regmap)
 phys=[]
 for val,label in PHYS_HINTS.items():
  rows=raw_word_refs(e,val);code=[]
  for key,cfg in reach.functions.items():
   for c in absolute_constants(e,cfg):
    if c['value']==val:code.append({'function':cfg.start,'va':c['va'],'instruction':c['instruction'],'parents':parents.get(key,[])})
  phys.append({'physical_base':val,'public_label':label,'raw_word_refs':rows,'code_constant_refs':code})
 all_ref_vas=[r['va'] for p in phys for r in p['raw_word_refs'] if r['va'] is not None];lo=min(all_ref_vas)-0x20;hi=max(all_ref_vas)+0x40
 base_region={'start_va':lo,'words':read_words(e,lo,(hi-lo)//4),'code_xrefs':data_table_xrefs(e,reach,lo,hi)}
 broad=[]
 for key,cfg in reach.functions.items():
  for c in absolute_constants(e,cfg):
   if 0xE5020000<=c['value']<0xE5030000:broad.append({'value':c['value'],'function':cfg.start,'va':c['va'],'instruction':c['instruction']})
 roots=[]
 for name in ('start_entry','stop_entry'):
  raw=e.resolve_ibo32(e.modinfo.get(name,0xffffffff))
  if raw:
   key=(raw&~1,bool(raw&1));cfg=reach.functions.get(key)
   if cfg:roots.append({'kind':name,'start':cfg.start,'calls':call_semantics(cfg,stubs),'absolute_constants':absolute_constants(e,cfg),'instructions':[{'va':i.address,'text':ins_text(i)} for i in all_insns(cfg)]})
 result={'schema':2,'firmware':'3.65','elf_sha256':e.sha256,'iftu_exports':sorted(exports,key=lambda x:x['va']),'physical_base_evidence':phys,'physical_base_data_region':base_region,'broad_e502_code_constants':broad,'module_lifecycle':roots,'enable_state_to_register_map':regmap,'csc_field_to_mmio':cscmaps,'private_setters':[private_setter_record(e,reach,0x0FCBF457,'private_csc_cache_A',stubs),private_setter_record(e,reach,0xD64F4C6B,'private_csc_cache_B',stubs)],'notes':['CSC field-to-MMIO map is asserted against Sony ksceIftuEnable symbolic provenance and matches direct private-setter register writes','stage A does not program the four post-clamp fields; stage B does','physical-base identity still requires binding the selected per-plane register pointer to the physical-base table/init path before direct MMIO mutation is considered']}
 a.json.write_text(json.dumps(result,indent=2)+'\n')
 print('IFTU_MMIO_TOPOLOGY')
 for p in phys:print(f"  physical 0x{p['physical_base']:08X} {p['public_label']} raw_refs={p['raw_word_refs']} code_refs={[(hex(x['function']),hex(x['va'])) for x in p['code_constant_refs']]}")
 print('CSC_FIELD_TO_MMIO')
 for sn,s in cscmaps.items():
  print(f"  {sn} public={s['public_conv_field']} cache=0x{s['cache_base']:X}")
  for x in s['register_fields']:print(f"    word{x['word_index']:02d} {x['field']} cache+0x{x['cache_offset']-s['cache_base']:02X} -> reg+0x{x['mmio_offset']:03X}")
  print(f"    not_programmed={s['not_programmed_fields']}")
 print('PHYSICAL_BASE_TABLE_XREFS')
 for x in base_region['code_xrefs']:print(f"  fn=0x{x['function']:08X} insn=0x{x['va']:08X} -> 0x{x['target_va']:08X} {x['instruction']}")
if __name__=='__main__':main()
