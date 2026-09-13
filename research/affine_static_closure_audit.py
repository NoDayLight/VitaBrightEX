#!/usr/bin/env python3
from __future__ import annotations
import argparse,json,struct
from collections import defaultdict
from pathlib import Path
from capstone.arm import ARM_OP_IMM,ARM_OP_MEM,ARM_OP_REG,ARM_REG_PC
from vita_elf_audit import VitaElf,Reachability,FunctionCFG,PT_LOAD
from topology_common import all_insns,ins_text,load_nid_names,import_stub_map,call_semantics,absolute_constants,function_parents

LOWIO_SHA='f791cfe2c6db955deb9446c57bb72ce1870bde2c6c485cac363e343d5128f744'
DISPLAY_SHA='83ef39adf741a4bbbc1cec67d00e2594de1d17be0f5ea6cc77c935eae46065e5'
PLANE_BASE=0x8100B37C
PLANE_STRIDE=0x214
CACHE_A=(0x10C,0x148)
CACHE_B=(0x148,0x184)
IFTU_CSC_NID=0x67E37EFC
FOCUS_LOWIO=(0x81000350,0x81005D3C,0x81005D48,0x81005E24,0x8100639C,0x8100678C)
FOCUS_DISPLAY=(0x81004AC8,0x81004C00,0x81005048)


def rname(ins,op): return ins.reg_name(op.reg) if op.type==ARM_OP_REG else None

def combine_lo_hi(lo,hi): return (lo&0xFFFF)|((hi&0xFFFF)<<16)

def scan_plane_state_function(cfg):
    """Conservative linear provenance scanner.

    It distinguishes writes through a plane-object-derived address from writes
    through the MMIO pointer loaded from plane+0. Unknown control-flow joins are
    intentionally not promoted to proof.
    """
    regs={f'r{i}':('arg',i) for i in range(4)}; out=[]; const_lo={}
    def mem_base(ins,op): return ins.reg_name(op.mem.base)
    for ins in all_insns(cfg):
        ops=getattr(ins,'operands',[]);m=ins.mnemonic.lower()
        if m=='movw' and len(ops)>=2 and ops[0].type==ARM_OP_REG and ops[1].type==ARM_OP_IMM:
            d=rname(ins,ops[0]); const_lo[d]=ops[1].imm&0xFFFF; regs[d]=('const16',ops[1].imm&0xFFFF); continue
        if m=='movt' and len(ops)>=2 and ops[0].type==ARM_OP_REG and ops[1].type==ARM_OP_IMM:
            d=rname(ins,ops[0]);
            if d in const_lo:
                v=combine_lo_hi(const_lo[d],ops[1].imm); regs[d]=('plane_array',0) if v==PLANE_BASE else ('const',v)
            continue
        if m.startswith('mov') and len(ops)>=2 and ops[0].type==ARM_OP_REG:
            d=rname(ins,ops[0])
            if ops[1].type==ARM_OP_REG: regs[d]=regs.get(rname(ins,ops[1]),('unknown',None))
            elif ops[1].type==ARM_OP_IMM: regs[d]=('const',int(ops[1].imm)&0xFFFFFFFF)
            continue
        if m.startswith('mul') and len(ops)>=3 and ops[0].type==ARM_OP_REG:
            d=rname(ins,ops[0]); a=regs.get(rname(ins,ops[1])); b=regs.get(rname(ins,ops[2])) if ops[2].type==ARM_OP_REG else None
            vals=(a,b)
            if ('const',PLANE_STRIDE) in vals and any(x and x[0]=='arg' for x in vals): regs[d]=('plane_delta_dynamic',None)
            elif a and a[0] in ('const16','const') and a[1]==PLANE_STRIDE and b and b[0]=='arg': regs[d]=('plane_delta_dynamic',None)
            elif b and b[0] in ('const16','const') and b[1]==PLANE_STRIDE and a and a[0]=='arg': regs[d]=('plane_delta_dynamic',None)
            else: regs[d]=('unknown',None)
            continue
        if m.startswith(('add','sub')) and len(ops)>=2 and ops[0].type==ARM_OP_REG:
            d=rname(ins,ops[0]); s=regs.get(rname(ins,ops[1])) if ops[1].type==ARM_OP_REG else None
            sign=-1 if m.startswith('sub') else 1
            if len(ops)>=3 and ops[2].type==ARM_OP_IMM:
                imm=sign*int(ops[2].imm)
                if s and s[0]=='plane_array': regs[d]=('plane_addr',imm)
                elif s and s[0]=='plane_obj': regs[d]=('plane_addr',s[1]+imm)
                elif s and s[0]=='plane_addr': regs[d]=('plane_addr',s[1]+imm)
                else: regs[d]=('unknown',None)
            elif len(ops)>=3 and ops[2].type==ARM_OP_REG:
                t=regs.get(rname(ins,ops[2]))
                if s and s[0]=='plane_array' and t and t[0]=='plane_delta_dynamic': regs[d]=('plane_obj',0)
                elif t and t[0]=='plane_array' and s and s[0]=='plane_delta_dynamic': regs[d]=('plane_obj',0)
                else: regs[d]=('unknown',None)
            continue
        if m.startswith('ldr') and len(ops)>=2 and ops[0].type==ARM_OP_REG and ops[1].type==ARM_OP_MEM:
            d=rname(ins,ops[0]); b=regs.get(mem_base(ins,ops[1])); disp=int(ops[1].mem.disp)
            if b and b[0] in ('plane_obj','plane_addr'):
                off=b[1]+disp
                regs[d]=('mmio_ptr',off) if off==0 else ('plane_value',off)
                out.append({'function':cfg.start,'va':ins.address,'kind':'plane_read','offset':off,'instruction':ins_text(ins)})
            elif b and b[0]=='plane_array' and disp==0:
                regs[d]=('mmio_ptr',0)
            else: regs[d]=('unknown',None)
            continue
        if m.startswith('str') and len(ops)>=2 and ops[0].type==ARM_OP_REG and ops[1].type==ARM_OP_MEM:
            b=regs.get(mem_base(ins,ops[1]));disp=int(ops[1].mem.disp)
            if b and b[0] in ('plane_obj','plane_addr'):
                off=b[1]+disp;out.append({'function':cfg.start,'va':ins.address,'kind':'plane_write','offset':off,'instruction':ins_text(ins)})
            elif b and b[0]=='mmio_ptr': out.append({'function':cfg.start,'va':ins.address,'kind':'mmio_write','offset':disp,'instruction':ins_text(ins)})
            continue
    return out


