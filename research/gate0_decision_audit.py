#!/usr/bin/env python3
from __future__ import annotations
import argparse,json
from pathlib import Path
from gate0_trace_decode import decode, TraceError

LEVELS=('PROVEN','STRONG','OBSERVED','NOT OBSERVED','INCONCLUSIVE','CONTRADICTED')
CAPS=('SEED','CAPTURE_A_BASELINE','CAPTURE_B_BRIGHTNESS','CAPTURE_C_COLORSPACE','CAPTURE_D_DISPLAY','CAPTURE_E_APPLICATION','CAPTURE_F_SUSPEND_RESUME')
CSC={'CSC_A','CSC_B'}

def evs(d,names): return [r for r in d['records'] if r['event'] in names]
def success(r): return r.get('return_semantics')=='SUCCESS'
def scope_children(d,enter_event):
    out=[]
    for e in evs(d,{enter_event}):
        inv=e['invocation_id']; children=[r for r in d['records'] if inv in r['scope_invocation_ids']]
        out.append((e,children))
    return out

def state(level,evidence,reason): return {'status':level,'evidence':evidence,'reason':reason}
def analyze(captures):
    affine_auth=all(d['affine_authoritative'] for d in captures.values())
    panel_auth=all(d['panel_authoritative'] for d in captures.values())
    allrec=[r for d in captures.values() for r in d['records']]
    csc=[r for r in allrec if r['event'] in CSC]
    nonnull=[r for r in csc if not(r['flags']&1)]
    failed=[r for r in csc if not success(r)]
    planes=sorted(set(r['plane'] for r in csc))
    active_planes=[]
    for d in captures.values():
        for i,p in enumerate(d['post_snapshot']['planes']):
            if p['active_state_1e8']!=0: active_planes.append(i)
    active_planes=sorted(set(active_planes))
    nulls=[r for r in csc if r['flags']&1]
    bypayload={}
    for r in nonnull:bypayload.setdefault((r['event'],r['plane'],r['payload_hex']),0);bypayload[(r['event'],r['plane'],r['payload_hex'])]+=1

    def rewrite(cap,key):
        d=captures.get(cap)
        if not d:return state('INCONCLUSIVE',[],f'{cap} missing')
        rows=[]
        for enter in ('DISPLAY_BRIGHTNESS_ENTER','LCD_BRIGHTNESS_ENTER') if key=='brightness' else ('DISPLAY_COLORSPACE_ENTER','LCD_COLORSPACE_ENTER'):
            for e,ch in scope_children(d,enter):
                cs=[x for x in ch if x['event'] in CSC]
                if cs:rows.append({'scope':e['event'],'arg0':e['arg0'],'csc':[x['event'] for x in cs],'returns':[x['return_semantics'] for x in cs]})
        if rows and all(all(x=='SUCCESS' for x in r['returns']) for r in rows):return state('OBSERVED',rows,'Sony operation scopes contain successful private CSC traffic')
        if rows:return state('CONTRADICTED',rows,'CSC traffic occurred but at least one setter did not succeed')
        return state('NOT OBSERVED',[],f'No CSC traffic observed inside {key} scopes; absence in one capture is not a capability disproof')

    brightness=rewrite('CAPTURE_B_BRIGHTNESS','brightness'); colorspace=rewrite('CAPTURE_C_COLORSPACE','colorspace')
    def capture_csc(cap,label):
        d=captures.get(cap)
        if not d:return state('INCONCLUSIVE',[],f'{cap} missing')
        rows=[r for r in d['records'] if r['event'] in CSC]
        if rows and all(success(r) for r in rows):return state('OBSERVED',[{'event':r['event'],'plane':r['plane'],'null':bool(r['flags']&1)} for r in rows],f'successful CSC traffic observed during {label}')
        if rows:return state('CONTRADICTED',[],f'failed CSC traffic observed during {label}')
        return state('NOT OBSERVED',[],f'no CSC traffic observed during {label}; not a disproof')
    display=capture_csc('CAPTURE_D_DISPLAY','display off/on'); app=capture_csc('CAPTURE_E_APPLICATION','application transition'); suspend=capture_csc('CAPTURE_F_SUSPEND_RESUME','suspend/resume')

    pristine = state('PROVEN' if affine_auth and nonnull and not failed else ('CONTRADICTED' if failed else 'INCONCLUSIVE'),
        [{'event':r['event'],'plane':r['plane'],'seq':r['sequence']} for r in nonnull[:32]],
        'read-only hooks capture Sony input before continuing the original setter' if nonnull and not failed else 'requires authoritative successful non-NULL setter traffic')
    nullmeaning=state('OBSERVED' if nulls else 'NOT OBSERVED',[{'event':r['event'],'plane':r['plane'],'seq':r['sequence']} for r in nulls],
        'NULL cache-reapply traffic captured' if nulls else 'no NULL event captured; absence is not proof it does not occur')
    restoration=state('STRONG' if pristine['status']=='PROVEN' and (brightness['status'] in ('OBSERVED','NOT OBSERVED')) and (colorspace['status'] in ('OBSERVED','NOT OBSERVED')) and display['status']!='CONTRADICTED' and suspend['status']!='CONTRADICTED' else 'INCONCLUSIVE',
        {'unique_pristine_objects':len(bypayload)},'latest pristine Sony objects are observable, but restoration/reacquisition remains lifecycle-dependent')

    required=[pristine,brightness,colorspace,display,suspend]
    aplus='PROVEN' if affine_auth and active_planes and len(planes)==1 and all(x['status'] in ('PROVEN','OBSERVED','STRONG') for x in required) and nullmeaning['status']=='OBSERVED' else ('DISPROVEN' if any(x['status']=='CONTRADICTED' for x in required) else 'INCONCLUSIVE')
    report={'capture_authority':{'affine_all':affine_auth,'panel_all':panel_auth},'AFFINE':{
        'active_planes':active_planes,'setter_planes':planes,'setter_failures':len(failed),'pristine_sony_nonnull':pristine,'null_meaning':nullmeaning,'brightness_reacquisition':brightness,'color_space_reacquisition':colorspace,'display_off_on':display,'application_transition':app,'suspend_resume':suspend,'restoration_base':restoration,'A+':aplus,'READY_FOR_IDENTITY_GATE':'YES' if aplus=='PROVEN' else 'NO',
        'architecture_ranking':['A+ — private Sony CSC setter composition','B — Sony builder/reconciler contingency','D — direct proven MMIO contingency']},
        'NONLINEAR':{'panel_capture_authority':'AUTHORITATIVE' if panel_auth else 'PARTIAL','runtime_panel_stream_captured':'YES' if any(r['event']=='PANEL_WRITE' for r in allrec) else 'NO','official_panel_reads':len([r for r in allrec if r['event']=='PANEL_READ_EXIT']),'controller_identity':'UNKNOWN','leading_true_gamma_path':'PANEL_CONTROLLER unless bounded IFTU evidence later proves a nonlinear stage'}}
    return report

