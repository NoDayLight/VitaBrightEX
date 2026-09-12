#!/usr/bin/env python3
"""Verify VitaBrightEX's raw SceLcd brightness-table layout against a decrypted image.

The taiHEN injection offset is relative to module segment 0, not necessarily to
file offset 0. For a decrypted ELF this tool maps segment-0 + table_offset back
to the corresponding PT_LOAD file bytes before checking the exact stock table.
Encrypted SELF/SKPRX input is deliberately rejected: decrypt it first.
"""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import struct
import sys

STOCK = bytes((31, 37, 43, 50, 58, 67, 77, 88, 100, 114, 129, 147, 166, 182, 203, 227, 255))
OFFSETS = {
    "3.60": 0x1B00,
    "3.65": 0x1B48,
    "3.67": 0x1B48,
    "3.68": 0x1B48,
    "3.69": 0x1B48,
    "3.70": 0x1B48,
}
PT_LOAD = 1


def die(message: str) -> "NoReturn":
    print(f"ERROR: {message}", file=sys.stderr)
    raise SystemExit(1)


def elf_segment0_file_offset(data: bytes, relative_offset: int) -> tuple[int, dict[str, int]]:
    if len(data) < 52 or data[:4] != b"\x7fELF":
        die("input is not a decrypted ELF; use --raw-segment-0 for an actual segment dump")
    if data[4] != 1 or data[5] != 1:
        die("expected a 32-bit little-endian ARM ELF")

    e_phoff = struct.unpack_from("<I", data, 28)[0]
    e_phentsize = struct.unpack_from("<H", data, 42)[0]
    e_phnum = struct.unpack_from("<H", data, 44)[0]
    if e_phentsize < 32 or not e_phnum:
        die("ELF has no usable program-header table")

    loads: list[dict[str, int]] = []
    for index in range(e_phnum):
        off = e_phoff + index * e_phentsize
        if off + 32 > len(data):
            die("ELF program-header table is truncated")
        p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align = struct.unpack_from("<8I", data, off)
        if p_type == PT_LOAD:
            loads.append({
                "ph_index": index,
                "p_offset": p_offset,
                "p_vaddr": p_vaddr,
                "p_paddr": p_paddr,
                "p_filesz": p_filesz,
                "p_memsz": p_memsz,
                "p_flags": p_flags,
                "p_align": p_align,
            })

    if not loads:
        die("ELF contains no PT_LOAD segments")

    # taiHEN segment indices follow module load segments. Segment 0 is the first
    # loadable segment, so its raw injection offset maps to p_offset + offset.
    seg0 = loads[0]
    if relative_offset + len(STOCK) > seg0["p_memsz"]:
        die(f"segment-0 offset 0x{relative_offset:X} is outside p_memsz=0x{seg0['p_memsz']:X}")
    if relative_offset + len(STOCK) > seg0["p_filesz"]:
        die("target lies in segment-0 zero-fill/BSS and cannot contain the stock table")

    file_offset = seg0["p_offset"] + relative_offset
    if file_offset + len(STOCK) > len(data):
        die("mapped table location is beyond end of ELF")
    return file_offset, seg0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=Path, help="decrypted SceLcd ELF, or raw segment-0 dump with --raw-segment-0")
    parser.add_argument("--firmware", required=True, choices=sorted(OFFSETS), help="firmware layout to verify")
    parser.add_argument("--raw-segment-0", action="store_true", help="treat input bytes as an exact dump of module segment 0")
    parser.add_argument("--context", type=int, default=16, help="bytes of context to print before/after the table")
    args = parser.parse_args()

    data = args.image.read_bytes()
    relative = OFFSETS[args.firmware]
    digest = hashlib.sha256(data).hexdigest()

    if args.raw_segment_0:
        file_offset = relative
        seg = None
        if file_offset + len(STOCK) > len(data):
            die(f"raw segment is too small for offset 0x{relative:X}")
    else:
        file_offset, seg = elf_segment0_file_offset(data, relative)

    actual = data[file_offset:file_offset + len(STOCK)]
    print(f"image: {args.image}")
    print(f"sha256: {digest}")
    print(f"firmware: {args.firmware}")
    print(f"segment-0 relative table offset: 0x{relative:X}")
    if seg is not None:
        print(
            "segment-0: "
            f"PH#{seg['ph_index']} file=0x{seg['p_offset']:X} "
            f"vaddr=0x{seg['p_vaddr']:X} filesz=0x{seg['p_filesz']:X} memsz=0x{seg['p_memsz']:X}"
        )
    print(f"mapped file offset: 0x{file_offset:X}")
    print(f"expected: {STOCK.hex(' ')}")
    print(f"actual:   {actual.hex(' ')}")

    start = max(0, file_offset - max(0, args.context))
    end = min(len(data), file_offset + len(STOCK) + max(0, args.context))
    print(f"context[0x{start:X}:0x{end:X}]: {data[start:end].hex(' ')}")

    if actual != STOCK:
        print("RESULT: FAIL — offset does not contain the exact stock SceLcd brightness table", file=sys.stderr)
        return 1

    print("RESULT: PASS — exact stock SceLcd brightness table found at the configured segment-0 offset")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
