#!/usr/bin/env python3
from __future__ import annotations
import argparse, json, struct
from pathlib import Path

MAGIC=0x56425452; DUMP_MAGIC=0x56424450; VERSION=5; FW=0x03650000; COMMITTED=0xC01117ED
RECORD_SIZE=304; STATUS_SIZE=68; PLANE_SIZE=136; LCD_SIZE=20; SNAP_DATA_SIZE=700; SNAP_SIZE=720; HEADER_SIZE=108
PAYLOAD_MAX=256; CSC_SIZE=0x3C; PLANE_COUNT=5
FLAG_NULL=1<<0; FLAG_TRUNC=1<<1; FLAG_READ_UNCERTAIN=1<<2; FLAG_READ_PROVEN=1<<3; FLAG_RETURN_VALID=1<<4
HOOKS={'CSC_A':1<<0,'CSC_B':1<<1,'DISPLAY_BRIGHT':1<<2,'DISPLAY_COLOR':1<<3,'LCD_BRIGHT':1<<4,'LCD_COLOR':1<<5,'DISPLAY_ON':1<<6,'DISPLAY_OFF':1<<7,'IFTU_ENABLE':1<<8,'PANEL_WRITE':1<<9,'PANEL_READ':1<<10}
AFFINE_REQUIRED=sum(HOOKS[k] for k in ('CSC_A','CSC_B','DISPLAY_BRIGHT','DISPLAY_COLOR','LCD_BRIGHT','LCD_COLOR','DISPLAY_ON','DISPLAY_OFF','IFTU_ENABLE'))
PANEL_REQUIRED=AFFINE_REQUIRED|HOOKS['PANEL_WRITE']|HOOKS['PANEL_READ']
SNAP_LOWIO=1; SNAP_LCD=2; AFFINE_SNAPS=SNAP_LOWIO; PANEL_SNAPS=SNAP_LOWIO|SNAP_LCD
CAP_AUTHORITATIVE=1; CAP_PARTIAL=2; SNAP_STABLE=1
EVENT={1:'CSC_A',2:'CSC_B',3:'DISPLAY_BRIGHTNESS_ENTER',4:'DISPLAY_BRIGHTNESS_EXIT',5:'DISPLAY_COLORSPACE_ENTER',6:'DISPLAY_COLORSPACE_EXIT',7:'LCD_BRIGHTNESS_ENTER',8:'LCD_BRIGHTNESS_EXIT',9:'LCD_COLORSPACE_ENTER',10:'LCD_COLORSPACE_EXIT',11:'DISPLAY_ON_ENTER',12:'DISPLAY_ON_EXIT',13:'DISPLAY_OFF_ENTER',14:'DISPLAY_OFF_EXIT',15:'IFTU_ENABLE_ENTER',16:'IFTU_ENABLE_EXIT',17:'PANEL_WRITE',18:'PANEL_READ_ENTER',19:'PANEL_READ_EXIT'}
ENTER_TO_EXIT={3:4,5:6,7:8,9:10,11:12,13:14,15:16,18:19}; EXIT_TO_ENTER={v:k for k,v in ENTER_TO_EXIT.items()}
RAW_EVENTS={1,2,4,6,8,10,12,14,16,17,19}; LEAF_ZERO_INVOC={1,2,17}
STATUS_FIELDS=('magic','version','firmware_version','enabled','record_capacity','slots_reserved','committed_records','lost_records','last_sequence','active_hooks','installed_hook_mask','required_hook_mask','missing_required_mask','snapshot_available_mask','required_snapshot_mask','missing_required_snapshot_mask','hook_fail_mask')
HEADER_FIELDS=('magic','version','header_size','status_size','pre_snapshot_size','post_snapshot_size','record_size','record_count','capture_quality','pre_snapshot_present','pre_snapshot_retry_count','post_snapshot_retry_count','validation_flags','affine_required_mask','affine_missing_required_mask','panel_required_mask','panel_missing_required_mask','affine_required_snapshot_mask','affine_missing_required_snapshot_mask','panel_required_snapshot_mask','panel_missing_required_snapshot_mask','affine_capture_quality','panel_capture_quality','panel_read_validity','reserved0','reserved1','reserved2')
REC=struct.Struct('<IIIIIHhIiIIII256s'); U32_17=struct.Struct('<17I'); U32_27=struct.Struct('<27I')
if REC.size!=RECORD_SIZE or U32_17.size!=STATUS_SIZE or U32_27.size!=HEADER_SIZE: raise RuntimeError('host struct size drift')

