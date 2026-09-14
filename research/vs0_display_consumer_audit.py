#!/usr/bin/env python3
from __future__ import annotations
import argparse,json,re
from pathlib import Path
from vita_elf_audit import VitaElf

AV_CSM=0x3414347F
AV_RGB=0x67E1A494
GXM_GAMMA=0xF5C89643
TERMS=('color_space_mode','rgb_range_mode','gamma','color_temperature','white_balance','color_matrix','contrast','tone_curve','panel_mode')

def ascii_strings(data):
    return {m.group().decode('ascii','ignore') for m in re.finditer(rb'[ -~]{4,}',data)}

def lib_summary(libs):
    out=[]
    for lib in libs:
        name=lib.get('library_name') or ''
        if any(k in name.lower() for k in ('display','lcd','avconfig','reg','gxm','sysroot')):
            out.append({'library':name,'library_nid':lib.get('library_nid'),'function_nids':[f['nid'] for f in lib.get('functions',[])]})
    return out

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('--elf-dir',type=Path,required=True); ap.add_argument('--json',type=Path,required=True); a=ap.parse_args()
    mods=[]
    for p in sorted(a.elf_dir.glob('*.elf')):
        try:e=VitaElf(p)
        except Exception:continue
        libs=e.imports(); exlibs=e.exports()
        imports={f['nid'] for lib in libs for f in lib['functions']}
        exports={f['nid'] for lib in exlibs for f in lib['functions']}
        regmgr=any(lib.get('library_name') and 'reg' in lib['library_name'].lower() and 'mgr' in lib['library_name'].lower() for lib in libs)
        ss=ascii_strings(e.data)
        hits={t:sorted(x for x in ss if t.lower() in x.lower())[:32] for t in TERMS}; hits={k:v for k,v in hits.items() if v}
        interesting=bool(imports&{AV_CSM,AV_RGB,GXM_GAMMA} or exports&{AV_CSM,AV_RGB,GXM_GAMMA} or hits)
        if interesting:
            mods.append({'file':p.name,'module':e.modinfo['name'],'sha256':e.sha256,
                'imports_avconfig_colorspace':AV_CSM in imports,'exports_avconfig_colorspace':AV_CSM in exports,
                'imports_hdmi_rgb_range':AV_RGB in imports,'imports_gxm_gamma':GXM_GAMMA in imports,
                'imports_regmgr':regmgr,'display_related_imports':lib_summary(libs),'strings':hits})
    impl=[m for m in mods if m['exports_avconfig_colorspace']]
    csm=[m for m in mods if m['imports_avconfig_colorspace']]
    rgb=[m for m in mods if m['imports_hdmi_rgb_range']]
    gxm=[m for m in mods if m['imports_gxm_gamma']]
    system_gxm=[m for m in gxm if any(k in (m['module']+' '+m['file']).lower() for k in ('shell','display','avconfig','settings','npxs10015'))]
    consumers={t:[m['module'] for m in mods if t in m['strings'] and m['imports_regmgr']] for t in TERMS}
    out={'schema':2,'target':'retail-3.65 targeted vs0 display/control-plane pass','modules':mods,
      'avconfig':{
        'implementation_exports':impl,
        'runtime_colorspace_consumers':csm,
        'implementation_display_imports':[{'module':m['module'],'imports':m['display_related_imports']} for m in impl],
        'classification':'LIVE_RUNTIME_API_SEPARATE_FROM_REGISTRY' if impl and csm else 'INCOMPLETE_TARGET_SELECTION_OR_INDIRECTION',
        'kernel_path_classification':'DIRECT_RELEVANT_IMPORTS_LISTED' if any(m['display_related_imports'] for m in impl) else 'USER_SERVICE_INDIRECTION_REQUIRES_RUNTIME_HOOK_CORRELATION',
        'hdmi_rgb_range_consumers':rgb,
        'rgb_range_classification':'HDMI/PSTV_NAMED_API_NOT_HANDHELD_ENGINE' if rgb else 'NO_TARGETED_LIVE_HANDHELD_RGB_RANGE_CONSUMER_FOUND'},
      'gxm_pbe':{
        'gamma_mode_consumers':gxm,
        'system_display_consumers':system_gxm,
        'classification':'SYSTEM_DISPLAY_PATH_USES_FIXED_GAMMA_SELECTOR' if system_gxm else 'NO_TARGETED_SYSTEM_DISPLAY_GAMMA_SELECTOR_USE_FOUND',
        'arbitrary_gamma':'NOT_ESTABLISHED_ENUM_STYLE_PRIMITIVE_ONLY'},
      'registry_terms':consumers,
      'consumer_backed_hidden_color_controls':{t:bool(consumers[t]) for t in ('gamma','color_temperature','white_balance','color_matrix','contrast','tone_curve','panel_mode')},
      'notes':['A string is counted as a registry control only when the same module imports a RegMgr-like library.','sceAVConfigHdmiSetRgbRange is classified by its public API semantics as HDMI/PSTV-oriented, not as proof of a handheld LCD range engine.']}
    a.json.write_text(json.dumps(out,indent=2)+'\n')
    print('VS0_DISPLAY_CONSUMERS')
    print(f"  AVConfig implementations={len(impl)} consumers={len(csm)} classification={out['avconfig']['classification']}")
    print(f"  AVConfig live path={out['avconfig']['kernel_path_classification']}")
    print(f"  HDMI rgb-range consumers={len(rgb)} result={out['avconfig']['rgb_range_classification']}")
    print(f"  GXM gamma consumers={len(gxm)} system-display={len(system_gxm)} result={out['gxm_pbe']['classification']}")
    print('  registry='+json.dumps(consumers,sort_keys=True))
if __name__=='__main__':main()
