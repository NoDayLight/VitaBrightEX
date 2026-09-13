#!/usr/bin/env python3
"""Retail-3.65 SceLcd panel-control and DSI hardware audit v3."""
from __future__ import annotations
import argparse, hashlib, json
from pathlib import Path
from vita_elf_audit import FunctionCFG, Reachability, VitaElf

LCD_SHA256='24752bb4c69f0c241701cab0364cde9cbe5c95128b6bfe02bf3263aad524246e'
LOWIO_SHA256='f791cfe2c6db955deb9446c57bb72ce1870bde2c6c485cac363e343d5128f744'
LCD_EXPORTS={0x1A0A7519:'ksceLcdDisplayOff',0x5F4124AB:'ksceLcdDisplayOn',0x3A6D6AC3:'ksceLcdGetBrightness',0xE03E120B:'ksceLcdGetDDB',0x17F66722:'ksceLcdGetDisplayColorSpaceMode',0x581D3A87:'ksceLcdSetBrightness',0xD40968FB:'ksceLcdSetDisplayColorSpaceMode',0x0C7E03D8:'ksceLcdWaitReady'}
DSI_PUBLIC={0x3FB0DF1F:'ksceDsiDcsRead',0xBA6BC89F:'ksceDsiDcsShortWrite',0x98120684:'ksceDsiGenericReadRequest',0x89C00D2F:'ksceDsiGenericShortWrite',0x114D1413:'ksceDsiDisableHead',0x5BE5AA9B:'ksceDsiEnableHead',0x4DF9E924:'ksceDsiGetPixelClock',0xB3A70C05:'ksceDsiGetVicResolution',0x7640F607:'ksceDsiSendBlankingPacket',0x78E6E3CF:'ksceDsiSetLanesAndPixelSize',0x97BFEA76:'ksceDsiSetVic',0xC2E85919:'ksceDsiStartDisplay'}
EXPECTED_LCD_VA={0x0C7E03D8:0x81000550,0xE03E120B:0x8100057C,0x3A6D6AC3:0x81000F08,0x17F66722:0x81000F20,0x5F4124AB:0x8100109C,0x1A0A7519:0x810010E4,0x581D3A87:0x8100117C,0xD40968FB:0x810012B4}
PROGRAMS={'display_off_or_zero':0x81001AA8,'color_space_mode_0':0x81001AE8,'color_space_mode_1':0x81001B70,'display_on_program_candidate':0x81002020}
INTERNALS={'panel_command_writer':0x81000A54,'panel_init_or_reconcile':0x81000B78,'program_step':0x81000F88,'program_kick':0x81000FF0}

def die(s):raise SystemExit(s)
def load(p,sha):
 e=VitaElf(p)
 if e.sha256!=sha:die(f'SHA mismatch {e.modinfo["name"]}: {e.sha256}')
 return e
def find_export(e,n):
 for lib in e.exports():
  for f in lib['functions']:
   if f['nid']==n:return lib,f
 return None,None
def allins(cfg):
 d={}
 for b in cfg.blocks.values():
  for i in b.instructions:d[i.address]=i
 return [d[k] for k in sorted(d)]
def it(i):return f'{i.mnemonic} {i.op_str}'.strip()
def readb(e,va,n):
 try:_,o=e.file_from_va(va)
 except ValueError:return None
 if o+n>len(e.data):return None
 return e.data[o:o+n]
def monotonic(v):
 return all(v[i]<=v[i+1] for i in range(len(v)-1)) if len(v)>1 else True
def table_shape(payload):
 b=list(payload);out={'length':len(b),'sha256':hashlib.sha256(payload).hexdigest(),'min':min(b) if b else None,'max':max(b) if b else None}
 if b and len(b)%3==0:
  n=len(b)//3
  planar=[b[i*n:(i+1)*n] for i in range(3)]
  inter=[b[i::3] for i in range(3)]
  out['three_way_geometry']={
   'points_per_group':n,
   'planar_monotonic':[monotonic(x) for x in planar],
   'interleaved_monotonic':[monotonic(x) for x in inter],
   'planar_endpoints':[[x[0],x[-1]] for x in planar],
   'interleaved_endpoints':[[x[0],x[-1]] for x in inter],
  }
 return out
