#!/usr/bin/env python3
from pathlib import Path
import struct,tempfile
from gate0_trace_decode import decode,TraceError,HDR,STATUS,REC,MAGIC,TRACE_MAGIC,VERSION,COMMITTED,BOUNDS,READ_UNPROVEN,RET

def record(seq,event,flags=0,plane=-1,a0=0,a1=0,p=b'',raw=0,inv=0):
 return REC.pack(COMMITTED,seq,seq,1,inv,event,plane,flags,raw,a0,a1,len(p),0,p.ljust(256,b'\0'))
def dump(rows,lost=0,missing=0):
 h=[0]*27;h[0]=MAGIC;h[1]=VERSION;h[2]=HDR.size;h[3]=STATUS.size;h[4]=720;h[5]=720;h[6]=REC.size;h[7]=len(rows);h[8]=1
 s=[0]*17;s[0]=TRACE_MAGIC;s[1]=VERSION;s[2]=0x3650000;s[4]=512;s[6]=len(rows);s[7]=lost;s[10]=0x603;s[11]=0x603;s[12]=missing
 return HDR.pack(*h)+STATUS.pack(*s)+bytes(1440)+b''.join(rows)
def check(name,fn):
 fn();print('PASS',name)
def run_blob(blob):
 with tempfile.TemporaryDirectory() as d:
  p=Path(d)/'t.bin';p.write_bytes(blob);return decode(p)
def main():
 tests=[]
 def t1():
  d=run_blob(dump([record(1,20,a0=1)]));assert d['capture_quality']=='AUTHORITATIVE' and d['records'][0]['marker']=='BASELINE_IDLE'
 tests.append(('marker authoritative',t1))
 def t2():
  d=run_blob(dump([record(1,17,a0=0x51,a1=3,p=b'abc',flags=RET)]));assert d['records'][0]['payload_hex']=='616263'
 tests.append(('writer payload',t2))
 def t3():
  d=run_blob(dump([record(1,17,a1=300,flags=BOUNDS|RET)]));assert d['capture_quality']=='PARTIAL' and d['bounds_rejected']==1
 tests.append(('writer reject no truncation',t3))
 def t4():
  d=run_blob(dump([record(1,18,flags=READ_UNPROVEN,a0=10,a1=1,inv=4),record(2,19,flags=READ_UNPROVEN|RET,a0=10,a1=1,inv=4)]));assert d['reader_events']==2 and all(r['payload_length']==0 for r in d['records'])
 tests.append(('reader no-copy',t4))
 def t5():
  try:run_blob(dump([record(1,19,flags=READ_UNPROVEN|RET,p=b'x')]))
  except TraceError:return
  raise AssertionError('reader payload accepted')
 tests.append(('reader payload rejected',t5))
 def t6():
  d=run_blob(dump([record(1,1,plane=0,p=bytes(60),flags=RET)]));assert d['records'][0]['payload_length']==60
 tests.append(('CSC 0x3c',t6))
 def t7():
  d=run_blob(dump([record(1,1,plane=0,flags=1|RET)]));assert d['records'][0]['payload_length']==0
 tests.append(('CSC NULL',t7))
 def t8():assert run_blob(dump([],lost=2))['capture_quality']=='PARTIAL'
 tests.append(('lost partial',t8))
 def t9():assert run_blob(dump([],missing=2))['capture_quality']=='PARTIAL'
 tests.append(('missing hook partial',t9))
 def t10():
  b=bytearray(dump([]));struct.pack_into('<I',b,4,5)
  with tempfile.TemporaryDirectory() as d:
   p=Path(d)/'x';p.write_bytes(b)
   try:decode(p)
   except TraceError:return
  raise AssertionError('v5 accepted')
 tests.append(('protocol pin',t10))
 for n,f in tests:check(n,f)
 print(f'gate0 synthetic tests: {len(tests)} PASS')
if __name__=='__main__':main()
