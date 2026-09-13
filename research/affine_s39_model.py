#!/usr/bin/env python3
"""Pure host model for the proven S3.9 coefficient encoding.
No hardware access and no post-add semantics are assumed here.
"""
from __future__ import annotations
import argparse,json,math
from pathlib import Path

SCALE=512
MIN_RAW_SIGNED=-2048
MAX_RAW_SIGNED=2047
MIN_VALUE=MIN_RAW_SIGNED/SCALE
MAX_VALUE=MAX_RAW_SIGNED/SCALE

def decode12(raw):
 raw&=0xFFF
 s=raw-0x1000 if raw&0x800 else raw
 return s/SCALE

def encode12_exact(value):
 scaled=value*SCALE
 if not float(scaled).is_integer():raise ValueError('value is not exactly representable in S3.9')
 s=int(scaled)
 if not MIN_RAW_SIGNED<=s<=MAX_RAW_SIGNED:raise OverflowError('outside signed 12-bit S3.9 range')
 return s&0xFFF

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--json',type=Path);a=ap.parse_args()
 for raw in range(0x1000):
  v=decode12(raw);enc=encode12_exact(v)
  if enc!=raw:raise SystemExit(f'roundtrip failed raw=0x{raw:03X} enc=0x{enc:03X}')
 expected={1.0:0x200,1.25:0x280,1.5:0x300,2.0:0x400,3.0:0x600,-1.0:0xE00,-4.0:0x800,MAX_VALUE:0x7FF}
 for v,r in expected.items():
  got=encode12_exact(v)
  if got!=r:raise SystemExit(f'known vector {v}: 0x{got:03X} != 0x{r:03X}')
 if decode12(0x800)!=-4.0 or decode12(0x7FF)!=MAX_VALUE:raise SystemExit('endpoint decode failure')
 result={'format':'signed S3.9','fraction_bits':9,'raw_bits':12,'minimum':MIN_VALUE,'maximum':MAX_VALUE,'step':1/SCALE,'roundtrip_vectors_tested':4096,'known_vectors':{str(k):f'0x{v:03X}' for k,v in expected.items()},'scope':'coefficient encoding only; post-add/clamp encoding intentionally excluded until Sony semantics are closed'}
 if a.json:a.json.write_text(json.dumps(result,indent=2)+'\n')
 print(json.dumps(result,sort_keys=True))
if __name__=='__main__':main()
