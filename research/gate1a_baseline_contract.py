#!/usr/bin/env python3
import argparse,hashlib,json,struct
from pathlib import Path

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--manifest',type=Path,required=True);a=ap.parse_args()
    d=json.loads(a.manifest.read_text())
    expect={'A':'2f9fd211d1d389611267070cfbc936063b0b790a59245243feb404ee3c00daf6','B':'5dc12dfcae42a648dc093db831b661cc3068f2d70f199b03fb582ce301b188b5'}
    for k,h in expect.items():
        o=d['objects'][k];words=[int(x,16) for x in o['words_le_u32']]
        if len(words)!=15:raise SystemExit(f'{k}: word count')
        raw=struct.pack('<15I',*words);got=hashlib.sha256(raw).hexdigest()
        if got!=h or o['sha256']!=h:raise SystemExit(f'{k}: baseline drift {got} {o["sha256"]}')
        print(f'GATE0A_{k}_SHA256={got}')
    print('GATE1A_GATE0A_BASELINE_CONTRACT=PASS')
if __name__=='__main__':main()
