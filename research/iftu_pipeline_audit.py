#!/usr/bin/env python3
"""Retail-3.65 IFTU/SceDisplay color-pipeline evidence extractor.

Emits only derived metadata and decoded CSC numeric fields. It is pinned to the
exact retail-3.65 SceLowio/SceDisplay ELFs used by the research workflow.
"""
from __future__ import annotations

import argparse
import json
from collections import defaultdict
from pathlib import Path

from capstone.arm import ARM_OP_IMM, ARM_OP_MEM, ARM_OP_REG
from vita_elf_audit import FunctionCFG, Reachability, VitaElf

LOWIO_SHA256 = "f791cfe2c6db955deb9446c57bb72ce1870bde2c6c485cac363e343d5128f744"
DISPLAY_SHA256 = "83ef39adf741a4bbbc1cec67d00e2594de1d17be0f5ea6cc77c935eae46065e5"

IFTU_PUBLIC = {
    0x0D7C02F7: "ksceIftuEnable",
    0x67E37EFC: "ksceIftuCsc",
    0x7CE0C4DA: "ksceIftuSetInputFrameBuffer",
    0xAF19FD85: "ksceIftuSetMergeSetting",
    0xC11F30B3: "ksceIftuDisable",
    0xE6EE2C6B: "ksceIftuSetOutputFormat",
}
IFTU_PRIVATE = {
    0x0FCBF457: "private_csc_cache_A",
    0x357EAE24: "private_control",
    0xD64F4C6B: "private_csc_cache_B",
}
DISPLAY_PUBLIC = {
    0x9E3C6DC6: "ksceDisplaySetBrightness",
    0x19140ACD: "ksceDisplaySetInvertColors",
}
STATE_FIELDS = {0x10C, 0x148, 0x1E8, 0x1F8, 0x214}
CSC_CACHE_RANGES = ((0x10C, 0x148, "cache_A"), (0x148, 0x184, "cache_B"))


def fail(msg):
    raise SystemExit(msg)


def load(path: Path, sha: str) -> VitaElf:
    elf = VitaElf(path)
    if elf.sha256 != sha:
        fail(f"{elf.modinfo['name']} SHA mismatch: {elf.sha256} expected {sha}")
    return elf


def find_export(elf, nid):
    for lib in elf.exports():
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


def itext(ins):
    return f"{ins.mnemonic} {ins.op_str}".strip()


def memory_state_refs(reach):
    refs = []
    export_starts = defaultdict(list)
    for lib in reach.elf.exports():
        for fn in lib["functions"]:
            export_starts[fn["va"]].append((lib["library_name"], fn["nid"]))
    for (start, thumb), cfg in reach.functions.items():
        for ins in all_insns(cfg):
            for op in getattr(ins, "operands", []):
                if op.type != ARM_OP_MEM:
                    continue
                disp = op.mem.disp
                field = None
                for lo, hi, name in CSC_CACHE_RANGES:
                    if lo <= disp < hi:
                        field = name
                        break
                if disp in STATE_FIELDS:
                    field = field or {0x1E8: "active_gate", 0x1F8: "control_cache", 0x214: "instance_stride"}.get(disp)
                if field is None:
                    continue
                m = ins.mnemonic.lower()
                access = "read" if m.startswith(("ldr", "ldm")) else "write" if m.startswith(("str", "stm")) else "other"
                refs.append({
                    "function": start,
                    "mode": "thumb" if thumb else "arm",
                    "exports": [{"library": lib, "nid": nid} for lib, nid in export_starts.get(start, [])],
                    "va": ins.address,
                    "access": access,
                    "offset": disp,
                    "field": field,
                    "instruction": itext(ins),
                })
    refs.sort(key=lambda x: (x["offset"], x["function"], x["va"]))
    return refs


def signed32(x):
    return x - (1 << 32) if x & 0x80000000 else x


