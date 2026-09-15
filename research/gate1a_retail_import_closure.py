#!/usr/bin/env python3
from __future__ import annotations
import argparse,json,re
from pathlib import Path
from vita_elf_audit import VitaElf

LINE=re.compile(r'^library=(\S+) library_nid=(0x[0-9A-F]{8}) function_nid=(0x[0-9A-F]{8}) function_name=(\S+)$')
TAIHEN_SHA='309b3800bcb8ebbd5e4f5e5e920af3da3590b829'
EXPECTED={
 ('SceModulemgrForDriver',0xD4A60A52,0x5182E212),
 ('SceSysmemForDriver',0x6F25E18A,0x6D88EF8A),
 ('SceThreadmgrForDriver',0xE2C40624,0x59D06540),
}

def parse(p):
    out=[]
    for n,line in enumerate(p.read_text().splitlines(),1):
        m=LINE.match(line)
        if not m:raise SystemExit(f'{p}:{n}: malformed {line!r}')
        out.append((m.group(1),int(m.group(2),16),int(m.group(3),16),m.group(4)))
    return out

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--manifest',type=Path,required=True);ap.add_argument('--elf-dir',type=Path,required=True);ap.add_argument('--taihen-source-sha',required=True);ap.add_argument('--json',type=Path,required=True);ap.add_argument('--text',type=Path,required=True);a=ap.parse_args()
    if a.taihen_source_sha!=TAIHEN_SHA:raise SystemExit('taiHEN source drift')
    rows=parse(a.manifest)
    if any(x[0]=='SceModulemgrForKernel' for x in rows):raise SystemExit('obsolete SceModulemgrForKernel direct import present')
    sony={(n,l,f) for n,l,f,_ in rows if n.startswith('Sce')}
    if sony!=EXPECTED:raise SystemExit(f'direct Sony import set mismatch: {sorted(sony)}')
    other=[x for x in rows if not x[0].startswith('Sce') and not x[0].lower().startswith('taihen')]
    if other:raise SystemExit(f'unclassified direct imports: {other}')
    exports=[];mods=[]
    for p in sorted(a.elf_dir.glob('*.elf')):
        e=VitaElf(p);mods.append({'file':p.name,'module':e.modinfo['name'],'sha256':e.sha256})
        for lib in e.exports():
            ln=lib.get('library_nid')
            if ln is None:continue
            for f in lib.get('functions',()):exports.append((int(ln),int(f['nid']),e.modinfo['name'],p.name,int(f['va'])))
    proven=[]
    for name,ln,fn in sorted(EXPECTED):
        hits=[x for x in exports if x[0]==ln and x[1]==fn]
        if len(hits)!=1:raise SystemExit(f'{name} 0x{ln:08X}/0x{fn:08X}: retail hits={len(hits)}')
        h=hits[0];proven.append({'library':name,'library_nid':f'0x{ln:08X}','function_nid':f'0x{fn:08X}','provider_module':h[2],'provider_file':h[3],'va':f'0x{h[4]:08X}'})
    doc={'schema':1,'firmware':'3.65','direct_sony_import_closure':'PASS','obsolete_modulemgr_for_kernel':'ABSENT','taihen_provider_sha':TAIHEN_SHA,'proven':proven,'retail_modules':mods}
    a.json.write_text(json.dumps(doc,indent=2,sort_keys=True)+'\n')
    lines=['GATE1A_DIRECT_SONY_IMPORT_CLOSURE=PASS','OLD_MODULEMGR_DIRECT_IMPORT=ABSENT',f'TAIHEN_PROVIDER_SOURCE={TAIHEN_SHA}']+[f"IMPORT {x['library']} {x['library_nid']}/{x['function_nid']} provider={x['provider_module']} va={x['va']}" for x in proven]
    a.text.write_text('\n'.join(lines)+'\n');print('\n'.join(lines))
if __name__=='__main__':main()
