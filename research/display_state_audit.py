#!/usr/bin/env python3
"""Focused retail-3.65 display-state audit.

This consumes the reachable-CFG model from vita_elf_audit.py and emits derived
evidence needed for ownership/restoration decisions. On these retail modules
there is no exidx boundary data, so the CFG walker is not claimed as a generic
proof-grade function-boundary recovery system. Raw firmware bytes are never
written by this tool.
"""
from pathlib import Path
from collections import defaultdict
from capstone.arm import ARM_OP_IMM, ARM_OP_MEM, ARM_OP_REG, ARM_REG_PC
from vita_elf_audit import VitaElf, Reachability, FunctionCFG

EXPECTED = {
    "SceLowio": "f791cfe2c6db955deb9446c57bb72ce1870bde2c6c485cac363e343d5128f744",
    "SceDisplay": "83ef39adf741a4bbbc1cec67d00e2594de1d17be0f5ea6cc77c935eae46065e5",
    "SceLcd": "24752bb4c69f0c241701cab0364cde9cbe5c95128b6bfe02bf3263aad524246e",
    "SceOled": "3398e102641936223fca6a36b8216be31a83fd7a41db07d174b20155fe00f1d7",
}

IFTU_SET = 0x81005D48
IFTU_STATE_FUNCS = (0x81005B08, 0x81005D48, 0x81005E24, 0x81005F20,
                    0x81005FEC, 0x810062B4, 0x81006338, 0x8100639C,
                    0x81006714)
INVERT_NID = 0x19140ACD
INVERT_CACHE_ADDRS = (0x8100B04C, 0x8100B0E4)
OLED_OFFSETS = (0x1AB8, 0x1C20, 0x1E00)

def insns(cfg):
    out = {}
    for block in cfg.blocks.values():
        for ins in block.instructions:
            out[ins.address] = ins
    return [out[k] for k in sorted(out)]

def all_reachable(reach):
    seen = {}
    for (start, thumb), cfg in reach.functions.items():
        for ins in insns(cfg):
            seen.setdefault(ins.address, (start, thumb, ins))
    return [seen[k] for k in sorted(seen)]

def export_by_start(elf):
    out = defaultdict(list)
    for lib in elf.exports():
        for fn in lib['functions']:
            out[fn['va']].append((lib['library_name'], lib['library_nid'], fn['nid']))
    return out

def read_u32_va(elf, va):
    try:
        _, off = elf.file_from_va(va)
    except ValueError:
        return None
    if off + 4 > len(elf.data):
        return None
    return int.from_bytes(elf.data[off:off+4], 'little')

def memory_absolute_refs(reach):
    """Track simple MOVW/MOVT bases and PC-relative literals in reachable blocks."""
    refs = []
    for (start, thumb), cfg in reach.functions.items():
        for block in cfg.blocks.values():
            constants = {}
            for ins in block.instructions:
                ops = getattr(ins, 'operands', [])
                m = ins.mnemonic.lower()
                if m == 'movw' and len(ops) >= 2 and ops[0].type == ARM_OP_REG and ops[1].type == ARM_OP_IMM:
                    constants[ops[0].reg] = ops[1].imm & 0xFFFF
                elif m == 'movt' and len(ops) >= 2 and ops[0].type == ARM_OP_REG and ops[1].type == ARM_OP_IMM:
                    reg = ops[0].reg
                    if reg in constants:
                        constants[reg] = (constants[reg] & 0xFFFF) | ((ops[1].imm & 0xFFFF) << 16)
                if not (m.startswith('ldr') or m.startswith('str')):
                    continue
                for op in ops:
                    if op.type != ARM_OP_MEM:
                        continue
                    base = op.mem.base
                    disp = op.mem.disp
                    if base in constants:
                        refs.append((constants[base] + disp, start, thumb, ins, 'constructed-base'))
                    elif base == ARM_REG_PC and m.startswith('ldr'):
                        literal_va = ((ins.address + 4) & ~3) + disp
                        value = read_u32_va(reach.elf, literal_va)
                        if value is not None:
                            refs.append((value, start, thumb, ins, 'pc-literal'))
    return refs