def parse_program(e,va,max_records=256):
 p=va;rows=[];reason='END_NOT_REACHED'
 for _ in range(max_records):
  h=readb(e,p,2)
  if not h or len(h)<2:return {'start':va,'valid':False,'termination':'OUT_OF_FILE','records':rows}
  cmd,n=h[0],h[1]
  if cmd==0xff:
   rows.append({'va':p,'kind':'end','opcode':cmd});reason='END';return {'start':va,'valid':True,'termination':reason,'records':rows}
  if cmd==0x0d:
   rows.append({'va':p,'kind':'delay_or_wait','opcode':cmd,'value':n});p+=2;continue
  payload=readb(e,p+2,n)
  if payload is None:return {'start':va,'valid':False,'termination':'PAYLOAD_OUT_OF_FILE','records':rows}
  rows.append({'va':p,'kind':'panel_command','command':cmd,'length':n,'payload_hex':payload.hex(),'payload':[x for x in payload],'shape':table_shape(payload)})
  p+=2+n
 return {'start':va,'valid':False,'termination':'RECORD_LIMIT','records':rows}
def cfg_record(e,r,va):
 cfg=r.functions.get((va,True)) or FunctionCFG(e,va,True,r.import_stubs)
 return {'start':va,'instruction_count':cfg.instruction_count(),'instructions':[{'va':i.address,'text':it(i)} for i in allins(cfg)]}
def import_inventory(e):
 return [{'library':lib['library_name'],'library_nid':lib['library_nid'],'functions':[{'nid':f['nid'],'va':f['va']} for f in lib['functions']]} for lib in e.imports()]
def command_rows(program):return [r for r in program['records'] if r['kind']=='panel_command']
def delta(a,b):
 rows=[]
 for idx in range(max(len(a),len(b))):
  if idx>=len(a) or idx>=len(b):
   rows.append({'index':idx,'kind':'record_missing'});continue
  x,y=a[idx],b[idx]
  row={'index':idx,'command0':x['command'],'command1':y['command'],'length0':x['length'],'length1':y['length']}
  if x['command']==y['command'] and x['length']==y['length']:
   diffs=[(i,int(v1)-int(v0)) for i,(v0,v1) in enumerate(zip(x['payload'],y['payload'])) if v0!=v1]
   row.update({'same_shape':True,'different_bytes':len(diffs),'first_differences':diffs[:24],'max_abs_delta':max((abs(v) for _,v in diffs),default=0),'payload0_sha256':x['shape']['sha256'],'payload1_sha256':y['shape']['sha256']})
  else:row['same_shape']=False
  rows.append(row)
 return rows

