#!/usr/bin/env python3
from __future__ import annotations
import argparse, hashlib, json
from pathlib import Path
from vita_elf_audit import VitaElf

MODULE_NAME='SceKernelModulemgr'
DRIVER_LIB_NAME='SceModulemgrForDriver'
DRIVER_LIB_NID=0xD4A60A52
FW_FUNC_NID=0x5182E212
KERNEL_LIB_NAME='SceModulemgrForKernel'
KERNEL_LIB_NID=0x92C9FFC2
GET_INFO_NID=0xDAA90093

def find_export(e,lib_name,lib_nid,func_nid):
    hits=[]
    for lib in e.exports():
        if lib['library_name']!=lib_name or lib['library_nid']!=lib_nid:
            continue
        for fn in lib['functions']:
            if fn['nid']==func_nid:
                hits.append(fn)
    return hits

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--modulemgr',type=Path,required=True)
    ap.add_argument('--json',type=Path,required=True)
    ap.add_argument('--text',type=Path,required=True)
    a=ap.parse_args()
    e=VitaElf(a.modulemgr)
    if e.modinfo['name']!=MODULE_NAME:
        raise SystemExit(f'expected module {MODULE_NAME}, got {e.modinfo["name"]}')
    driver=find_export(e,DRIVER_LIB_NAME,DRIVER_LIB_NID,FW_FUNC_NID)
    kernel=find_export(e,KERNEL_LIB_NAME,KERNEL_LIB_NID,GET_INFO_NID)
    if len(kernel)!=1:
        raise SystemExit(f'3.65 ksceKernelGetModuleInfo export identity mismatch: hits={len(kernel)}')
    doc={
        'schema':1,
        'firmware':'3.65',
        'module':MODULE_NAME,
        'elf_sha256':e.sha256,
        'modulemgr_365':{
            'library_name':KERNEL_LIB_NAME,
            'library_nid':f'0x{KERNEL_LIB_NID:08X}',
            'function_name':'ksceKernelGetModuleInfo',
            'function_nid':f'0x{GET_INFO_NID:08X}',
            'present':True,
            'va':f'0x{kernel[0]["va"]:08X}',
        },
        'system_sw_version_direct_import':{
            'library_name':DRIVER_LIB_NAME,
            'library_nid':f'0x{DRIVER_LIB_NID:08X}',
            'function_name':'ksceKernelGetSystemSwVersion',
            'function_nid':f'0x{FW_FUNC_NID:08X}',
            'present':len(driver)==1,
            'hit_count':len(driver),
            'va':f'0x{driver[0]["va"]:08X}' if len(driver)==1 else None,
        },
    }
    a.json.write_text(json.dumps(doc,indent=2)+'\n')
    classification='RETAIL_365_PROVEN' if len(driver)==1 else 'RETAIL_365_ABSENT'
    lines=[
        'GATE0_V7_MODULEMGR_RETAIL_AUDIT=PASS',
        f'MODULEMGR_ELF_SHA256={e.sha256}',
        f'MODULEMGR_365_GET_INFO=PASS library=0x{KERNEL_LIB_NID:08X} function=0x{GET_INFO_NID:08X} va=0x{kernel[0]["va"]:08X}',
        f'SYSTEM_SW_VERSION_DIRECT_IMPORT={classification} library=0x{DRIVER_LIB_NID:08X} function=0x{FW_FUNC_NID:08X}',
    ]
    if len(driver)>1:
        raise SystemExit(f'3.65 ksceKernelGetSystemSwVersion export not unique: hits={len(driver)}')
    if len(driver)==1:
        lines.append(f'SYSTEM_SW_VERSION_EXPORT_VA=0x{driver[0]["va"]:08X}')
    a.text.write_text('\n'.join(lines)+'\n')
    print('\n'.join(lines))

if __name__=='__main__':main()
