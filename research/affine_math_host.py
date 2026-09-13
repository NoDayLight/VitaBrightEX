#!/usr/bin/env python3
"""Host-only fixed-point and Sony-table algebra checks.

No hardware mutation. Raw S3.9 representation and the eventual policy quantizer
are deliberately separate. The post-add model is tracked as a hypothesis until
all Sony tables/pipeline domains are explained.
"""
from __future__ import annotations
import argparse,json,math
from decimal import Decimal, ROUND_HALF_UP, localcontext
from pathlib import Path

S39_SCALE=512
S39_MIN=Decimal('-4')
S39_MAX=Decimal(2047)/Decimal(512)

def decode_s39(raw:int)->Decimal:
 raw&=0xFFF
 n=raw-0x1000 if raw&0x800 else raw
 return Decimal(n)/Decimal(S39_SCALE)

def encode12_exact(n:int)->int:
 if not -2048<=n<=2047:raise ValueError('raw S3.9 integer out of range')
 return n&0xFFF

def quantize_s39(value)->int:
 """Nearest S3.9, ties away from zero, reject overflow (never saturate)."""
 x=Decimal(str(value))
 if x<S39_MIN or x>S39_MAX:raise OverflowError(f'S3.9 unrepresentable: {x}')
 with localcontext() as ctx:
  ctx.prec=50
  mag=(abs(x)*S39_SCALE).quantize(Decimal('1'),rounding=ROUND_HALF_UP)
 n=int(mag) * (-1 if x<0 else 1)
 if not -2048<=n<=2047:raise OverflowError(f'S3.9 rounded out of range: {x}')
 return n&0xFFF

def apply_matrix_then_add_clamp(matrix,add,clamp,vec):
 out=[]
 for row,a,(lo,hi) in zip(matrix,add,clamp):
  y=sum(Decimal(str(c))*Decimal(str(x)) for c,x in zip(row,vec))+Decimal(str(a))
  out.append(min(Decimal(hi),max(Decimal(lo),y)))
 return out

def tests():
 # Exhaustive raw representation round-trip.
 for raw in range(4096):
  s=raw-4096 if raw&0x800 else raw
  assert encode12_exact(s)==raw
  assert decode_s39(raw)==Decimal(s)/512
 # Policy quantizer: identity and signs exact; no implicit saturation.
 assert quantize_s39(1)==0x200
 assert quantize_s39(-1)==0xE00
 assert quantize_s39(1.25)==0x280
 assert quantize_s39(Decimal(1)/1024)==0x001   # +half LSB -> away from zero
 assert quantize_s39(Decimal(-1)/1024)==0xFFF  # -half LSB -> away from zero
 try:quantize_s39(4)
 except OverflowError:pass
 else:raise AssertionError('positive overflow must reject')
 try:quantize_s39(-4.001)
 except OverflowError:pass
 else:raise AssertionError('negative overflow must reject')
 # Sony full->limited RGB evidence. 0.857421875 == 439/512.
 s=Decimal(439)/512
 M=[[s,0,0],[0,s,0],[0,0,s]]
 lohi=[(64,940)]*3
 assert apply_matrix_then_add_clamp(M,[64,64,64],lohi,[0,0,0])==[Decimal(64)]*3
 white=apply_matrix_then_add_clamp(M,[64,64,64],lohi,[1023,1023,1023])
 assert white==[Decimal(940)]*3
 # This closes one algebraic fact only: this table behaves exactly as
 # matrix-then-add-then-clamp for full-range RGB endpoints. It does NOT prove
 # that every IFTU pixel domain reaches CSC with the same unpack/offset rules.
 return {'raw_roundtrip':4096,'quantizer':'nearest, ties away from zero; overflow rejects','limited_rgb_endpoint_model':'PASS: M*x + 64 then clamp[64,940] predicts 0->64 and 1023->940','global_post_add_semantics':'UNRESOLVED: YUV-like Sony tables require pixel-domain/csc_control semantics before generalization'}

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--json',type=Path,required=True);a=ap.parse_args();r=tests();a.json.write_text(json.dumps(r,indent=2)+'\n')
 print('AFFINE_HOST_MATH')
 for k,v in r.items():print(f'  {k}: {v}')
if __name__=='__main__':main()
