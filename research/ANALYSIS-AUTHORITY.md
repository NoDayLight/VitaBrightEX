# VitaBrightEX LCD color-engine analysis authority

## Current function-analysis authority

`research/vita_elf_legacy.py` preserves the original ARM.exidx-bounded implementation only for reproducibility of older evidence. It is not the current authority for function-local claims.

`research/vita_elf_audit.py` is the validated front door. Logical-function boundaries are closed over ARM.exidx starts, exports, module start/stop entries, import stubs, reachable direct BL/BLX targets, and explicit regression starts. Known no-return stack-check failures terminate paths. Import tables are count-aware and do not dereference absent function, variable, or TLS tables.

Derived function-local evidence must carry `start`, `logical_end`, `exidx_range`, `exidx_exact`, `boundary_sources`, and `termination_reason`. A known logical start may not be consumed as silent linear fallthrough; direct branches to another established start are recorded as shared/tail-entry edges.

## Immutable historical authority

The older `research-iftu-static-audit` line remains an immutable supporting authority rather than a competing truth system:

```text
research SHA:
7e6eb480b9919365be31d73a64cac4d620e20c4b

workflow:
IFTU static audit
run ID:
34752217746
result:
SUCCESS
```

That frozen evidence remains authoritative for the established `0x3C` `SceIftuCscParams` contract, the legacy v1.3 three-argument ABI defect, the private CSC setter identities/callsites, the invert-restoration limitation, and the static firmware-layout facts that the current workflow does not need to rediscover merely for duplication.

The current `research-lcd-color-engine` workflow independently reasserts the production-relevant public `0x3C` structure contract and the exact retail-3.65 private trace ABI/return facts needed by Gate-0.

## Static-unknown semantics

A research workflow succeeds when its analyzers execute correctly, pinned inputs match, and every asserted invariant remains true. Discovery is not itself a success criterion.

For the SceLcd working-program branch:

```text
PROVEN_STATIC_RECONSTRUCTION
```

means the exact static model reconstructs the relevant source, while:

```text
INCOMPLETE_STATIC_RECONSTRUCTION
```

means a required source/runtime dependency is not fully recoverable from file-backed firmware evidence. The latter is a valid green research result. It must not be rewritten into fabricated static success and it does not block read-only Gate-0 observation.

## Affine architecture lock

The pre-hardware front-runner is Architecture A+:

```text
Sony high-level display state
        ↓
Sony generates pristine CSC object
        ↓
private CSC setter A/B
        ↓
VBE observes/captures pristine Sony input
        ↓
VBE derives exactly one candidate from that pristine input
        ↓
original Sony setter
        ↓
Sony-owned Lowio cache and MMIO programming
```

Pure high-level builder interception is a contingency only if Gate-0 shows the private setters lose necessary pristine/lifecycle information. Direct MMIO is a contingency only if the normal Sony setter path cannot provide safe ownership/restoration.

Setter call order is not evidence of physical pixel execution order. Actual stage order remains for a later identity-safe, reversible, non-commuting micro-experiment after pristine ownership and exact restoration are proven.

The remaining pre-identity affine unknowns are physical/runtime facts:

```text
active PCH-2000 plane
pristine-state stability/idempotence
NULL setter semantics
brightness rewrite/reacquisition
color-space rewrite/reacquisition
display off/on lifecycle
application launch/exit lifecycle
suspend/resume lifecycle
latest pristine restoration base
```

## Pseudo-v1.4 feature-scope lock

Production's current `AFFINE_CSC` and `TRANSFER` unsupported status is a safety gate, not the final feature scope.

| Feature | pseudo-v1.4 scope | Pre-Gate status |
| --- | --- | --- |
| CCT / white balance | ACTIVE TARGET | BACKEND PENDING RUNTIME PROOF |
| arbitrary 3×3 calibration | ACTIVE TARGET | BACKEND PENDING RUNTIME PROOF |
| luma-aware saturation | ACTIVE TARGET | BACKEND PENDING RUNTIME PROOF |
| pivoted contrast | ACTIVE TARGET | BACKEND PENDING RUNTIME PROOF; additive semantics pending |
| brightness offset | ACTIVE TARGET | BACKEND PENDING RUNTIME PROOF; additive semantics pending |
| black-level offset | ACTIVE TARGET | BACKEND PENDING RUNTIME PROOF; additive semantics pending |
| arbitrary useful gamma | ACTIVE TARGET | BACKEND STILL UNDER RESEARCH; panel-controller path leads |
| tone / midtone shaping | ACTIVE TARGET | BACKEND STILL UNDER RESEARCH; panel-controller path leads |
| measured panel linearisation | ACTIVE TARGET | BACKEND STILL UNDER RESEARCH; requires programmable transfer plus physical calibration |

Once A+ ownership and the correct RGB-domain matrix stage are established, matrix-side work may advance as `MATRIX_SIDE_READY` while post-add/component arithmetic remains `ADDITIVE_SIDE_PENDING`. That staging must not silently remove contrast, brightness, or black-level controls from v1.4 scope.

## Nonlinear topology stop rules

The final bounded IFTU register-surface pass classifies only Sony-used retail-3.65 programming. If it finds no indexed LUT/address-data programming, curve upload, piecewise coefficient state, per-channel lookup state, curve pointer, or transfer-enable facility in the complete recovered Sony-used surface, its terminal result is:

```text
NO SONY-USED IFTU NONLINEAR FACILITY FOUND
```

This does **not** assert that undocumented unused IFTU registers cannot exist. It closes N3 as the active Sony-used path unless runtime evidence reopens it.

Closing N3 does not close the nonlinear feature. The primary remaining route is the PCH-2000 panel-controller command engine. Gate-0 records Sony's own panel writes, panel reads, DDB/bucket state, and lifecycle without issuing any new panel command. The physical command/read fingerprint then becomes the authority for controller identification and vendor gamma/transfer investigation.

GXM/PBE is bounded as a fallback/helper branch: the public gamma control is enum-like/fixed. It is not evidence of arbitrary transfer. It remains relevant only if the targeted system-display path actually preserves/uses that state without an added framebuffer pass.

## Gate-0 evidence authority

Protocol v5 is the first authoritative runtime evidence format for this stage. A malformed, lossy, incomplete-authority, unstable-boundary, or wrong-version capture cannot select an architecture. `NOT OBSERVED` is not `UNSUPPORTED`.

Gate-0 is read-only. It must not issue private CSC calls, new panel commands, direct MMIO writes, brightness mutations beyond legitimate Sony UI/API behavior, or framebuffer transformations.