def focus_record(e,reach,stubs,va):
    cfg=next((c for c in reach.functions.values() if c.start==va),None)
    if cfg is None: cfg=FunctionCFG(e,va,True,reach.import_stubs,boundary_index=reach.boundaries,noreturn_stubs=reach.noreturn_stubs)
    m=cfg.compact(False)
    return {'start':cfg.start,'logical_end':cfg.logical_end,'exidx_range':m['exidx_range'],'exidx_exact':cfg.exidx_exact,'boundary_sources':cfg.boundary_sources,'termination_reason':m['termination_reason'],'calls':call_semantics(cfg,stubs),'absolute_constants':absolute_constants(e,cfg),'instructions':[{'va':i.address,'text':ins_text(i)} for i in all_insns(cfg)]}


def code_pointer_refs(e,reach,lo,hi):
    out=[]
    for cfg in reach.functions.values():
        lo16={}
        for i in all_insns(cfg):
            ops=getattr(i,'operands',[]);m=i.mnemonic.lower()
            if m=='movw' and len(ops)>=2 and ops[0].type==ARM_OP_REG and ops[1].type==ARM_OP_IMM: lo16[rname(i,ops[0])]=ops[1].imm&0xffff
            elif m=='movt' and len(ops)>=2 and ops[0].type==ARM_OP_REG and ops[1].type==ARM_OP_IMM:
                d=rname(i,ops[0])
                if d in lo16:
                    v=combine_lo_hi(lo16[d],ops[1].imm)
                    if lo<=v<hi: out.append({'function':cfg.start,'va':i.address,'target':v,'instruction':ins_text(i),'kind':'movw_movt'})
            elif m.startswith('ldr') and len(ops)>=2 and ops[1].type==ARM_OP_MEM and ops[1].mem.base==ARM_REG_PC:
                v=((i.address+4)&~3)+int(ops[1].mem.disp)
                if lo<=v<hi: out.append({'function':cfg.start,'va':i.address,'target':v,'instruction':ins_text(i),'kind':'pc_literal_address'})
    return out


def read_region(e,va,size):
    _,o=e.file_from_va(va);b=e.data[o:o+size]
    return {'va':va,'size':len(b),'words':[int.from_bytes(b[x:x+4],'little') for x in range(0,len(b)-3,4)]}


