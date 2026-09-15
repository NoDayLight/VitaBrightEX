#!/usr/bin/env python3
from __future__ import annotations
import argparse,hashlib,struct,sys
from pathlib import Path

STATUS_SIZE=384
CAPS_SIZE=64
ACTION_SIZE=4+STATUS_SIZE
STATUS_BUNDLE_SIZE=CAPS_SIZE+STATUS_SIZE
IDENTITY='5dc12dfcae42a648dc093db831b661cc3068f2d70f199b03fb582ce301b188b5'
RANGE='fff18ae2defe21c92e9a4e1e4795c85bfe6ba7f5548a1b90a2e1b46c707ee07e'
MILD='090035115d31436fa114dfd54781c8184e17ade1e61326a90eac38b8a61faf50'
RANGE_MILD='8abe961127d94bcdd9e259cf65ae95e379ef13e67bc58c3afbeafe6c2e8f3eed'

def sha_words(words):return hashlib.sha256(struct.pack('<15I',*words)).hexdigest()
def require(c,m):
    if not c:raise ValueError(m)

def parse_plane(raw,off):
    h=struct.unpack_from('<4Ii5I',raw,off);off+=40
    src=list(struct.unpack_from('<15I',raw,off));off+=60
    fwd=list(struct.unpack_from('<15I',raw,off));off+=60
    keys=('valid','pristine_generation','baseline_class','last_forwarded_policy_generation','last_sony_return','baseline_mismatch_count','overflow_count','policy_read_fail_count','source_fnv1a','forward_fnv1a')
    d=dict(zip(keys,h));d['source_words']=src;d['forward_words']=fwd;d['source_sha256']=sha_words(src);d['forward_sha256']=sha_words(fwd)
    return d,off

def parse_status(raw):
    require(len(raw)==STATUS_SIZE,f'status size {len(raw)}')
    vals=struct.unpack_from('<10Ii5I',raw,0)
    keys=('size','abi_version','target_supported','hook_owned','hook_fail','requested_generation','active_published_generation','policy_enabled','pending_plane_mask','reapply_mode','last_request_result','request_validation_fail_count','status_update_drop_count','reserved0','reserved1','reserved2')
    s=dict(zip(keys,vals));require(s['size']==STATUS_SIZE,'status ABI size');require(s['abi_version']==1,'status ABI version');require(s['target_supported']==1,'target unsupported');require(s['hook_owned']==1 and s['hook_fail']==0,'B hook ownership');require(s['requested_generation']==s['active_published_generation'],'requested/published divergence');require(s['policy_enabled'] in (0,1),'policy_enabled');require(s['reapply_mode']==2,'unexpected reapply mode');require(s['reserved0']==s['reserved1']==s['reserved2']==0,'reserved nonzero')
    off=64;s['planes']=[]
    for p in range(2):
        d,off=parse_plane(raw,off);d['plane']=p;s['planes'].append(d)
    require(off==STATUS_SIZE,'status parse length')
    return s

def validate_common(s):
    require(s['status_update_drop_count']==0,'status update drop')
    for p in s['planes']:
        require(p['valid']==1,f'p{p["plane"]} not observed')
        require(p['last_sony_return']>=0,f'p{p["plane"]} Sony return failure')
        require(p['overflow_count']==0,f'p{p["plane"]} overflow')
        require(p['policy_read_fail_count']==0,f'p{p["plane"]} policy read failure')

def validate_phase(s,phase,action=None):
    validate_common(s)
    if phase in ('neutral','reset-applied'):
        require(s['policy_enabled']==0,'neutral policy not disabled');require(s['pending_plane_mask']==0,'neutral still pending')
        for p in s['planes']:
            require(p['source_sha256']==IDENTITY,f'p{p["plane"]} neutral source not canonical identity')
            require(p['forward_sha256']==IDENTITY,f'p{p["plane"]} neutral forward not exact Sony identity')
            require(p['last_forwarded_policy_generation']==s['active_published_generation'],f'p{p["plane"]} neutral generation not applied')
    elif phase=='mild-applied':
        require(s['policy_enabled']==1,'mild policy disabled');require(s['pending_plane_mask']==0,'mild still pending')
        for p in s['planes']:
            require(p['baseline_class']==1,f'p{p["plane"]} unexpected baseline class')
            require(p['source_sha256']==IDENTITY,f'p{p["plane"]} pristine source drift')
            require(p['forward_sha256']==MILD,f'p{p["plane"]} mild forward mismatch')
            require(p['last_forwarded_policy_generation']==s['active_published_generation'],f'p{p["plane"]} mild generation not applied')
    elif phase=='mild-pending':
        require(action==1,'mild request did not return ACCEPTED_PENDING_REPLAY');require(s['policy_enabled']==1,'mild policy disabled');require(s['pending_plane_mask']!=0,'mild unexpectedly claims fully applied synchronously')
    elif phase=='reset-pending':
        require(action==1,'reset did not return ACCEPTED_PENDING_REPLAY');require(s['policy_enabled']==0,'reset policy still enabled');require(s['pending_plane_mask']!=0,'reset unexpectedly claims fully applied synchronously')

def main():
    ap=argparse.ArgumentParser();ap.add_argument('file',type=Path);ap.add_argument('--phase',choices=('generic','neutral','mild-pending','mild-applied','reset-pending','reset-applied'),default='generic');a=ap.parse_args();raw=a.file.read_bytes();print('FILE_SHA256='+hashlib.sha256(raw).hexdigest());action=None
    try:
        if len(raw)==ACTION_SIZE:
            action=struct.unpack_from('<i',raw,0)[0];s=parse_status(raw[4:]);print(f'KIND=ACTION ACTION_RESULT={action}')
        elif len(raw)==STATUS_BUNDLE_SIZE:
            caps=struct.unpack_from('<16I',raw,0);require(caps[0]==CAPS_SIZE and caps[1]==1,'capabilities ABI');require(caps[3]==1,'matrix capability unsupported');require(caps[4]==0 and caps[5]==2,'reapply capability drift');require(caps[6]==0 and caps[7]==0 and caps[8]==0 and caps[9]==0 and caps[10]==0,'unsupported capability accidentally enabled');s=parse_status(raw[CAPS_SIZE:]);print('KIND=STATUS')
        else:raise ValueError(f'unexpected bundle size {len(raw)}')
        if a.phase!='generic':validate_phase(s,a.phase,action)
        print(f"POLICY=requested:{s['requested_generation']} active:{s['active_published_generation']} enabled:{s['policy_enabled']} pending:0x{s['pending_plane_mask']:X} reapply:{s['reapply_mode']}")
        for p in s['planes']:
            print(f"P{p['plane']} pristine_gen={p['pristine_generation']} class={p['baseline_class']} applied_gen={p['last_forwarded_policy_generation']} raw_return={p['last_sony_return']} mismatch={p['baseline_mismatch_count']} overflow={p['overflow_count']} policy_read_fail={p['policy_read_fail_count']} source_sha256={p['source_sha256']} forward_sha256={p['forward_sha256']}")
        print('GATE1D_MATRIX_STATUS=PASS phase='+a.phase)
    except ValueError as e:
        print('GATE1D_MATRIX_STATUS=FAIL '+str(e),file=sys.stderr);return 2
    return 0
if __name__=='__main__':raise SystemExit(main())
