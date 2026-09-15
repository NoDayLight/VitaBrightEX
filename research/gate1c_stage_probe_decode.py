#!/usr/bin/env python3
from __future__ import annotations
import argparse,hashlib,json,struct,sys
from pathlib import Path

MAGIC=0x31434256
DUMP_MAGIC=0x31434456
VERSION=1
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
F_NULL=1<<0;F_RETURN=1<<1;F_TARGET=1<<2;F_OUTSIDE=1<<3;F_CAPTURED=1<<4;F_OWNED=1<<5;F_TRANSFORM=1<<6;F_MISMATCH=1<<7;F_CANON=1<<8
KNOWN_FLAGS=F_NULL|F_RETURN|F_TARGET|F_OUTSIDE|F_CAPTURED|F_OWNED|F_TRANSFORM|F_MISMATCH|F_CANON
ZERO60=b'\0'*60
CANON_A='2f9fd211d1d389611267070cfbc936063b0b790a59245243feb404ee3c00daf6'
CANON_B='5dc12dfcae42a648dc093db831b661cc3068f2d70f199b03fb582ce301b188b5'
PROBE_B='61f4aae357db9e2a513942f1f58a0de03cc490621e6ca9c557baea70c0bc6c17'
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
    keys=('magic','version','firmware','lifecycle','capacity','slots_reserved','committed_records','lost_records','last_sequence','active_producers','owned_hook_mask','required_hook_mask','missing_required_mask','hook_fail_mask','ring_epoch','transformed_b','null_calls','baseline_mismatch_count')
    s=dict(zip(keys,STATUS.unpack(raw)))
    require(s['magic']==MAGIC,'status magic');require(s['version']==VERSION,'status version');require(s['firmware']==FW365,'status firmware');require(s['capacity']==CAPACITY,'status capacity')
    require(s['required_hook_mask']==REQUIRED,'required hook mask');require(s['owned_hook_mask']==REQUIRED,'hook ownership');require(s['missing_required_mask']==0,'missing hooks');require(s['hook_fail_mask']==0,'hook failure');require(s['lost_records']==0,'lost records');require(s['baseline_mismatch_count']==0,'baseline mismatch');require(s['active_producers']==0,'active producers')
    if expected_lifecycle is not None:require(s['lifecycle']==expected_lifecycle,f'lifecycle {s["lifecycle"]}')
    require(s['slots_reserved']<=CAPACITY,'slots > capacity');require(s['committed_records']>=s['slots_reserved'],'committed < slots')
    return s

def parse_record(raw:bytes):
    require(len(raw)==RECORD_SIZE,'record size')
    keys=('committed','sequence','completion_sequence','thread_id','invocation_id','event_type','plane','flags','raw_return','source_pointer','generation','source_hash32','forward_hash32')
    r=dict(zip(keys,PREFIX.unpack(raw[:48])))
    r['source_payload']=raw[48:108];r['forward_payload']=raw[108:168];r['ring_epoch']=struct.unpack_from('<I',raw,168)[0];r['reserved']=struct.unpack_from('<5I',raw,172)
    r['source_sha256']=sha(r['source_payload']);r['forward_sha256']=sha(r['forward_payload'])
    return r

