#!/usr/bin/env python3
from __future__ import annotations
import re
from collections import defaultdict
from pathlib import Path
from capstone.arm import ARM_OP_IMM, ARM_OP_MEM, ARM_OP_REG, ARM_REG_PC


def all_insns(cfg):
    d={}
    for b in cfg.blocks.values():
        for i in b.instructions:d[i.address]=i
    return [d[k] for k in sorted(d)]

def ins_text(i): return f"{i.mnemonic} {i.op_str}".strip()

def load_nid_names(root: Path):
    by_nid=defaultdict(list)
    for p in sorted(root.glob('db/360/*.yml')):
        for line in p.read_text(errors='replace').splitlines():
            m=re.match(r'^\s+([A-Za-z_][A-Za-z0-9_]*):\s+(0x[0-9A-Fa-f]+)\s*$',line)
            if not m or m.group(1)=='nid': continue
            by_nid[int(m.group(2),16)].append(m.group(1))
    return {k:sorted(set(v)) for k,v in by_nid.items()}

def best_name(by_nid,nid):
    v=by_nid.get(nid,[])
    return v[0] if v else None

def import_stub_map(elf, names=None):
    out={}
    for lib in elf.imports():
        for f in lib['functions']:
            if not f['va']: continue
            out[f['va']]={'library':lib['library_name'],'library_nid':lib['library_nid'],'nid':f['nid'],'name':best_name(names or {},f['nid'])}
    return out

def call_semantics(cfg, stubmap):
    out=[]
    for c in cfg.calls:
        row={'call_va':c['va'],'target':c['target'],'instruction':c['instruction']}
        if c['target'] in stubmap: row.update(stubmap[c['target']])
        else: row['internal']=True
        out.append(row)
    return out

def constructed_constants(cfg):
    out=[]
    for b in cfg.blocks.values():
        regs={}
        for i in b.instructions:
            ops=getattr(i,'operands',[]);m=i.mnemonic.lower()
            if m=='movw' and len(ops)>=2 and ops[0].type==ARM_OP_REG and ops[1].type==ARM_OP_IMM:
                regs[ops[0].reg]=ops[1].imm&0xffff
            elif m=='movt' and len(ops)>=2 and ops[0].type==ARM_OP_REG and ops[1].type==ARM_OP_IMM:
                r=ops[0].reg
                if r in regs:
                    v=(regs[r]&0xffff)|((ops[1].imm&0xffff)<<16)
                    regs[r]=v;out.append({'va':i.address,'value':v,'instruction':ins_text(i)})
            elif ops and ops[0].type==ARM_OP_REG and m not in ('cmp','cmn','tst','teq','str','str.w','strh','strb'):
                regs.pop(ops[0].reg,None)
    seen={}
    for x in out: seen[(x['va'],x['value'])]=x
    return [seen[k] for k in sorted(seen)]

def literal_constants(elf,cfg):
    out=[]
    for i in all_insns(cfg):
        ops=getattr(i,'operands',[])
        if not i.mnemonic.lower().startswith('ldr') or len(ops)<2 or ops[1].type!=ARM_OP_MEM or ops[1].mem.base!=ARM_REG_PC: continue
        addr=((i.address+4)&~3)+ops[1].mem.disp
        try:
            _,o=elf.file_from_va(addr);v=int.from_bytes(elf.data[o:o+4],'little')
        except Exception: continue
        out.append({'va':i.address,'literal_va':addr,'value':v,'instruction':ins_text(i)})
    return out

def absolute_constants(elf,cfg):
    rows=constructed_constants(cfg)+literal_constants(elf,cfg)
    seen={}
    for x in rows: seen[(x['va'],x['value'])]=x
    return [seen[k] for k in sorted(seen)]

def relevant_abs(elf,cfg):
    vals=absolute_constants(elf,cfg)
    return [x for x in vals if (0x80000000<=x['value']<0x90000000) or (0xE0000000<=x['value']<0xF0000000)]

def function_parents(reach):
    p=defaultdict(list)
    for key,cfg in reach.functions.items():
        for child in cfg.direct_callees:p[child].append({'caller':cfg.start,'mode':'thumb' if cfg.thumb else 'arm'})
    return p

def function_for_va(reach,va):
    best=None
    for (start,thumb),cfg in reach.functions.items():
        if cfg.range_start <= (va&~1) < cfg.range_end:
            if best is None or start>best[0]:best=(start,thumb,cfg)
    return best[2] if best else None