def find_import_target(display,nid):
    for lib in display.imports():
        for f in lib['functions']:
            if f['nid']==nid:return f['va']
    return None


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--lowio',type=Path,required=True);ap.add_argument('--display',type=Path,required=True);ap.add_argument('--nid-db-root',type=Path,required=True);ap.add_argument('--json',type=Path,required=True);a=ap.parse_args()
    low=VitaElf(a.lowio);disp=VitaElf(a.display)
    if low.sha256!=LOWIO_SHA:raise SystemExit('Lowio hash drift')
    if disp.sha256!=DISPLAY_SHA:raise SystemExit('Display hash drift')
    names=load_nid_names(a.nid_db_root)
    lr=Reachability(low,low.exports(),low.imports(),extra_starts=FOCUS_LOWIO);ls=import_stub_map(low,names)
    dr=Reachability(disp,disp.exports(),disp.imports(),extra_starts=FOCUS_DISPLAY);ds=import_stub_map(disp,names)

    plane_refs=[];cache_writers=defaultdict(list);mmio_writers=defaultdict(list)
    for cfg in lr.functions.values():
        refs=scan_plane_state_function(cfg)
        if refs: plane_refs.extend(refs)
        for x in refs:
            if x['kind']=='plane_write' and (CACHE_A[0]<=x['offset']<CACHE_A[1] or CACHE_B[0]<=x['offset']<CACHE_B[1]): cache_writers[cfg.start].append(x)
            if x['kind']=='mmio_write': mmio_writers[cfg.start].append(x)

    # Authoritative source copy loops in the two setters use an incrementing
    # plane+cache pointer; record them explicitly because their dynamic loop
    # addresses are stronger evidence than a literal-offset grep.
    explicit={
      0x81005D48:{'stage':'A','cache_range':[0x10C,0x148],'source':'arg1 non-NULL copies exactly 0x3C; NULL reuses cache','classification':'PROVEN_DISASSEMBLY'},
      0x81005E24:{'stage':'B','cache_range':[0x148,0x184],'source':'arg1 non-NULL copies exactly 0x3C; NULL reuses cache','classification':'PROVEN_DISASSEMBLY'},
    }
    writer_graph=[]
    for va,desc in explicit.items(): writer_graph.append({'function':va,**desc,'symbolic_writes':cache_writers.get(va,[])})
    other=[]
    for va,rows in sorted(cache_writers.items()):
        if va not in explicit: other.append({'function':va,'writes':rows})

    # Physical-base neighborhood previously established by exact words. Expand
    # it enough to expose pointer-table/descriptor structure without assigning
    # semantics to adjacency.
    descriptor=read_region(low,0x81008F80,0x180)
    descriptor['code_pointer_refs']=code_pointer_refs(low,lr,0x81008F80,0x81009100)
    descriptor['physical_words']=[{'index':i,'va':descriptor['va']+4*i,'value':v} for i,v in enumerate(descriptor['words']) if 0xE5020000<=v<0xE5040000]

    low_focus={f'0x{x:08X}':focus_record(low,lr,ls,x) for x in FOCUS_LOWIO}
    disp_focus={f'0x{x:08X}':focus_record(disp,dr,ds,x) for x in FOCUS_DISPLAY}
    csc_stub=find_import_target(disp,IFTU_CSC_NID)
    csc_calls=[]
    if csc_stub:
        for cfg in dr.functions.values():
            for c in cfg.calls:
                if c['target']==csc_stub:
                    csc_calls.append({'caller':cfg.start,'call_va':c['va'],'window':cfg.window_for_call(c['va'],before=48,after=8),'caller_record':focus_record(disp,dr,ds,cfg.start)})

    result={'schema':1,'firmware':'3.65','analysis_authority':'validated-logical-boundaries','cache_writer_graph':{'explicit_private_setters':writer_graph,'other_symbolically_proven_cache_writers':other,'interpretation':'A+ is complete only if runtime/init evidence confirms no persistent cache mutation path omitted by symbolic alias limits.'},'plane_state_accesses':plane_refs,'mmio_writers_by_function':[{'function':k,'writes':v} for k,v in sorted(mmio_writers.items())],'physical_base_region':descriptor,'lowio_focus':low_focus,'display_capture_focus':disp_focus,'ksceIftuCsc_callsites':csc_calls,'blockers':{'plane_to_physical_binding':'Requires proving which initialization dataflow writes plane+0 MMIO pointers; descriptor adjacency is not treated as binding.','csc_control':'Requires reconstructing SceIftuConvParams at the emitted callsites and correlating pixel formats.','stage_order':'Must be derived from csc_control + callsite format semantics, not register offsets.'}}
    a.json.write_text(json.dumps(result,indent=2)+'\n')
    print('AFFINE_STATIC_CLOSURE')
    print('CACHE_WRITER_GRAPH')
    for x in writer_graph:print(f"  setter {x['stage']} fn=0x{x['function']:08X} cache=0x{x['cache_range'][0]:X}..0x{x['cache_range'][1]-1:X} {x['classification']}")
    print(f'  other_symbolic_cache_writer_functions={len(other)} {[hex(x["function"]) for x in other]}')
    print('LOWIO_INIT_FOCUS')
    for k in ('0x81000350','0x81005D3C'):
        x=low_focus[k];print(f"  {k}..0x{x['logical_end']:08X} calls={[(c.get('name'),hex(c['target'])) for c in x['calls']]} constants={[hex(c['value']) for c in x['absolute_constants']]}")
    print('PHYSICAL_BASE_REGION_REFS')
    for x in descriptor['code_pointer_refs']:print(f"  fn=0x{x['function']:08X} 0x{x['va']:08X} -> 0x{x['target']:08X} {x['kind']}")
    print('CSC_CALLSITES')
    for x in csc_calls:print(f"  caller=0x{x['caller']:08X} call=0x{x['call_va']:08X}")

if __name__=='__main__':main()
