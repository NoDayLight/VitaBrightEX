#!/usr/bin/env python3
"""Retail-3.65 SceLcd vs public SceDsi topology audit.

This tool deliberately does NOT flat-parse SceLcd source program roots. Sony builds a
composite working program in 0x81000B78 and the source roots overlap byte ranges;
flat parsing them independently produced false structure in earlier research.
Use lcd_panel_topology_audit.py for the actual SPI panel-control path.
"""
from __future__ import annotations
import argparse,json
from pathlib import Path
from vita_elf_audit import VitaElf

LCD_SHA256='24752bb4c69f0c241701cab0364cde9cbe5c95128b6bfe02bf3263aad524246e'
LOWIO_SHA256='f791cfe2c6db955deb9446c57bb72ce1870bde2c6c485cac363e343d5128f744'
LCD_EXPORTS={0x1A0A7519:'ksceLcdDisplayOff',0x5F4124AB:'ksceLcdDisplayOn',0x3A6D6AC3:'ksceLcdGetBrightness',0xE03E120B:'ksceLcdGetDDB',0x17F66722:'ksceLcdGetDisplayColorSpaceMode',0x581D3A87:'ksceLcdSetBrightness',0xD40968FB:'ksceLcdSetDisplayColorSpaceMode',0x0C7E03D8:'ksceLcdWaitReady'}
DSI_PUBLIC={0x3FB0DF1F:'ksceDsiDcsRead',0xBA6BC89F:'ksceDsiDcsShortWrite',0x98120684:'ksceDsiGenericReadRequest',0x89C00D2F:'ksceDsiGenericShortWrite',0x114D1413:'ksceDsiDisableHead',0x5BE5AA9B:'ksceDsiEnableHead',0x4DF9E924:'ksceDsiGetPixelClock',0xB3A70C05:'ksceDsiGetVicResolution',0x7640F607:'ksceDsiSendBlankingPacket',0x78E6E3CF:'ksceDsiSetLanesAndPixelSize',0x97BFEA76:'ksceDsiSetVic',0xC2E85919:'ksceDsiStartDisplay'}
EXPECTED_LCD_VA={0x0C7E03D8:0x81000550,0xE03E120B:0x8100057C,0x3A6D6AC3:0x81000F08,0x17F66722:0x81000F20,0x5F4124AB:0x8100109C,0x1A0A7519:0x810010E4,0x581D3A87:0x8100117C,0xD40968FB:0x810012B4}

def load(p,sha):
 e=VitaElf(p)
 if e.sha256!=sha:raise SystemExit(f'SHA mismatch {e.modinfo["name"]}: {e.sha256}')
 return e
def find_export(e,n):
 for lib in e.exports():
  for f in lib['functions']:
   if f['nid']==n:return lib,f
 return None,None

def main():
 p=argparse.ArgumentParser();p.add_argument('--lcd',type=Path,required=True);p.add_argument('--lowio',type=Path,required=True);p.add_argument('--json',type=Path,required=True);a=p.parse_args();lcd=load(a.lcd,LCD_SHA256);low=load(a.lowio,LOWIO_SHA256)
 ex=[]
 for n,nm in LCD_EXPORTS.items():
  lib,f=find_export(lcd,n)
  if not f or f['va']!=EXPECTED_LCD_VA[n]:raise SystemExit(f'{nm} export/VA drift')
  ex.append({'nid':n,'name':nm,'va':f['va'],'library':lib['library_name']})
 inv=[]
 for lib in lcd.imports():inv.append({'library':lib['library_name'],'library_nid':lib['library_nid'],'functions':[{'nid':f['nid'],'va':f['va']} for f in lib['functions']]})
 dsi_imports=[lib for lib in inv if lib['library']=='SceDsiForDriver' or any(f['nid'] in DSI_PUBLIC for f in lib['functions'])]
 low_dsi=[]
 for n,nm in DSI_PUBLIC.items():
  lib,f=find_export(low,n)
  if f:low_dsi.append({'nid':n,'name':nm,'va':f['va'],'library':lib['library_name']})
 result={'schema':3,'firmware':'3.65','elf_sha256':{'SceLcd':lcd.sha256,'SceLowio':low.sha256},'lcd_exports':sorted(ex,key=lambda x:x['va']),'lcd_import_inventory':inv,'sce_dsi_imports_used_by_scelcd':dsi_imports,'lowio_public_dsi_exports':low_dsi,'flat_source_program_parser':'REMOVED_AS_INVALID_MODEL','panel_control_topology':'SceLcd uses a private Pervasive-SPI/GPIO panel register-control path; see panel-bus-map.json','standard_dcs_0x26_status':'UNRESOLVED_SECONDARY_HYPOTHESIS; absence from a flat source parse is not evidence','reason':'Sony composes overlapping source segments into a working command program in internal function 0x81000B78; source roots cannot be treated as standalone linear command streams.'}
 a.json.write_text(json.dumps(result,indent=2)+'\n')
 print('SCELCD_EXPORT_MAP')
 for x in sorted(ex,key=lambda x:x['va']):print(f"  0x{x['nid']:08X} {x['name']} -> 0x{x['va']:08X}")
 print(f'SceDsiForDriver imports used by retail SceLcd: {sum(len(x["functions"]) for x in dsi_imports)}')
 print('FLAT_SOURCE_PROGRAM_PARSER REMOVED_AS_INVALID_MODEL')
 print('PANEL_CONTROL SceLcd private Pervasive-SPI/GPIO path; detailed composition in panel-bus-map.json')
if __name__=='__main__':main()
