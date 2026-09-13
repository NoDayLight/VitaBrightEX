#!/usr/bin/env python3
"""Exact retail-3.65 assertions for the private IFTU CSC path.

This is intentionally image-specific.  It does not claim generic function-boundary
recovery: it validates facts already recovered from the pinned retail 3.65 ELFs,
the pinned VitaSDK header revision, and pinned legacy v1.3 source.
"""
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

from capstone.arm import ARM_OP_IMM, ARM_OP_MEM, ARM_OP_REG
from vita_elf_audit import FunctionCFG, Reachability, VitaElf

LOWIO_SHA256 = "f791cfe2c6db955deb9446c57bb72ce1870bde2c6c485cac363e343d5128f744"
DISPLAY_SHA256 = "83ef39adf741a4bbbc1cec67d00e2594de1d17be0f5ea6cc77c935eae46065e5"
SET1_NID = 0x0FCBF457
SET2_NID = 0xD64F4C6B
CONV_NID = 0x357EAE24
INVALID_PLANE = 0x803F0700
EXPECTED_CALLS = {
    SET1_NID: {0x81000AF2, 0x81002E02, 0x8100317A},
    CONV_NID: {0x81002C40, 0x81002C48},
    SET2_NID: {0x81000AAA, 0x81002D7C, 0x810030F4},
}
CANDIDATE_ONLY = {0x810019B6, 0x810019BE}


def fail(msg: str) -> None:
    raise SystemExit(msg)


def find_export(elf: VitaElf, nid: int):
    for lib in elf.exports():
        for fn in lib["functions"]:
            if fn["nid"] == nid:
                return lib, fn
    fail("export 0x%08X missing" % nid)


def cfg_for(elf: VitaElf, nid: int):
    _lib, fn = find_export(elf, nid)
    reach = Reachability(elf, elf.exports(), elf.imports())
    return fn, reach.functions.get((fn["va"], fn["thumb"])) or FunctionCFG(
        elf, fn["va"], fn["thumb"], reach.import_stubs
    )


def instructions(cfg):
    by = {}
    for block in cfg.blocks.values():
        for ins in block.instructions:
            by[ins.address] = ins
    return [by[k] for k in sorted(by)]


def text(ins) -> str:
    return (ins.mnemonic + " " + ins.op_str).strip().lower().replace(" ", "")


def assert_header(header: str) -> None:
    if "SCE_IFTU_ERROR_INVALID_PLANE" not in header or "0x803F0700" not in header:
        fail("pinned header invalid-plane mapping missing")
    body = re.search(r"typedef\s+struct\s+SceIftuCscParams\s*\{(?P<body>.*?)\}\s*SceIftuCscParams\s*;", header, re.S)
    if not body:
        fail("SceIftuCscParams missing from pinned header")
    fields = body.group("body")
    for name in (
        "post_add_0", "post_add_1_2", "post_clamp_max_0", "post_clamp_min_0",
        "post_clamp_max_1_2", "post_clamp_min_1_2", "ctm[3][3]",
    ):
        if name not in fields:
            fail("SceIftuCscParams field missing: " + name)
    if "VITASDK_BUILD_ASSERT_EQ(0x3C, SceIftuCscParams);" not in header:
        fail("pinned header does not assert SceIftuCscParams == 0x3C")
    conv = re.search(r"typedef\s+struct\s+SceIftuConvParams\s*\{(?P<body>.*?)\}\s*SceIftuConvParams\s*;", header, re.S)
    if not conv:
        fail("SceIftuConvParams missing")
    for name in ("csc_params1", "csc_params2", "csc_control"):
        if name not in conv.group("body"):
            fail("SceIftuConvParams field missing: " + name)


def assert_legacy_layout(legacy: str) -> None:
    m = re.search(r"typedef\s+struct\s*\{(?P<body>.*?)\}\s*SceIftuCscParams\s*;", legacy, re.S)
    if not m:
        fail("legacy SceIftuCscParams missing")
    body = m.group("body")
    # Six leading 32-bit words and nine explicit matrix words = 15 words = 0x3C.
    leading = len(re.findall(r"uint32_t\s+unk(?:00|04|08|0C|10|14)\s*;", body))
    matrix_names = re.findall(r"csc_(?:rr|rg|rb|gr|gg|gb|br|bg|bb)", body)
    if leading != 6 or len(set(matrix_names)) != 9:
        fail("legacy CSC layout is not six leading words + nine matrix words")
    if "ksceIftuSetCscParams)(int head, int fb_idx" not in legacy.replace("\n", " ").replace("  ", " "):
        # Keep the ABI assertion tolerant of whitespace while still proving the legacy shape.
        if not re.search(r"ksceIftuSetCscParams\)\(int\s+head,\s*int\s+fb_idx,\s*const\s+SceIftuCscParams\s*\*p\)", legacy, re.S):
            fail("legacy three-argument ABI declaration missing")


