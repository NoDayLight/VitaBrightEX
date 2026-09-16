#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FTP="${VBE_FTP:-ftp://192.168.1.131:1337}"
mode="${1:---deploy}"
expected_id="$(tr -d '\r\n' < "$ROOT/EXPECTED_BUILD_ID.txt")"
for f in vitabright.skprx vbe_release_preflight.vpk vbe_release_mild.vpk vbe_release_reset.vpk EXPECTED_BUILD_ID.txt; do test -s "$ROOT/$f"; done

if [ "$mode" = "--verify-runtime" ]; then
  rm -f "$ROOT/runtime-preflight.bin"
  curl -q --fail --show-error --ftp-method nocwd "$FTP//ux0:/data/vbe_release_preflight.bin" -o "$ROOT/runtime-preflight.bin"
  python3 "$ROOT/tools/release_preflight_verify.py" "$ROOT/runtime-preflight.bin" --expected-build-id "$expected_id"
  # Reaching this point proves the reboot-owned runtime is this exact build.
  for f in vbe_release_mild.vpk vbe_release_reset.vpk; do
    curl -q --fail --show-error --ftp-method nocwd -T "$ROOT/$f" "$FTP//ux0:/data/$f"
  done
  echo "VBE_RELEASE_REGRESSION_PAYLOADS_STAGED=PASS"
  echo "Install the Mild/Reset VPKs now; they were intentionally unavailable before runtime preflight passed."
  exit 0
fi

test "$mode" = "--deploy"
mkdir -p "$ROOT/deploy-state"
curl -q --fail --show-error --ftp-method nocwd "$FTP//ur0:/tai/config.txt" -o "$ROOT/deploy-state/config.before.txt"
cp "$ROOT/deploy-state/config.before.txt" "$ROOT/deploy-state/config.release.txt"
python3 - "$ROOT/deploy-state/config.release.txt" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1]); lines=p.read_text().splitlines(); remove={'ur0:tai/vbe_gate1e_immediate.skprx','ur0:tai/vbe_gate1e_chain.skprx'}; lines=[x for x in lines if x.strip() not in remove]; prod='ur0:tai/vitabright.skprx'
if sum(x.strip()==prod for x in lines)>1: raise SystemExit('duplicate vitabright')
if not any(x.strip()==prod for x in lines):
    i=next(i for i,x in enumerate(lines) if x.strip()=='*KERNEL'); lines.insert(i+1,prod)
p.write_text('\n'.join(lines)+'\n')
PY
curl -q --fail --show-error --ftp-method nocwd -T "$ROOT/vitabright.skprx" "$FTP//ur0:/tai/vitabright.skprx"
# Only the preflight client is staged before the mandatory cold-boot identity check.
curl -q --fail --show-error --ftp-method nocwd -T "$ROOT/vbe_release_preflight.vpk" "$FTP//ux0:/data/vbe_release_preflight.vpk"
for stale in vbe_release_mild.vpk vbe_release_reset.vpk; do
  curl -q --ftp-method nocwd -Q "DELE ux0:/data/$stale" "$FTP/" >/dev/null 2>&1 || true
done
curl -q --fail --show-error --ftp-method nocwd -T "$ROOT/deploy-state/config.release.txt" "$FTP//ur0:/tai/config.txt"
curl -q --ftp-method nocwd -Q "DELE ux0:/data/vbe_release_preflight.bin" "$FTP/" >/dev/null 2>&1 || true
curl -q --fail --show-error --ftp-method nocwd "$FTP//ur0:/tai/vitabright.skprx" -o "$ROOT/deploy-state/readback.skprx"
installed_sha="$(shasum -a 256 "$ROOT/deploy-state/readback.skprx" | awk '{print $1}')"
package_sha="$(shasum -a 256 "$ROOT/vitabright.skprx" | awk '{print $1}')"
test "$installed_sha" = "$package_sha"
echo "VBE_RELEASE_DEPLOY_READBACK=PASS"
echo "ON_DISK_SKPRX_SHA256=$installed_sha"
echo "REGRESSION_PAYLOADS_STAGED=NO"
echo "MANDATORY: install Preflight, then true Power Off -> wait -> Power On -> run Preflight -> start VitaShell FTP -> run:"
echo "  $0 --verify-runtime"
echo "Mild/Reset packages are staged only after VBE_RELEASE_RUNTIME_PREFLIGHT=PASS."
