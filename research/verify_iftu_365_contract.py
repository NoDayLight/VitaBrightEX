#!/usr/bin/env python3
"""Exact retail-3.65 assertions for the private IFTU CSC path.

This is intentionally image-specific. It does not claim generic function-boundary
recovery: it validates facts recovered from the pinned retail 3.65 ELFs, the
pinned VitaSDK header revision, and pinned legacy v1.3 source.
"""
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

from vita_elf_audit import FunctionCFG, Reachability, VitaElf

LOWIO_SHA256 = "f791cfe2c6db955deb9446c57bb72ce1870bde2c6c485cac363e343d5128f744"
DISPLAY_SHA256 = "83ef39adf741a4bbbc1cec67d00e2594de1d17be0f5ea6cc77c935eae46065e5"
SET1_NID = 0x0FCBF457
SET2_NID = 0xD64F4C6B
CONV_NID = 0x357EAE24
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


def assert_instruction(by, va: int, mnemonic: str, operands: str) -> None:
    got = by.get(va)
    want = mnemonic.lower() + operands.lower()
    if got is None or text(got) != want:
        fail("retail 3.65 invariant mismatch at 0x%08X: got=%r want=%r" %
             (va, None if got is None else text(got), want))


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
    leading = len(re.findall(r"uint32_t\s+unk(?:00|04|08|0C|10|14)\s*;", body))
    matrix_names = re.findall(r"csc_(?:rr|rg|rb|gr|gg|gb|br|bg|bb)", body)
    if leading != 6 or len(set(matrix_names)) != 9:
        fail("legacy CSC layout is not six leading words + nine matrix words")
    if not re.search(r"ksceIftuSetCscParams\)\(int\s+head,\s*int\s+fb_idx,\s*const\s+SceIftuCscParams\s*\*p\)", legacy, re.S):
        fail("legacy three-argument ABI declaration missing")


def assert_setter(lowio: VitaElf, nid: int, expected: dict[int, tuple[str, str]]) -> None:
    _fn, cfg = cfg_for(lowio, nid)
    by = {x.address: x for x in instructions(cfg)}
    for va, (mnemonic, operands) in expected.items():
        assert_instruction(by, va, mnemonic, operands)


def assert_plane_and_3c_copy(lowio: VitaElf) -> None:
    set1 = {
        0x81005D48: ("cmp", "r0,#4"),
        0x81005D4E: ("mov", "r4,r1"),
        0x81005D72: ("cbz", "r4,#0x81005db8"),
        0x81005D74: ("mov", "r3,r4"),
        0x81005D76: ("add.w", "r2,r6,#0x10c"),
        0x81005D7A: ("add.w", "r0,r4,#0x30"),
        0x81005D7E: ("ldr.w", "sl,[r3]"),
        0x81005D82: ("adds", "r3,#0x10"),
        0x81005D88: ("adds", "r2,#0x10"),
        0x81005D92: ("cmp", "r3,r0"),
        0x81005DA4: ("bne", "#0x81005d7e"),
        0x81005DA6: ("ldr.w", "lr,[r3]"),
        0x81005DAA: ("ldr", "r0,[r3,#4]"),
        0x81005DAC: ("ldr", "r3,[r3,#8]"),
        0x81005DAE: ("str.w", "lr,[r2]"),
        0x81005DB2: ("str", "r0,[r2,#4]"),
        0x81005DB4: ("str", "r3,[r2,#8]"),
        0x81005DB8: ("add.w", "r4,r6,#0x10c"),
        0x81005E18: ("mov.w", "r0,#0x700"),
        0x81005E1C: ("movt", "r0,#0x803f"),
    }
    set2 = {
        0x81005E24: ("cmp", "r0,#4"),
        0x81005E2A: ("mov", "r4,r1"),
        0x81005E50: ("cbz", "r4,#0x81005e96"),
        0x81005E52: ("mov", "r3,r4"),
        0x81005E54: ("add.w", "r2,r7,#0x148"),
        0x81005E58: ("add.w", "r0,r4,#0x30"),
        0x81005E5C: ("ldr.w", "sl,[r3]"),
        0x81005E60: ("adds", "r3,#0x10"),
        0x81005E66: ("adds", "r2,#0x10"),
        0x81005E70: ("cmp", "r3,r0"),
        0x81005E82: ("bne", "#0x81005e5c"),
        0x81005E84: ("ldr.w", "lr,[r3]"),
        0x81005E88: ("ldr", "r0,[r3,#4]"),
        0x81005E8A: ("ldr", "r3,[r3,#8]"),
        0x81005E8C: ("str.w", "lr,[r2]"),
        0x81005E90: ("str", "r0,[r2,#4]"),
        0x81005E92: ("str", "r3,[r2,#8]"),
        0x81005E96: ("add.w", "r4,r7,#0x148"),
        0x81005F16: ("mov.w", "r0,#0x700"),
        0x81005F1A: ("movt", "r0,#0x803f"),
    }
    assert_setter(lowio, SET1_NID, set1)
    assert_setter(lowio, SET2_NID, set2)
    print("retail setter plane semantics: STATICALLY PROVEN (r0 0..4; r0>4 => 0x803F0700)")
    print("retail CSC object copy size: STATICALLY PROVEN 0x3C in both setters")
    print("cached objects: +0x10C and +0x148, adjacent by exactly 0x3C")
    print("NULL r1 selects the corresponding cached object; higher-level reapply semantics remain unproven")
    print("CSC1/CSC2 semantic naming: STRONG HYPOTHESIS, not promoted by adjacency alone")


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
            fail("IFTU import 0x%08X exact call-site mismatch: got=%r expected=%r" %
                 (nid, sorted(proven), sorted(expected)))
        if CANDIDATE_ONLY & proven:
            fail("candidate-only legacy xref was promoted to proven call")
        print("0x%08X exact proven calls=%s" %
              (nid, ",".join("0x%08X" % x for x in sorted(proven))))
    all_proven = {x["call_va"] for item in bynid.values()
                  for x in item.get("proven_callsites", [])}
    if CANDIDATE_ONLY & all_proven:
        fail("0x810019B6/0x810019BE must remain candidate-only")
    conv_candidates = {x["call_va"] for x in bynid[CONV_NID]["candidate_xrefs"]}
    if not CANDIDATE_ONLY.issubset(conv_candidates):
        fail("expected historical candidate xrefs disappeared: got=%r" % sorted(conv_candidates))


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

    print("SceIftuCscParams layout: VERY STRONGLY CORROBORATED by retail 0x3C copies + pinned SDK layout + legacy 15-word layout")
    print("private NID exact semantic names/prototypes: NOT YET PROVEN")
    print("original-state acquisition/restoration/power lifecycle: NOT YET PROVEN")
    print("production AFFINE_CSC gate: UNSUPPORTED")


if __name__ == "__main__":
    main()
