#!/usr/bin/env python3
from __future__ import annotations
import argparse,collections,json
from pathlib import Path
from gate0_trace_decode import decode

def audit(paths):
 out={'schema':2,'method':'Gate-0 observational evidence only','captures':[],'conclusions':[]}
 for p in paths:
  d=decode(p);phase='PRE_MARKER';ph=collections.OrderedDict();ph[phase]=[]
  for r in d['records']:
   if r['event']=='MARKER':phase=r.get('marker','UNKNOWN');ph.setdefault(phase,[]);continue
   ph.setdefault(phase,[]).append(r)
  phases=[]
  for name,rows in ph.items():
   c=collections.Counter(r['event'] for r in rows);writes=[r for r in rows if r['event']=='PANEL_WRITE'];csc=[r for r in rows if r['event'] in ('CSC_A','CSC_B')]
   phases.append({'phase':name,'event_counts':dict(c),'panel_writes':[{'command':r['arg0'],'length':r['arg1'],'payload_sha256':r['payload_sha256'],'return':r['raw_return']} for r in writes],'csc':[{'event':r['event'],'plane':r['plane'],'null':bool(r['flags']&1),'payload_sha256':r['payload_sha256'],'return':r['raw_return']} for r in csc]})
  out['captures'].append({'path':str(p),'authority':d['capture_quality'],'lost':d['status']['lost_records'],'bounds_rejected':d['bounds_rejected'],'phases':phases})
 out['conclusions'].append({'tag':'OBSERVED','statement':'report contains only recorded Gate-0 ordering, returns, command/length values and payload hashes; semantic names for changing Sony state are intentionally not inferred'})
 return out
def main():
 ap=argparse.ArgumentParser();ap.add_argument('trace',type=Path,nargs='+');ap.add_argument('--json',type=Path);a=ap.parse_args();r=audit(a.trace);s=json.dumps(r,indent=2);print(s);a.json and a.json.write_text(s+'\n')
if __name__=='__main__':main()