def text(r):
    a=r['AFFINE']; n=r['NONLINEAR']; return '\n'.join([
        'AFFINE',f"active planes: {a['active_planes']}",f"setter planes: {a['setter_planes']}",f"A+: {a['A+']}",f"READY FOR IDENTITY GATE: {a['READY_FOR_IDENTITY_GATE']}",
        f"pristine: {a['pristine_sony_nonnull']['status']}",f"NULL: {a['null_meaning']['status']}",f"brightness: {a['brightness_reacquisition']['status']}",f"color-space: {a['color_space_reacquisition']['status']}",f"off/on: {a['display_off_on']['status']}",f"app: {a['application_transition']['status']}",f"suspend/resume: {a['suspend_resume']['status']}",'',
        'NONLINEAR',f"panel authority: {n['panel_capture_authority']}",f"runtime panel stream: {n['runtime_panel_stream_captured']}",f"official panel reads: {n['official_panel_reads']}",f"controller identity: {n['controller_identity']}",f"leading path: {n['leading_true_gamma_path']}"])
def main():
    ap=argparse.ArgumentParser()
    for c in CAPS: ap.add_argument('--'+c.lower().replace('_','-'),dest=c,type=Path)
    ap.add_argument('--json',type=Path); a=ap.parse_args(); captures={c:decode(getattr(a,c)) for c in CAPS if getattr(a,c)}
    if not captures: raise SystemExit('at least one Gate-0 capture is required')
    r=analyze(captures); print(text(r));
    if a.json:a.json.write_text(json.dumps(r,indent=2)+'\n')
if __name__=='__main__':main()
