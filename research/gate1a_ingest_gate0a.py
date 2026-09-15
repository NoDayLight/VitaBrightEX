#!/usr/bin/env python3
from __future__ import annotations
import argparse, hashlib, importlib.util, json, struct
from collections import Counter
from pathlib import Path

SCIENCE = {
    '00_CLEAN_BOOT': 'clean_boot',
    '01_NOOP_CONTROL_A': 'control',
    '02_NOOP_CONTROL_B': 'control',
    '09_BRIGHTNESS_RESTART_AT_B': 'setup_boundary',
    '10_B_TO_A_R1': 'brightness_controlled',
    '11_A_TO_B_R1': 'brightness_controlled',
    '12_B_TO_A_R2': 'brightness_controlled',
    '13_A_TO_B_R2': 'brightness_controlled',
    '14_SCREEN_POWER_CYCLE': 'screen_power',
    '15_SUSPEND_RESUME': 'suspend_resume',
    '16_INACTIVITY_DIM_RESTORE': 'dim_restore',
}
SPECIAL = {
    'C10_TRACE_TRANSPORT_VALIDATION': 'transport_validation',
    '03_B_TO_A_1': 'quarantined_action_label',
    '05_B_TO_A_2': 'quarantined_action_label',
    'SETUP_B_MAX': 'pre_restart_setup',
}
CSC_TARGETS = {'00_CLEAN_BOOT','09_BRIGHTNESS_RESTART_AT_B','15_SUSPEND_RESUME'}
EVENT_ORDER = ['CSC_A','CSC_B','IFTU_ENABLE_ENTER','IFTU_ENABLE_EXIT','PANEL_WRITE','PANEL_READ_ENTER','PANEL_READ_EXIT','MARKER']

def load_decoder(path: Path):
    spec=importlib.util.spec_from_file_location('gate0_trace_decode_frozen', path)
    mod=importlib.util.module_from_spec(spec); assert spec.loader; spec.loader.exec_module(mod); return mod

def sha256(b: bytes)->str:return hashlib.sha256(b).hexdigest()
def s39_num(w:int)->int:
    v=w & 0xfff
    if v & 0x800:v-=0x1000
    return v