def assert_plane_and_3c_copy(lowio: VitaElf) -> None:
    fn, cfg = cfg_for(lowio, SET1_NID)
    ins = instructions(cfg)
    by = {x.address: x for x in ins}
    required = {
        0x81005D48: ("cmp", "r0,#4"),
        0x81005D4E: ("mov", "r4,r1"),
        0x81005D72: ("cbz", "r4,#0x81005db8"),
        0x81005D7A: ("add.w", "r0,r4,#0x30"),
    }
    for va, (mn, frag) in required.items():
        got = by.get(va)
        if got is None or got.mnemonic.lower() != mn or frag not in text(got):
            fail("retail 3.65 invariant mismatch at 0x%08X: %r" % (va, None if got is None else text(got)))

    # The bulk loop stops at source+0x30.  Retail code then consumes the three
    # trailing 32-bit words (+0x30,+0x34,+0x38), establishing a 0x3C object.
    tail = set()
    for x in ins:
        if not x.mnemonic.lower().startswith("ldr"):
            continue
        for op in getattr(x, "operands", []):
            if op.type == ARM_OP_MEM and op.mem.disp in (0x30, 0x34, 0x38):
                tail.add(op.mem.disp)
    if tail != {0x30, 0x34, 0x38}:
        fail("0x3C copy tail not proven; trailing source word offsets observed=%r" % sorted(tail))

    # cmp r0,#4 plus the dedicated invalid-plane return is image-specific proof
    # that r0 is the IFTU plane selector accepted for indices 0..4.
    # Find the split MOVW/MOVT materialization of 0x803F0700 anywhere in this CFG.
    reg_lo = {}
    constants = set()
    for x in ins:
        ops = getattr(x, "operands", [])
        m = x.mnemonic.lower()
        if m == "movw" and len(ops) >= 2 and ops[0].type == ARM_OP_REG and ops[1].type == ARM_OP_IMM:
            reg_lo[ops[0].reg] = ops[1].imm & 0xFFFF
        elif m == "movt" and len(ops) >= 2 and ops[0].type == ARM_OP_REG and ops[1].type == ARM_OP_IMM:
            r = ops[0].reg
            if r in reg_lo:
                constants.add(reg_lo[r] | ((ops[1].imm & 0xFFFF) << 16))
    if INVALID_PLANE not in constants:
        fail("retail setter does not materialize expected invalid-plane code 0x803F0700")

    # Exact adjacent cache bases.  +0x148 - +0x10C == 0x3C.
    offsets = set()
    for x in ins:
        for op in getattr(x, "operands", []):
            if op.type == ARM_OP_IMM and (op.imm & 0xFFFFFFFF) in (0x10C, 0x148):
                offsets.add(op.imm & 0xFFFFFFFF)
    # SET1 is expected to establish +0x10C. SET2 is checked separately for +0x148.
    if 0x10C not in offsets:
        fail("SET1 cached CSC base +0x10C missing")

    _fn2, cfg2 = cfg_for(lowio, SET2_NID)
    offsets2 = set()
    for x in instructions(cfg2):
        for op in getattr(x, "operands", []):
            if op.type == ARM_OP_IMM and (op.imm & 0xFFFFFFFF) in (0x10C, 0x148):
                offsets2.add(op.imm & 0xFFFFFFFF)
    if 0x148 not in offsets2:
        fail("SET2 cached CSC base +0x148 missing")

    print("retail setter plane semantics: STATICALLY PROVEN (r0 range 0..4 + INVALID_PLANE)")
    print("retail CSC object copy size: STATICALLY PROVEN 0x3C")
    print("cache bases: +0x10C and +0x148 observed; spacing exactly 0x3C")
    print("CSC1/CSC2 semantic naming: HYPOTHESIS, not promoted by spacing alone")


def assert_calls(audit_json: Path) -> None:
    records = json.loads(audit_json.read_text())
    display = next((x for x in records if x["module"] == "SceDisplay"), None)
    if display is None:
        fail("SceDisplay audit record missing")
    bynid = {x["nid"]: x for x in display["target_imports"]}
    for nid, expected in EXPECTED_CALLS.items():
        if nid not in bynid:
            fail("IFTU import 0x%08X missing" % nid)
        proven = {x["call_va"] for x in bynid[nid]["proven_callsites"]}
        if proven != expected:
            fail("IFTU import 0x%08X exact call-site mismatch: got=%r expected=%r" % (nid, sorted(proven), sorted(expected)))
        candidates = {x["call_va"] for x in bynid[nid]["candidate_xrefs"]}
        if CANDIDATE_ONLY & proven:
            fail("candidate-only legacy xref was promoted to proven call")
        print("0x%08X exact proven calls=%s" % (nid, ",".join("0x%08X" % x for x in sorted(proven))))
    all_proven = {x["call_va"] for item in bynid.values() for x in item.get("proven_callsites", [])}
    if CANDIDATE_ONLY & all_proven:
        fail("0x810019B6/0x810019BE must remain candidate-only")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--lowio", type=Path, required=True)
    ap.add_argument("--display", type=Path, required=True)
    ap.add_argument("--audit-json", type=Path, required=True)
    ap.add_argument("--iftu-header", type=Path, required=True)
    ap.add_argument("--legacy-screen-filter", type=Path, required=True)
    a = ap.parse_args()

    lowio = VitaElf(a.lowio)
    display = VitaElf(a.display)
    if lowio.sha256 != LOWIO_SHA256:
        fail("unexpected SceLowio SHA-256 " + lowio.sha256)
    if display.sha256 != DISPLAY_SHA256:
        fail("unexpected SceDisplay SHA-256 " + display.sha256)

    assert_header(a.iftu_header.read_text(encoding="utf-8"))
    assert_legacy_layout(a.legacy_screen_filter.read_text(encoding="utf-8"))
    assert_calls(a.audit_json)
    assert_plane_and_3c_copy(lowio)

    print("SceIftuCscParams layout: VERY STRONGLY CORROBORATED by exact 0x3C copy + pinned SDK layout + legacy 15-word layout")
    print("private NID semantic names/prototypes: NOT YET PROVEN")
    print("original-state acquisition/restoration/lifecycle: NOT YET PROVEN")
    print("production AFFINE_CSC gate: UNSUPPORTED until restoration ownership is proven")


if __name__ == "__main__":
    main()
