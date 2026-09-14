#!/usr/bin/env python3
from __future__ import annotations
import argparse,hashlib,json,struct
from pathlib import Path
MAGIC=0x56424450;TRACE_MAGIC=0x56425452;VERSION=7;HDR=struct.Struct('<16I');STATUS=struct.Struct('<18I');REC=struct.Struct('<5IHhIi8I60sI');COMMITTED=0xC01117ED;RET=1<<2
EVENT={1:'CSC_A',2:'CSC_B',3:'IFTU_ENABLE_ENTER',4:'IFTU_ENABLE_EXIT',5:'PANEL_WRITE',6:'PANEL_READ_ENTER',7:'PANEL_READ_EXIT',8:'MARKER'}
MARKERS={1:'BASELINE_IDLE',2:'BRIGHTNESS_A_1',3:'BRIGHTNESS_B',4:'BRIGHTNESS_A_2',5:'COLORSPACE_0_1',6:'COLORSPACE_1',7:'COLORSPACE_0_2',8:'DISPLAY_OFF',9:'DISPLAY_ON',10:'PRE_SUSPEND',11:'POST_RESUME',12:'DIM_ENTRY',13:'DIM_EXIT'}
PTR={0:'UNPROVEN',1:'INVALID_RANGE',2:'SCE_LCD_MAPPED_SEGMENT_0',3:'SCE_LCD_MAPPED_SEGMENT_1',4:'SCE_LCD_MAPPED_SEGMENT_2',5:'SCE_LCD_MAPPED_SEGMENT_3'}
PAYLOAD={0:'NONE',1:'ZERO_LENGTH',2:'PAYLOAD_NOT_CAPTURED_UNPROVEN',3:'INVALID_RANGE',4:'NULL_NONZERO',5:'CSC_EXACT_3C',6:'CSC_NULL',7:'CSC_INVALID_PLANE'}
class TraceError(Exception):pass
def decode(path:Path,authoritative=False):
 b=path.read_bytes()
 if len(b)<HDR.size+STATUS.size:raise TraceError('short Gate-0A dump')
 h=HDR.unpack_from(b,0)
 if h[0]!=MAGIC or h[1]!=VERSION or h[2]!=HDR.size or h[3]!=STATUS.size or h[4]!=REC.size:raise TraceError('not exact Gate-0 protocol v7')
 st=STATUS.unpack_from(b,HDR.size)
 if st[0]!=TRACE_MAGIC or st[1]!=VERSION:raise TraceError('status protocol mismatch')
 count=h[5];off=HDR.size+STATUS.size
 if off+count*REC.size!=len(b):raise TraceError('dump size/count mismatch')
 rows=[];seqs=[];comps=[]
 for x in range(count):
  v=REC.unpack_from(b,off+x*REC.size);comm,seq,comp,tid,inv,event,plane,flags,raw,a0,a1,ptr,prov,pstate,n,lost,epoch,payload,res=v
  if comm!=COMMITTED:raise TraceError(f'uncommitted record {x}')
  if event not in EVENT:raise TraceError(f'impossible event type {event}')
  if prov not in PTR or pstate not in PAYLOAD:raise TraceError('unknown provenance/payload state')
  if n>60:raise TraceError('payload length overflow')
  if event in(5,6,7)and n!=0:raise TraceError('Gate-0A panel payload bytes present')
  if event in(1,2):
   if pstate==5 and n!=60:raise TraceError('CSC exact payload is not 0x3C')
   if pstate!=5 and n!=0:raise TraceError('non-captured CSC carries bytes')
  seqs.append(seq);comps.append(comp);row={'sequence':seq,'completion_sequence':comp,'thread_id':tid,'invocation_id':inv,'event':EVENT[event],'plane':plane,'flags':flags,'raw_return':raw if flags&RET else None,'arg0':a0,'arg1':a1,'pointer_value':ptr,'pointer_provenance':PTR[prov],'payload_state':PAYLOAD[pstate],'payload_length':n,'payload_hex':payload[:n].hex(),'payload_sha256':hashlib.sha256(payload[:n]).hexdigest()if n else None,'lost_snapshot':lost,'ring_epoch':epoch}
  if event==8:row['marker']=MARKERS.get(a0,f'UNKNOWN_{a0}')
  rows.append(row)
 if seqs!=list(range(1,count+1)):raise TraceError('reservation sequence is not exact monotonic 1..N')
 if sorted(comps)!=list(range(1,count+1)):raise TraceError('completion sequence is not a permutation of 1..N')
 pairs={}
 for r in rows:
  if r['event']in('IFTU_ENABLE_ENTER','IFTU_ENABLE_EXIT','PANEL_READ_ENTER','PANEL_READ_EXIT'):
   k=(r['invocation_id'],r['thread_id'],r['event'].split('_ENTER')[0].split('_EXIT')[0]);pairs.setdefault(k,[]).append(r['event'])
 for k,ev in pairs.items():
  base=k[2]
  if sorted(ev)!=sorted([base+'_ENTER',base+'_EXIT']):raise TraceError(f'ENTER/EXIT pairing violation {k}: {ev}')
 status={'firmware_version':st[2],'lifecycle':st[3],'record_capacity':st[4],'slots_reserved':st[5],'committed_records':st[6],'lost_records':st[7],'last_sequence':st[8],'active_producers':st[9],'owned_hook_mask':st[10],'required_hook_mask':st[11],'missing_required_mask':st[12],'hook_fail_mask':st[13],'ring_epoch':st[14],'install_publication_class':st[15],'flags':st[16]}
 if status['committed_records']!=count:raise TraceError('status committed count mismatch')
 if authoritative and(status['lost_records']or status['missing_required_mask']or status['hook_fail_mask']):raise TraceError('authoritative Gate-0A preconditions failed')
 return{'version':VERSION,'status':status,'records':rows,'baseline':'EMPIRICAL_BASELINE_COMPLETE'if authoritative else'DECODED'}
def main():
 ap=argparse.ArgumentParser();ap.add_argument('trace',type=Path);ap.add_argument('--json',type=Path);ap.add_argument('--authoritative',action='store_true');a=ap.parse_args();d=decode(a.trace,a.authoritative);s=json.dumps(d,indent=2);print(s);a.json and a.json.write_text(s+'\n')
if __name__=='__main__':main()
