#!/usr/bin/env python3
from __future__ import annotations
import argparse,hashlib,json,struct
from pathlib import Path
MAGIC=0x56424450;TRACE_MAGIC=0x56425452;VERSION=6;HDR=struct.Struct('<27I');STATUS=struct.Struct('<17I');REC=struct.Struct('<5IHhIi4I256s');SNAP=720
COMMITTED=0xC01117ED;NULL=1;BOUNDS=2;READ_UNPROVEN=4;READ_PROVEN=8;RET=16
EVENT={1:'CSC_A',2:'CSC_B',17:'PANEL_WRITE',18:'PANEL_READ_ENTER',19:'PANEL_READ_EXIT',20:'MARKER'}
MARKERS={1:'BASELINE_IDLE',2:'BRIGHTNESS_A_1',3:'BRIGHTNESS_B',4:'BRIGHTNESS_A_2',5:'BRIGHTNESS_A_REPEAT',6:'COLORSPACE_0_1',7:'COLORSPACE_1',8:'COLORSPACE_0_2',9:'DISPLAY_OFF',10:'DISPLAY_ON',11:'PRE_SUSPEND',12:'POST_RESUME',13:'DIM_ENTRY',14:'DIM_EXIT'}
class TraceError(Exception):pass

def decode(path:Path):
 b=path.read_bytes();
 if len(b)<HDR.size+STATUS.size+2*SNAP:raise TraceError('short Gate-0 dump')
 hv=HDR.unpack_from(b,0)
 if hv[0]!=MAGIC or hv[1]!=VERSION or hv[2]!=HDR.size or hv[3]!=STATUS.size or hv[6]!=REC.size:raise TraceError('not exact Gate-0 protocol v6')
 sv=STATUS.unpack_from(b,HDR.size)
 if sv[0]!=TRACE_MAGIC or sv[1]!=VERSION:raise TraceError('status protocol mismatch')
 off=HDR.size+STATUS.size+hv[4]+hv[5];count=hv[7]
 if off+count*REC.size!=len(b):raise TraceError('dump size/count mismatch')
 rows=[];seen=set();bounds=0;reader=0
 for x in range(count):
  v=REC.unpack_from(b,off+x*REC.size);comm,seq,comp,tid,inv,event,plane,flags,raw,a0,a1,n,lost,payload=v
  if comm!=COMMITTED:raise TraceError(f'uncommitted record {x}')
  if seq in seen:raise TraceError('duplicate sequence');seen.add(seq)
  if n>256:raise TraceError('payload length overflow')
  name=EVENT.get(event,f'UNKNOWN_{event}')
  if event in (18,19):
   reader+=1
   if n!=0 or not(flags&READ_UNPROVEN) or (flags&READ_PROVEN):raise TraceError('reader payload contract violated')
  if event==17 and flags&BOUNDS:
   bounds+=1
   if n!=0:raise TraceError('bounds-rejected writer copied payload')
  if event in (1,2) and n not in (0,60):raise TraceError('CSC payload size invalid')
  row={'sequence':seq,'completion_sequence':comp,'thread_id':tid,'invocation_id':inv,'event':name,'plane':plane,'flags':flags,'raw_return':raw if flags&RET else None,'arg0':a0,'arg1':a1,'payload_length':n,'payload_hex':payload[:n].hex(),'payload_sha256':hashlib.sha256(payload[:n]).hexdigest() if n else None,'lost_snapshot':lost,'scope_invocation_ids':[]}
  if event==20:row['marker']=MARKERS.get(a0,f'UNKNOWN_{a0}')
  rows.append(row)
 lost=sv[7];missing=sv[12];quality='AUTHORITATIVE' if not lost and not missing and not bounds else 'PARTIAL'
 return {'version':VERSION,'header':hv,'status':{'enabled':sv[3],'lost_records':lost,'active_hooks':sv[9],'installed_hook_mask':sv[10],'required_hook_mask':sv[11],'missing_required_mask':missing,'hook_fail_mask':sv[16]},'records':sorted(rows,key=lambda r:r['sequence']),'capture_quality':quality,'affine_authoritative':quality=='AUTHORITATIVE','panel_authoritative':quality=='AUTHORITATIVE','reader_events':reader,'bounds_rejected':bounds,'pre_snapshot':{'planes':[]},'post_snapshot':{'planes':[]}}
def main():
 ap=argparse.ArgumentParser();ap.add_argument('trace',type=Path);ap.add_argument('--json',type=Path);a=ap.parse_args();d=decode(a.trace);s=json.dumps(d,indent=2);print(s);a.json and a.json.write_text(s+'\n')
if __name__=='__main__':main()