def validate_record(r,epoch):
    require(r['committed']==COMMITTED,'incomplete record');require(r['ring_epoch']==epoch,'record epoch mismatch');require(r['event_type'] in (CSC_A,CSC_B,ENABLE_ENTER,ENABLE_EXIT),'invalid event type');require((r['flags']&~KNOWN_FLAGS)==0,'unknown flags');require(all(x==0 for x in r['reserved']),'record reserved nonzero')
    if r['event_type'] in (CSC_A,CSC_B):
        target=r['plane'] in (0,1);require(bool(r['flags']&F_TARGET)==target,'target-plane flag mismatch');require(bool(r['flags']&F_OUTSIDE)==(not target),'outside-plane flag mismatch');isnull=bool(r['flags']&F_NULL)
        if isnull:
            require(r['source_pointer']==0,'NULL with nonzero pointer');require(not(r['flags']&(F_CAPTURED|F_OWNED|F_TRANSFORM|F_MISMATCH|F_CANON)),'NULL polluted flags');require(r['source_payload']==ZERO60 and r['forward_payload']==ZERO60,'NULL payload dereference evidence');require(r['source_hash32']==0 and r['forward_hash32']==0 and r['generation']==0,'NULL metadata polluted')
        elif not target:
            require(r['source_pointer']!=0,'outside-plane non-NULL missing pointer');require(not(r['flags']&(F_CAPTURED|F_OWNED|F_TRANSFORM|F_MISMATCH|F_CANON)),'outside-plane mutation/capture');require(r['source_payload']==ZERO60 and r['forward_payload']==ZERO60,'outside-plane payload dereference evidence');require(r['source_hash32']==0 and r['forward_hash32']==0 and r['generation']==0,'outside-plane metadata polluted')
        else:
            require(r['source_pointer']!=0,'target non-NULL missing pointer');require(r['generation']>0,'generation zero');require(r['flags']&F_CAPTURED,'source not captured');require(r['flags']&F_RETURN,'CSC return missing');require(r['source_hash32']==fnv32(r['source_payload']),'source FNV mismatch');require(r['forward_hash32']==fnv32(r['forward_payload']),'forward FNV mismatch');require(not(r['flags']&F_MISMATCH),'baseline mismatch fallback observed')
            if r['event_type']==CSC_A:
                require(r['source_sha256']==CANON_A,'A source canonical hash drift');require(r['forward_sha256']==CANON_A,'A forward drift');require(r['source_payload']==r['forward_payload'],'A changed');require(r['flags']&F_CANON,'A canonical flag missing');require(not(r['flags']&(F_OWNED|F_TRANSFORM)),'A unexpectedly owned/transformed')
            else:
                require(r['source_sha256']==CANON_B,'B source canonical hash drift');require(r['forward_sha256']==PROBE_B,'B probe hash mismatch');require(r['flags']&F_CANON,'B canonical flag missing');require(r['flags']&F_OWNED,'B owned-forward flag missing');require(r['flags']&F_TRANSFORM,'B transform flag missing');sw=struct.unpack('<15I',r['source_payload']);fw=struct.unpack('<15I',r['forward_payload']);diff=[i for i,(a,b) in enumerate(zip(sw,fw)) if a!=b];require(diff==[6],f'B changed words {diff}');require(sw[6]==0x200 and fw[6]==0x100,'B ctm00 probe value')
    else:
        require(r['source_pointer']==0 and r['generation']==0 and r['source_hash32']==0 and r['forward_hash32']==0,'enable CSC metadata nonzero');require(r['source_payload']==ZERO60 and r['forward_payload']==ZERO60,'enable payload nonzero');require(not(r['flags']&(F_NULL|F_TARGET|F_OUTSIDE|F_CAPTURED|F_OWNED|F_TRANSFORM|F_MISMATCH|F_CANON)),'enable CSC flags present')
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
        require(len(g)==2,f'enable invocation {inv}: count');g=sorted(g,key=lambda x:x['sequence']);require(g[0]['event_type']==ENABLE_ENTER and g[1]['event_type']==ENABLE_EXIT,f'enable invocation {inv}: malformed');require(g[0]['plane']==g[1]['plane'],f'enable invocation {inv}: plane mismatch');require(g[0]['thread_id']==g[1]['thread_id'],f'enable invocation {inv}: thread mismatch')

def validate_phase(records,phase):
    toks=focused_tokens(records)
    if phase=='boot':require(toks==['B0','A0','B1','A1','B2','A2','B3','A3'],f'boot focused order {toks}')
    elif phase=='resume':require(toks==['B0','A0','B1','A1','ENABLE0','ENABLE1'],f'resume focused order {toks}')
    active_b=[r for r in records if r['event_type']==CSC_B and r['plane'] in (0,1)]
    active_a=[r for r in records if r['event_type']==CSC_A and r['plane'] in (0,1)]
    if phase in ('boot','resume'):
        require(len(active_b)==2,f'{phase} active B count');require(len(active_a)==2,f'{phase} active A count')