def verify_iftu_setter(lowio, reach):
    cfg = reach.functions.get((IFTU_SET, True)) or FunctionCFG(lowio, IFTU_SET, True, reach.import_stubs)
    by = {x.address: f'{x.mnemonic} {x.op_str}'.strip() for x in insns(cfg)}
    required = {
        0x81005D48: 'cmp r0, #4',
        0x81005D4E: 'mov r4, r1',
        0x81005D72: 'cbz r4, #0x81005db8',
        0x81005D74: 'mov r3, r4',
        0x81005D76: 'add.w r2, r6, #0x10c',
        0x81005D7A: 'add.w r0, r4, #0x30',
        0x81005D7E: 'ldr.w sl, [r3]',
        0x81005DA6: 'ldr.w lr, [r3]',
        0x81005DAA: 'ldr r0, [r3, #4]',
        0x81005DAC: 'ldr r3, [r3, #8]',
        0x81005DAE: 'str.w lr, [r2]',
        0x81005DB2: 'str r0, [r2, #4]',
        0x81005DB4: 'str r3, [r2, #8]',
        0x81005DB8: 'add.w r4, r6, #0x10c',
        0x81005DBC: 'ldr.w r3, [r6, #0x1e8]',
        0x81005DC0: 'cbz r3, #0x81005e0c',
        0x81005DC6: 'ldr.w r3, [r5, r8]',
        0x81005DDC: 'str.w fp, [r3, #0x104]',
        0x81005E04: 'str.w r4, [r3, #0x12c]',
        0x81005E12: 'movs r0, #0',
        0x81005E18: 'mov.w r0, #0x700',
        0x81005E1C: 'movt r0, #0x803f',
    }
    for va, want in required.items():
        got = by.get(va)
        if got != want:
            raise SystemExit('IFTU invariant mismatch at 0x%08X: %r != %r' % (va, got, want))
    print('IFTU_0FCBF457_RECONSTRUCTION')
    print('  r0: IFTU plane index, accepted range 0..4; r0>4 returns 0x803F0700 (SCE_IFTU_ERROR_INVALID_PLANE)')
    print('  r1: optional pointer to a 0x3C / 15-word CSC parameter object')
    print('  non-NULL: three 16-byte loop copies + three trailing 32-bit words = exactly 0x3C bytes')
    print('  object layout: strongly corroborated as SceIftuCscParams by pinned VitaSDK 0x3C definition')
    print('  r1=NULL: selects cached instance+0x10C object; external reapply semantics are not yet proven')
    print('  r2/r3: not consumed as entry arguments before being overwritten internally')
    print('  active gate: instance+0x1E8; cached values feed hardware-state object writes')
    print('  private NID semantic name/complete prototype: NOT YET PROVEN')
    print('  production ABI status: BLOCKED — original-state acquisition/restoration/power lifecycle unresolved')


def print_iftu_state_machine(lowio, reach):
    print('IFTU_PER_INSTANCE_STATE_MACHINE')
    for start in IFTU_STATE_FUNCS:
        cfg = reach.functions.get((start, True))
        if cfg is None:
            continue
        interesting = []
        for x in insns(cfg):
            text = f'{x.mnemonic} {x.op_str}'.strip()
            if x.mnemonic.lower() in ('bl', 'blx'):
                interesting.append((x.address, text))
                continue
            if ('#0x10' in text or '#0x11' in text or '#0x12' in text or
                '#0x13' in text or '#0x14' in text or '#0x15' in text or
                '#0x16' in text or '#0x1e8' in text or '#0x1f8' in text or
                '#0x214' in text):
                interesting.append((x.address, text))
        print('  function=0x%08X blocks=%d insns=%d' % (start, len(cfg.blocks), cfg.instruction_count()))
        for va, text in interesting[:28]:
            print('    0x%08X %s' % (va, text))
        if len(interesting) > 28:
            print('    ... %d additional selected state/call instructions omitted' % (len(interesting)-28))