class TraceError(ValueError): pass
def req(c,m):
    if not c: raise TraceError(m)
def s39(raw:int):
    v=raw & 0xFFF
    if v & 0x800: v-=0x1000
    return {'raw_word':raw,'raw_s3_9':raw & 0xFFF,'decoded':v/512.0}

def csc(words):
    names=['post_add_0','post_add_1_2','post_clamp_max_0','post_clamp_min_0','post_clamp_max_1_2','post_clamp_min_1_2']
    out={'raw_words':[f'0x{x:08X}' for x in words]}
    for i,n in enumerate(names): out[n]=f'0x{words[i]:08X}'
    out['ctm']=[[s39(words[6+r*3+c]) for c in range(3)] for r in range(3)]
    return out

def parse_plane(data,off):
    a,c,p,res=struct.unpack_from('<4I',data,off); req(res==0,'plane reserved nonzero')
    ca=data[off+16:off+76]; cb=data[off+76:off+136]
    return {'active_state_1e8':a,'csc_control_100':c,'private_control_1f8':p,'csc_a':csc(struct.unpack('<15I',ca)),'csc_b':csc(struct.unpack('<15I',cb))}
def parse_snapshot(data,off):
    req(len(data)>=off+SNAP_SIZE,'truncated snapshot')
    magic,version,fw,avail,flags=struct.unpack_from('<5I',data,off)
    req((magic,version,fw)==(MAGIC,VERSION,FW),'snapshot identity mismatch'); req(flags & ~SNAP_STABLE ==0,'snapshot flags unknown')
    pos=off+20; planes=[parse_plane(data,pos+i*PLANE_SIZE) for i in range(PLANE_COUNT)]; pos+=PLANE_COUNT*PLANE_SIZE
    d08,d0a,bucket,res,bri,sec,csm=struct.unpack_from('<4H3I',data,pos); req(res==0,'lcd reserved nonzero')
    return {'available_mask':avail,'stable':bool(flags&SNAP_STABLE),'planes':planes,'lcd':{'ddb_08':d08,'ddb_0a':d0a,'bucket_0c':bucket,'brightness_1c':bri,'secondary_program_28':sec,'color_space_mode_2c':csm}}

def return_semantics(t,raw):
    if t in (1,2,17,19):
        if raw==0:return 'SUCCESS'
        if raw<0:return 'FAILURE'
    return f'RETURNED({raw})'

def parse_record(data,off):
    vals=REC.unpack_from(data,off); committed,seq,comp,tid,inv,t,plane,flags,raw,a0,a1,plen,lost,payload=vals
    req(committed==COMMITTED,'uncommitted record'); req(t in EVENT,'unknown event'); req(0<plen<=PAYLOAD_MAX or plen==0,'payload length invalid')
    allowed=FLAG_NULL|FLAG_TRUNC|FLAG_READ_UNCERTAIN|FLAG_READ_PROVEN|FLAG_RETURN_VALID; req(flags&~allowed==0,'unknown record flags')
    has_ret=bool(flags&FLAG_RETURN_VALID); req(has_ret==(t in RAW_EVENTS),'return-valid contract mismatch'); req(tid!=0,'zero thread id')
    if t in ENTER_TO_EXIT or t in EXIT_TO_ENTER: req(inv!=0,'scoped event missing invocation id')
    if t in LEAF_ZERO_INVOC: req(inv==0,'leaf event has invocation id')
    if t in (1,2): req(plen==0 if flags&FLAG_NULL else plen==CSC_SIZE,'CSC payload size mismatch')
    if t==17:
        req(plen==0 if flags&FLAG_NULL else plen==min(a1,PAYLOAD_MAX),'panel write payload mismatch')
        req(bool(flags&FLAG_TRUNC)==(not(flags&FLAG_NULL) and a1>PAYLOAD_MAX),'panel write truncation mismatch')
    if t==19:
        if flags&FLAG_NULL or raw!=0:req(plen==0,'failed/null panel read captured payload')
        else:req(plen==min(a1,PAYLOAD_MAX),'panel read payload mismatch')
        req(not((flags&FLAG_READ_PROVEN) and (flags&FLAG_READ_UNCERTAIN)),'contradictory read validity')
    item={'sequence':seq,'completion_sequence':comp,'thread_id':tid,'invocation_id':inv,'event_type':t,'event':EVENT[t],'plane':plane,'flags':flags,'raw_return':raw if has_ret else None,'return_semantics':return_semantics(t,raw) if has_ret else None,'arg0':a0,'arg1':a1,'payload_length':plen,'lost_snapshot':lost,'payload_hex':payload[:plen].hex()}
    if t in (1,2) and not(flags&FLAG_NULL): item['csc']=csc(struct.unpack('<15I',payload[:CSC_SIZE]))
    return item

