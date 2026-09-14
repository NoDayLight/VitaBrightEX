#!/usr/bin/env python3
from __future__ import annotations
import argparse
import re
import struct
from pathlib import Path

ELF32_HDR = '<16sHHIIIIIHHHHHH'
ELF32_PHDR = '<IIIIIIII'
PT_LOAD = 1
ET_SCE_RELEXEC = 0xFE04


def u16(b: bytes, off: int) -> int:
    return struct.unpack_from('<H', b, off)[0]


def u32(b: bytes, off: int) -> int:
    return struct.unpack_from('<I', b, off)[0]


def parse_elf(path: Path):
    data = path.read_bytes()
    if len(data) < 52 or data[:4] != b'\x7fELF':
        raise SystemExit(f'{path}: not ELF')
    if data[4] != 1 or data[5] != 1:
        raise SystemExit(f'{path}: expected ELF32 little-endian')
    hdr = struct.unpack_from(ELF32_HDR, data, 0)
    e_type, e_entry, e_phoff, e_phentsize, e_phnum = hdr[1], hdr[4], hdr[5], hdr[9], hdr[10]
    if e_type != ET_SCE_RELEXEC:
        raise SystemExit(f'{path}: expected ET_SCE_RELEXEC 0x{ET_SCE_RELEXEC:04X}, got 0x{e_type:04X}')
    if e_phentsize != struct.calcsize(ELF32_PHDR):
        raise SystemExit(f'{path}: unexpected phdr size {e_phentsize}')
    phdrs = []
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        if off + e_phentsize > len(data):
            raise SystemExit(f'{path}: truncated phdr table')
        p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align = struct.unpack_from(ELF32_PHDR, data, off)
        phdrs.append(dict(type=p_type, offset=p_offset, vaddr=p_vaddr, filesz=p_filesz, memsz=p_memsz, flags=p_flags))
    return data, e_entry, phdrs


def va_to_file(data: bytes, phdrs, va: int, size: int = 1) -> int:
    for ph in phdrs:
        if ph['type'] != PT_LOAD:
            continue
        start, end = ph['vaddr'], ph['vaddr'] + ph['filesz']
        if start <= va and va + size <= end:
            off = ph['offset'] + (va - start)
            if off + size <= len(data):
                return off
    raise SystemExit(f'VA 0x{va:08X}+0x{size:X} not backed by a PT_LOAD file range')


def cstr_at_va(data: bytes, phdrs, va: int) -> str:
    off = va_to_file(data, phdrs, va)
    end = data.find(b'\0', off)
    if end < 0:
        raise SystemExit(f'unterminated string at VA 0x{va:08X}')
    return data[off:end].decode('ascii', errors='replace')


def load_names(db_roots: list[Path]):
    names = {}
    lib_re = re.compile(r'^      ([A-Za-z0-9_]+):\s*$')
    lib_nid_re = re.compile(r'^        nid:\s*(0x[0-9A-Fa-f]+)\s*$')
    fn_re = re.compile(r'^          ([A-Za-z0-9_]+):\s*(0x[0-9A-Fa-f]+)\s*$')
    for root in db_roots:
        if not root.exists():
            continue
        for p in root.rglob('*.yml'):
            lib = None
            lib_nid = None
            in_functions = False
            for raw in p.read_text(errors='replace').splitlines():
                m = lib_re.match(raw)
                if m:
                    lib, lib_nid, in_functions = m.group(1), None, False
                    continue
                if lib is None:
                    continue
                m = lib_nid_re.match(raw)
                if m and lib_nid is None:
                    lib_nid = int(m.group(1), 16)
                    continue
                if raw == '        functions:':
                    in_functions = True
                    continue
                if in_functions:
                    m = fn_re.match(raw)
                    if m and lib_nid is not None:
                        names[(lib, lib_nid, int(m.group(2), 16))] = m.group(1)
                    elif raw and not raw.startswith('          ') and not raw.lstrip().startswith('#'):
                        in_functions = False
    return names


def parse_imports(path: Path):
    data, e_entry, phdrs = parse_elf(path)
    seg = e_entry >> 30
    modoff = e_entry & 0x3FFFFFFF
    if seg >= len(phdrs):
        raise SystemExit(f'{path}: module-info segment index {seg} out of range')
    mph = phdrs[seg]
    mod_file = mph['offset'] + modoff
    if mod_file + 0x5C > len(data):
        raise SystemExit(f'{path}: truncated module info')
    import_top = u32(data, mod_file + 0x2C)
    import_end = u32(data, mod_file + 0x30)
    if import_top == 0 and import_end == 0:
        return []
    if import_end < import_top:
        raise SystemExit(f'{path}: invalid import range')
    cur = mph['offset'] + import_top
    end = mph['offset'] + import_end
    if end > len(data):
        raise SystemExit(f'{path}: import table outside file')
    rows = []
    while cur < end:
        size = u16(data, cur)
        if size == 0x34:
            num_funcs = u16(data, cur + 0x06)
            library_nid = u32(data, cur + 0x10)
            library_name_va = u32(data, cur + 0x14)
            func_nids_va = u32(data, cur + 0x1C)
        elif size == 0x24:
            num_funcs = u16(data, cur + 0x06)
            library_nid = u32(data, cur + 0x0C)
            library_name_va = u32(data, cur + 0x10)
            func_nids_va = u32(data, cur + 0x14)
        else:
            raise SystemExit(f'{path}: unsupported import descriptor size 0x{size:X} at 0x{cur:X}')
        library_name = cstr_at_va(data, phdrs, library_name_va) if library_name_va else '<anonymous>'
        if num_funcs:
            fn_off = va_to_file(data, phdrs, func_nids_va, num_funcs * 4)
            for i in range(num_funcs):
                rows.append((library_name, library_nid, u32(data, fn_off + i * 4)))
        cur += size
    if cur != end:
        raise SystemExit(f'{path}: import table framing mismatch')
    return sorted(rows)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('velf', type=Path)
    ap.add_argument('--nid-db', action='append', default=[], type=Path)
    ap.add_argument('--output', type=Path)
    args = ap.parse_args()
    names = load_names(args.nid_db)
    rows = parse_imports(args.velf)
    text = ''.join(
        f'library={lib} library_nid=0x{lnid:08X} function_nid=0x{fnid:08X} function_name={names.get((lib, lnid, fnid), "UNKNOWN")}\n'
        for lib, lnid, fnid in rows
    )
    if args.output:
        args.output.write_text(text)
    else:
        print(text, end='')


if __name__ == '__main__':
    main()