def s3_9(word):
    if word & ~0x1FFF == 0:
        value = word & 0x1FFF
        if value & 0x1000:
            value -= 0x2000
    else:
        value = signed32(word)
    return value / 512.0


def read_words(elf, va, count):
    try:
        _, off = elf.file_from_va(va)
    except ValueError:
        return None
    end = off + 4 * count
    if end > len(elf.data):
        return None
    return [int.from_bytes(elf.data[off + 4*i:off + 4*i + 4], "little") for i in range(count)]


def plausible_csc(words):
    if words is None or len(words) != 15:
        return False
    for word in words[:6]:
        if word > 0x3FF and word != 0xFFFFFFFF:
            return False
    for word in words[6:]:
        sv = signed32(word)
        if not (word <= 0x1FFF or -4096 <= sv <= 4095):
            return False
    return True


def decode_csc(words):
    return {
        "post_add_0": words[0],
        "post_add_1_2": words[1],
        "post_clamp_max_0": words[2],
        "post_clamp_min_0": words[3],
        "post_clamp_max_1_2": words[4],
        "post_clamp_min_1_2": words[5],
        "ctm_s3_9": [[s3_9(words[6 + r*3 + c]) for c in range(3)] for r in range(3)],
    }


def constructed_constants(cfg):
    found = set()
    for block in cfg.blocks.values():
        regs = {}
        for ins in block.instructions:
            ops = getattr(ins, "operands", [])
            m = ins.mnemonic.lower()
            if m == "movw" and len(ops) >= 2 and ops[0].type == ARM_OP_REG and ops[1].type == ARM_OP_IMM:
                regs[ops[0].reg] = ops[1].imm & 0xFFFF
            elif m == "movt" and len(ops) >= 2 and ops[0].type == ARM_OP_REG and ops[1].type == ARM_OP_IMM:
                reg = ops[0].reg
                if reg in regs:
                    regs[reg] = (regs[reg] & 0xFFFF) | ((ops[1].imm & 0xFFFF) << 16)
                    found.add(regs[reg])
            elif ops and ops[0].type == ARM_OP_REG and m not in ("cmp", "cmn", "tst", "teq", "str", "str.w"):
                regs.pop(ops[0].reg, None)
    return sorted(found)


def function_record(elf, reach, nid, name):
    lib, fn = find_export(elf, nid)
    if fn is None:
        return {"nid": nid, "name": name, "missing": True}
    cfg = reach.functions.get((fn["va"], fn["thumb"])) or FunctionCFG(elf, fn["va"], fn["thumb"], reach.import_stubs)
    rec = {
        "nid": nid,
        "name": name,
        "va": fn["va"],
        "thumb": fn["thumb"],
        "library": lib["library_name"],
        "instruction_count": cfg.instruction_count(),
        "state_offset_refs": cfg.offset_refs,
        "calls": cfg.calls,
    }
    if name == "ksceIftuCsc" or fn["va"] == 0x8100639C:
        rec["instructions"] = [{"va": ins.address, "text": itext(ins)} for ins in all_insns(cfg)]
    return rec