def validate_scopes(records):
    stacks={}
    for r in records:
        st=stacks.setdefault(r['thread_id'],[]); t=r['event_type']
        if t in ENTER_TO_EXIT: st.append((r['invocation_id'],ENTER_TO_EXIT[t],r['sequence']))
        elif t in EXIT_TO_ENTER:
            req(st,'scope exit without enter'); inv,want,_=st.pop(); req((inv,want)==(r['invocation_id'],t),'non-nested or mismatched invocation')
        r['scope_invocation_ids']=[x[0] for x in st]
    req(all(not s for s in stacks.values()),'unterminated invocation scope')

def decode_bytes(data:bytes):
    req(len(data)>=HEADER_SIZE+STATUS_SIZE+SNAP_SIZE,'file too short')
    h=dict(zip(HEADER_FIELDS,U32_27.unpack_from(data,0))); req(h['magic']==DUMP_MAGIC,'bad dump magic'); req(h['version']==VERSION,'unsupported trace version (v5 required)')
    req(h['header_size']==HEADER_SIZE and h['status_size']==STATUS_SIZE and h['record_size']==RECORD_SIZE,'serialized size mismatch')
    req(h['post_snapshot_size']==SNAP_SIZE,'post snapshot size mismatch'); req(h['pre_snapshot_present'] in (0,1),'pre snapshot presence invalid')
    req(h['pre_snapshot_size']==(SNAP_SIZE if h['pre_snapshot_present'] else 0),'pre snapshot size/presence mismatch'); req(not any(h[f'reserved{i}'] for i in range(3)),'header reserved nonzero')
    pos=HEADER_SIZE; s=dict(zip(STATUS_FIELDS,U32_17.unpack_from(data,pos))); pos+=STATUS_SIZE
    req((s['magic'],s['version'],s['firmware_version'])==(MAGIC,VERSION,FW),'status identity mismatch'); req(s['record_capacity']==512,'record capacity drift')
    req(s['required_hook_mask']==PANEL_REQUIRED and s['missing_required_mask']==(PANEL_REQUIRED & ~s['installed_hook_mask']),'status hook mask contradiction')
    req(s['required_snapshot_mask']==PANEL_SNAPS and s['missing_required_snapshot_mask']==(PANEL_SNAPS & ~s['snapshot_available_mask']),'status snapshot mask contradiction')
    pre=None
    if h['pre_snapshot_present']: pre=parse_snapshot(data,pos); pos+=SNAP_SIZE
    post=parse_snapshot(data,pos); pos+=SNAP_SIZE
    expected=pos+h['record_count']*RECORD_SIZE; req(expected==len(data),'record-count/file-length mismatch or trailing bytes')
    records=[parse_record(data,pos+i*RECORD_SIZE) for i in range(h['record_count'])]
    seq=[r['sequence'] for r in records]; comp=[r['completion_sequence'] for r in records]
    req(len(set(seq))==len(seq) and seq==sorted(seq) and seq==list(range(1,len(seq)+1)),'entry sequence not monotonic/contiguous')
    req(set(comp)==set(range(1,len(comp)+1)),'completion sequence not unique/contiguous'); req(s['last_sequence']==len(records) and s['committed_records']==len(records),'status/record sequence mismatch')
    req(s['lost_records']==0 or h['affine_capture_quality']==CAP_PARTIAL,'lost records with authoritative affine quality'); req(s['lost_records']==0 or h['panel_capture_quality']==CAP_PARTIAL,'lost records with authoritative panel quality')
    req(h['affine_required_mask']==AFFINE_REQUIRED and h['panel_required_mask']==PANEL_REQUIRED,'header hook authority mask drift')
    req(h['affine_missing_required_mask']==(AFFINE_REQUIRED & ~s['installed_hook_mask']),'affine missing mask contradiction'); req(h['panel_missing_required_mask']==(PANEL_REQUIRED & ~s['installed_hook_mask']),'panel missing mask contradiction')
    common=(pre['available_mask'] & post['available_mask']) if pre else 0
    req(h['affine_required_snapshot_mask']==AFFINE_SNAPS and h['panel_required_snapshot_mask']==PANEL_SNAPS,'header snapshot masks drift')
    req(h['affine_missing_required_snapshot_mask']==(AFFINE_SNAPS & ~common),'affine snapshot mask contradiction'); req(h['panel_missing_required_snapshot_mask']==(PANEL_SNAPS & ~common),'panel snapshot mask contradiction')
    if h['affine_capture_quality']==CAP_AUTHORITATIVE:
        req(pre is not None and pre['stable'] and post['stable'] and h['affine_missing_required_mask']==0 and h['affine_missing_required_snapshot_mask']==0 and s['lost_records']==0,'false affine authority')
    if h['panel_capture_quality']==CAP_AUTHORITATIVE:
        req(h['affine_capture_quality']==CAP_AUTHORITATIVE and h['panel_missing_required_mask']==0 and h['panel_missing_required_snapshot_mask']==0 and h['panel_read_validity']!=2,'false panel authority')
    validate_scopes(records)
    return {'protocol_version':VERSION,'header':h,'status':s,'pre_snapshot':pre,'post_snapshot':post,'records':records,'affine_authoritative':h['affine_capture_quality']==CAP_AUTHORITATIVE,'panel_authoritative':h['panel_capture_quality']==CAP_AUTHORITATIVE}

