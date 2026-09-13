#!/usr/bin/env python3
"""Deterministic retail-3.65 IFTU nonlinear/table-upload audit.

This does not search for a specific LUT byte count. It walks the complete call
subgraph rooted at all nine IFTU exports and emits every loop/back-edge, table-
size immediate hint, and memory-stream shape as candidates for manual semantic
classification. Absence is reported only for this bounded proven graph.
"""
from __future__ import annotations

import argparse
import json
from collections import deque
from pathlib import Path

from capstone.arm import ARM_OP_IMM, ARM_OP_MEM, ARM_OP_REG
from vita_elf_audit import FunctionCFG, Reachability, VitaElf

LOWIO_SHA256 = "f791cfe2c6db955deb9446c57bb72ce1870bde2c6c485cac363e343d5128f744"
IFTU_NIDS = {
    0x0D7C02F7: "ksceIftuEnable",
    0x0FCBF457: "private_csc_cache_A",
    0x357EAE24: "private_control",
    0x67E37EFC: "ksceIftuCsc",
    0x7CE0C4DA: "ksceIftuSetInputFrameBuffer",
    0xAF19FD85: "ksceIftuSetMergeSetting",
    0xC11F30B3: "ksceIftuDisable",
    0xD64F4C6B: "private_csc_cache_B",
    0xE6EE2C6B: "ksceIftuSetOutputFormat",
}
SIZE_HINTS = {16,17,32,33,64,65,128,129,256,257,357,512,1024}


def find_export(elf, nid):
    for lib in elf.exports():
        for fn in lib["functions"]:
            if fn["nid"] == nid:
                return fn
    return None


def all_insns(cfg):
    d = {}
    for block in cfg.blocks.values():
        for ins in block.instructions:
            d[ins.address] = ins
    return [d[k] for k in sorted(d)]


def rooted_subgraph(reach, roots):
    q = deque(roots)
    seen = set()
    while q:
        key = q.popleft()
        if key in seen:
            continue
        cfg = reach.functions.get(key)
        if cfg is None:
            continue
        seen.add(key)
        for child in cfg.direct_callees:
            if child not in seen:
                q.append(child)
    return seen


def function_summary(cfg):
    hints = []
    mem = []
    pointer_steps = []
    for ins in all_insns(cfg):
        ops = getattr(ins, "operands", [])
        for op in ops:
            if op.type == ARM_OP_IMM and int(op.imm) in SIZE_HINTS:
                hints.append({"va": ins.address, "value": int(op.imm), "instruction": f"{ins.mnemonic} {ins.op_str}".strip()})
            if op.type == ARM_OP_MEM:
                mem.append({"va": ins.address, "mnemonic": ins.mnemonic, "disp": op.mem.disp, "writeback": bool(getattr(ins, "writeback", False))})
        m = ins.mnemonic.lower()
        if m.startswith(("add", "sub")) and len(ops) >= 2:
            for op in ops[1:]:
                if op.type == ARM_OP_IMM and 0 < abs(int(op.imm)) <= 16:
                    pointer_steps.append({"va": ins.address, "step": int(op.imm) * (1 if m.startswith("add") else -1), "instruction": f"{ins.mnemonic} {ins.op_str}".strip()})
    backedges = []
    for block in cfg.blocks.values():
        for succ in block.successors:
            if succ <= block.start:
                block_mem = []
                for ins in block.instructions:
                    if any(op.type == ARM_OP_MEM for op in getattr(ins, "operands", [])):
                        block_mem.append(f"{ins.mnemonic} {ins.op_str}".strip())
                backedges.append({"block": block.start, "target": succ, "memory_ops": block_mem})
    stream_score = len(backedges) + sum(1 for x in mem if x["writeback"]) + min(3, len(pointer_steps))
    return {
        "start": cfg.start,
        "mode": "thumb" if cfg.thumb else "arm",
        "instruction_count": cfg.instruction_count(),
        "calls": cfg.calls,
        "size_hints": hints,
        "backedges": backedges,
        "memory_op_count": len(mem),
        "writeback_memory_ops": [x for x in mem if x["writeback"]],
        "small_pointer_steps": pointer_steps,
        "table_stream_candidate_score": stream_score,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lowio", type=Path, required=True)
    ap.add_argument("--json", type=Path, required=True)
    a = ap.parse_args()
    elf = VitaElf(a.lowio)
    if elf.sha256 != LOWIO_SHA256:
        raise SystemExit(f"SceLowio SHA mismatch: {elf.sha256}")
    reach = Reachability(elf, elf.exports(), elf.imports())

    roots = []
    export_map = []
    for nid, name in IFTU_NIDS.items():
        fn = find_export(elf, nid)
        if fn is None:
            raise SystemExit(f"missing IFTU export {name}")
        key = (fn["va"], fn["thumb"])
        roots.append(key)
        export_map.append({"nid": nid, "name": name, "va": fn["va"], "thumb": fn["thumb"]})

    graph = rooted_subgraph(reach, roots)
    functions = [function_summary(reach.functions[key]) for key in sorted(graph)]
    candidates = [x for x in functions if x["backedges"] or x["size_hints"] or x["table_stream_candidate_score"] >= 3]
    candidates.sort(key=lambda x: (-x["table_stream_candidate_score"], x["start"]))

    result = {
        "schema": 1,
        "firmware": "3.65",
        "elf_sha256": elf.sha256,
        "iftu_exports": sorted(export_map, key=lambda x: x["nid"]),
        "rooted_reachable_function_count": len(graph),
        "functions": functions,
        "nonlinear_table_candidates": candidates,
        "audit_scope": "complete direct-call graph rooted at all nine retail SceLowio IFTU exports",
    }
    a.json.write_text(json.dumps(result, indent=2) + "\n")

    print("IFTU_NONLINEAR_AUDIT")
    print(f"  rooted reachable functions={len(graph)}")
    print(f"  loop/size/stream candidates={len(candidates)}")
    for c in candidates:
        print(f"  fn=0x{c['start']:08X} score={c['table_stream_candidate_score']} backedges={len(c['backedges'])} hints={[x['value'] for x in c['size_hints']]}")
        for edge in c["backedges"]:
            print(f"    loop block=0x{edge['block']:08X}->0x{edge['target']:08X} mem={edge['memory_ops'][:8]}")
    print("EVIDENCE_BOUNDARY")
    print("  A candidate is not a gamma/LUT path until register/dataflow semantics prove it.")
    print("  Empty candidate classification would apply only to this fully enumerated IFTU-rooted graph, not to the LCD panel controller.")


if __name__ == "__main__":
    main()
