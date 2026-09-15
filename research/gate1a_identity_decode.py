#!/usr/bin/env python3
from __future__ import annotations
import argparse,hashlib,json,struct,sys
from pathlib import Path

MAGIC=0x31474256
DUMP_MAGIC=0x31444256
VERSION=2
FW365=0x03650000
CAPACITY=256
COMMITTED=0xC01117ED
RECORD_SIZE=192
STATUS_SIZE=72
HEADER_SIZE=64
REQUIRED=0x7
LIFECYCLE_RUNNING=1
LIFECYCLE_PAUSED=2
CSC_A=1;CSC_B=2;ENABLE_ENTER=3;ENABLE_EXIT=4
F_NULL=1<<0;F_EQUAL=1<<1;F_RETURN=1<<2;F_VALID=1<<3;F_INVALID=1<<4;F_SUB=1<<5;F_MISMATCH=1<<6
KNOWN_FLAGS=F_NULL|F_EQUAL|F_RETURN|F_VALID|F_INVALID|F_SUB|F_MISMATCH
ZERO60=b'\0'*60
CANON={'A':'2f9fd211d1d389611267070cfbc936063b0b790a59245243feb404ee3c00daf6','B':'5dc12dfcae42a648dc093db831b661cc3068f2d70f199b03fb582ce301b188b5'}
HDR=struct.Struct('<16I');STATUS=struct.Struct('<18I');PREFIX=struct.Struct('<IIIIIHhIiIIII')

class DecodeError(Exception):pass

def fnv32(b:bytes)->int:
    h=2166136261
    for x in b:h=((h^x)*16777619)&0xffffffff
    return h

def sha(b:bytes)->str:return hashlib.sha256(b).hexdigest()
def require(c,msg):
    if not c:raise DecodeError(msg)

def parse_status(raw:bytes,expected_lifecycle:int|None=None):
    require(len(raw)==STATUS_SIZE,f'status size {len(raw)} != {STATUS_SIZE}')
    keys=('magic','version','firmware','lifecycle','capacity','slots_reserved','committed_records','lost_records','last_sequence','active_producers','owned_hook_mask','required_hook_mask','missing_required_mask','hook_fail_mask','ring_epoch','substituted_nonnull','null_calls','mismatch_count')
    s=dict(zip(keys,STATUS.unpack(raw)))
    require(s['magic']==MAGIC,'status magic');require(s['version']==VERSION,'status version');require(s['firmware']==FW365,'status firmware');require(s['capacity']==CAPACITY,'status capacity')
    require(s['required_hook_mask']==REQUIRED,'required hook mask');require(s['owned_hook_mask']==REQUIRED,'hook ownership');require(s['missing_required_mask']==0,'missing hooks');require(s['hook_fail_mask']==0,'hook failure');require(s['lost_records']==0,'lost records');require(s['mismatch_count']==0,'mismatch_count != 0');require(s['active_producers']==0,'active producers')
    if expected_lifecycle is not None:require(s['lifecycle']==expected_lifecycle,f'lifecycle {s["lifecycle"]}')
    require(s['slots_reserved']<=CAPACITY,'slots > capacity');require(s['committed_records']>=s['slots_reserved'],'committed < slots')
    return s

def parse_record(raw:bytes):
    require(len(raw)==RECORD_SIZE,'record size')
    keys=('committed','sequence','completion_sequence','thread_id','invocation_id','event_type','plane','flags','raw_return','source_pointer','generation','source_hash32','copy_hash32')
    r=dict(zip(keys,PREFIX.unpack(raw[:48])))
    r['source_payload']=raw[48:108];r['copy_payload']=raw[108:168];r['ring_epoch']=struct.unpack_from('<I',raw,168)[0];r['reserved']=struct.unpack_from('<5I',raw,172)
    r['source_sha256']=sha(r['source_payload']);r['copy_sha256']=sha(r['copy_payload'])
    return r

