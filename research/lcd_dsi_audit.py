#!/usr/bin/env python3
"""Retail-3.65 SceLcd -> SceDsi command/dataflow audit.

The output is derived semantic evidence: export/import maps, reachable DSI call
sites, statically-resolved argument values, and focused control-flow for the
color-space/DDB functions. Unknown dynamic arguments remain UNKNOWN.
"""
from __future__ import annotations

import argparse
import json
from collections import defaultdict, deque
from pathlib import Path

from capstone.arm import ARM_OP_IMM, ARM_OP_MEM, ARM_OP_REG, ARM_REG_PC
from vita_elf_audit import FunctionCFG, Reachability, VitaElf, immediate_target, is_call

LCD_SHA256 = "24752bb4c69f0c241701cab0364cde9cbe5c95128b6bfe02bf3263aad524246e"
LCD_EXPORTS = {
    0x1A0A7519: "ksceLcdDisplayOff",
    0x5F4124AB: "ksceLcdDisplayOn",
    0x3A6D6AC3: "ksceLcdGetBrightness",
    0xE03E120B: "ksceLcdGetDDB",
    0x17F66722: "ksceLcdGetDisplayColorSpaceMode",
    0x581D3A87: "ksceLcdSetBrightness",
    0xD40968FB: "ksceLcdSetDisplayColorSpaceMode",
    0x0C7E03D8: "ksceLcdWaitReady",
}
DSI_IMPORTS = {
    0x3FB0DF1F: "ksceDsiDcsRead",
    0xBA6BC89F: "ksceDsiDcsShortWrite",
    0x114D1413: "ksceDsiDisableHead",
    0x5BE5AA9B: "ksceDsiEnableHead",
    0x98120684: "ksceDsiGenericReadRequest",
    0x89C00D2F: "ksceDsiGenericShortWrite",
    0x4DF9E924: "ksceDsiGetPixelClock",
    0xB3A70C05: "ksceDsiGetVicResolution",
    0x7640F607: "ksceDsiSendBlankingPacket",
    0x78E6E3CF: "ksceDsiSetLanesAndPixelSize",
    0x97BFEA76: "ksceDsiSetVic",
    0xC2E85919: "ksceDsiStartDisplay",
}
ARG_REG_NAMES = ("r0", "r1", "r2", "r3")


def fail(msg):
    raise SystemExit(msg)


def find_export(elf, nid):
    for lib in elf.exports():
        for fn in lib["functions"]:
            if fn["nid"] == nid:
                return lib, fn
    return None, None


def find_import(elf, nid):
    for lib in elf.imports():
        for fn in lib["functions"]:
            if fn["nid"] == nid:
                return lib, fn
    return None, None


def all_insns(cfg):
    out = {}
    for block in cfg.blocks.values():
        for ins in block.instructions:
            out[ins.address] = ins
    return [out[k] for k in sorted(out)]


def read_u32_va(elf, va):
    try:
        _, off = elf.file_from_va(va)
    except ValueError:
        return None
    if off + 4 > len(elf.data):
        return None
    return int.from_bytes(elf.data[off:off+4], "little")


