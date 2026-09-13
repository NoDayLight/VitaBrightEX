#!/usr/bin/env python3
import argparse, hashlib
from pathlib import Path
from vita_elf_audit import VitaElf

LCD_365_OFFSET = 0x1B48
LCD_STOCK = bytes([31,37,43,50,58,67,77,88,100,114,129,147,166,182,203,227,255])
OLED_CANDIDATE_OFFSETS = (0x1AB8,0x1C20,0x1E00)
OLED_LUT_SIZE = 17*21

def segment_slice(elf, segment, offset, size):
    p=elf.phdrs[segment]
    if offset < 0 or offset+size > p.p_filesz:
        raise SystemExit(f"{elf.path}: segment {segment} range 0x{offset:X}+0x{size:X} outside file-backed bytes")
    start=p.p_offset+offset
    return elf.data[start:start+size]

def main():
    ap=argparse.ArgumentParser(); ap.add_argument("--lcd",type=Path,required=True); ap.add_argument("--oled",type=Path,required=True); a=ap.parse_args()
    lcd=VitaElf(a.lcd)
    actual=segment_slice(lcd,0,LCD_365_OFFSET,len(LCD_STOCK))
    print("SceLcd 3.65 static layout evidence:")
    print(f"  module_sha256={lcd.sha256}")
    print(f"  segment=0 offset=0x{LCD_365_OFFSET:X}")
    print("  bytes="+",".join(str(x) for x in actual))
    if actual != LCD_STOCK: raise SystemExit("SceLcd 3.65 stock brightness signature mismatch")
    print("  result=EXACT_SIGNATURE_MATCH")
    oled=VitaElf(a.oled)
    print("SceOled candidate LUT regions (structural evidence only):")
    print(f"  module_sha256={oled.sha256}")
    for off in OLED_CANDIDATE_OFFSETS:
        data=segment_slice(oled,0,off,OLED_LUT_SIZE)
        rows=[data[i:i+21] for i in range(0,len(data),21)]
        nontrivial=sum(1 for r in rows if any(x != 0 for x in r) and any(x != 0xff for x in r))
        print(f"  segment=0 offset=0x{off:X} size={len(data)} sha256={hashlib.sha256(data).hexdigest()} nontrivial_rows={nontrivial}/17")
    print("  result=CANDIDATE_REGIONS_RECORDED_NOT_PROMOTED")
if __name__=="__main__": main()