def validate_record(r,epoch):
    require(r['committed']==COMMITTED,'incomplete record');require(r['ring_epoch']==epoch,'record epoch mismatch');require(r['event_type'] in (CSC_A,CSC_B,ENABLE_ENTER,ENABLE_EXIT),'invalid event type');require((r['flags']&~KNOWN_FLAGS)==0,'unknown flags');require(all(x==0 for x in r['reserved']),'record reserved nonzero')
    if r['event_type'] in (CSC_A,CSC_B):
        valid=0<=r['plane']<5;require(bool(r['flags']&F_VALID)==valid,'valid-plane flag mismatch');require(bool(r['flags']&F_INVALID)==(not valid),'invalid-plane flag mismatch');isnull=bool(r['flags']&F_NULL)
        if isnull:
            require(r['source_pointer']==0,'NULL with nonzero source pointer');require(not(r['flags']&(F_SUB|F_EQUAL|F_MISMATCH)),'NULL substituted/equal/mismatch');require(r['source_payload']==ZERO60 and r['copy_payload']==ZERO60,'NULL payload dereference evidence');require(r['source_hash32']==0 and r['copy_hash32']==0 and r['generation']==0,'NULL metadata polluted')
        elif not valid:
            require(r['source_pointer']!=0,'invalid-plane non-NULL missing pointer');require(not(r['flags']&(F_SUB|F_EQUAL|F_MISMATCH)),'invalid-plane substitution');require(r['source_payload']==ZERO60 and r['copy_payload']==ZERO60,'invalid-plane payload dereference evidence');require(r['source_hash32']==0 and r['copy_hash32']==0 and r['generation']==0,'invalid-plane metadata polluted')
        else:
            require(r['source_pointer']!=0,'valid non-NULL missing source pointer');require(r['generation']>0,'valid non-NULL generation zero');require(r['source_hash32']==fnv32(r['source_payload']),'source FNV mismatch');require(r['copy_hash32']==fnv32(r['copy_payload']),'copy FNV mismatch');require(r['flags']&F_RETURN,'CSC return missing')
            if r['flags']&F_MISMATCH:raise DecodeError('mismatch fallback observed')
            require(r['flags']&F_SUB,'valid non-NULL not substituted');require(r['flags']&F_EQUAL,'substituted without equality flag');require(r['source_payload']==r['copy_payload'],'source/copy payload inequality')
    else:
        require(r['source_pointer']==0 and r['generation']==0 and r['source_hash32']==0 and r['copy_hash32']==0,'enable CSC metadata nonzero');require(r['source_payload']==ZERO60 and r['copy_payload']==ZERO60,'enable payload nonzero');require(not(r['flags']&(F_NULL|F_VALID|F_INVALID|F_SUB|F_EQUAL|F_MISMATCH)),'enable CSC flags present')
        if r['event_type']==ENABLE_ENTER:require(not(r['flags']&F_RETURN),'enable ENTER has return')
        else:require(r['flags']&F_RETURN,'enable EXIT missing return')

def focused_tokens(records):
    out=[]
    for r in records:
        if r['event_type']==CSC_A:out.append(f'A{r["plane"]}')
        elif r['event_type']==CSC_B:out.append(f'B{r["plane"]}')
        elif r['event_type']==ENABLE_ENTER:out.append(f'ENABLE{r["plane"]}')
    return out

def validate_pairing(records):
    groups={}
    for r in records:
        if r['event_type'] in (ENABLE_ENTER,ENABLE_EXIT):groups.setdefault(r['invocation_id'],[]).append(r)
    for inv,g in groups.items():
        require(len(g)==2,f'enable invocation {inv}: record count');g=sorted(g,key=lambda x:x['sequence']);require(g[0]['event_type']==ENABLE_ENTER and g[1]['event_type']==ENABLE_EXIT,f'enable invocation {inv}: malformed ENTER/EXIT');require(g[0]['plane']==g[1]['plane'],f'enable invocation {inv}: plane mismatch');require(g[0]['thread_id']==g[1]['thread_id'],f'enable invocation {inv}: thread mismatch')

def validate_phase(records,phase):
    toks=focused_tokens(records)
    if phase=='screen-power':require(toks==[],'screen-power unexpectedly hit Gate-1 boundary')
    elif phase in ('boot','resume'):
        require(toks==['B0','A0','B1','A1','ENABLE0','ENABLE1'],f'{phase} focused order {toks}')
        c=[r for r in records if r['event_type'] in (CSC_A,CSC_B)];require(len(c)==4,f'{phase} CSC count')
        for r in c:
            stage='A' if r['event_type']==CSC_A else 'B';require(r['source_sha256']==CANON[stage],f'{phase} {stage}{r["plane"]} canonical hash drift')

