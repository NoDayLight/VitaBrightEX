#!/usr/bin/env python3
from __future__ import annotations
import argparse, json
from pathlib import Path

REQUIRED = {
    'persistent_affine_csc',
    'cct_white_balance',
    'arbitrary_3x3_calibration',
    'luma_aware_saturation',
    'pivoted_contrast',
    'brightness_offset',
    'black_level_offset',
    'arbitrary_gamma',
    'tone_shaping',
    'panel_linearisation',
}
FORBIDDEN = {'DEFERRED', 'OUT_OF_SCOPE', 'REMOVED', 'UNSUPPORTED_FINAL'}


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument('--manifest', type=Path, default=Path('research/DISPLAY-FEATURE-SCOPE.json'))
    ap.add_argument('--json', type=Path)
    a = ap.parse_args()
    data = json.loads(a.manifest.read_text())
    if data.get('schema') != 1:
        raise SystemExit('scope manifest schema drift')
    feats = data.get('features')
    if not isinstance(feats, list):
        raise SystemExit('scope manifest features must be a list')
    by = {}
    for f in feats:
        name = f.get('feature')
        if not name or name in by:
            raise SystemExit(f'duplicate/invalid feature entry: {name!r}')
        by[name] = f
    missing = sorted(REQUIRED - set(by))
    if missing:
        raise SystemExit('missing critical feature targets: ' + ', '.join(missing))
    extra = sorted(set(by) - REQUIRED)
    if extra:
        raise SystemExit('unexpected critical feature entries require explicit scope-contract update: ' + ', '.join(extra))
    decisions = data.get('scope_decisions', [])
    if not isinstance(decisions, list):
        raise SystemExit('scope_decisions must be a list')
    decision_for = {d.get('feature'): d for d in decisions if isinstance(d, dict) and d.get('feature')}
    for name in sorted(REQUIRED):
        f = by[name]
        scope = f.get('scope')
        if scope != 'ACTIVE_TARGET':
            if scope in FORBIDDEN and name in decision_for and decision_for[name].get('evidence'):
                raise SystemExit(f'{name}: evidence-backed removal is recorded but pseudo-v1.4 critical-target contract requires explicit contract revision')
            raise SystemExit(f'{name}: critical target scope drifted to {scope!r}; expected ACTIVE_TARGET')
        if not f.get('backend'):
            raise SystemExit(f'{name}: backend state missing; feature scope and backend state must remain separate')
    policy = data.get('scope_policy', {})
    prod = policy.get('current_production_checkpoint', {})
    if prod.get('affine_csc_policy') != 'UNSUPPORTED_SAFE_GATE' or prod.get('transfer_policy') != 'UNSUPPORTED_SAFE_GATE':
        raise SystemExit('frozen production safe-gate policy drift')
    result = {
        'schema': 1,
        'critical_target_count': len(REQUIRED),
        'scope': 'ALL_ACTIVE_TARGET',
        'production_safe_gate_preserved': True,
    }
    if a.json:
        a.json.write_text(json.dumps(result, indent=2) + '\n')
    print(f"DISPLAY_FEATURE_SCOPE_CONTRACT PASS {len(REQUIRED)}/{len(REQUIRED)} ACTIVE_TARGET")


if __name__ == '__main__':
    main()
