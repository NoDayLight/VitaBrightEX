#!/usr/bin/env python3
"""Compact PSVita SCE ELF export/import evidence extractor.

The module-info locator and PRX2 libent/libstub layouts intentionally follow
VitaLoaderRedux (pinned in research/DEPENDENCIES.md). This tool emits only
derived metadata and small targeted instruction windows; it never copies module
payload bytes into repository evidence.
"""
from __future__ import annotations
import argparse, hashlib, json, struct
from dataclasses import dataclass
from pathlib import Path

PT_LOAD = 1
ET_SCE_EXEC = 0xFE00
ET_SCE_RELEXEC = 0xFE04
ET_SCE_ARMRELEXEC = 0xFFA5
TARGET_NIDS = {0x0FCBF457, 0x357EAE24, 0xD64F4C6B}

def u8(b,o): return b[o]
def u16(b,o): return struct.unpack_from("<H",b,o)[0]
def u32(b,o): return struct.unpack_from("<I",b,o)[0]

@dataclass
class Phdr:
    index:int; p_type:int; p_offset:int; p_vaddr:int; p_paddr:int
    p_filesz:int; p_memsz:int; p_flags:int; p_align:int

class VitaElf:
    def __init__(self, path: Path):
        self.path = path
        self.data = path.read_bytes()
        if self.data[:4] != b"\x7fELF" or self.data[4] != 1 or self.data[5] != 1:
            raise ValueError(f"{path}: not ELF32 little-endian")
        self.e_type = u16(self.data, 16)
        self.e_machine = u16(self.data, 18)
        self.e_entry = u32(self.data, 24)
        self.e_phoff = u32(self.data, 28)
        entsz = u16(self.data, 42)
        count = u16(self.data, 44)
        if entsz != 0x20: raise ValueError("unexpected phdr size")
        self.phdrs=[]
        for i in range(count):
            o=self.e_phoff+i*entsz
            vals=struct.unpack_from("<IIIIIIII",self.data,o)
            self.phdrs.append(Phdr(i,*vals))
        self.sha256=hashlib.sha256(self.data).hexdigest()
        self.modinfo_segment, self.modinfo_seg_off, self.modinfo_file_off = self._locate_modinfo()
        self.modinfo = self._parse_modinfo()
        self.segment_base = self.phdrs[self.modinfo_segment].p_vaddr

    def _locate_modinfo(self):
        if self.e_machine != 40: raise ValueError("not ARM")
        if self.e_type == ET_SCE_ARMRELEXEC:
            p=self.phdrs[0]
            off=p.p_paddr-p.p_offset
            if off < 0 or off >= p.p_filesz: raise ValueError("bad PRX1 module info")
            return 0, off, p.p_offset+off
        if self.e_type == ET_SCE_EXEC:
            for p in self.phdrs:
                if p.p_paddr and p.p_paddr < p.p_filesz:
                    return p.index, p.p_paddr, p.p_offset+p.p_paddr
        seg=(self.e_entry>>30)&3
        off=self.e_entry&0x3fffffff
        if seg >= len(self.phdrs): raise ValueError("module info segment outside phdrs")
        p=self.phdrs[seg]
        if p.p_type != PT_LOAD or off >= p.p_filesz:
            raise ValueError("bad e_entry module info locator")
        return seg,off,p.p_offset+off

    def _parse_modinfo(self):
        o=self.modinfo_file_off
        attrs=u16(self.data,o); ver=list(self.data[o+2:o+4])
        rawname=self.data[o+4:o+31]
        if b"\0" not in rawname: raise ValueError("unterminated module name")
        name=rawname.split(b"\0",1)[0].decode("ascii")
        infover=u8(self.data,o+31)
        if infover not in (0,2,3,6): raise ValueError(f"unsupported infover {infover}")
        reserve, ent_top, ent_end, stub_top, stub_end = struct.unpack_from("<IIIII",self.data,o+32)
        if ent_end < ent_top or stub_end < stub_top: raise ValueError("reversed tables")
        return dict(attributes=attrs,version=ver,name=name,infover=infover,reserve=reserve,
                    ent_top=ent_top,ent_end=ent_end,stub_top=stub_top,stub_end=stub_end)

    def file_from_va(self, va:int):
        for p in self.phdrs:
            if p.p_type==PT_LOAD and p.p_vaddr <= va < p.p_vaddr+p.p_filesz:
                return p.index,p.p_offset+(va-p.p_vaddr)
        raise ValueError(f"VA 0x{va:08X} not in a file-backed PT_LOAD")

    def cstr_va(self,va:int):
        _,o=self.file_from_va(va)
        e=self.data.find(b"\0",o,o+256)
        if e<0: raise ValueError("unterminated string")
        return self.data[o:e].decode("ascii",errors="replace")

    def exports(self):
        p=self.phdrs[self.modinfo_segment]
        cur=p.p_offset+self.modinfo["ent_top"]
        end=p.p_offset+self.modinfo["ent_end"]
        if not (p.p_offset <= cur <= end <= p.p_offset+p.p_filesz):
            raise ValueError("export table outside module-info segment")
        out=[]
        while cur < end:
            size=u8(self.data,cur)
            if size not in (0x1c,0x20):
                raise ValueError(f"unknown libent size 0x{size:X} at file 0x{cur:X}")
            nfunc=u16(self.data,cur+6); nvar=u16(self.data,cur+8); ntls=u16(self.data,cur+10)
            if size==0x20:
                libnid=u32(self.data,cur+16); libname_va=u32(self.data,cur+20)
                nid_va=u32(self.data,cur+24); add_va=u32(self.data,cur+28)
            else:
                libnid=None; libname_va=u32(self.data,cur+16)
                nid_va=u32(self.data,cur+20); add_va=u32(self.data,cur+24)
            name=self.cstr_va(libname_va) if libname_va else ""
            _,nid_o=self.file_from_va(nid_va)
            _,add_o=self.file_from_va(add_va)
            funcs=[]
            for i in range(nfunc):
                nid=u32(self.data,nid_o+4*i); raw=u32(self.data,add_o+4*i)
                va=raw & ~1
                seg,fo=self.file_from_va(va)
                funcs.append(dict(nid=nid,raw_entry=raw,thumb=bool(raw&1),va=va,
                                  segment=seg,file_offset=fo))
            out.append(dict(file_offset=cur,size=size,library_name=name,library_nid=libnid,
                            nfunc=nfunc,nvar=nvar,ntlsvar=ntls,functions=funcs))
            cur += size
        return out

    def imports(self):
        p=self.phdrs[self.modinfo_segment]
        cur=p.p_offset+self.modinfo["stub_top"]
        end=p.p_offset+self.modinfo["stub_end"]
        if not (p.p_offset <= cur <= end <= p.p_offset+p.p_filesz):
            raise ValueError("import table outside module-info segment")
        out=[]
        while cur < end:
            first=u8(self.data,cur)
            size=first if first==0x24 else u16(self.data,cur)
            if size==0x24:
                nfunc=u16(self.data,cur+6); nvar=u16(self.data,cur+8); ntls=u16(self.data,cur+10)
                libnid=u32(self.data,cur+12); libname_va=u32(self.data,cur+16)
                fnid_va=u32(self.data,cur+20); ftab_va=u32(self.data,cur+24)
            elif size==0x34:
                nfunc=u16(self.data,cur+6); nvar=u16(self.data,cur+8); ntls=u16(self.data,cur+10)
                libnid=u32(self.data,cur+16); libname_va=u32(self.data,cur+20)
                fnid_va=u32(self.data,cur+28); ftab_va=u32(self.data,cur+32)
            else:
                raise ValueError(f"unknown libstub size 0x{size:X} at file 0x{cur:X}")
            name=self.cstr_va(libname_va) if libname_va else ""
            _,nid_o=self.file_from_va(fnid_va)
            _,tab_o=self.file_from_va(ftab_va)
            funcs=[]
            for i in range(nfunc):
                nid=u32(self.data,nid_o+4*i); raw=u32(self.data,tab_o+4*i)
                va=raw & ~1
                try: seg,fo=self.file_from_va(va)
                except ValueError: seg,fo=None,None
                funcs.append(dict(nid=nid,raw_entry=raw,thumb=bool(raw&1),va=va,
                                  segment=seg,file_offset=fo))
            out.append(dict(file_offset=cur,size=size,library_name=name,library_nid=libnid,
                            nfunc=nfunc,nvar=nvar,ntlsvar=ntls,functions=funcs))
            cur += size
        return out