def decode_dump(raw:bytes,phase='generic'):
    require(len(raw)>=HEADER_SIZE+STATUS_SIZE,'truncated dump')
    hk=('magic','version','header_size','status_size','record_size','record_count','required_hook_mask','missing_required_mask','lost_records','ring_epoch','lifecycle','mismatch_count','r0','r1','r2','r3');h=dict(zip(hk,HDR.unpack(raw[:HEADER_SIZE])))
    require(h['magic']==DUMP_MAGIC,'dump magic');require(h['version']==VERSION,'dump version');require(h['header_size']==HEADER_SIZE and h['status_size']==STATUS_SIZE and h['record_size']==RECORD_SIZE,'dump layout');require(h['record_count']<=CAPACITY,'malformed record count');require(len(raw)==HEADER_SIZE+STATUS_SIZE+h['record_count']*RECORD_SIZE,'dump byte count');require(h['required_hook_mask']==REQUIRED and h['missing_required_mask']==0,'dump hook ownership');require(h['lost_records']==0,'dump lost records');require(h['mismatch_count']==0,'dump mismatch_count');require(h['lifecycle']==LIFECYCLE_PAUSED,'dump lifecycle');require(all(h[f'r{i}']==0 for i in range(4)),'header reserved nonzero')
    st=parse_status(raw[HEADER_SIZE:HEADER_SIZE+STATUS_SIZE],LIFECYCLE_PAUSED);require(st['ring_epoch']==h['ring_epoch'],'header/status epoch');require(st['slots_reserved']==h['record_count'],'status/record count');require(st['missing_required_mask']==h['missing_required_mask'] and st['lost_records']==h['lost_records'] and st['mismatch_count']==h['mismatch_count'],'header/status mismatch')
    records=[];off=HEADER_SIZE+STATUS_SIZE
    for i in range(h['record_count']):
        r=parse_record(raw[off+i*RECORD_SIZE:off+(i+1)*RECORD_SIZE]);validate_record(r,h['ring_epoch']);records.append(r)
    require([r['sequence'] for r in records]==list(range(1,len(records)+1)),'sequence corruption');require(sorted(r['completion_sequence'] for r in records)==list(range(1,len(records)+1)),'completion sequence corruption');validate_pairing(records);validate_phase(records,phase)
    return h,st,records

def rec_json(r):
    stage='A' if r['event_type']==CSC_A else ('B' if r['event_type']==CSC_B else None);d={k:v for k,v in r.items() if k not in ('source_payload','copy_payload','reserved')}
    if stage:d.update(stage=stage,exact_equality=(r['source_payload']==r['copy_payload']),baseline=('GATE0A_'+stage if r['source_sha256']==CANON[stage] else 'UNKNOWN'))
    return d

def main(argv=None):
    ap=argparse.ArgumentParser();ap.add_argument('file',type=Path);ap.add_argument('--phase',choices=('generic','boot','screen-power','resume'),default='generic');ap.add_argument('--json',type=Path);a=ap.parse_args(argv);raw=a.file.read_bytes()
    try:
        if len(raw)==STATUS_SIZE:
            s=parse_status(raw,LIFECYCLE_RUNNING);doc={'kind':'status','status':s};print('GATE1A_STATUS=PASS');print(json.dumps(s,sort_keys=True))
        else:
            h,s,recs=decode_dump(raw,a.phase);doc={'kind':'dump','phase':a.phase,'header':h,'status':s,'records':[rec_json(r) for r in recs]};print('GATE1A_DUMP=PASS')
            for r in recs:
                if r['event_type'] in (CSC_A,CSC_B):
                    j=rec_json(r);print(f"seq={r['sequence']} stage={j['stage']} plane={r['plane']} source=0x{r['source_pointer']:08X} generation={r['generation']} source_sha256={r['source_sha256']} copy_sha256={r['copy_sha256']} equal={j['exact_equality']} raw_return={r['raw_return']} baseline={j['baseline']}")
            print('FOCUSED_ORDER='+' '.join(focused_tokens(recs)))
        if a.json:a.json.write_text(json.dumps(doc,indent=2,sort_keys=True)+'\n')
        return 0
    except DecodeError as e:
        print('GATE1A_DECODE=FAIL '+str(e),file=sys.stderr);return 2
if __name__=='__main__':raise SystemExit(main())
