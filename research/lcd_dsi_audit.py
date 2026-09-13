#!/usr/bin/env python3
"""Retail-3.65 SceLcd panel-command and DSI hardware audit v2."""
from __future__ import annotations
import argparse,json
from pathlib import Path
from vita_elf_audit import FunctionCFG, Reachability, VitaElf

LCD_SHA256='24752bb4c69f0c241701cab0364cde9cbe5c95128b6bfe02bf3263aad524246e'
LOWIO_SHA256='f791cfe2c6db955deb9446c57bb72ce1870bde2c6c485cac363e343d5128f744'
LCD_EXPORTS={0x1A0A7519:'ksceLcdDisplayOff',0x5F4124AB:'ksceLcdDisplayOn',0x3A6D6AC3:'ksceLcdGetBrightness',0xE03E120B:'ksceLcdGetDDB',0x17F66722:'ksceLcdGetDisplayColorSpaceMode',0x581D3A87:'ksceLcdSetBrightness',0xD40968FB:'ksceLcdSetDisplayColorSpaceMode',0x0C7E03D8:'ksceLcdWaitReady'}
DSI_PUBLIC={0x3FB0DF1F:'ksceDsiDcsRead',0xBA6BC89F:'ksceDsiDcsShortWrite',0x98120684:'ksceDsiGenericReadRequest',0x89C00D2F:'ksceDsiGenericShortWrite',0x114D1413:'ksceDsiDisableHead',0x5BE5AA9B:'ksceDsiEnableHead',0x4DF9E924:'ksceDsiGetPixelClock',0xB3A70C05:'ksceDsiGetVicResolution',0x7640F607:'ksceDsiSendBlankingPacket',0x78E6E3CF:'ksceDsiSetLanesAndPixelSize',0x97BFEA76:'ksceDsiSetVic',0xC2E85919:'ksceDsiStartDisplay'}
EXPECTED_LCD_VA={0x0C7E03D8:0x81000550,0xE03E120B:0x8100057C,0x3A6D6AC3:0x81000F08,0x17F66722:0x81000F20,0x5F4124AB:0x8100109C,0x1A0A7519:0x810010E4,0x581D3A87:0x8100117C,0xD40968FB:0x810012B4}
PROGRAMS={'display_off_or_zero':0x81001AA8,'color_space_mode_0':0x81001AE8,'color_space_mode_1':0x81001B70,'display_on_program_candidate':0x81002020}
INTERNALS={'panel_command_writer':0x81000A54,'panel_init_or_reconcile':0x81000B78,'program_step':0x81000F88,'program_kick':0x81000FF0,'async_brightness_worker':0x81001A6C}

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
def parse_program(e,va,max_records=128):
 p=va;rows=[];valid=True;reason='END_NOT_REACHED'
 for _ in range(max_records):
  h=readb(e,p,2)
  if not h or len(h)<2:valid=False;reason='OUT_OF_FILE';break
  cmd,n=h[0],h[1]
  if cmd==0xff:
   rows.append({'va':p,'kind':'end','opcode':cmd});reason='END';break
  if cmd==0x0d:
   rows.append({'va':p,'kind':'delay_or_wait','opcode':cmd,'value':n});p+=2;continue
  if n>96:
   valid=False;reason=f'IMPLAUSIBLE_LENGTH_{n}';break
  payload=readb(e,p+2,n)
  if payload is None:valid=False;reason='PAYLOAD_OUT_OF_FILE';break
  rows.append({'va':p,'kind':'panel_command','command':cmd,'length':n,'payload_hex':payload.hex(),'payload':[x for x in payload]})
  p+=2+n
 else:valid=False;reason='RECORD_LIMIT'
 return {'start':va,'valid':valid,'termination':reason,'records':rows}
def cfg_record(e,r,va):
 cfg=r.functions.get((va,True)) or FunctionCFG(e,va,True,r.import_stubs)
 return {'start':va,'instruction_count':cfg.instruction_count(),'instructions':[{'va':i.address,'text':it(i)} for i in allins(cfg)]}