def audit_invert(display, reach):
    exports = export_by_start(display)
    target = None
    for lib in display.exports():
        for fn in lib['functions']:
            if fn['nid'] == INVERT_NID:
                target = fn
    if target is None:
        raise SystemExit('invert export missing')
    cfg = reach.functions.get((target['va'], target['thumb'])) or FunctionCFG(display, target['va'], target['thumb'], reach.import_stubs)
    lines = {x.address: f'{x.mnemonic} {x.op_str}'.strip() for x in insns(cfg)}
    required_fragments = ((0x8100303C, '#0xe4]'), (0x81003088, '#0x4c]'))
    for va, fragment in required_fragments:
        if va not in lines or fragment not in lines[va]:
            raise SystemExit('invert cache invariant missing at 0x%08X: %r' % (va, lines.get(va)))

    refs = memory_absolute_refs(reach)
    print('INVERT_STATE_AUDIT')
    print('  setter_nid=0x19140ACD entry=0x%08X' % target['va'])
    print('  exact cached-state writes: 0x8100B04C and 0x8100B0E4')
    readers = []
    for absolute, start, thumb, ins, kind in refs:
        if absolute not in INVERT_CACHE_ADDRS or not ins.mnemonic.lower().startswith('ldr'):
            continue
        readers.append((absolute, start, thumb, ins, kind))
    if not readers:
        print('  cached-state reader: NOT FOUND by reachable constructed-base/PC-literal scan')
        print('  restoration status: existing setter-only limitation remains; no getter invented')
    else:
        for absolute, start, thumb, ins, kind in readers:
            meta = exports.get(start, [])
            tag = ','.join('%s/0x%08X' % (name, nid) for name, _libnid, nid in meta) if meta else 'internal'
            print('  reader_candidate cache=0x%08X function=0x%08X %s %s at=0x%08X %s' %
                  (absolute, start, 'thumb' if thumb else 'arm', tag, ins.address,
                   f'{ins.mnemonic} {ins.op_str}'.strip()))
        print('  restoration status: reader candidates require semantic validation before production use')


def audit_oled(oled, reach):
    base = oled.phdrs[0].p_vaddr
    regions = [(base + off, base + off + 357, off) for off in OLED_OFFSETS]
    refs = memory_absolute_refs(reach)
    hits = []
    for absolute, start, thumb, ins, kind in refs:
        for lo, hi, off in regions:
            if lo <= absolute < hi:
                hits.append((off, absolute, start, thumb, ins, kind))
    print('OLED_STOCK_REGION_CONTROL_FLOW_SCAN')
    print('  segment0_base=0x%08X' % base)
    if not hits:
        print('  no reachable direct address construction/literal reference to 0x1AB8/0x1C20/0x1E00 regions found')
        print('  classification remains structural candidate; indirect selection/dataflow still requires tracing')
    else:
        for off, absolute, start, thumb, ins, kind in hits:
            print('  PROVEN_REACHABLE_DATA_REF region_off=0x%X addr=0x%08X function=0x%08X mode=%s kind=%s at=0x%08X %s' %
                  (off, absolute, start, 'thumb' if thumb else 'arm', kind,
                   ins.address, f'{ins.mnemonic} {ins.op_str}'.strip()))
        print('  note: reachable address reference proves use, not yet DDB-to-panel selection semantics')


def nonlinear_probe(modules):
    print('NONLINEAR_TRANSFER_PROBE')
    total = 0
    for elf, reach in modules:
        hits = []
        for start, thumb, ins in all_reachable(reach):
            for op in getattr(ins, 'operands', []):
                if op.type == ARM_OP_IMM and (op.imm & 0xFFFFFFFF) == 0x165:
                    hits.append((start, thumb, ins))
        if hits:
            print('  %s: reachable immediate-357 references=%d' % (elf.modinfo['name'], len(hits)))
            for start, thumb, ins in hits[:12]:
                print('    function=0x%08X mode=%s at=0x%08X %s' %
                      (start, 'thumb' if thumb else 'arm', ins.address,
                       f'{ins.mnemonic} {ins.op_str}'.strip()))
            total += len(hits)
    if total == 0:
        print('  no reachable immediate 357-byte transfer operation identified in this focused pass')
    print('  conclusion: no production nonlinear-transfer backend proven by this pass; research remains open')


def load(path):
    elf = VitaElf(Path(path))
    want = EXPECTED.get(elf.modinfo['name'])
    if want is None or elf.sha256 != want:
        raise SystemExit('%s hash mismatch: %s expected %s' % (elf.modinfo['name'], elf.sha256, want))
    ex, im = elf.exports(), elf.imports()
    return elf, Reachability(elf, ex, im)

def main():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument('--lowio', required=True)
    ap.add_argument('--display', required=True)
    ap.add_argument('--lcd', required=True)
    ap.add_argument('--oled', required=True)
    a = ap.parse_args()
    lowio, lr = load(a.lowio)
    display, dr = load(a.display)
    lcd, lcr = load(a.lcd)
    oled, orr = load(a.oled)
    verify_iftu_setter(lowio, lr)
    print_iftu_state_machine(lowio, lr)
    audit_invert(display, dr)
    audit_oled(oled, orr)
    nonlinear_probe(((lowio, lr), (display, dr), (lcd, lcr), (oled, orr)))

if __name__ == '__main__':
    main()
