#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path

from trace_abi_audit import CSC_A, CSC_B, WRITER, READER, READ_CALLER, cfg
from vita_elf_audit import VitaElf

EXPECTED = {
    "lowio": "f791cfe2c6db955deb9446c57bb72ce1870bde2c6c485cac363e343d5128f744",
    "lcd": "24752bb4c69f0c241701cab0364cde9cbe5c95128b6bfe02bf3263aad524246e",
    "display": "83ef39adf741a4bbbc1cec67d00e2594de1d17be0f5ea6cc77c935eae46065e5",
}


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def segment_layout(elf: VitaElf):
    out = []
    for i, p in enumerate(elf.phdrs):
        out.append({
            "segment": i,
            "vaddr": f"0x{p.p_vaddr:08X}",
            "file_offset": f"0x{p.p_offset:X}",
            "filesz": f"0x{p.p_filesz:X}",
            "memsz": f"0x{getattr(p, 'p_memsz', p.p_filesz):X}",
            "flags": int(getattr(p, "p_flags", 0)),
        })
    return out


def compact_target(name: str, elf: VitaElf, va: int):
    c = cfg(elf, va)
    compact = c.compact(blocks=True)
    return {
        "name": name,
        "tested_function_va": f"0x{va:08X}",
        "function_identity": {
            "cfg_start": f"0x{c.start:08X}",
            "logical_end": f"0x{c.logical_end:08X}",
            "boundary_sources": compact.get("boundary_sources", []),
            "decode_failures": [f"0x{x:08X}" for x in compact.get("decode_failures", [])],
        },
        "decoded_basic_blocks": compact.get("blocks", []),
        "actual_dataflow_evidence": [
            insn
            for block in compact.get("blocks", [])
            for insn in block.get("instructions", [])
        ],
    }


def parse_legacy(text: str):
    failure = None
    m = re.search(
        r"failed_invariant:\s*\n"
        r"\s*function:\s*(?P<function>[^\n]+)\n"
        r"\s*expected_va:\s*(?P<va>[^\n]+)\n"
        r"\s*expected_semantic:\s*(?P<semantic>[^\n]+)\n"
        r"\s*expected_instruction_string:\s*(?P<expected>[^\n]+)\n"
        r"\s*actual_decoded_instruction:\s*(?P<actual>[^\n]+)",
        text,
    )
    if m:
        failure = {k: v.strip() for k, v in m.groupdict().items()}
    return failure


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--lowio", type=Path, required=True)
    ap.add_argument("--lcd", type=Path, required=True)
    ap.add_argument("--display", type=Path, required=True)
    ap.add_argument("--legacy-output", type=Path, required=True)
    ap.add_argument("--legacy-status", type=int, required=True)
    ap.add_argument("--workflow-commit", required=True)
    ap.add_argument("--workflow-file", type=Path, required=True)
    ap.add_argument("--analyzer", type=Path, required=True)
    ap.add_argument("--analyzer-git-blob", required=True)
    ap.add_argument("--json", type=Path, required=True)
    ap.add_argument("--text", type=Path, required=True)
    a = ap.parse_args()

    module_paths = {"lowio": a.lowio, "lcd": a.lcd, "display": a.display}
    modules = {}
    elves = {}
    for name, path in module_paths.items():
        digest = sha256(path)
        if digest != EXPECTED[name]:
            raise SystemExit(f"{name} module hash drift: {digest}")
        e = VitaElf(path)
        elves[name] = e
        modules[name] = {
            "sha256": digest,
            "segment_layout": segment_layout(e),
        }

    targets = [
        compact_target("csc_a", elves["lowio"], CSC_A),
        compact_target("csc_b", elves["lowio"], CSC_B),
        compact_target("panel_writer", elves["lcd"], WRITER),
        compact_target("panel_reader", elves["lcd"], READER),
        compact_target("panel_reader_known_caller", elves["lcd"], READ_CALLER),
    ]

    legacy_text = a.legacy_output.read_text(errors="replace")
    failed = parse_legacy(legacy_text)
    doc = {
        "schema": 1,
        "purpose": "forensic analyzer provenance; no Sony-semantic assertion changes",
        "workflow_commit": a.workflow_commit,
        "analyzer": {
            "path": str(a.analyzer),
            "sha256": sha256(a.analyzer),
            "git_object_identity": a.analyzer_git_blob,
        },
        "workflow_sha256": sha256(a.workflow_file),
        "modules": modules,
        "legacy_analyzer_exit_status": a.legacy_status,
        "failed_invariant": failed,
        "tested_functions": targets,
    }
    a.json.write_text(json.dumps(doc, indent=2) + "\n")

    lines = [
        "GATE0_ABI_PROVENANCE_DIAGNOSTIC",
        f"workflow_commit: {a.workflow_commit}",
        f"analyzer_sha256: {doc['analyzer']['sha256']}",
        f"analyzer_git_object: {a.analyzer_git_blob}",
        f"workflow_sha256: {doc['workflow_sha256']}",
        f"lcd_sha256: {modules['lcd']['sha256']}",
        f"display_sha256: {modules['display']['sha256']}",
        f"lowio_sha256: {modules['lowio']['sha256']}",
        f"legacy_analyzer_exit_status: {a.legacy_status}",
    ]
    if failed:
        lines.extend([
            f"failed_function: {failed['function']}",
            f"failed_va: {failed['va']}",
            f"failed_invariant: {failed['semantic']}",
            f"expected_instruction: {failed['expected']}",
            f"actual_instruction: {failed['actual']}",
        ])
    else:
        lines.append("failed_invariant: NONE_REPORTED")
    for t in targets:
        lines.append(f"function {t['name']} {t['tested_function_va']} decoded_blocks={len(t['decoded_basic_blocks'])}")
        for block in t["decoded_basic_blocks"]:
            lines.append(f"  block 0x{block['start']:08X} successors={block['successors']}")
            lines.extend("    " + x for x in block["instructions"])
    a.text.write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
