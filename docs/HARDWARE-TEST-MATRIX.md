# Pseudo-v1.4 hardware validation matrix

Hardware rows are never passed by source review or CI alone. The purpose of the final software checkpoint is to make one exact build safe and coherent enough for controlled physical validation.

## Software gate

The exact hardware-test commit must have branch-exact green CI covering:

- source/parser structural contracts;
- packaged-asset byte identity;
- production LUT/config parsers;
- source authority and config-source rollback;
- transaction ownership and result severity;
- persistence ownership/sequencing;
- synchronization and module lifecycle;
- diagnostics/error domains;
- filter requested/committed/unsupported truth;
- OLED register transform and complete base/runtime state;
- reproducible CCT generation and pure affine host tests;
- release SKPRX under `-Werror`;
- diagnostic SKPRX under `-Werror`;
- generated/installed syscall stubs;
- matching editor against those stubs;
- exact checkout/toolchain/package reporting;
- exact payload SHA-256 and one PCH-2000 hardware-bundle ZIP containing its own commit/manifest.

Semantic tests compile the production-shared C cores. Source greps are structural guardrails only.

## Proven pre-existing PCH-2000 evidence

Target: PCH-2000 Slim / LCD, firmware `0x03650000`, 3.65 Ensō.

```text
isolated hardened cold boot: PASS
v1.3 boot hang: not reproduced
malformed-LUT fail-open: PASS
SceLcd segment 0 / 0x1B48 runtime signature: PROVEN
SceLcd segment 0 / 0x1B48 retail static signature: PROVEN
stock-vs-extended LUT A/B: PASS
brightness slider sweep: PASS
mid-brightness inactivity dim: PASS
very-low behavior: consistent with design
true-maximum inactivity behavior: PENDING
suspend/resume: PENDING
normal plugin-stack compatibility: PENDING
RGB-range pattern behavior: PENDING
```

Static provenance is documented separately in [`FIRMWARE-LAYOUT-VERIFICATION.md`](FIRMWARE-LAYOUT-VERIFICATION.md).

## Gate A — final safe-default cold boot

Use exactly one authoritative CI bundle containing:

```text
release vitabright.skprx
normal commented vitabright_lcd_lut.txt
production vitabrightex.cfg
matching editor VPK
WORKFLOW_COMMIT.txt
SHA256SUMS
```

First capture is deliberately narrow:

```text
full power off
normal boot
no L bypass
no Circle/reload before first inspection
```

Expected state:

```text
Build plugin=<same sha8> editor=<same sha8> MATCH
Hardware PCH-2000 LCD
firmware 0x03650000
ABI 2
Core=active
Table=active
Layout=active
Lock=active
Brightness hook=active
Power hook=active
Last kernel error=0
detail=0
cfg=0 bright=0 color=0 filter=0 input=0 sync=0
```

Generic filter capabilities may display `unsupported`; that is capability truth, not an error.

## Gate B — LCD high-contrast/color-space mode

After Gate A, toggle only the verified panel color-space/high-contrast mode. Record original mode, request the alternate mode, require read-back equality, then return to original and verify. Repeat around app launch and suspend/resume before claiming lifecycle coverage.

Historical config keys `lcd_color_space_mode`, `lcd_ips_enhance` and `lcd_saturation_boost` all target this same one hardware state. They are not tested as independent effects.

## Gate C — brightness lifecycle

Exercise:

- min -> max -> min system-slider sweep;
- true maximum followed by inactivity dim;
- mid and very-low brightness inactivity behavior;
- app-driven brightness/power transitions;
- suspend/resume cycles;
- repeated explicit reloads after initial capture.

The diagnostic SKPRX is used only if an observed release-build failure needs logs.

## Gate D — source/persistence/rollback

After safe-default boot:

- make one valid monotonic LCD RAM edit and verify immediate apply;
- persist a FILE source and verify reboot/reload identity;
- separately exercise COMPILED fallback: save must report no file source rather than materialize one;
- preferred missing + fallback valid;
- preferred malformed: terminal, no fallback;
- candidate replacement failure with clean rollback;
- controlled dirty/fault paths only when a safe injection mechanism exists.

Required invariant:

```text
committed FILE ux0:A
candidate FILE ur0:B
candidate backend fails cleanly
rollback succeeds
=> authoritative source remains ux0:A
```

## Gate E — generic filters

For pseudo-v1.4 generic filter mutation is intentionally unsupported:

```text
invert       UNSUPPORTED
AFFINE_CSC   UNSUPPORTED
TRANSFER     UNSUPPORTED
```

No invert on/off hardware test is requested because original-state ownership was not proven. Instead verify that a non-neutral request is retained in `GetState.requested`, appears in `unsupported_domains`, leaves committed state neutral, produces no FILTER runtime error and causes no display mutation.

If a requested unsupported CCT is present, changing only the invert request in the editor must preserve that requested CCT rather than silently erase it.

## Gate F — PCH-1000 OLED

Use at least one known DDB P4 or P5 device before declaring OLED RGB-bias behavior physically validated.

Required ordinary backend coverage:

- DDB/panel detection;
- layout plausibility validation;
- base/runtime state commit;
- brightness and inactivity behavior;
- FILE source authority and base-only persistence;
- rollback/degraded ownership;
- panel color-space restoration.

For P4/P5 RGB register bias, test small single-channel offsets first and verify the intended direction visually before stronger values. The feature is defined only as saturating register-code offsets over all seven RGB triplets per row.

For P6/unknown, a non-neutral bias request must return unsupported while runtime remains the base LUT and brightness remains ACTIVE.

Warm/night mode is not a v1.4 hardware test: no calibrated profile is shipped, so the request must remain unsupported.

## Gate G — RGB-range evidence

Do not restore v1.3 registry mutation. On PCH-2000 use controlled test patterns containing at least:

```text
0
16
235
255
```

If the internal LCD path already preserves full range, no feature is needed. Only demonstrated range compression plus a separately proven safe hardware transform could justify future expansion.

## Gate H — plugin-stack compatibility

Reintroduce the preserved normal plugin stack incrementally after isolated release behavior is clean. Every stage must still reach LiveArea and keep brightness/layout/hooks/lock active with zero runtime diagnostics.

## Gate I — stop/unload ownership

Where safe to exercise, confirm panel color-space original state is restored and taiHEN resources are released before clean unload. Any unresolved resource or synchronization ownership must refuse unload rather than claim success.

There is no generic invert restore stage because production no longer mutates invert.

## Result recording

Record for every physical target:

- model/panel DDB;
- firmware/exploit;
- exact plugin stack;
- commit/build IDs;
- workflow/run number;
- artifact and payload hashes;
- full status + diagnostics + filter state;
- source identity/fault stage where relevant;
- runtime/static layout evidence separately;
- recovery action and final gate result.

PR #1 remains draft/unmerged during this validation. No measured sub-watt bound is claimed without suitable instrumentation.