def disasm_window(elf, va, thumb, max_bytes=96, max_insn=28):
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_THUMB
    seg,fo=elf.file_from_va(va)
    p=elf.phdrs[seg]
    avail=min(max_bytes, p.p_offset+p.p_filesz-fo)
    md=Cs(CS_ARCH_ARM, CS_MODE_THUMB if thumb else CS_MODE_ARM)
    result=[]
    for ins in md.disasm(elf.data[fo:fo+avail], va):
        result.append(f"0x{ins.address:08X}: {ins.mnemonic} {ins.op_str}".rstrip())
        if len(result)>=max_insn: break
        op=ins.op_str.replace(" ","").lower()
        if ins.mnemonic=="bx" and op=="lr": break
        if ins.mnemonic=="pop" and "pc" in op: break
    return result

def branch_refs(elf, targets, limit_per_target=16):
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_THUMB
    from capstone.arm import ARM_OP_IMM
    refs={t:[] for t in targets}
    for p in elf.phdrs:
        if p.p_type!=PT_LOAD or not (p.p_flags & 1): continue
        blob=elf.data[p.p_offset:p.p_offset+p.p_filesz]
        for mode,step in ((CS_MODE_THUMB,2),(CS_MODE_ARM,4)):
            md=Cs(CS_ARCH_ARM,mode); md.detail=True
            for off in range(0,max(0,len(blob)-4),step):
                insns=list(md.disasm(blob[off:off+4],p.p_vaddr+off,count=1))
                if not insns: continue
                ins=insns[0]
                if ins.mnemonic not in ("bl","blx","b","b.w"): continue
                if not ins.operands or ins.operands[0].type != ARM_OP_IMM: continue
                target=ins.operands[0].imm & 0xFFFFFFFF
                if target in refs and len(refs[target])<limit_per_target:
                    refs[target].append(dict(call_va=ins.address,mode="thumb" if mode==CS_MODE_THUMB else "arm",
                                             instruction=f"{ins.mnemonic} {ins.op_str}"))
    return refs

