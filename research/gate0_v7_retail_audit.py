#!/usr/bin/env python3
from __future__ import annotations
import argparse,json
from pathlib import Path
from vita_elf_audit import VitaElf,Reachability,FunctionCFG
from topology_common import all_insns,ins_text
LOWIO_SHA='f791cfe2c6db955deb9446c57bb72ce1870bde2c6c485cac363e343d5128f744';LCD_SHA='24752bb4c69f0c241701cab0364cde9cbe5c95128b6bfe02bf3263aad524246e';CSC_A_NID=0x0FCBF457;CSC_B_NID=0xD64F4C6B;ENABLE_NID=0x0D7C02F7;CSC_A_VA=0x81005D48;CSC_B_VA=0x81005E24;WRITER=0x81000A54;READER=0x810005B4
def die(x):raise SystemExit(x)
def exports_for(e,n):return[(l,f)for l in e.exports()for f in l['functions']if f['nid']==n]
def cfg_at(e,v):
 r=Reachability(e,e.exports(),e.imports(),extra_starts=(v,));return r.functions.get((v,True))or FunctionCFG(e,v,True,r.import_stubs,boundary_index=r.boundaries,noreturn_stubs=r.noreturn_stubs)
def norm(i):return ins_text(i).lower().replace(' ','')
def byva(c):return{i.address:i for i in all_insns(c)}
def exact(b,v,*a):
 g=norm(b[v])if v in b else'<missing>'
 if g not in[x.replace(' ','').lower()for x in a]:die(f'0x{v:08X}: {g} not in {a}')
def prove_csc(e,n,v,nv,base,load,inc,cmpv,last):
 ex=exports_for(e,n)
 if len(ex)!=1 or ex[0][1]['va']!=v:die(f'CSC export 0x{n:08X} identity mismatch')
 c=cfg_at(e,v);b=byva(c);exact(b,v,'cmp r0, #4');exact(b,v+6,'mov r4, r1')
 if nv not in b or not norm(b[nv]).startswith('cbz'):die('CSC NULL branch missing')
 exact(b,base,'add.w r0, r4, #0x30')
 if load not in b or not norm(b[load]).startswith('ldr'):die('CSC loop load missing')
 exact(b,inc,'adds r3, #0x10');exact(b,cmpv,'cmp r3, r0')
 for off in(last,last+4,last+6):
  if off not in b or not norm(b[off]).startswith('ldr'):die(f'CSC final 0x3C load missing at 0x{off:08X}')
 return{'nid':f'0x{n:08X}','va':f'0x{v:08X}','logical_range':[f'0x{c.start:08X}',f'0x{c.logical_end:08X}'],'copy_safety':'valid plane + non-NULL consumes complete 0x3C object','return':'raw r0'}
def incoming_arg_unused(c,names):
 blocks=c.blocks;preds={k:[]for k in blocks}
 for k,b in blocks.items():
  for s in b.successors:
   if s in preds:preds[s].append(k)
 allr=set(names);entry={k:(set()if k==c.start else set(allr))for k in blocks};out={k:set()for k in blocks};changed=True
 while changed:
  changed=False
  for k in sorted(blocks):
   if k!=c.start:
    ps=preds[k];new=set.intersection(*(out[p]for p in ps))if ps else set()
    if new!=entry[k]:entry[k]=new;changed=True
   defined=set(entry[k])
   for ins in blocks[k].instructions:
    try:reads,writes=ins.regs_access()
    except Exception:reads,writes=([],[])
    rn={ins.reg_name(x)for x in reads};wn={ins.reg_name(x)for x in writes};bad=(rn&allr)-defined
    if bad:return False,{'va':f'0x{ins.address:08X}','instruction':ins_text(ins),'incoming_reads':sorted(bad)}
    defined|=(wn&allr)
   if defined!=out[k]:out[k]=defined;changed=True
 return True,None
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--lowio',type=Path,required=True);ap.add_argument('--lcd',type=Path,required=True);ap.add_argument('--json',type=Path,required=True);ap.add_argument('--text',type=Path,required=True);a=ap.parse_args();lo,lc=VitaElf(a.lowio),VitaElf(a.lcd)
 if lo.sha256!=LOWIO_SHA:die('SceLowio hash drift')
 if lc.sha256!=LCD_SHA:die('SceLcd hash drift')
 ca=prove_csc(lo,CSC_A_NID,CSC_A_VA,0x81005D72,0x81005D7A,0x81005D7E,0x81005D82,0x81005D92,0x81005DA6);cb=prove_csc(lo,CSC_B_NID,CSC_B_VA,0x81005E50,0x81005E58,0x81005E5C,0x81005E60,0x81005E70,0x81005E84)
 en=exports_for(lo,ENABLE_NID)
 if len(en)!=1:die(f'IFTU enable export must be unique, got {len(en)}')
 fn=en[0][1];eva=fn['va'];ec=cfg_at(lo,eva);eb=byva(ec)
 if eb.get(eva)is None:die('IFTU enable entry undecodable')
 ok,why=incoming_arg_unused(ec,('r1','r2','r3'))
 if not ok:die('IFTU enable incoming r1-r3 semantically required: '+json.dumps(why))
 r0=False
 for ins in all_insns(ec):
  try:reads,writes=ins.regs_access()
  except Exception:continue
  if'r0'in{ins.reg_name(x)for x in reads}:r0=True;break
  if'r0'in{ins.reg_name(x)for x in writes}:break
 if not r0:die('IFTU enable does not prove incoming r0 use')
 wb=byva(cfg_at(lc,WRITER));rb=byva(cfg_at(lc,READER));exact(wb,0x81000A60,'mov r5, r0');exact(wb,0x81000A62,'mov sb, r1');exact(wb,0x81000A64,'mov r8, r2');exact(rb,0x810005C0,'mov sb, r0');exact(rb,0x810005C2,'mov r7, r1');exact(rb,0x810005C4,'mov r8, r2')
 doc={'schema':1,'firmware':'3.65','hashes':{'SceLowio':lo.sha256,'SceLcd':lc.sha256},'csc_a':ca,'csc_b':cb,'iftu_enable':{'nid':'0x0D7C02F7','name':'ksceIftuEnable','evidence_class':'RETAIL_365_DISASSEMBLY_DERIVED','va':f'0x{eva:08X}','logical_range':[f'0x{ec.start:08X}',f'0x{ec.logical_end:08X}'],'abi':'int (int plane)','incoming_r0':'read before overwrite','incoming_r1_r3':'not read before definite overwrite on any reachable path','return':'raw r0 / AAPCS int-compatible'},'panel_writer':{'va':'0x81000A54','abi':'int (unsigned command, const void *ptr, unsigned len)','payload_policy':'GATE0A_METADATA_ONLY'},'panel_reader':{'va':'0x810005B4','abi':'int (unsigned command, void *ptr, unsigned len)','payload_policy':'GATE0A_METADATA_ONLY'}}
 a.json.write_text(json.dumps(doc,indent=2)+'\n');lines=['GATE0_V7_RETAIL_ABI=PASS',f'IFTU_ENABLE_EXPORT_VA=0x{eva:08X}','IFTU_ENABLE_ABI=RETAIL_365_DISASSEMBLY_DERIVED int(int plane)','CSC_COPY_SAFETY=PASS exact 0x3C for valid non-NULL only','PANEL_ABI=PASS writer 0x81000A54 reader 0x810005B4 metadata-only'];a.text.write_text('\n'.join(lines)+'\n');print('\n'.join(lines))
if __name__=='__main__':main()
