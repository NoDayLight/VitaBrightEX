#!/usr/bin/env python3
from __future__ import annotations
import argparse,hashlib,json,re
from collections import defaultdict
from pathlib import Path
from vita_elf_audit import VitaElf
LCD_SHA='24752bb4c69f0c241701cab0364cde9cbe5c95128b6bfe02bf3263aad524246e';BASE={'low':0x81001AF8,'mid':0x81001AC8,'high':0x81001B90};COLOR={0:0x81001AE8,1:0x81001B70};SECONDARY={0:0x81001B80,1:0x81001B20};WORK=0x81002020;SPLIT=0x29;END=0xFF;DELAY=0x0D
COMMON_DCS_HINTS={0x0A:'GET_POWER_MODE',0x26:'SET_GAMMA_CURVE',0x29:'SET_DISPLAY_ON',0x2D:'WRITE_LUT'}
def byte(e,va):_,o=e.file_from_va(va);return e.data[o]
def record(e,va,end_is_terminator=True):
 cmd=byte(e,va)
 if cmd==END and end_is_terminator:return {'cmd':cmd,'length':0,'payload':b'','next':va+1,'raw_len':1}
 n=byte(e,va+1)
 if cmd==DELAY:return {'cmd':cmd,'length':n,'payload':b'','next':va+2,'raw_len':2}
 _,o=e.file_from_va(va+2);payload=e.data[o:o+n]
 if len(payload)!=n:raise ValueError('record exceeds file-backed data')
 return {'cmd':cmd,'length':n,'payload':payload,'next':va+2+n,'raw_len':2+n}
def append_record(dst,rec,prov):dst.append({'cmd':rec['cmd'],'length':rec['length'],'payload':rec['payload'],'provenance':prov})
def copy_until(e,start,stop_cmd=None,include_stop=False,end_terminates=True):
 out=[];va=start;guard=0
 while guard<1024:
  guard+=1;cmd=byte(e,va)
  if stop_cmd is not None and cmd==stop_cmd:
   if include_stop:
    r=record(e,va,end_is_terminator=end_terminates);append_record(out,r,{'source_va':va})
   return out,va
  if cmd==END and end_terminates:return out,va
  r=record(e,va,end_is_terminator=end_terminates);append_record(out,r,{'source_va':va});va=r['next']
 raise ValueError(f'record walk did not terminate from 0x{start:08X}')
def working(e,bucket,color,secondary):
 base=BASE[bucket];prefix,split_va=copy_until(e,base,SPLIT,False,False)
 if byte(e,split_va)!=SPLIT:raise ValueError('base walk failed to reach split command 0x29')
 col,_=copy_until(e,COLOR[color],END,False,True);sec,_=copy_until(e,SECONDARY[secondary],END,False,True);suffix,_=copy_until(e,split_va,END,False,True);rows=[]
 for label,src in [('base_prefix',prefix),('color',col),('secondary',sec),('base_suffix',suffix)]:
  for x in src:x=dict(x);x['fragment']=label;rows.append(x)
 if not any(x['cmd']==SPLIT and x['fragment']=='base_suffix' for x in rows):raise ValueError('composed stream does not contain required base-suffix split command 0x29')
 return rows
def record_meta(x,index):p=x['payload'];return {'index':index,'command':x['cmd'],'length':x['length'],'kind':'delay' if x['cmd']==DELAY else 'panel_write','payload_sha256':hashlib.sha256(p).hexdigest(),'source_fragment':x['fragment'],'source_va':x['provenance']['source_va']}
def stream_digest(rows):
 h=hashlib.sha256()
 for x in rows:h.update(bytes([x['cmd']]));h.update(bytes([x['length']]));h.update(b'' if x['cmd']==DELAY else x['payload'])
 h.update(bytes([END]));return h.hexdigest()
def lcs_pairs(a,b):
 n,m=len(a),len(b);dp=[[0]*(m+1) for _ in range(n+1)]
 for i in range(n-1,-1,-1):
  for j in range(m-1,-1,-1):dp[i][j]=1+dp[i+1][j+1] if a[i]['cmd']==b[j]['cmd'] else max(dp[i+1][j],dp[i][j+1])
 i=j=0;p=[]
 while i<n and j<m:
  if a[i]['cmd']==b[j]['cmd']:p.append((i,j));i+=1;j+=1
  elif dp[i+1][j]>=dp[i][j+1]:i+=1
  else:j+=1
 return p
def diff(a,b):
 pairs=lcs_pairs(a,b);out=[];ai=bi=0
 for i,j in pairs+[(len(a),len(b))]:
  while ai<i:out.append({'classification':'command_removed','mode0_index':ai,'command':a[ai]['cmd'],'length':a[ai]['length']});ai+=1
  while bi<j:out.append({'classification':'command_added','mode1_index':bi,'command':b[bi]['cmd'],'length':b[bi]['length']});bi+=1
  if i<len(a):
   x,y=a[i],b[j];hx=hashlib.sha256(x['payload']).hexdigest();hy=hashlib.sha256(y['payload']).hexdigest();kind='same_command_changed_length' if x['length']!=y['length'] else ('same_command_changed_payload' if hx!=hy else 'same_command_identical')
   if kind!='same_command_identical':out.append({'classification':kind,'mode0_index':i,'mode1_index':j,'command':x['cmd'],'length0':x['length'],'length1':y['length'],'payload0_sha256':hx,'payload1_sha256':hy})
   ai=i+1;bi=j+1
 return out