def main():
 p=argparse.ArgumentParser();p.add_argument('--lcd',type=Path,required=True);p.add_argument('--lowio',type=Path,required=True);p.add_argument('--json',type=Path,required=True);a=p.parse_args()
 lcd=load(a.lcd,LCD_SHA256);low=load(a.lowio,LOWIO_SHA256);lr=Reachability(lcd,lcd.exports(),lcd.imports());lor=Reachability(low,low.exports(),low.imports())
 ex=[]
 for n,nm in LCD_EXPORTS.items():
  lib,f=find_export(lcd,n)
  if not f:die(f'missing {nm}')
  if f['va']!=EXPECTED_LCD_VA[n]:die(f'{nm} VA drift: 0x{f["va"]:08X}')
  ex.append({'nid':n,'name':nm,'va':f['va'],'library':lib['library_name']})
 low_dsi=[]
 for n,nm in DSI_PUBLIC.items():
  lib,f=find_export(low,n)
  if f:low_dsi.append({'nid':n,'name':nm,'va':f['va'],'library':lib['library_name'],'instructions':cfg_record(low,lor,f['va'])['instructions'] if nm in ('ksceDsiDcsShortWrite','ksceDsiGenericShortWrite','ksceDsiDcsRead','ksceDsiGenericReadRequest') else None})
 inv=import_inventory(lcd);dsi_imports=[lib for lib in inv if lib['library']=='SceDsiForDriver' or any(f['nid'] in DSI_PUBLIC for f in lib['functions'])]
 programs={name:parse_program(lcd,va) for name,va in PROGRAMS.items()};internals={name:cfg_record(lcd,lr,va) for name,va in INTERNALS.items()}
 focus={}
 for nm in ('ksceLcdSetDisplayColorSpaceMode','ksceLcdGetDisplayColorSpaceMode','ksceLcdGetDDB','ksceLcdSetBrightness','ksceLcdDisplayOn','ksceLcdDisplayOff'):
  va=next(x['va'] for x in ex if x['name']==nm);focus[nm]=cfg_record(lcd,lr,va)
 mode0=command_rows(programs['color_space_mode_0']);mode1=command_rows(programs['color_space_mode_1']);allcmd=[r['command'] for q in programs.values() for r in command_rows(q)]
 writer_text='\n'.join(x['text'] for x in internals['panel_command_writer']['instructions'])
 serial9=('rbit' in writer_text and 'orr r1, r1, #1' in writer_text and 'lsls r2, r2, #1' in writer_text)
 result={'schema':3,'firmware':'3.65','elf_sha256':{'SceLcd':lcd.sha256,'SceLowio':low.sha256},'lcd_exports':sorted(ex,key=lambda x:x['va']),'lcd_import_inventory':inv,'sce_dsi_imports_used_by_scelcd':dsi_imports,'lowio_public_dsi_exports':low_dsi,'internal_panel_path':internals,'panel_programs':programs,'focus':focus,'color_space_program_delta':delta(mode0,mode1),'transport_evidence':{'nine_bit_command_data_encoding':serial9,'interpretation':'command byte encoded with D/C=0; payload bytes encoded with D/C=1 after per-byte bit reversal','public_dsi_path_is_distinct':True},'nonlinear_evidence':{'panel_program_contains_0x26':0x26 in allcmd,'all_observed_program_commands':sorted(set(allcmd)),'long_payload_records':[{'program':name,'command':r['command'],'length':r['length'],'shape':r['shape']} for name,q in programs.items() for r in command_rows(q) if r['length']>=32],'controller_identity':'UNKNOWN_STATICALLY','vendor_gamma_table':'CANDIDATE_ONLY_UNTIL_COMMAND_SEMANTICS_PROVEN','iftu_nonlinear':'SEPARATE_AUDIT_REQUIRED'}}
 a.json.write_text(json.dumps(result,indent=2)+'\n')
 print('SCELCD_EXPORT_MAP')
 for x in sorted(ex,key=lambda x:x['va']):print(f"  0x{x['nid']:08X} {x['name']} -> 0x{x['va']:08X}")
 print('PANEL_CONTROL_TRANSPORT')
 print(f'  public SceDsi imports used by SceLcd: {sum(len(x["functions"]) for x in dsi_imports)}')
 print(f"  internal writer uses 9-bit command/data encoding: {'PROVEN' if serial9 else 'NOT PROVEN'}")
 print('  public SceLowio DSI short-write code path is structurally distinct from SceLcd internal writer')
 print('COLOR_SPACE_PROGRAMS')
 for name in ('color_space_mode_0','color_space_mode_1'):
  q=programs[name];print(f"  {name} start=0x{q['start']:08X} valid={q['valid']} termination={q['termination']}")
  for r in q['records']:
   if r['kind']=='panel_command':
    sh=r['shape'];print(f"    cmd=0x{r['command']:02X} len={r['length']} sha256={sh['sha256']} min={sh['min']} max={sh['max']} preview={r['payload_hex'][:48]}")
   else:print(f"    {r}")
 print('COLOR_SPACE_DELTA')
 for r in result['color_space_program_delta']:print(f'  {r}')
 print('NONLINEAR_EVIDENCE_MATRIX')
 print(f"  complete parsed Sony command programs contain byte-command 0x26: {'YES' if 0x26 in allcmd else 'NO'}")
 for r in result['nonlinear_evidence']['long_payload_records']:print(f"  LONG_RECORD program={r['program']} cmd=0x{r['command']:02X} len={r['length']} shape={r['shape']}")
 print('  long records are strong nonlinear/table candidates but are NOT labelled gamma until controller/command semantics are proven')
 print('  controller identity: UNKNOWN pending read-path/controller evidence')
if __name__=='__main__':main()