def compact_module(elf):
    ex=elf.exports(); im=elf.imports(); targets=[]; imports=[]
    for lib in ex:
        for f in lib["functions"]:
            if f["nid"] in TARGET_NIDS:
                targets.append(dict(library_name=lib["library_name"],library_nid=lib["library_nid"],**f))
    for lib in im:
        for f in lib["functions"]:
            if f["nid"] in TARGET_NIDS:
                imports.append(dict(library_name=lib["library_name"],library_nid=lib["library_nid"],**f))
    target_vas={t["va"] for t in targets} | {t["va"] for t in imports if t["va"]}
    refs=branch_refs(elf,target_vas)
    for t in targets:
        t["instructions"]=disasm_window(elf,t["va"],t["thumb"])
        t["callers"]=refs.get(t["va"],[])
    for t in imports:
        t["callers"]=refs.get(t["va"],[]) if t["va"] else []
    return dict(module=elf.modinfo["name"],sha256=elf.sha256,e_type=f"0x{elf.e_type:04X}",e_entry=f"0x{elf.e_entry:08X}",
        program_headers=[dict(index=p.index,type=p.p_type,offset=p.p_offset,vaddr=p.p_vaddr,paddr=p.p_paddr,filesz=p.p_filesz,memsz=p.p_memsz,flags=p.p_flags) for p in elf.phdrs],
        module_info=dict(segment=elf.modinfo_segment,segment_offset=elf.modinfo_seg_off,file_offset=elf.modinfo_file_off,infover=elf.modinfo["infover"],ent_top=elf.modinfo["ent_top"],ent_end=elf.modinfo["ent_end"],stub_top=elf.modinfo["stub_top"],stub_end=elf.modinfo["stub_end"]),
        export_libraries=[dict(name=l["library_name"],nid=l["library_nid"],nfunc=l["nfunc"]) for l in ex],target_exports=targets,target_imports=imports)

def main():
    ap=argparse.ArgumentParser(); ap.add_argument("elf",nargs="+",type=Path); ap.add_argument("--json",type=Path); args=ap.parse_args()
    records=[]
    for p in args.elf:
        e=VitaElf(p); rec=compact_module(e); records.append(rec)
        print(f"MODULE {rec['module']} sha256={rec['sha256']}")
        mi=rec["module_info"]
        print(f"  module_info seg={mi['segment']} seg_off=0x{mi['segment_offset']:X} file_off=0x{mi['file_offset']:X} infover={mi['infover']}")
        for t in rec["target_exports"]:
            print(f"  EXPORT {t['library_name']} libnid={t['library_nid']!s} nid=0x{t['nid']:08X} raw=0x{t['raw_entry']:08X} thumb={int(t['thumb'])} va=0x{t['va']:08X} seg={t['segment']} file=0x{t['file_offset']:X}")
            for ins in t.get("instructions",[]): print("    "+ins)
            for ref in t.get("callers",[]): print(f"    CALLER 0x{ref['call_va']:08X} {ref['mode']} {ref['instruction']}")
        for t in rec["target_imports"]:
            fileoff=('0x%X'%t['file_offset']) if t['file_offset'] is not None else 'unmapped'
            print(f"  IMPORT {t['library_name']} libnid=0x{t['library_nid']:08X} nid=0x{t['nid']:08X} raw=0x{t['raw_entry']:08X} thumb={int(t['thumb'])} va=0x{t['va']:08X} seg={t['segment']} file={fileoff}")
            for ref in t.get("callers",[]): print(f"    CALLER 0x{ref['call_va']:08X} {ref['mode']} {ref['instruction']}")
    if args.json: args.json.write_text(json.dumps(records,indent=2)+"\n")
if __name__=="__main__": main()