def command_inventory(variants,by):
 inv=defaultdict(lambda:{'lengths':set(),'occurrences':0,'source_fragments':set(),'ddb_buckets':set(),'color_space_modes':set(),'secondary_states':set(),'payload_sha256':set()});max_payload=0
 for v in variants:
  rows=by[(v['ddb_bucket'],v['secondary_state'],v['color_space_mode'])]
  for x in rows:
   if x['cmd']==DELAY:continue
   q=inv[x['cmd']];q['lengths'].add(x['length']);q['occurrences']+=1;q['source_fragments'].add(x['fragment']);q['ddb_buckets'].add(v['ddb_bucket']);q['color_space_modes'].add(v['color_space_mode']);q['secondary_states'].add(v['secondary_state']);q['payload_sha256'].add(hashlib.sha256(x['payload']).hexdigest());max_payload=max(max_payload,x['length'])
 rows=[]
 for cmd in sorted(inv):
  q=inv[cmd];rows.append({'command':cmd,'command_hex':f'0x{cmd:02X}','common_dcs_name_hint':COMMON_DCS_HINTS.get(cmd),'lengths':sorted(q['lengths']),'occurrences':q['occurrences'],'source_fragments':sorted(q['source_fragments']),'ddb_buckets':sorted(q['ddb_buckets']),'color_space_modes':sorted(q['color_space_modes']),'secondary_states':sorted(q['secondary_states']),'unique_payload_count':len(q['payload_sha256']),'payload_sha256':sorted(q['payload_sha256'])})
 return rows,max_payload
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--lcd',type=Path,required=True);ap.add_argument('--json',type=Path,required=True);a=ap.parse_args();e=VitaElf(a.lcd)
 if e.sha256!=LCD_SHA:raise SystemExit(f'SceLcd hash drift {e.sha256}')
 variants=[];by={}
 for bucket in ('low','mid','high'):
  for secondary in (0,1):
   for color in (0,1):
    rows=working(e,bucket,color,secondary);key=(bucket,secondary,color);by[key]=rows;variants.append({'ddb_bucket':bucket,'secondary_state':secondary,'color_space_mode':color,'record_count':len(rows),'working_stream_sha256':stream_digest(rows),'split_0x29_count':sum(1 for x in rows if x['cmd']==SPLIT),'records':[record_meta(x,i) for i,x in enumerate(rows)]})
 if any(v['split_0x29_count']!=1 for v in variants):raise SystemExit('every reconstructed working stream must contain exactly one copied 0x29 split record')
 deltas=[]
 for bucket in ('low','mid','high'):
  for secondary in (0,1):
   a0=by[(bucket,secondary,0)];a1=by[(bucket,secondary,1)];deltas.append({'ddb_bucket':bucket,'secondary_state':secondary,'mode0_sha256':stream_digest(a0),'mode1_sha256':stream_digest(a1),'changes':diff(a0,a1)})
 inventory,max_payload=command_inventory(variants,by);protocol=(Path(__file__).parent/'diagnostics/iftu_csc_trace/trace_protocol.h').read_text();m=re.search(r'^\s*#define\s+VBE_TRACE_PAYLOAD_MAX\s+(\d+)(?:u|U)?\s*$',protocol,re.M)
 if not m:raise SystemExit('cannot derive VBE_TRACE_PAYLOAD_MAX from trace protocol')
 trace_payload_capacity=int(m.group(1))
 if trace_payload_capacity<max_payload:raise SystemExit(f'trace payload capacity {trace_payload_capacity} < proven Sony max payload {max_payload}')
 result={'schema':2,'firmware':'3.65','status':'PROVEN_STATIC_RECONSTRUCTION','builder_va':0x81000B78,'working_buffer_va':WORK,'record_format':{'terminator':END,'delay_command':DELAY,'delay_encoding':'0x0D,length; no payload','write_encoding':'command,length,payload[length]'},'builder_semantics':{'base_prefix':'copy records until command 0x29; 0xFF is not terminal in this first loop','color_fragment':'copy until 0xFF','secondary_fragment':'copy until 0xFF','base_suffix':'resume at and copy the 0x29 record through 0xFF'},'branch_space':{'ddb_buckets':{'low':'state+0x0C <= 0x24','mid':'0x24 < state+0x0C <= 0x31','high':'state+0x0C > 0x31'},'color_space_modes':[0,1],'secondary_state':[0,1]},'max_panel_write_payload_length':max_payload,'trace_payload_capacity':trace_payload_capacity,'trace_payload_capacity_assertion':'PASS','command_inventory':inventory,'variants':variants,'mode_deltas':deltas,'command_namespace_note':'Common MIPI-DCS names are hints only; matching command numbers do not prove controller identity or semantic equivalence.','artifact_policy':'No raw source or composed proprietary payload bytes are emitted; only command IDs, lengths, source VAs, dependence metadata, and SHA-256 metadata.'}
 a.json.write_text(json.dumps(result,indent=2)+'\n');print('PANEL_PROGRAM_RECONSTRUCTION');print('  valid branch combinations=12');print(f'  maximum single panel-write payload={max_payload}');print(f'  trace payload capacity={trace_payload_capacity} PASS')
 for v in variants:print(f"  bucket={v['ddb_bucket']} secondary={v['secondary_state']} mode={v['color_space_mode']} records={v['record_count']} split29={v['split_0x29_count']} sha256={v['working_stream_sha256']}")
 print('COMMAND_INVENTORY')
 for x in inventory:print(f"  {x['command_hex']} lengths={x['lengths']} occurrences={x['occurrences']} fragments={x['source_fragments']} dcs_hint={x['common_dcs_name_hint']}")
if __name__=='__main__':main()
