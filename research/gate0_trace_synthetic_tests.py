#!/usr/bin/env python3
from __future__ import annotations
import struct
import gate0_trace_decode as d
from gate0_decision_audit import analyze

PASS=0

def snap(avail=d.PANEL_SNAPS):
    b=bytearray(d.SNAP_SIZE)
    struct.pack_into('<5I',b,0,d.MAGIC,d.VERSION,d.FW,avail,d.SNAP_STABLE)
    struct.pack_into('<I',b,20,1)
    return bytes(b)

def rec(seq,t,*,comp=None,tid=7,inv=0,raw=0,flags=0,arg0=0,arg1=0,payload=b'',lost=0):
    if comp is None: comp=seq
    if t in d.RAW_EVENTS: flags |= d.FLAG_RETURN_VALID
    p=payload.ljust(d.PAYLOAD_MAX,b'\0')[:d.PAYLOAD_MAX]
    return d.REC.pack(d.COMMITTED,seq,comp,tid,inv,t,-1,flags,raw,arg0,arg1,len(payload),lost,p)

def neutral_csc():
    w=[0,0,1023,0,1023,0,0x200,0,0,0x200,0,0,0,0x200]
    return struct.pack('<15I',*w)

def build(records,*,pre=True,lost=0,installed=d.PANEL_REQUIRED,snaps=d.PANEL_SNAPS,
          affine_quality=d.CAP_AUTHORITATIVE,panel_quality=d.CAP_AUTHORITATIVE,panel_read=0):
    n=len(records); prebytes=snap(snaps) if pre else b''; post=snap(snaps)
    common=snaps if pre else 0
    aff_m=d.AFFINE_REQUIRED & ~installed; pan_m=d.PANEL_REQUIRED & ~installed
    aff_sm=d.AFFINE_SNAPS & ~common; pan_sm=d.PANEL_SNAPS & ~common
    status=(d.MAGIC,d.VERSION,d.FW,0,512,n,n,lost,n,0,installed,d.PANEL_REQUIRED,d.PANEL_REQUIRED&~installed,snaps,d.PANEL_SNAPS,d.PANEL_SNAPS&~snaps,0)
    header=[d.DUMP_MAGIC,d.VERSION,d.HEADER_SIZE,d.STATUS_SIZE,d.SNAP_SIZE if pre else 0,d.SNAP_SIZE,d.RECORD_SIZE,n,panel_quality,1 if pre else 0,0,0,0,
            d.AFFINE_REQUIRED,aff_m,d.PANEL_REQUIRED,pan_m,d.AFFINE_SNAPS,aff_sm,d.PANEL_SNAPS,pan_sm,affine_quality,panel_quality,panel_read,0,0,0]
    return d.U32_27.pack(*header)+d.U32_17.pack(*status)+prebytes+post+b''.join(records)

def expect_ok(name,data,check=None):
    global PASS
    x=d.decode_bytes(data)
    if check: check(x)
    PASS+=1; print('PASS',name)

def assert_affine_only(x):
    assert x['affine_authoritative'] and not x['panel_authoritative']

def expect_bad(name,data):
    global PASS
    try:d.decode_bytes(data)
    except d.TraceError:
        PASS+=1; print('PASS',name); return
    raise AssertionError(name+' unexpectedly accepted')

r1=[rec(1,1,payload=neutral_csc()),rec(2,2,payload=neutral_csc())]
expect_ok('valid authoritative affine',build(r1,installed=d.AFFINE_REQUIRED,affine_quality=1,panel_quality=2),lambda x: (_ for _ in ()).throw(AssertionError()) if not x['affine_authoritative'] or x['panel_authoritative'] else None)
r2=[rec(1,18,inv=1,arg0=0x0A,arg1=1),rec(2,19,inv=1,arg0=0x0A,arg1=1,payload=b'\x9c')]
expect_ok('valid authoritative panel',build(r2,panel_read=1))
x=d.decode_bytes(build([rec(1,1,raw=-1,payload=neutral_csc())])); assert x['records'][0]['return_semantics']=='FAILURE'; PASS+=1; print('PASS failed CSC A retained')
x=d.decode_bytes(build([rec(1,2,raw=-2,payload=neutral_csc())])); assert x['records'][0]['return_semantics']=='FAILURE'; PASS+=1; print('PASS failed CSC B retained')
x=d.decode_bytes(build([rec(1,1,flags=d.FLAG_NULL)])); assert x['records'][0]['payload_length']==0; PASS+=1; print('PASS NULL CSC')
x=d.decode_bytes(build([rec(1,17,raw=-3,arg0=0x29,arg1=0)])); assert x['records'][0]['return_semantics']=='FAILURE'; PASS+=1; print('PASS failed panel write')
r=[rec(1,18,inv=1,arg0=0xDA,arg1=2),rec(2,19,inv=1,arg0=0xDA,arg1=2,payload=b'AB',flags=d.FLAG_READ_UNCERTAIN)]
expect_ok('uncertain panel read',build(r,panel_quality=2,panel_read=2))
expect_bad('lost records with false authority',build(r1,lost=1,affine_quality=1,panel_quality=2))
p=b'X'*d.PAYLOAD_MAX
expect_ok('truncated payload partial',build([rec(1,17,arg1=300,payload=p,flags=d.FLAG_TRUNC)],panel_quality=2))
expect_ok('missing pre boundary partial',build(r1,pre=False,affine_quality=2,panel_quality=2))
b=bytearray(build(r1)); struct.pack_into('<I',b,4,4); expect_bad('v4 rejection',bytes(b))
b=bytearray(build(r1)); struct.pack_into('<I',b,8,104); expect_bad('bad sizes',bytes(b))
expect_bad('bad EOF',build(r1)+b'X')
b=bytearray(build(r1)); struct.pack_into('<I',b,28,3); expect_bad('bad count',bytes(b))
r=[rec(2,1,payload=neutral_csc()),rec(1,2,payload=neutral_csc())]; expect_bad('bad entry sequence',build(r))
r=[rec(1,1,comp=1,payload=neutral_csc()),rec(2,2,comp=1,payload=neutral_csc())]; expect_bad('bad completion sequence',build(r))
expect_ok('missing affine authority',build([],installed=d.AFFINE_REQUIRED & ~d.HOOKS['CSC_A'],affine_quality=2,panel_quality=2))
expect_ok('affine without panel authority',build([],installed=d.AFFINE_REQUIRED,affine_quality=1,panel_quality=2),assert_affine_only)
r=[rec(1,3,comp=4,tid=10,inv=1),rec(2,5,comp=3,tid=10,inv=2),rec(3,6,comp=2,tid=10,inv=2),rec(4,4,comp=1,tid=10,inv=1)]
expect_ok('nested causal IDs + distinct completion order',build(r))
r=[rec(1,3,tid=10,inv=1),rec(2,4,tid=11,inv=1)]; expect_bad('cross-thread nesting',build(r))
cap=d.decode_bytes(build([],installed=d.AFFINE_REQUIRED,affine_quality=1,panel_quality=2))
rep=analyze({'CAPTURE_B_BRIGHTNESS':cap,'CAPTURE_C_COLORSPACE':cap,'CAPTURE_D_DISPLAY':cap,'CAPTURE_F_SUSPEND_RESUME':cap})
assert rep['AFFINE']['brightness_reacquisition']['status']=='NOT OBSERVED'
assert rep['AFFINE']['A+']!='PROVEN'
PASS+=1; print('PASS NOT OBSERVED != UNSUPPORTED; malformed/incomplete evidence cannot select A+')
assert PASS==21, PASS
print('gate0 synthetic tests: 21 PASS')
