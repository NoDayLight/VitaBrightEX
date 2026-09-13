#!/usr/bin/env python3
"""PS Vita SCE-ELF evidence extractor with proof-grade reachable call analysis.

The parser follows the pinned VitaLoaderRedux SCE-ELF/module-info rules. Two
xref classes are deliberately kept separate:

* PROVEN_CALLSITE: direct call decoded while recursively traversing a reachable
  instruction stream rooted at an exported function or module start/stop and
  bounded by ARM.exidx function ranges where available.
* CANDIDATE_XREF: brute-force executable-byte decoding retained only as a
  discovery aid; it is never ABI authority.

Only compact derived metadata and targeted CFG evidence are emitted.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
from collections import deque
from dataclasses import dataclass
from pathlib import Path

PT_LOAD = 1
ET_SCE_EXEC = 0xFE00
ET_SCE_ARMRELEXEC = 0xFFA5
IFTU_NIDS = {0x0FCBF457, 0x357EAE24, 0xD64F4C6B}
DISPLAY_NIDS = {0x19140ACD}
DEFAULT_TARGET_NIDS = IFTU_NIDS | DISPLAY_NIDS
STATE_OFFSETS = {0x10C, 0x148, 0x1E8, 0x1F8, 0x214}

def u8(b,o): return b[o]
def u16(b,o): return struct.unpack_from('<H',b,o)[0]
def u32(b,o): return struct.unpack_from('<I',b,o)[0]

def prel31(word):
    value=word & 0x7FFFFFFF
    if value & 0x40000000: value-=0x80000000
    return value

@dataclass
class Phdr:
    index:int; p_type:int; p_offset:int; p_vaddr:int; p_paddr:int
    p_filesz:int; p_memsz:int; p_flags:int; p_align:int

class VitaElf:
    def __init__(self,path:Path):
        self.path=path; self.data=path.read_bytes()
        if self.data[:4]!=b'\x7fELF' or self.data[4]!=1 or self.data[5]!=1:
            raise ValueError(f'{path}: not ELF32 little-endian')
        self.e_type=u16(self.data,16); self.e_machine=u16(self.data,18)
        self.e_entry=u32(self.data,24); self.e_phoff=u32(self.data,28)
        entsz=u16(self.data,42); count=u16(self.data,44)
        if entsz!=0x20: raise ValueError('unexpected phdr size')
        self.phdrs=[]
        for i in range(count):
            o=self.e_phoff+i*entsz
            self.phdrs.append(Phdr(i,*struct.unpack_from('<IIIIIIII',self.data,o)))
        self.sha256=hashlib.sha256(self.data).hexdigest()
        self.modinfo_segment,self.modinfo_seg_off,self.modinfo_file_off=self._locate_modinfo()
        self.segment_base=self.phdrs[self.modinfo_segment].p_vaddr
        self.modinfo=self._parse_modinfo(); self.exidx=self._parse_exidx()

    def _locate_modinfo(self):
        if self.e_machine!=40: raise ValueError('not ARM')
        if self.e_type==ET_SCE_ARMRELEXEC:
            p=self.phdrs[0]; off=p.p_paddr-p.p_offset
            if off<0 or off>=p.p_filesz: raise ValueError('bad PRX1 module info')
            return 0,off,p.p_offset+off
        if self.e_type==ET_SCE_EXEC:
            for p in self.phdrs:
                if p.p_paddr and p.p_paddr<p.p_filesz:
                    return p.index,p.p_paddr,p.p_offset+p.p_paddr
        seg=(self.e_entry>>30)&3; off=self.e_entry&0x3FFFFFFF
        if seg>=len(self.phdrs): raise ValueError('module info segment outside phdrs')
        p=self.phdrs[seg]
        if p.p_type!=PT_LOAD or off>=p.p_filesz: raise ValueError('bad e_entry module info locator')
        return seg,off,p.p_offset+off

    def _parse_modinfo(self):
        o=self.modinfo_file_off; attrs=u16(self.data,o); ver=list(self.data[o+2:o+4])
        rawname=self.data[o+4:o+31]
        if b'\0' not in rawname: raise ValueError('unterminated module name')
        name=rawname.split(b'\0',1)[0].decode('ascii'); infover=u8(self.data,o+31)
        if infover not in (0,2,3,6): raise ValueError(f'unsupported infover {infover}')
        reserve,ent_top,ent_end,stub_top,stub_end=struct.unpack_from('<IIIII',self.data,o+32)
        if ent_end<ent_top or stub_end<stub_top: raise ValueError('reversed tables')
        out=dict(attributes=attrs,version=ver,name=name,infover=infover,reserve=reserve,
                 ent_top=ent_top,ent_end=ent_end,stub_top=stub_top,stub_end=stub_end,
                 dbg_fingerprint=0,tls_top=0,tls_filesz=0,tls_memsz=0,
                 start_entry=0xFFFFFFFF,stop_entry=0xFFFFFFFF,
                 arm_exidx_top=0,arm_exidx_end=0,arm_extab_top=0,arm_extab_end=0)
        if infover==0: return out
        out['dbg_fingerprint']=u32(self.data,o+52)
        if infover in (2,3):
            out['start_entry']=u32(self.data,o+56); out['stop_entry']=u32(self.data,o+60)
            out['arm_exidx_top']=u32(self.data,o+64); out['arm_exidx_end']=u32(self.data,o+68)
            if infover==3:
                out['tls_top']=u32(self.data,o+72); out['tls_filesz']=u32(self.data,o+76); out['tls_memsz']=u32(self.data,o+80)
        else:
            out['tls_top']=u32(self.data,o+56); out['tls_filesz']=u32(self.data,o+60); out['tls_memsz']=u32(self.data,o+64)
            out['start_entry']=u32(self.data,o+68); out['stop_entry']=u32(self.data,o+72)
            out['arm_exidx_top']=u32(self.data,o+76); out['arm_exidx_end']=u32(self.data,o+80)
            out['arm_extab_top']=u32(self.data,o+84); out['arm_extab_end']=u32(self.data,o+88)
        return out

    def file_from_va(self,va):
        va &= ~1
        for p in self.phdrs:
            if p.p_type==PT_LOAD and p.p_vaddr<=va<p.p_vaddr+p.p_filesz:
                return p.index,p.p_offset+(va-p.p_vaddr)
        raise ValueError(f'VA 0x{va:08X} not file-backed')

    def is_exec_va(self,va):
        va &= ~1
        return any(p.p_type==PT_LOAD and (p.p_flags&1) and p.p_vaddr<=va<p.p_vaddr+p.p_filesz for p in self.phdrs)

    def cstr_va(self,va):
        _,o=self.file_from_va(va); e=self.data.find(b'\0',o,o+256)
        if e<0: raise ValueError('unterminated string')
        return self.data[o:e].decode('ascii',errors='replace')

    def resolve_ibo32(self,raw):
        if raw in (0,0xFFFFFFFF): return None
        try:
            self.file_from_va(raw & ~1); return raw
        except ValueError:
            candidate=(self.segment_base+raw)&0xFFFFFFFF
            try:
                self.file_from_va(candidate & ~1); return candidate
            except ValueError: return None

    def exports(self):
        p=self.phdrs[self.modinfo_segment]; cur=p.p_offset+self.modinfo['ent_top']; end=p.p_offset+self.modinfo['ent_end']
        if not (p.p_offset<=cur<=end<=p.p_offset+p.p_filesz): raise ValueError('export table outside segment')
        out=[]
        while cur<end:
            size=u8(self.data,cur)
            if size not in (0x1C,0x20): raise ValueError(f'unknown libent size 0x{size:X}')
            nfunc=u16(self.data,cur+6); nvar=u16(self.data,cur+8); ntls=u16(self.data,cur+10)
            if size==0x20:
                libnid=u32(self.data,cur+16); libname_va=u32(self.data,cur+20); nid_va=u32(self.data,cur+24); add_va=u32(self.data,cur+28)
            else:
                libnid=None; libname_va=u32(self.data,cur+16); nid_va=u32(self.data,cur+20); add_va=u32(self.data,cur+24)
            name=self.cstr_va(libname_va) if libname_va else ''
            _,nid_o=self.file_from_va(nid_va); _,add_o=self.file_from_va(add_va); funcs=[]
            for i in range(nfunc):
                nid=u32(self.data,nid_o+4*i); raw=u32(self.data,add_o+4*i); va=raw&~1
                seg,fo=self.file_from_va(va)
                funcs.append(dict(nid=nid,raw_entry=raw,thumb=bool(raw&1),va=va,segment=seg,file_offset=fo))
            out.append(dict(file_offset=cur,size=size,library_name=name,library_nid=libnid,nfunc=nfunc,nvar=nvar,ntlsvar=ntls,functions=funcs)); cur+=size
        return out

    def imports(self):
        p=self.phdrs[self.modinfo_segment]; cur=p.p_offset+self.modinfo['stub_top']; end=p.p_offset+self.modinfo['stub_end']; out=[]
        if not (p.p_offset<=cur<=end<=p.p_offset+p.p_filesz): raise ValueError('import table outside segment')
        while cur<end:
            first=u8(self.data,cur); size=first if first==0x24 else u16(self.data,cur)
            if size==0x24:
                nfunc=u16(self.data,cur+6); nvar=u16(self.data,cur+8); ntls=u16(self.data,cur+10)
                libnid=u32(self.data,cur+12); libname_va=u32(self.data,cur+16); fnid_va=u32(self.data,cur+20); ftab_va=u32(self.data,cur+24)
            elif size==0x34:
                nfunc=u16(self.data,cur+6); nvar=u16(self.data,cur+8); ntls=u16(self.data,cur+10)
                libnid=u32(self.data,cur+16); libname_va=u32(self.data,cur+20); fnid_va=u32(self.data,cur+28); ftab_va=u32(self.data,cur+32)
            else: raise ValueError(f'unknown libstub size 0x{size:X}')
            name=self.cstr_va(libname_va) if libname_va else ''; _,nid_o=self.file_from_va(fnid_va); _,tab_o=self.file_from_va(ftab_va); funcs=[]
            for i in range(nfunc):
                nid=u32(self.data,nid_o+4*i); raw=u32(self.data,tab_o+4*i); va=raw&~1
                try: seg,fo=self.file_from_va(va)
                except ValueError: seg,fo=None,None
                funcs.append(dict(nid=nid,raw_entry=raw,thumb=bool(raw&1),va=va,segment=seg,file_offset=fo))
            out.append(dict(file_offset=cur,size=size,library_name=name,library_nid=libnid,nfunc=nfunc,nvar=nvar,ntlsvar=ntls,functions=funcs)); cur+=size
        return out

    def _parse_exidx(self):
        top=self.resolve_ibo32(self.modinfo.get('arm_exidx_top',0)); end=self.resolve_ibo32(self.modinfo.get('arm_exidx_end',0))
        if top is None or end is None or end<=top: return []
        try:
            _,top_o=self.file_from_va(top); _,end_o=self.file_from_va(end-1); end_o+=1
        except ValueError: return []
        if (end_o-top_o)%8: return []
        out=[]; entry_va=top&~1
        for o in range(top_o,end_o,8):
            raw=(entry_va+prel31(u32(self.data,o)))&0xFFFFFFFF; entry_va+=8
            va=raw&~1
            if self.is_exec_va(va): out.append(dict(raw=raw,va=va,thumb_hint=bool(raw&1)))
        uniq={x['va']:x for x in out}; return [uniq[k] for k in sorted(uniq)]

    def function_range(self,va):
        va &= ~1; starts=[x['va'] for x in self.exidx]
        prior=None; nxt=None
        for s in starts:
            if s<=va: prior=s
            else: nxt=s; break
        if prior is not None:
            if nxt is None:
                seg,_=self.file_from_va(prior); p=self.phdrs[seg]; nxt=p.p_vaddr+p.p_filesz
            return prior,nxt,prior==va
        seg,_=self.file_from_va(va); p=self.phdrs[seg]; return va,p.p_vaddr+p.p_filesz,False

class Decoder:
    def __init__(self):
        from capstone import Cs,CS_ARCH_ARM,CS_MODE_ARM,CS_MODE_THUMB
        self.arm=Cs(CS_ARCH_ARM,CS_MODE_ARM); self.thumb=Cs(CS_ARCH_ARM,CS_MODE_THUMB)
        self.arm.detail=True; self.thumb.detail=True
    def one(self,elf,va,thumb):
        try: _,fo=elf.file_from_va(va)
        except ValueError: return None
        md=self.thumb if thumb else self.arm; ins=list(md.disasm(elf.data[fo:fo+4],va,count=1))
        return ins[0] if ins and ins[0].address==va else None
DECODER=Decoder()

def immediate_target(ins):
    from capstone.arm import ARM_OP_IMM
    return (ins.operands[0].imm&0xFFFFFFFF) if ins.operands and ins.operands[0].type==ARM_OP_IMM else None

def target_mode(ins,current_thumb,target):
    if target&1: return True
    return (not current_thumb) if ins.mnemonic.lower()=='blx' else current_thumb

def is_call(ins): return ins.mnemonic.lower() in ('bl','blx')
def is_branch(ins):
    m=ins.mnemonic.lower(); return (m.startswith('b') and m not in ('bl','blx','bic','bfi','bfc')) or m in ('cbz','cbnz')
def is_uncond(ins): return ins.mnemonic.lower() in ('b','b.w')
def terminal(ins):
    m=ins.mnemonic.lower(); ops=ins.op_str.replace(' ','').lower()
    return m in ('bx','bxj','tbb','tbh','eret','rfe','rfeia','rfedb') or (m=='pop' and 'pc' in ops) or (m.startswith('ldm') and 'pc' in ops) or (m in ('ldr','mov') and ops.startswith('pc,'))

@dataclass
class Block:
    start:int; instructions:list; successors:list

class FunctionCFG:
    def __init__(self,elf,start,thumb,import_stubs):
        self.elf=elf; self.start=start&~1; self.thumb=bool(thumb); self.import_stubs=import_stubs
        self.range_start,self.range_end,self.exidx_exact=elf.function_range(self.start)
        self.blocks={}; self.calls=[]; self.direct_callees=[]; self.offset_refs=[]; self.decode_failures=[]; self._build()
    def _within(self,va): return self.range_start<=(va&~1)<self.range_end and self.elf.is_exec_va(va)
    def _build(self):
        pending=deque([self.start]); seen=set(); owners={}; total=0
        while pending:
            bs=pending.popleft()&~1
            if bs in seen or not self._within(bs): continue
            seen.add(bs); insns=[]; succ=[]; va=bs
            while total<4096 and self._within(va):
                if va in owners and owners[va]!=bs: succ.append(va); break
                ins=DECODER.one(self.elf,va,self.thumb)
                if ins is None: self.decode_failures.append(va); break
                owners[va]=bs; insns.append(ins); total+=1
                from capstone.arm import ARM_OP_IMM
                for op in getattr(ins,'operands',[]):
                    if op.type==ARM_OP_IMM and (op.imm&0xFFFFFFFF) in STATE_OFFSETS:
                        self.offset_refs.append(dict(va=ins.address,immediate=op.imm&0xFFFFFFFF,instruction=f'{ins.mnemonic} {ins.op_str}'.strip()))
                nv=va+ins.size
                if is_call(ins):
                    t=immediate_target(ins)
                    if t is not None:
                        tv=t&~1; tm=target_mode(ins,self.thumb,t)
                        self.calls.append(dict(va=ins.address,instruction=f'{ins.mnemonic} {ins.op_str}'.strip(),target=tv,target_mode='thumb' if tm else 'arm'))
                        if self.elf.is_exec_va(tv) and tv not in self.import_stubs: self.direct_callees.append((tv,tm))
                    va=nv; continue
                if is_branch(ins):
                    t=immediate_target(ins)
                    if t is not None and self._within(t&~1): succ.append(t&~1); pending.append(t&~1)
                    if is_uncond(ins): break
                    va=nv; continue
                if terminal(ins): break
                va=nv
            self.blocks[bs]=Block(bs,insns,sorted(set(succ)))
    def instruction_count(self): return sum(len(b.instructions) for b in self.blocks.values())
    def window_for_call(self,call_va,before=10,after=5):
        for b in self.blocks.values():
            for i,x in enumerate(b.instructions):
                if x.address==call_va:
                    return [f'0x{y.address:08X}: {y.mnemonic} {y.op_str}'.rstrip() for y in b.instructions[max(0,i-before):min(len(b.instructions),i+after+1)]]
        return []
    def compact(self,blocks=False):
        out=dict(start=self.start,mode='thumb' if self.thumb else 'arm',range_start=self.range_start,range_end=self.range_end,exidx_exact=self.exidx_exact,block_count=len(self.blocks),instruction_count=self.instruction_count(),calls=self.calls,state_offset_refs=self.offset_refs,decode_failures=self.decode_failures)
        if blocks:
            out['blocks']=[dict(start=b.start,successors=b.successors,instructions=[f'0x{x.address:08X}: {x.mnemonic} {x.op_str}'.rstrip() for x in b.instructions]) for b in sorted(self.blocks.values(),key=lambda z:z.start)]
        return out

class Reachability:
    def __init__(self,elf,exports,imports):
        self.elf=elf; self.import_stubs={f['va'] for l in imports for f in l['functions'] if f['va']}; self.functions={}; self.roots=[]
        for l in exports:
            for f in l['functions']:
                if elf.is_exec_va(f['va']): self.roots.append((f['va'],f['thumb'],'export'))
        for name in ('start_entry','stop_entry'):
            r=elf.resolve_ibo32(elf.modinfo.get(name,0xFFFFFFFF))
            if r is not None and elf.is_exec_va(r): self.roots.append((r&~1,bool(r&1),name))
        self._build()
    def _build(self):
        q=deque(self.roots)
        while q:
            va,thumb,reason=q.popleft(); key=(va&~1,bool(thumb))
            if key in self.functions or not self.elf.is_exec_va(va): continue
            cfg=FunctionCFG(self.elf,va,thumb,self.import_stubs); self.functions[key]=cfg
            for cv,ct in cfg.direct_callees:
                if (cv,ct) not in self.functions: q.append((cv,ct,f'call-from-0x{cfg.start:08X}'))
    def proven_calls_to(self,target):
        target &= ~1; out=[]
        for (start,thumb),cfg in self.functions.items():
            for c in cfg.calls:
                if c['target']==target:
                    out.append(dict(classification='PROVEN_CALLSITE',function_start=start,function_mode='thumb' if thumb else 'arm',exidx_exact=cfg.exidx_exact,call_va=c['va'],instruction=c['instruction'],pre_post_window=cfg.window_for_call(c['va'])))
        return sorted(out,key=lambda x:x['call_va'])
    def state_refs(self):
        out=[]
        for (start,thumb),cfg in self.functions.items():
            for r in cfg.offset_refs: out.append(dict(function_start=start,function_mode='thumb' if thumb else 'arm',exidx_exact=cfg.exidx_exact,**r))
        return sorted(out,key=lambda x:(x['immediate'],x['va']))

def candidate_branch_refs(elf,targets,limit=32):
    from capstone import Cs,CS_ARCH_ARM,CS_MODE_ARM,CS_MODE_THUMB
    from capstone.arm import ARM_OP_IMM
    refs={t&~1:[] for t in targets}
    for p in elf.phdrs:
        if p.p_type!=PT_LOAD or not (p.p_flags&1): continue
        blob=elf.data[p.p_offset:p.p_offset+p.p_filesz]
        for mode,step in ((CS_MODE_THUMB,2),(CS_MODE_ARM,4)):
            md=Cs(CS_ARCH_ARM,mode); md.detail=True
            for off in range(0,max(0,len(blob)-4),step):
                ins=list(md.disasm(blob[off:off+4],p.p_vaddr+off,count=1))
                if not ins: continue
                x=ins[0]
                if x.mnemonic not in ('bl','blx','b','b.w') or not x.operands or x.operands[0].type!=ARM_OP_IMM: continue
                t=x.operands[0].imm&0xFFFFFFFE
                if t in refs and len(refs[t])<limit: refs[t].append(dict(classification='CANDIDATE_XREF',call_va=x.address,mode='thumb' if mode==CS_MODE_THUMB else 'arm',instruction=f'{x.mnemonic} {x.op_str}'))
    return refs

def compact_module(elf,target_nids,emit_cfg_nids):
    ex=elf.exports(); im=elf.imports(); reach=Reachability(elf,ex,im); tex=[]; tim=[]
    for l in ex:
        for f in l['functions']:
            if f['nid'] in target_nids:
                item=dict(library_name=l['library_name'],library_nid=l['library_nid'],**f); cfg=reach.functions.get((f['va'],f['thumb'])) or FunctionCFG(elf,f['va'],f['thumb'],reach.import_stubs); item['cfg']=cfg.compact(f['nid'] in emit_cfg_nids); tex.append(item)
    for l in im:
        for f in l['functions']:
            if f['nid'] in target_nids:
                item=dict(library_name=l['library_name'],library_nid=l['library_nid'],**f); item['proven_callsites']=reach.proven_calls_to(f['va']) if f['va'] else []; tim.append(item)
    tv={x['va'] for x in tex}|{x['va'] for x in tim if x['va']}; cand=candidate_branch_refs(elf,tv)
    for x in tex+tim: x['candidate_xrefs']=cand.get(x['va'],[])
    sr=elf.resolve_ibo32(elf.modinfo.get('start_entry',0xFFFFFFFF)); tr=elf.resolve_ibo32(elf.modinfo.get('stop_entry',0xFFFFFFFF))
    return dict(module=elf.modinfo['name'],sha256=elf.sha256,e_type=f'0x{elf.e_type:04X}',e_entry=f'0x{elf.e_entry:08X}',program_headers=[dict(index=p.index,type=p.p_type,offset=p.p_offset,vaddr=p.p_vaddr,paddr=p.p_paddr,filesz=p.p_filesz,memsz=p.p_memsz,flags=p.p_flags) for p in elf.phdrs],module_info=dict(segment=elf.modinfo_segment,segment_offset=elf.modinfo_seg_off,file_offset=elf.modinfo_file_off,infover=elf.modinfo['infover'],ent_top=elf.modinfo['ent_top'],ent_end=elf.modinfo['ent_end'],stub_top=elf.modinfo['stub_top'],stub_end=elf.modinfo['stub_end'],start_entry_raw=elf.modinfo.get('start_entry',0xFFFFFFFF),start_entry_resolved=sr,stop_entry_raw=elf.modinfo.get('stop_entry',0xFFFFFFFF),stop_entry_resolved=tr,arm_exidx_top_raw=elf.modinfo.get('arm_exidx_top',0),arm_exidx_end_raw=elf.modinfo.get('arm_exidx_end',0),exidx_function_count=len(elf.exidx)),export_libraries=[dict(name=l['library_name'],nid=l['library_nid'],nfunc=l['nfunc']) for l in ex],target_exports=tex,target_imports=tim,reachable_function_count=len(reach.functions),state_offset_refs=reach.state_refs())

def print_record(rec):
    print(f"MODULE {rec['module']} sha256={rec['sha256']}"); mi=rec['module_info']
    print(f"  module_info seg={mi['segment']} seg_off=0x{mi['segment_offset']:X} file_off=0x{mi['file_offset']:X} infover={mi['infover']} exidx_functions={mi['exidx_function_count']} reachable_functions={rec['reachable_function_count']}")
    for t in rec['target_exports']:
        libnid='None' if t['library_nid'] is None else f"0x{t['library_nid']:08X}"
        print(f"  EXPORT {t['library_name']} libnid={libnid} nid=0x{t['nid']:08X} raw=0x{t['raw_entry']:08X} thumb={int(t['thumb'])} va=0x{t['va']:08X} seg={t['segment']} file=0x{t['file_offset']:X}")
        c=t['cfg']; print(f"    CFG start=0x{c['start']:08X} mode={c['mode']} range=[0x{c['range_start']:08X},0x{c['range_end']:08X}) exidx_exact={int(c['exidx_exact'])} blocks={c['block_count']} insns={c['instruction_count']}")
        for b in c.get('blocks',[]):
            succ=','.join(f'0x{x:08X}' for x in b['successors']) or 'return/terminal'; print(f"      BLOCK 0x{b['start']:08X} -> {succ}")
            for line in b['instructions']: print('        '+line)
    for t in rec['target_imports']:
        fo=f"0x{t['file_offset']:X}" if t['file_offset'] is not None else 'unmapped'
        print(f"  IMPORT {t['library_name']} libnid=0x{t['library_nid']:08X} nid=0x{t['nid']:08X} raw=0x{t['raw_entry']:08X} thumb={int(t['thumb'])} va=0x{t['va']:08X} seg={t['segment']} file={fo}")
        proven={r['call_va'] for r in t['proven_callsites']}
        for r in t['proven_callsites']:
            print(f"    PROVEN_CALLSITE function=0x{r['function_start']:08X} mode={r['function_mode']} exidx_exact={int(r['exidx_exact'])} call=0x{r['call_va']:08X} {r['instruction']}")
            for line in r['pre_post_window']: print('      '+line)
        for r in t['candidate_xrefs']:
            if r['call_va'] not in proven: print(f"    CANDIDATE_XREF 0x{r['call_va']:08X} {r['mode']} {r['instruction']}")
    if rec['state_offset_refs']:
        print('  REACHABLE_STATE_OFFSET_REFS')
        for r in rec['state_offset_refs']: print(f"    +0x{r['immediate']:X} function=0x{r['function_start']:08X} mode={r['function_mode']} exidx_exact={int(r['exidx_exact'])} at=0x{r['va']:08X} {r['instruction']}")

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('elf',nargs='+',type=Path); ap.add_argument('--json',type=Path); ap.add_argument('--target-nid',action='append',default=[]); ap.add_argument('--emit-cfg-nid',action='append',default=[]); args=ap.parse_args()
    targets=DEFAULT_TARGET_NIDS|{int(x,0) for x in args.target_nid}; emit={int(x,0) for x in args.emit_cfg_nid} or (IFTU_NIDS|DISPLAY_NIDS); records=[]
    for p in args.elf:
        rec=compact_module(VitaElf(p),targets,emit); records.append(rec); print_record(rec)
    if args.json: args.json.write_text(json.dumps(records,indent=2)+'\n')
if __name__=='__main__': main()
