# VitaBrightEX pseudo-v1.4

VitaBrightEX pseudo-v1.4 is a hardening and display-pipeline fork of VitaBrightEX. Its release rule is deliberately narrower than v1.3 marketing: a feature is writable only when the hardware mechanism, ownership, rollback, persistence and unload semantics are defensible. Optional enhancement failure must not prevent LiveArea from loading, while uncertain runtime ownership must stop further mutation.

> **Development state:** PR #1 remains open, draft and unmerged. PCH-2000 / firmware 3.65 Ensō has already passed the hardened brightness backend cold-boot and runtime layout gate. Retail 3.65 static analysis independently confirms the same `SceLcd` segment-0 `0x1B48` stock table. The next hardware work uses one exact final CI-built bundle after the display-pipeline checkpoint is frozen.

## Current capability truth

| Capability | PCH-1000 OLED | PCH-2000 LCD |
|---|---|---|
| Extended brightness | transactional | transactional; physically proven on 3.65 |
| Inactivity/power brightness hooks | implemented | implemented |
| LUT editing and authoritative save | base LUT; FILE source only | FILE source; COMPILED has no fake save target |
| OLED RGB register bias | **P4/P5 supported** | n/a |
| OLED RGB bias on P6/unknown | **unsupported; base runtime retained** | n/a |
| Warm/night profile | **unsupported; no calibration fabricated** | n/a |
| Panel color-space/high-contrast mode | read-back verified | read-back verified |
| Generic hardware invert | **unsupported for safe ownership** | **unsupported for safe ownership** |
| Persistent affine CSC / CCT / contrast / brightness offset | **unsupported for v1.4** | **unsupported for v1.4** |
| Gamma/nonlinear transfer | **unsupported** | **unsupported** |
| True panel linearisation | **unsupported** | **unsupported** |
| Persistent registry RGB-range mutation | not used | not used |

### Why generic invert is unsupported

Retail analysis verifies `ksceDisplaySetInvertColors` (`0x19140ACD`) and cached invert-related state, but no trustworthy original-state getter has been established. The previously suspected export `0x9E3C6DC6` is identified by the pinned VitaSDK headers as `ksceDisplaySetBrightness`, not an invert getter. Pseudo-v1.4 therefore does not assume the original state is zero and performs no generic invert mutation.

### Why persistent CSC is unsupported

Retail 3.65 analysis proves the relevant private IFTU object is **0x3C bytes**, strongly identifies it as `SceIftuCscParams`, proves `r0` is an IFTU plane index, and proves the legacy v1.3 three-argument ABI was wrong. That is not enough for ownership: original-state acquisition, active CSC-set selection, power-transition behavior and exact restoration are not sufficiently proven. No private IFTU setter is called by production v1.4.

### OLED RGB-bias semantics

For DDB panel types P4/P5, `color_r_bias`, `color_g_bias` and `color_b_bias` are saturating **register-code offsets**. Each offset is applied to the matching channel byte of all seven documented RGB reference triplets in every 21-byte LUT row.

```text
authoritative base LUT
        -> panel capability decision
        -> pure register-domain transform
        -> derived runtime LUT
        -> transactional injection
```

The base LUT is the only editable/persisted authority. A transform change always re-derives from base and never compounds runtime onto runtime. On P6/unknown, a non-neutral bias request returns `VBE_RESULT_UNSUPPORTED`, the applied transform is neutral, runtime remains bit-identical to base, and ordinary brightness remains healthy. These offsets are not described as linear-light gains, calibrated white balance or Kelvin CCT.

## Ownership and result model

Brightness backends use exactly:

```text
CLEAN     no backend taiHEN resource owned
ACTIVE    complete committed backend installed
DEGRADED uncertain/retained ownership; no new mutation or rollback
```

Transaction results are `TXN_OK`, `TXN_FAILED_CLEAN`, or `TXN_FAILED_DIRTY`. Rollback is legal only after a clean candidate failure when a prior committed state existed and ownership is CLEAN.

Multi-stage public results compose by severity:

```text
negative runtime failure
    > positive capability/partial result
    > zero success
```

Synchronization release is a stricter ownership boundary: an unlock/release failure dominates the scalar syscall result while the operation's own domain error remains independently recorded.

## Requested versus committed filter state

`VitaBrightStatus` remains ABI v2 and `VitaBrightDiagnostics` remains additive ABI v1. `VitaBrightDisplayFilterState` ABI v1 exposes:

```text
requested parameters
committed parameters
requested domains
committed domains
unsupported domains
failed domains
```

`GetParams` returns committed hardware state. `GetState` preserves requested intent even when unsupported. The editor clones `GetState.requested` before changing one field, so changing an invert request cannot silently erase a requested-but-unsupported CCT.

## Source authority and persistence

Config and LUT sources use explicit `NONE`, `FILE + exact path`, or `COMPILED + no path` identity. Only OPEN-stage explicit not-found permits fallback; once a file opens, read/parse/close failure is terminal.

A candidate identity commits only with its corresponding committed state. Failed reload restores the preceding config/source snapshot. In particular:

```text
committed FILE ux0:A
candidate FILE ur0:B
backend replacement fails cleanly
rollback succeeds
=> config/source remains FILE ux0:A
```

Normal LUT persistence requires `ACTIVE + FILE`. OLED serializes the **base** LUT, never the derived runtime LUT. LCD compiled fallback returns positive `VBE_RESULT_NO_FILE_SOURCE` rather than inventing `ur0:tai/...`.

## Firmware/layout evidence

PCH-2000 production injection always requires the exact loaded-module Sony stock signature before mutation:

```text
31 37 43 50 58 67 77 88 100 114 129 147 166 182 203 227 255
```

For retail firmware 3.65, segment 0 + `0x1B48` is both:

```text
STATICALLY PROVEN
+
PHYSICALLY PROVEN
```

Static provenance:

```text
retail 3.65 PUP SHA-256
86859b3071681268b6d0beb5ef691da874b6726e85b6f06a1cdf6a0e183e77c6

decrypted os0 SHA-256
35480a01ea783df859326d8698831afd3e850717f5b7785ab9964eab23712856

SceLcd ELF SHA-256
24752bb4c69f0c241701cab0364cde9cbe5c95128b6bfe02bf3263aad524246e
```

The exact research/audit provenance is documented in [`docs/FIRMWARE-LAYOUT-VERIFICATION.md`](docs/FIRMWARE-LAYOUT-VERIFICATION.md). 3.71–3.74 remain unsupported for raw-table injection rather than receiving guessed offsets.

## Performance contract

No polling thread, periodic timer, framebuffer interception, CPU pixel processing, per-frame LUT recomputation or unnecessary steady-state filesystem activity is introduced. Computation occurs only at boot, explicit reload/editor mutation, or a genuinely required OS reapply path; rendering remains hardware-driven.

## Validation

The repository executes production-shared host tests for parsers, source authority, config-source rollback, transaction ownership, result severity, persistence, synchronization/module lifecycle, diagnostics, filter requested/committed truth, OLED transform/state ownership and the retained pure affine math core. CCT anchors are reproducibly generated and checked even though affine CSC is not linked into the production SKPRX.

Release and diagnostic SKPRX are built with `-Werror`; syscall stubs are generated/installed before the matching editor is built. Final authoritative CI records toolchain/package versions, exact checkout SHA, payload hashes and a PCH-2000 hardware-test ZIP containing `WORKFLOW_COMMIT.txt` and `SHA256SUMS`.

See:

- [`docs/V1.4-ARCHITECTURE.md`](docs/V1.4-ARCHITECTURE.md)
- [`docs/SOURCE-AUTHORITY.md`](docs/SOURCE-AUTHORITY.md)
- [`docs/DIAGNOSTICS-ERROR-MODEL.md`](docs/DIAGNOSTICS-ERROR-MODEL.md)
- [`docs/FIRMWARE-LAYOUT-VERIFICATION.md`](docs/FIRMWARE-LAYOUT-VERIFICATION.md)
- [`docs/HARDWARE-TEST-MATRIX.md`](docs/HARDWARE-TEST-MATRIX.md)

## Physical evidence already recorded

```text
PCH-2000 / 3.65 isolated cold boot: PASS
v1.3 boot hang: not reproduced
SceLcd 3.65 segment 0 / 0x1B48 runtime signature: PASS
SceLcd 3.65 segment 0 / 0x1B48 retail static signature: PASS
stock-vs-extended LUT A/B: PASS
brightness slider sweep: PASS
mid-brightness inactivity dim: PASS
true-maximum inactivity behavior: PENDING
suspend/resume: PENDING
normal plugin-stack reintegration: PENDING
RGB-range pattern validation: PENDING
```

PR #1 remains draft until the controlled physical matrix is completed. No `<0.01 W` or similar measured power claim is made without appropriate instrumentation.
