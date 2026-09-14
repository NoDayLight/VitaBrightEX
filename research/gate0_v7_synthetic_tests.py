#!/usr/bin/env python3
import tempfile
from pathlib import Path
from gate0_trace_decode import decode,TraceError,HDR,STATUS,REC,MAGIC,TRACE_MAGIC,COMMITTED
REQ=0x1f
def blob(version=7,panel_bytes=0,event_pair=True):
 def rec(seq,event,inv=0,n=0,pstate=0):return REC.pack(COMMITTED,seq,seq,1,inv,event,-1,0,0,0,0,0,0,pstate,n,0,1,b'X'*n+b'\0'*(60-n),0)
 rows=[rec(1,3,7),rec(2,4,7),rec(3,5,8,panel_bytes,2),rec(4,8,9)]if event_pair else[rec(1,3,7)]
 h=HDR.pack(MAGIC,version,HDR.size,STATUS.size,REC.size,len(rows),REQ,0,0,1,2,0,0,0,0,0);st=STATUS.pack(TRACE_MAGIC,version,0x03650000,2,512,len(rows),len(rows),0,len(rows),0,REQ,REQ,0,0,1,1,1,0);return h+st+b''.join(rows)
def expect_fail(data):
 p=Path(tempfile.mkstemp()[1]);p.write_bytes(data)
 try:decode(p);raise AssertionError('expected failure')
 except TraceError:pass
 finally:p.unlink()
def main():
 p=Path(tempfile.mkstemp()[1]);p.write_bytes(blob());d=decode(p,True);assert d['version']==7 and len(d['records'])==4;p.unlink();expect_fail(blob(version=6));expect_fail(blob(panel_bytes=1));expect_fail(blob(event_pair=False));print('gate0 v7 synthetic decoder tests: 4 PASS')
if __name__=='__main__':main()