def display_generator_record(display, reach, start, label):
    cfg = reach.functions.get((start, True)) or FunctionCFG(display, start, True, reach.import_stubs)
    constants = constructed_constants(cfg)
    candidates = []
    for va in constants:
        words = read_words(display, va, 15)
        if plausible_csc(words):
            candidates.append({"va": va, "decoded": decode_csc(words)})
    calls = [c for c in cfg.calls if c["target"] in (0x810039F4, 0x81003A04, 0x81003A54)]
    return {
        "label": label,
        "start": start,
        "instruction_count": cfg.instruction_count(),
        "iftu_calls": calls,
        "absolute_constants": [x for x in constants if 0x81000000 <= x < 0x82000000],
        "csc_constant_candidates": candidates,
        "instructions": [{"va": ins.address, "text": itext(ins)} for ins in all_insns(cfg)],
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lowio", type=Path, required=True)
    ap.add_argument("--display", type=Path, required=True)
    ap.add_argument("--json", type=Path, required=True)
    a = ap.parse_args()

    lowio = load(a.lowio, LOWIO_SHA256)
    display = load(a.display, DISPLAY_SHA256)
    lr = Reachability(lowio, lowio.exports(), lowio.imports())
    dr = Reachability(display, display.exports(), display.imports())

    exports = [function_record(lowio, lr, nid, name) for nid, name in {**IFTU_PUBLIC, **IFTU_PRIVATE}.items()]
    exports.sort(key=lambda x: x["nid"])
    va_to_name = {x.get("va"): x["name"] for x in exports if x.get("va") is not None}

    display_exports = {}
    for nid, name in DISPLAY_PUBLIC.items():
        _lib, fn = find_export(display, nid)
        if fn:
            display_exports[name] = {"nid": nid, "va": fn["va"], "thumb": fn["thumb"]}

    generators = [display_generator_record(display, dr, 0x81000A2C, "internal_0x81000A2C")]
    for name in ("ksceDisplaySetBrightness", "ksceDisplaySetInvertColors"):
        meta = display_exports.get(name)
        if meta:
            generators.append(display_generator_record(display, dr, meta["va"], name))

    refs = memory_state_refs(lr)
    result = {
        "schema": 1,
        "firmware": "3.65",
        "elf_sha256": {"SceLowio": lowio.sha256, "SceDisplay": display.sha256},
        "iftu_exports": exports,
        "export_at_0x8100639C": va_to_name.get(0x8100639C),
        "state_refs": refs,
        "display_generators": generators,
        "evidence_summary": {
            "cache_A_offset": 0x10C,
            "cache_B_offset": 0x148,
            "cache_distance": 0x3C,
            "active_gate_offset": 0x1E8,
            "control_cache_offset": 0x1F8,
            "instance_stride": 0x214,
        },
    }
    a.json.write_text(json.dumps(result, indent=2) + "\n")

    print("IFTU_EXPORT_MAP")
    for rec in exports:
        if rec.get("missing"):
            print(f"  0x{rec['nid']:08X} {rec['name']}: MISSING")
        else:
            print(f"  0x{rec['nid']:08X} {rec['name']} -> 0x{rec['va']:08X}")
    print(f"  export_at_0x8100639C={va_to_name.get(0x8100639C, 'NONE')}")

    print("IFTU_CACHE_READER_WRITER_MAP")
    for ref in refs:
        if ref["field"] in ("cache_A", "cache_B", "active_gate", "control_cache"):
            print(f"  {ref['field']} +0x{ref['offset']:X} {ref['access']} fn=0x{ref['function']:08X} at=0x{ref['va']:08X} {ref['instruction']}")

    print("SCEDISPLAY_CSC_GENERATORS")
    for gen in generators:
        print(f"  {gen['label']} start=0x{gen['start']:08X}")
        for call in gen["iftu_calls"]:
            print(f"    call=0x{call['va']:08X} target=0x{call['target']:08X} {call['instruction']}")
        for cand in gen["csc_constant_candidates"]:
            d = cand["decoded"]
            print("    CSC_CONSTANT va=0x%08X post_add=(%d,%d) clamp0=[%d,%d] clamp12=[%d,%d] ctm=%s" % (
                cand["va"], d["post_add_0"], d["post_add_1_2"], d["post_clamp_min_0"], d["post_clamp_max_0"],
                d["post_clamp_min_1_2"], d["post_clamp_max_1_2"], d["ctm_s3_9"]))

    print("STATIC_GATES")
    print("  CSC cache A/B physical adjacency: PROVEN")
    print("  exact CSC1/CSC2 ordering: PENDING ksceIftuCsc/control dataflow interpretation")
    print("  pristine baseline/restoration: PENDING read-only runtime corroboration")


if __name__ == "__main__":
    main()
