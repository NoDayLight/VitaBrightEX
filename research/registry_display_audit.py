#!/usr/bin/env python3
from __future__ import annotations
import argparse,json
from pathlib import Path
from vita_elf_audit import VitaElf
from topology_common import load_nid_names,best_name

TERMS=[b'/CONFIG/DISPLAY/',b'color_space_mode',b'rgb_range_mode',b'gamma',b'contrast',b'color_temperature',b'white_balance',b'calibration',b'color_matrix',b'screen_mode',b'panel_mode']

def matches(data):
 low=data.lower();out=[]
 for t in TERMS:
  if t.lower() in low:out.append(t.decode())
 return out

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--elf-dir',type=Path,required=True);ap.add_argument('--nid-db-root',type=Path,required=True);ap.add_argument('--json',type=Path,required=True);a=ap.parse_args()
 names=load_nid_names(a.nid_db_root);rows=[]
 for p in sorted(a.elf_dir.glob('*.elf')):
  try:e=VitaElf(p)
  except Exception:continue
  mm=matches(e.data);reg=[]
  for lib in e.imports():
   if 'RegMgr' in lib['library_name']:
    reg.append({'library':lib['library_name'],'functions':[{'nid':f['nid'],'name':best_name(names,f['nid'])} for f in lib['functions']]})
  if mm or reg:rows.append({'file':p.name,'module':e.modinfo['name'],'matched_display_terms':mm,'regmgr_imports':reg})
 result={'schema':1,'firmware':'3.65','kernel_modules_with_registry_or_color_terms':rows,'terms_searched':[x.decode() for x in TERMS],'scope':'decrypted retail-3.65 os0 kernel ELFs only','limitations':['registry.db0 defaults and user-mode Settings/AVConfig binaries are outside this os0-only pass unless present in os0:/kd; absence here is not a topology-exhaustion result']}
 a.json.write_text(json.dumps(result,indent=2)+'\n')
 print('REGISTRY_DISPLAY_CONSUMERS')
 for r in rows:print(f"  {r['file']} module={r['module']} terms={r['matched_display_terms']} regmgr={[x['library'] for x in r['regmgr_imports']]}")
if __name__=='__main__':main()