def csc_row(capture: str, r: dict):
    payload=bytes.fromhex(r['payload_hex'])
    nonnull=r['payload_state']=='CSC_EXACT_3C'
    words=list(struct.unpack('<15I',payload)) if len(payload)==60 else []
    nums=[s39_num(x) for x in words[6:15]] if words else []
    return {
      'capture':capture,'event_sequence':r['sequence'],'completion_sequence':r['completion_sequence'],
      'stage':'A' if r['event']=='CSC_A' else 'B','plane':r['plane'],
      'null_status':'NON_NULL' if nonnull else ('NULL' if r['payload_state']=='CSC_NULL' else r['payload_state']),
      'pointer':f"0x{r['pointer_value']:08X}",'raw_return':r['raw_return'],'payload_sha256':r['payload_sha256'],
      'raw_words':[f'0x{x:08X}' for x in words],
      'fields': ({'post_add_0':words[0],'post_add_1_2':words[1],'post_clamp_max_0':words[2],'post_clamp_min_0':words[3],'post_clamp_max_1_2':words[4],'post_clamp_min_1_2':words[5]} if words else None),
      'ctm_s3_9_raw_signed':nums,'ctm_s3_9_rational':[f'{n}/512' for n in nums],'ctm_s3_9_float':[n/512.0 for n in nums],
    }

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--dataset',type=Path,required=True);ap.add_argument('--decoder',type=Path,required=True);ap.add_argument('--manifest',type=Path,required=True);ap.add_argument('--csc',type=Path,required=True);a=ap.parse_args()
    dec=load_decoder(a.decoder); gate=a.dataset; rows=[]; decoded={}; csc=[]
    for name,role in {**SCIENCE,**SPECIAL}.items():
        p=gate/(name+'.bin')
        if not p.exists():continue
        b=p.read_bytes(); d=dec.decode(p,authoritative=True); decoded[name]=d; s=d['status']; counts=Counter(r['event'] for r in d['records'])
        rows.append({'raw_filename':p.name,'sha256':sha256(b),'evidence_role':role,'authoritative_decode':'PASS','scientific_action_label_accepted':role!='quarantined_action_label','ring_epoch':s['ring_epoch'],'record_count':len(d['records']),'lost_records':s['lost_records'],'owned_hook_mask':f"0x{s['owned_hook_mask']:08X}",'required_hook_mask':f"0x{s['required_hook_mask']:08X}",'missing_required_mask':f"0x{s['missing_required_mask']:08X}",'hook_fail_mask':f"0x{s['hook_fail_mask']:08X}",'event_counts':{e:counts.get(e,0) for e in EVENT_ORDER}})
        if name in CSC_TARGETS:csc.extend(csc_row(name,r) for r in d['records'] if r['event'] in ('CSC_A','CSC_B'))
    brightness=[decoded[x] for x in ('10_B_TO_A_R1','11_A_TO_B_R1','12_B_TO_A_R2','13_A_TO_B_R2')]
    scr=decoded['14_SCREEN_POWER_CYCLE']['records']; sus=decoded['15_SUSPEND_RESUME']['records']; dim=decoded['16_INACTIVITY_DIM_RESTORE']['records']
    topology={'CONTROL':{'captures':['01_NOOP_CONTROL_A','02_NOOP_CONTROL_B'],'classification':'BACKGROUND_PROCEDURAL_CONTROL','record_counts':[len(decoded[x]['records']) for x in ('01_NOOP_CONTROL_A','02_NOOP_CONTROL_B')]},'BRIGHTNESS':{'captures':['10_B_TO_A_R1','11_A_TO_B_R1','12_B_TO_A_R2','13_A_TO_B_R2'],'classification':'NO_EVENT' if all(len(x['records'])==0 for x in brightness) else 'EVENT_OBSERVED'},'SCREEN_POWER':{'capture':'14_SCREEN_POWER_CYCLE','panel_write_count':sum(r['event']=='PANEL_WRITE' for r in scr),'csc_count':sum(r['event'] in ('CSC_A','CSC_B') for r in scr),'iftu_count':sum(r['event'].startswith('IFTU_ENABLE') for r in scr)},'SUSPEND_RESUME':{'capture':'15_SUSPEND_RESUME','non_panel_sequence':[f"{r['event']}({r['plane']})" for r in sus if r['event']!='PANEL_WRITE'],'panel_write_count':sum(r['event']=='PANEL_WRITE' for r in sus)},'DIM_RESTORE':{'capture':'16_INACTIVITY_DIM_RESTORE','classification':'NO_EVENT' if not dim else 'EVENT_OBSERVED'}}
    manifest={'schema':1,'source_decoder_commit':'f5a74d804d69187c4bf7e965be33d2bfca33ec51','captures':rows,'experiment_topology':topology}
    def find(cap,stage,plane):
        q=[x for x in csc if x['capture']==cap and x['stage']==stage and x['plane']==plane]; return q[0] if len(q)==1 else None
    comps={}
    for stage in ('A','B'):
      for plane in (0,1):
        bo=find('00_CLEAN_BOOT',stage,plane); re=find('15_SUSPEND_RESUME',stage,plane); k=f'BOOT_{stage}{plane}_vs_RESUME_{stage}{plane}'
        comps[k]='INSUFFICIENT' if bo is None or re is None else ('BYTE_IDENTICAL' if bo['payload_sha256']==re['payload_sha256'] and bo['raw_words']==re['raw_words'] else 'DIFFERENT')
    null='NULL_OBSERVED' if any(x['null_status']=='NULL' for x in csc) else 'NO_NULL_OBSERVED'
    cscout={'schema':1,'captures':sorted(CSC_TARGETS),'events':csc,'boot_resume_comparison':comps,'null_csc_events':null}
    a.manifest.write_text(json.dumps(manifest,indent=2)+'\n');a.csc.write_text(json.dumps(cscout,indent=2)+'\n')
    print(f'GATE1A_GATE0A_MANIFEST=PASS captures={len(rows)}');print(f'CSC_EVENTS={len(csc)} {null}')
    for k,v in comps.items():print(k+'='+v)

if __name__=='__main__':main()
