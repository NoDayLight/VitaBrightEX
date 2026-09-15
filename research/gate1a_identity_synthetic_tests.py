#!/usr/bin/env python3
from __future__ import annotations
import importlib.util,struct
from pathlib import Path

P=Path(__file__).with_name('gate1a_identity_decode.py')
spec=importlib.util.spec_from_file_location('g1dec',P);d=importlib.util.module_from_spec(spec);spec.loader.exec_module(d)

def status(slots=8,last=8,committed=8,mismatch=0,lost=0,lifecycle=d.LIFECYCLE_PAUSED):
    return d.STATUS.pack(d.MAGIC,d.VERSION,d.FW365,lifecycle,d.CAPACITY,slots,committed,lost,last,0,d.REQUIRED,d.REQUIRED,0,0,1,slots,0,mismatch)

def rec(seq,event,plane,inv,source=None,ptr=0x12345678,generation=1,flags_extra=0,rawret=0):
    flags=flags_extra;src=d.ZERO60;cpy=d.ZERO60;sh=ch=0
    if event in (d.CSC_A,d.CSC_B):
        valid=0<=plane<5;flags|=d.F_VALID if valid else d.F_INVALID
        if source is None:
            ptr=0;flags|=d.F_NULL;generation=0
        elif valid:
            src=source;cpy=source;sh=d.fnv32(src);ch=d.fnv32(cpy);flags|=d.F_SUB|d.F_EQUAL|d.F_RETURN
        else:generation=0
        flags|=d.F_RETURN
    elif event==d.ENABLE_EXIT:flags|=d.F_RETURN;ptr=0;generation=0
    else:ptr=0;generation=0
    prefix=d.PREFIX.pack(d.COMMITTED,seq,seq,7,inv,event,plane,flags,rawret,ptr,generation,sh,ch)
    return prefix+src+cpy+struct.pack('<I5I',1,0,0,0,0,0)

def canonical(stage):
    words={'A':[0,0x202,0x3ff]+[0]*12,'B':[0,0,0x3ff,0,0x3ff,0,0x200,0,0,0,0x200,0,0,0,0x200]}[stage]
    return struct.pack('<15I',*words)

def noncanonical(stage,plane):
    b=bytearray(canonical(stage));struct.pack_into('<I',b,0,0x1000+(plane<<4)+(1 if stage=='A' else 2));return bytes(b)

def pack_dump(rs):
    n=len(rs);s=status(n,n,n);h=d.HDR.pack(d.DUMP_MAGIC,d.VERSION,d.HEADER_SIZE,d.STATUS_SIZE,d.RECORD_SIZE,n,d.REQUIRED,0,0,1,d.LIFECYCLE_PAUSED,0,0,0,0,0)
    return h+s+b''.join(rs)

def resume_dump():
    rs=[];seq=1;inv=1
    for ev,pl,st in [(d.CSC_B,0,'B'),(d.CSC_A,0,'A'),(d.CSC_B,1,'B'),(d.CSC_A,1,'A')]:rs.append(rec(seq,ev,pl,inv,canonical(st)));seq+=1;inv+=1
    for pl in (0,1):rs.append(rec(seq,d.ENABLE_ENTER,pl,inv));seq+=1;rs.append(rec(seq,d.ENABLE_EXIT,pl,inv));seq+=1;inv+=1
    return pack_dump(rs)

def boot_dump():
    rs=[];seq=1;inv=1
    for pl in range(4):
        for ev,st in ((d.CSC_B,'B'),(d.CSC_A,'A')):
            payload=canonical(st) if pl in (0,1) else noncanonical(st,pl)
            rs.append(rec(seq,ev,pl,inv,payload));seq+=1;inv+=1
    return pack_dump(rs)

def reject(name,raw,needle=None,phase='resume'):
    try:d.decode_dump(raw,phase)
    except d.DecodeError as e:
        if needle and needle not in str(e):raise AssertionError(f'{name}: wrong error {e}')
        return
    raise AssertionError(name+': accepted')

def mutate(raw,off,b):x=bytearray(raw);x[off:off+len(b)]=b;return bytes(x)

def main():
    good=resume_dump();d.decode_dump(good,'resume')
    boot=boot_dump();d.decode_dump(boot,'boot')
    reject('resume as boot',good,'boot focused order',phase='boot')
    reject('boot as resume',boot,'resume focused order',phase='resume')
    reject('wrong magic',mutate(good,0,struct.pack('<I',0)),'magic')
    reject('wrong version',mutate(good,4,struct.pack('<I',99)),'version')
    reject('record count',mutate(good,20,struct.pack('<I',999)),'record count')
    reject('truncated',good[:-1],'byte count')
    rec0=d.HEADER_SIZE+d.STATUS_SIZE
    reject('sequence',mutate(good,rec0+4,struct.pack('<I',9)),'sequence')
    reject('incomplete',mutate(good,rec0,struct.pack('<I',0)),'incomplete')
    reject('hook ownership',mutate(good,28,struct.pack('<I',1)),'hook ownership')
    reject('lost',mutate(good,32,struct.pack('<I',1)),'lost records')
    reject('mismatch count',mutate(good,44,struct.pack('<I',1)),'mismatch_count')
    reject('invalid event',mutate(good,rec0+20,struct.pack('<H',99)),'invalid event')
    reject('payload inequality',mutate(good,rec0+108,b'X'),'copy FNV mismatch')
    bad=bytearray(good);struct.pack_into('<h',bad,rec0+22,7);struct.pack_into('<I',bad,rec0+24,d.F_INVALID|d.F_SUB|d.F_EQUAL|d.F_RETURN);reject('invalid substitution',bytes(bad),'invalid-plane substitution')
    bad=bytearray(good);struct.pack_into('<I',bad,rec0+24,d.F_VALID|d.F_NULL|d.F_SUB|d.F_RETURN);struct.pack_into('<I',bad,rec0+32,0);reject('null substitution',bytes(bad),'NULL substituted')
    exit_off=rec0+5*d.RECORD_SIZE;bad=mutate(good,exit_off+16,struct.pack('<I',99));reject('enable pairing',bad,'enable invocation')
    print('GATE1A_DECODER_SYNTHETIC=PASS')
if __name__=='__main__':main()