def import_inventory(e):
 out=[]
 for lib in e.imports():
  out.append({'library':lib['library_name'],'library_nid':lib['library_nid'],'functions':[{'nid':f['nid'],'va':f['va']} for f in lib['functions']]})
 return out

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
 inv=import_inventory(lcd)
 dsi_imports=[lib for lib in inv if lib['library']=='SceDsiForDriver' or any(f['nid'] in DSI_PUBLIC for f in lib['functions'])]
 programs={name:parse_program(lcd,va) for name,va in PROGRAMS.items()}
 internals={name:cfg_record(lcd,lr,va) for name,va in INTERNALS.items()}
 focus={}
 for nm in ('ksceLcdSetDisplayColorSpaceMode','ksceLcdGetDisplayColorSpaceMode','ksceLcdGetDDB','ksceLcdSetBrightness','ksceLcdDisplayOn','ksceLcdDisplayOff'):
  va=next(x['va'] for x in ex if x['name']==nm);focus[nm]=cfg_record(lcd,lr,va)
 mode0=[r for r in programs['color_space_mode_0']['records'] if r['kind']=='panel_command'];mode1=[r for r in programs['color_space_mode_1']['records'] if r['kind']=='panel_command']
 allcmd=[r['command'] for pgr in programs.values() for r in pgr['records'] if r['kind']=='panel_command']
 result={'schema':2,'firmware':'3.65','elf_sha256':{'SceLcd':lcd.sha256,'SceLowio':low.sha256},'lcd_exports':sorted(ex,key=lambda x:x['va']),'lcd_import_inventory':inv,'sce_dsi_imports_used_by_scelcd':dsi_imports,'lowio_public_dsi_exports':low_dsi,'internal_panel_path':internals,'panel_programs':programs,'focus':focus,'color_space_program_delta':{'mode0_commands':mode0,'mode1_commands':mode1},'nonlinear_evidence':{'panel_program_contains_0x26':0x26 in allcmd,'all_observed_program_commands':sorted(set(allcmd)),'controller_identity':'UNKNOWN_STATICALLY','vendor_gamma_table':'UNCLASSIFIED','iftu_nonlinear':'SEPARATE_AUDIT_REQUIRED'}}
 a.json.write_text(json.dumps(result,indent=2)+'\n')
 print('SCELCD_EXPORT_MAP')
 for x in sorted(ex,key=lambda x:x['va']):print(f"  0x{x['nid']:08X} {x['name']} -> 0x{x['va']:08X}")
 print('SCELCD_DSI_IMPORT_RESULT')
 print(f'  SceDsiForDriver imports used by retail SceLcd: {sum(len(x["functions"]) for x in dsi_imports)}')
 if not dsi_imports:print('  RESULT: ZERO. SceLcd uses an internal/direct panel transport path rather than the public SceDsi ABI.')
 print('LOWIO_PUBLIC_DSI_EXPORT_MAP')
 for x in low_dsi:print(f"  0x{x['nid']:08X} {x['name']} -> 0x{x['va']:08X}")
 print('COLOR_SPACE_PROGRAMS')
 for name in ('color_space_mode_0','color_space_mode_1'):
  q=programs[name];print(f"  {name} start=0x{q['start']:08X} valid={q['valid']} termination={q['termination']}")
  for r in q['records']:
   if r['kind']=='panel_command':print(f"    cmd=0x{r['command']:02X} len={r['length']} payload={r['payload_hex']}")
   else:print(f"    {r}")
 print('COLOR_SPACE_DELTA')
 a0=[(r['command'],r['payload_hex']) for r in mode0];a1=[(r['command'],r['payload_hex']) for r in mode1]
 print(f'  identical={a0==a1}')
 print(f'  mode0_only={[x for x in a0 if x not in a1]}')
 print(f'  mode1_only={[x for x in a1 if x not in a0]}')
 print('NONLINEAR_EVIDENCE_MATRIX')
 print(f"  observed command 0x26 in Sony panel programs: {'YES' if 0x26 in allcmd else 'NO'}")
 print(f"  observed command set: {[hex(x) for x in sorted(set(allcmd))]}")
 print('  IMPORTANT: command bytes are not labelled MIPI-DCS until the internal writer is matched to Lowio DCS packet semantics.')
 print('  controller identity: UNKNOWN pending command/read-path interpretation')
if __name__=='__main__':main()
