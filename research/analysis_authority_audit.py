#!/usr/bin/env python3
from __future__ import annotations
import argparse, json
from pathlib import Path
from vita_elf_audit import VitaElf, Reachability

DISPLAY_SHA='83ef39adf741a4bbbc1cec67d00e2594de1d17be0f5ea6cc77c935eae46065e5'
FOCUS_DISPLAY=(0x81000A2C,0x81007228,0x8100724C,0x8100726C,0x81007410)

def validate_imports(elf_dir: Path):
    rows=[]; failures=[]
    files=sorted(elf_dir.glob('*.elf'))
    for p in files:
        try:
            e=VitaElf(p); libs=e.imports()
            for lib in libs:
                if len(lib['functions'])!=lib['nfunc']: raise ValueError('function count mismatch')
                if len(lib['variables'])!=lib['nvar']: raise ValueError('variable count mismatch')
                if len(lib['tls_variables'])!=lib['ntlsvar']: raise ValueError('tls count mismatch')
            rows.append({'file':p.name,'module':e.modinfo['name'],'libraries':len(libs),
                         'functions':sum(x['nfunc'] for x in libs),'variables':sum(x['nvar'] for x in libs),
                         'tls_variables':sum(x['ntlsvar'] for x in libs),
                         'stub_sizes':sorted(set(x['size'] for x in libs))})
        except Exception as ex:
            failures.append({'file':p.name,'error':str(ex)})
    return files,rows,failures

def cfg_row(cfg):
    c=cfg.compact(False)
    return {k:c[k] for k in ('start','logical_end','exidx_range','exidx_exact','boundary_sources','termination_reason','boundary_edges','decode_failures')}

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--elf-dir',type=Path,required=True);ap.add_argument('--display',type=Path,required=True);ap.add_argument('--json',type=Path,required=True);a=ap.parse_args()
    files,imports,failures=validate_imports(a.elf_dir)
    if len(files)!=46: raise SystemExit(f'expected 46 retail kernel ELFs, got {len(files)}')
    if failures: raise SystemExit('import decoding failures: '+json.dumps(failures))
    by_file={x['file']:x for x in imports}
    for required in ('modulemgr.elf','sysstatemgr.elf'):
        if required not in by_file: raise SystemExit(f'missing required authority module {required}')

    disp=VitaElf(a.display)
    if disp.sha256!=DISPLAY_SHA: raise SystemExit(f'SceDisplay hash drift {disp.sha256}')
    ex,im=disp.exports(),disp.imports()
    reach=Reachability(disp,ex,im,extra_starts=FOCUS_DISPLAY)
    focused=[]
    for va in FOCUS_DISPLAY:
        matches=[cfg for (s,t),cfg in reach.functions.items() if s==va]
        if not matches: raise SystemExit(f'focused logical start 0x{va:08X} missing')
        cfg=matches[0]; focused.append(cfg_row(cfg))
        for other in FOCUS_DISPLAY:
            if other!=va:
                for b in cfg.blocks.values():
                    if any(i.address==other for i in b.instructions):
                        raise SystemExit(f'0x{va:08X} consumed focused start 0x{other:08X}')

    f7228=next(x for x in focused if x['start']==0x81007228)
    f724c=next(x for x in focused if x['start']==0x8100724C)
    if f7228['logical_end']>0x8100724C: raise SystemExit('0x81007228 still consumes 0x8100724C')
    if f724c['logical_end']>0x8100726C: raise SystemExit('0x8100724C still consumes 0x8100726C')

    all_violations=[]
    for cfg in reach.functions.values():
        if cfg.boundary_violations: all_violations.append({'start':cfg.start,'violations':cfg.boundary_violations})
    if all_violations: raise SystemExit('logical boundary violations: '+json.dumps(all_violations))

    result={'schema':1,'firmware':'3.65','status':'PASS','kernel_elf_count':len(files),
            'import_decoding':{'status':'PASS','failures':failures,'modules':imports},
            'cfg_authority':{'status':'PASS','boundary_iterations':reach.boundary_iterations,
                             'logical_start_count':len(reach.boundaries.sources),'focused_functions':focused,
                             'known_boundary_violations':all_violations},
            'authority_statement':'Logical boundaries validated from exidx, exports, module entries, import stubs, direct call targets, and explicit regression starts; no focused function consumes another established start.'}
    a.json.write_text(json.dumps(result,indent=2)+'\n')
    print('ANALYSER_AUTHORITY')
    print(f"  CFG logical boundaries: PASS starts={result['cfg_authority']['logical_start_count']} iterations={reach.boundary_iterations}")
    print(f"  46/46 import decoding: PASS modules={len(imports)}")
    for x in focused:
        print(f"  0x{x['start']:08X}..0x{x['logical_end']:08X} exidx={tuple(hex(v) for v in x['exidx_range'])} exact={x['exidx_exact']} sources={x['boundary_sources']} term={x['termination_reason']}")
    for required in ('modulemgr.elf','sysstatemgr.elf'):
        r=by_file[required];print(f"  {required}: libs={r['libraries']} funcs={r['functions']} vars={r['variables']} tls={r['tls_variables']} sizes={r['stub_sizes']}")

if __name__=='__main__':main()