def update_constants(elf, regs, ins):
    ops = getattr(ins, "operands", [])
    m = ins.mnemonic.lower()
    if not ops:
        return
    dest = ops[0].reg if ops[0].type == ARM_OP_REG else None

    if m in ("mov", "movs", "mov.w") and dest is not None and len(ops) >= 2:
        if ops[1].type == ARM_OP_IMM:
            regs[dest] = ops[1].imm & 0xFFFFFFFF
        elif ops[1].type == ARM_OP_REG and ops[1].reg in regs:
            regs[dest] = regs[ops[1].reg]
        else:
            regs.pop(dest, None)
        return
    if m == "movw" and dest is not None and len(ops) >= 2 and ops[1].type == ARM_OP_IMM:
        regs[dest] = ops[1].imm & 0xFFFF
        return
    if m == "movt" and dest is not None and len(ops) >= 2 and ops[1].type == ARM_OP_IMM:
        low = regs.get(dest, 0) & 0xFFFF
        regs[dest] = low | ((ops[1].imm & 0xFFFF) << 16)
        return
    if m.startswith("ldr") and dest is not None and len(ops) >= 2 and ops[1].type == ARM_OP_MEM and ops[1].mem.base == ARM_REG_PC:
        literal = ((ins.address + 4) & ~3) + ops[1].mem.disp
        value = read_u32_va(elf, literal)
        if value is None:
            regs.pop(dest, None)
        else:
            regs[dest] = value
        return
    if m in ("add", "adds", "add.w", "sub", "subs", "sub.w") and dest is not None and len(ops) >= 3:
        if ops[1].type == ARM_OP_REG and ops[1].reg in regs and ops[2].type == ARM_OP_IMM:
            base = regs[ops[1].reg]
            imm = ops[2].imm
            regs[dest] = (base + imm if m.startswith("add") else base - imm) & 0xFFFFFFFF
        else:
            regs.pop(dest, None)
        return
    if m in ("uxth", "uxtb") and dest is not None and len(ops) >= 2 and ops[1].type == ARM_OP_REG and ops[1].reg in regs:
        regs[dest] = regs[ops[1].reg] & (0xFFFF if m == "uxth" else 0xFF)
        return
    if m == "eor" and dest is not None and len(ops) >= 3 and ops[1].type == ARM_OP_REG and ops[2].type == ARM_OP_REG and ops[1].reg == ops[2].reg:
        regs[dest] = 0
        return

    # Calls can clobber r0-r3; caller-saved registers are invalid after call.
    if is_call(ins):
        for reg in list(regs):
            name = ins.reg_name(reg)
            if name in ARG_REG_NAMES:
                regs.pop(reg, None)
        return

    # Conservative destination invalidation for other register-writing ops.
    if dest is not None and m not in ("cmp", "cmn", "tst", "teq", "str", "str.w", "stm", "stm.w"):
        regs.pop(dest, None)


def args_before_call(elf, block, call_va):
    regs = {}
    snapshot = {}
    for ins in block.instructions:
        if ins.address == call_va:
            for name in ARG_REG_NAMES:
                reg = next((r for r in regs if ins.reg_name(r) == name), None)
                snapshot[name] = regs.get(reg) if reg is not None else None
            return snapshot
        update_constants(elf, regs, ins)
    return {name: None for name in ARG_REG_NAMES}


def export_reachability(elf, reach, exports):
    owners = defaultdict(set)
    for name, meta in exports.items():
        root = (meta["va"], meta["thumb"])
        q = deque([root])
        seen = set()
        while q:
            key = q.popleft()
            if key in seen:
                continue
            seen.add(key)
            owners[key[0]].add(name)
            cfg = reach.functions.get(key)
            if cfg is None:
                continue
            for child in cfg.direct_callees:
                q.append(child)
    return owners