def decode_dump(raw:bytes,phase='generic'):
    require(len(raw)>=HEADER_SIZE+STATUS_SIZE,'truncated dump')
    hk=('magic','version','header_size','status_size','record_size','record_count','required_hook_mask','missing_required_mask','lost_records','ring_epoch','lifecycle','baseline_mismatch_count','r0','r1','r2','r3');h=dict(zip(hk,HDR.unpack(raw[:HEADER_SIZE])))
    require(h['magic']==DUMP_MAGIC,'dump magic');require(h['version']==VERSION,'dump version');require(h['header_size']==HEADER_SIZE and h['status_size']==STATUS_SIZE and h['record_size']==RECORD_SIZE,'dump layout');require(h['record_count']<=CAPACITY,'record count');require(len(raw)==HEADER_SIZE+STATUS_SIZE+h['record_count']*RECORD_SIZE,'dump byte count');require(h['required_hook_mask']==REQUIRED and h['missing_required_mask']==0,'dump hook ownership');require(h['lost_records']==0,'dump lost');require(h['baseline_mismatch_count']==0,'dump baseline mismatch');require(h['lifecycle']==LIFECYCLE_PAUSED,'dump lifecycle');require(all(h[f'r{i}']==0 for i in range(4)),'header reserved')
    st=parse_status(raw[HEADER_SIZE:HEADER_SIZE+STATUS_SIZE],LIFECYCLE_PAUSED);require(st['ring_epoch']==h['ring_epoch'],'epoch mismatch');require(st['slots_reserved']==h['record_count'],'record count mismatch')
    records=[];off=HEADER_SIZE+STATUS_SIZE
    for i in range(h['record_count']):
        r=parse_record(raw[off+i*RECORD_SIZE:off+(i+1)*RECORD_SIZE]);validate_record(r,h['ring_epoch']);records.append(r)
    require([r['sequence'] for r in records]==list(range(1,len(records)+1)),'sequence corruption');require(sorted(r['completion_sequence'] for r in records)==list(range(1,len(records)+1)),'completion corruption');validate_pairing(records);validate_phase(records,phase)
    transformed=sum(1 for r in records if r['flags']&F_TRANSFORM);require(st['transformed_b']>=transformed,'status transformed count below dump')
    return h,st,records

def rec_json(r):
    d={k:v for k,v in r.items() if k not in ('source_payload','forward_payload','reserved')};d['stage']='A' if r['event_type']==CSC_A else ('B' if r['event_type']==CSC_B else None);d['transformed']=bool(r['flags']&F_TRANSFORM);return d

def self_test():
    a=struct.pack('<15I',0,0x202,0x3ff,0,0,0,0,0,0,0,0,0,0,0,0)
    b=struct.pack('<15I',0,0,0x3ff,0,0x3ff,0,0x200,0,0,0,0x200,0,0,0,0x200)
    bp=struct.pack('<15I',0,0,0x3ff,0,0x3ff,0,0x100,0,0,0,0x200,0,0,0,0x200)
    require(sha(a)==CANON_A,'A selftest');require(sha(b)==CANON_B,'B selftest');require(sha(bp)==PROBE_B,'probe selftest');print('GATE1C_STAGE_PROBE_DECODER_SELFTEST=PASS')

def main(argv=None):
    ap=argparse.ArgumentParser();ap.add_argument('file',nargs='?',type=Path);ap.add_argument('--phase',choices=('generic','boot','resume'),default='generic');ap.add_argument('--json',type=Path);ap.add_argument('--self-test',action='store_true');a=ap.parse_args(argv)
    if a.self_test:self_test();return 0
    if a.file is None:ap.error('provide capture file or --self-test')
    raw=a.file.read_bytes()
    try:
        if len(raw)==STATUS_SIZE:
            s=parse_status(raw,LIFECYCLE_RUNNING);doc={'kind':'status','status':s};print('GATE1C_STATUS=PASS');print(json.dumps(s,sort_keys=True))
        else:
            h,s,recs=decode_dump(raw,a.phase);doc={'kind':'dump','phase':a.phase,'header':h,'status':s,'records':[rec_json(r) for r in recs]};print('GATE1C_STAGE_PROBE_DUMP=PASS')
            for r in recs:
                if r['event_type'] in (CSC_A,CSC_B) and r['plane'] in (0,1):
                    j=rec_json(r);print(f"seq={r['sequence']} stage={j['stage']} plane={r['plane']} source_sha256={r['source_sha256']} forward_sha256={r['forward_sha256']} transformed={j['transformed']} raw_return={r['raw_return']}")
            print('FOCUSED_ORDER='+' '.join(focused_tokens(recs)))
            print('B_PROBE_TRANSFORM_RECORDS='+str(sum(1 for r in recs if r['flags']&F_TRANSFORM)))
        if a.json:a.json.write_text(json.dumps(doc,indent=2,sort_keys=True)+'\n')
        return 0
    except DecodeError as e:
        print('GATE1C_DECODE=FAIL '+str(e),file=sys.stderr);return 2
if __name__=='__main__':raise SystemExit(main())
