#!/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
FTP="${VBE_FTP:-ftp://192.168.1.131:1337}"
OLD_GATE1D_SHA="513d3a237933715ccd2e9ff156762c27241dd24ed5ad361e3cb42296ffd2d19d"
STATE="$ROOT/gate1e-transaction-deploy-state"
mkdir -p "$STATE"

for f in vitabright.skprx vbe_gate1e_integrated_mild.vpk vbe_gate1e_integrated_reset.vpk vbe_gate1e_integrated_status.vpk vbe_gate1e_integrated_rollback.vpk SHA256SUMS; do
  test -s "$ROOT/$f" || { echo "missing $ROOT/$f" >&2; exit 2; }
done

echo '=== LOCAL HASHES ==='
(cd "$ROOT" && shasum -a 256 -c SHA256SUMS)

curl -q --fail --show-error --ftp-method nocwd \
  "$FTP//ur0:/tai/vitabright.skprx" -o "$STATE/readback.before.skprx"
BEFORE_SHA="$(shasum -a 256 "$STATE/readback.before.skprx" | awk '{print $1}')"
if [ "$BEFORE_SHA" != "$OLD_GATE1D_SHA" ]; then
  echo "REFUSING: installed vitabright.skprx is $BEFORE_SHA, expected frozen Gate-1D $OLD_GATE1D_SHA" >&2
  exit 3
fi
cp "$STATE/readback.before.skprx" "$STATE/vitabright.gate1d.backup.skprx"

curl -q --fail --show-error --ftp-method nocwd \
  "$FTP//ur0:/tai/config.txt" -o "$STATE/config.before.txt"
cp "$STATE/config.before.txt" "$STATE/config.integrated.txt"

python3 - "$STATE/config.integrated.txt" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1]); lines=p.read_text().splitlines()
remove={
 'ur0:tai/vbe_gate1e_immediate.skprx',
 'ur0:tai/vbe_gate1e_chain.skprx',
 'ur0:tai/vbe_gate1b_snapshot.skprx',
 'ur0:tai/vbe_gate1c_stage_probe.skprx',
}
lines=[x for x in lines if x.strip() not in remove]
prod='ur0:tai/vitabright.skprx'
count=sum(x.strip()==prod for x in lines)
if count>1: raise SystemExit(f'refusing duplicate {prod}: {count}')
if count==0:
    try:i=next(i for i,x in enumerate(lines) if x.strip()=='*KERNEL')
    except StopIteration: raise SystemExit('no *KERNEL section')
    lines.insert(i+1,prod)
p.write_text('\n'.join(lines)+'\n')
PY

echo '=== CONFIG DIFF ==='
diff -u "$STATE/config.before.txt" "$STATE/config.integrated.txt" || true
echo '=== ACTIVE MATRIX/RESEARCH LINES ==='
grep -nE 'vitabright\.skprx|vbe_gate1[abcde]' "$STATE/config.integrated.txt" || true

test "$(grep -c '^ur0:tai/vitabright\.skprx$' "$STATE/config.integrated.txt")" -eq 1
! grep -q '^ur0:tai/vbe_gate1e_immediate\.skprx$' "$STATE/config.integrated.txt"
! grep -q '^ur0:tai/vbe_gate1e_chain\.skprx$' "$STATE/config.integrated.txt"

curl -q --fail --show-error --ftp-method nocwd -T "$ROOT/vitabright.skprx" "$FTP//ur0:/tai/vitabright.skprx"
for f in vbe_gate1e_integrated_mild.vpk vbe_gate1e_integrated_reset.vpk vbe_gate1e_integrated_status.vpk vbe_gate1e_integrated_rollback.vpk; do
  curl -q --fail --show-error --ftp-method nocwd -T "$ROOT/$f" "$FTP//ux0:/data/$f"
done
curl -q --fail --show-error --ftp-method nocwd -T "$STATE/config.integrated.txt" "$FTP//ur0:/tai/config.txt"

curl -q --fail --show-error --ftp-method nocwd "$FTP//ur0:/tai/vitabright.skprx" -o "$STATE/readback.integrated.skprx"
curl -q --fail --show-error --ftp-method nocwd "$FTP//ur0:/tai/config.txt" -o "$STATE/config.readback.txt"
cmp "$STATE/config.integrated.txt" "$STATE/config.readback.txt"
EXPECTED="$(awk '$2=="./vitabright.skprx"{print $1}' "$ROOT/SHA256SUMS")"
ACTUAL="$(shasum -a 256 "$STATE/readback.integrated.skprx" | awk '{print $1}')"
test -n "$EXPECTED"
test "$ACTUAL" = "$EXPECTED"
echo "INTEGRATED_SKPRX_SHA256=$ACTUAL"
echo 'GATE1E_TRANSACTION_DEPLOY_READBACK=PASS'
echo 'Install the four ux0:data/vbe_gate1e_integrated_*.vpk files, then fully power off/on.'