def decode(path:Path): return decode_bytes(path.read_bytes())
def human(d):
    lines=[f"Gate-0 trace v{VERSION}: records={len(d['records'])}",f"affine={'AUTHORITATIVE' if d['affine_authoritative'] else 'PARTIAL'} panel={'AUTHORITATIVE' if d['panel_authoritative'] else 'PARTIAL'}",f"lost={d['status']['lost_records']} panel_read_validity={d['header']['panel_read_validity']}"]
    if d['post_snapshot']:
        l=d['post_snapshot']['lcd']; lines.append(f"post LCD ddb=0x{l['ddb_08']:04X}/0x{l['ddb_0a']:04X} bucket=0x{l['bucket_0c']:04X} brightness={l['brightness_1c']} secondary={l['secondary_program_28']} color_space={l['color_space_mode_2c']}")
    for r in d['records']:
        extra=f" raw={r['raw_return']} {r['return_semantics']}" if r['raw_return'] is not None else ''
        lines.append(f"#{r['sequence']:03d}/c{r['completion_sequence']:03d} t={r['thread_id']} inv={r['invocation_id']} {r['event']}{extra} len={r['payload_length']} scope={r['scope_invocation_ids']}")
    return '\n'.join(lines)
def main():
    ap=argparse.ArgumentParser(); ap.add_argument('trace',type=Path); ap.add_argument('--json',type=Path); a=ap.parse_args()
    d=decode(a.trace); print(human(d));
    if a.json:a.json.write_text(json.dumps(d,indent=2)+'\n')
if __name__=='__main__': main()