def call_records(elf, reach, import_stubs, owners):
    records = []
    for (start, thumb), cfg in reach.functions.items():
        for block in cfg.blocks.values():
            for ins in block.instructions:
                if not is_call(ins):
                    continue
                target = immediate_target(ins)
                if target is None:
                    continue
                target &= ~1
                info = import_stubs.get(target)
                if info is None:
                    continue
                records.append({
                    "public_callers": sorted(owners.get(start, [])),
                    "function": start,
                    "mode": "thumb" if thumb else "arm",
                    "call_va": ins.address,
                    "import_nid": info["nid"],
                    "import_name": info["name"],
                    "args": args_before_call(elf, block, ins.address),
                    "instruction": f"{ins.mnemonic} {ins.op_str}".strip(),
                })
    records.sort(key=lambda x: x["call_va"])
    return records


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lcd", type=Path, required=True)
    ap.add_argument("--json", type=Path, required=True)
    a = ap.parse_args()

    elf = VitaElf(a.lcd)
    if elf.sha256 != LCD_SHA256:
        fail(f"SceLcd SHA mismatch: {elf.sha256} expected {LCD_SHA256}")
    reach = Reachability(elf, elf.exports(), elf.imports())

    exports = {}
    export_rows = []
    for nid, name in LCD_EXPORTS.items():
        lib, fn = find_export(elf, nid)
        if fn is None:
            fail(f"SceLcd export missing: {name} 0x{nid:08X}")
        meta = {"nid": nid, "name": name, "va": fn["va"], "thumb": fn["thumb"], "library": lib["library_name"]}
        exports[name] = meta
        export_rows.append(meta)

    import_stubs = {}
    import_rows = []
    for nid, name in DSI_IMPORTS.items():
        lib, fn = find_import(elf, nid)
        if fn is None:
            continue
        row = {"nid": nid, "name": name, "va": fn["va"], "thumb": fn["thumb"], "library": lib["library_name"]}
        import_rows.append(row)
        if fn["va"]:
            import_stubs[fn["va"]] = row

    owners = export_reachability(elf, reach, exports)
    calls = call_records(elf, reach, import_stubs, owners)

    focus = {}
    for name in ("ksceLcdSetDisplayColorSpaceMode", "ksceLcdGetDisplayColorSpaceMode", "ksceLcdGetDDB", "ksceLcdSetBrightness", "ksceLcdDisplayOn", "ksceLcdDisplayOff"):
        meta = exports[name]
        cfg = reach.functions.get((meta["va"], meta["thumb"])) or FunctionCFG(elf, meta["va"], meta["thumb"], reach.import_stubs)
        focus[name] = {
            "entry": meta["va"],
            "instructions": [{"va": ins.address, "text": f"{ins.mnemonic} {ins.op_str}".strip()} for ins in all_insns(cfg)],
            "dsi_calls": [c for c in calls if name in c["public_callers"]],
        }

    dcs_writes = [c for c in calls if c["import_name"] == "ksceDsiDcsShortWrite"]
    dcs_reads = [c for c in calls if c["import_name"] == "ksceDsiDcsRead"]
    generic_writes = [c for c in calls if c["import_name"] == "ksceDsiGenericShortWrite"]
    generic_reads = [c for c in calls if c["import_name"] == "ksceDsiGenericReadRequest"]
    gamma_calls = [c for c in dcs_writes if c["args"].get("r1") == 0x26]
    unresolved_dcs_commands = [c for c in dcs_writes if c["args"].get("r1") is None]

    result = {
        "schema": 1,
        "firmware": "3.65",
        "elf_sha256": elf.sha256,
        "exports": sorted(export_rows, key=lambda x: x["nid"]),
        "dsi_imports": sorted(import_rows, key=lambda x: x["nid"]),
        "dsi_calls": calls,
        "focus": focus,
        "command_summary": {
            "dcs_short_write_count": len(dcs_writes),
            "dcs_read_count": len(dcs_reads),
            "generic_short_write_count": len(generic_writes),
            "generic_read_count": len(generic_reads),
            "resolved_dcs_commands": sorted({c["args"]["r1"] for c in dcs_writes if c["args"].get("r1") is not None}),
            "standard_set_gamma_curve_calls": gamma_calls,
            "unresolved_dcs_command_calls": unresolved_dcs_commands,
        },
    }
    a.json.write_text(json.dumps(result, indent=2) + "\n")

    print("SCELCD_EXPORT_MAP")
    for row in sorted(export_rows, key=lambda x: x["va"]):
        print(f"  0x{row['nid']:08X} {row['name']} -> 0x{row['va']:08X}")

    print("SCELCD_DSI_IMPORT_MAP")
    for row in sorted(import_rows, key=lambda x: x["nid"]):
        print(f"  0x{row['nid']:08X} {row['name']} stub=0x{row['va']:08X}")

    print("REACHABLE_DSI_CALLS")
    for call in calls:
        args = " ".join(f"{name}={'UNKNOWN' if call['args'][name] is None else hex(call['args'][name])}" for name in ARG_REG_NAMES)
        roots = ",".join(call["public_callers"]) or "internal-only"
        print(f"  call=0x{call['call_va']:08X} {call['import_name']} roots={roots} {args}")

    print("NONLINEAR_EVIDENCE_MATRIX")
    if gamma_calls:
        print(f"  standard DCS 0x26 set_gamma_curve: OBSERVED count={len(gamma_calls)}")
    elif unresolved_dcs_commands:
        print("  standard DCS 0x26 set_gamma_curve: NOT RESOLVED; one or more DCS command arguments remain dynamic")
    else:
        print("  standard DCS 0x26 set_gamma_curve: NOT OBSERVED in complete reachable DCS short-write set")
    print(f"  DCS read calls={len(dcs_reads)} generic read calls={len(generic_reads)}")
    print("  vendor gamma programming: UNCLASSIFIED until command-sequence semantics are reviewed")
    print("  controller identity: UNKNOWN until observed/read ID path is semantically proven")


if __name__ == "__main__":
    main()
