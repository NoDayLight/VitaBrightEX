#!/usr/bin/env python3
from __future__ import annotations
import argparse,json,struct
from pathlib import Path
from gate0_trace_decode import decode,TraceError
def s39(v):
 v&=0xfff
 if v&0x800:v-=0x1000
 return v/512.0
def csc_detail(r):
 b=bytes.fromhex(r['payload_hex']);out={k:r[k]for k in('sequence','completion_sequence','thread_id','invocation_id','event','plane','raw_return','payload_state','payload_sha256')}
 if len(b)==60:
  words=list(struct.unpack('<15I',b));out['raw_words']=[f'0x{x:08X}'for x in words];out['s3_9']=[s39(x)for x in words[6:15]]
 return out
def compare(rows,a,b):
 def vals(mark):
  phase=False;v=[]
  for r in rows:
   if r['event']=='MARKER':phase=r.get('marker')==mark;continue
   if phase and r['event']in('CSC_A','CSC_B')and r['payload_length']==60:v.append((r['event'],r['plane'],r['payload_hex']))
  return v
 x,y=vals(a),vals(b)
 if not x or not y:return'NO_EVENT'
 if len(x)!=len(y):return'DIFFERENT'
 return'BYTE_IDENTICAL'if x==y else'DIFFERENT'
def main():
 ap=argparse.ArgumentParser();ap.add_argument('trace',type=Path);ap.add_argument('--json',type=Path);a=ap.parse_args()
 try:d=decode(a.trace,authoritative=True)
 except TraceError as e:out={'result':'EMPIRICAL_BASELINE_INCOMPLETE','reason':str(e)}
 else:
  phases={};cur='PRE_MARKER'
  for r in d['records']:
   if r['event']=='MARKER':cur=r.get('marker',f"UNKNOWN_{r['arg0']}");phases.setdefault(cur,[]);continue
   phases.setdefault(cur,[]).append(r)
  report={}
  for name,rows in phases.items():report[name]={'csc':[csc_detail(r)for r in rows if r['event']in('CSC_A','CSC_B')],'iftu':[r for r in rows if r['event'].startswith('IFTU_ENABLE')],'panel':[r for r in rows if r['event']=='PANEL_WRITE'],'reader':[r for r in rows if r['event'].startswith('PANEL_READ')]}
  out={'result':'EMPIRICAL_BASELINE_COMPLETE','status':d['status'],'phases':report,'equivalent_state':{'BRIGHTNESS_A_1_vs_A_2':compare(d['records'],'BRIGHTNESS_A_1','BRIGHTNESS_A_2'),'COLORSPACE_0_1_vs_0_2':compare(d['records'],'COLORSPACE_0_1','COLORSPACE_0_2')}}
 s=json.dumps(out,indent=2);print(s);a.json and a.json.write_text(s+'\n')
if __name__=='__main__':main()
