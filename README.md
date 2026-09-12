# VitaBrightEX pseudo-v1.4

VitaBrightEX is a hardening/rearchitecture fork of [devnoname120/vitabright](https://github.com/devnoname120/vitabright). This branch exists because VitaBrightEX 1.3 reproducibly hung a PCH-2000 / firmware 3.65 Ensō system at `PS logo → display on → no LiveArea`, while multiple v1.3 display features were speculative, ineffective, or based on incorrect assumptions.

> **Development status:** PR #1 remains draft. The mandatory isolated **PCH-2000 / 3.65 Ensō** cold boot has now passed without reproducing the v1.3 hang, and the loaded 3.65 `SceLcd` image passed the production exact-signature gate at segment-0 `0x1B48`. That first hardware run also exposed a real parser/CI incompatibility in the prior build; the corrected branch now makes CI execute the same streaming LUT parser core used by the SKPRX. A fresh cold boot with the normal commented packaged LUT is the next required checkpoint.

## Design contract

- Optional enhancement failure must never prevent LiveArea from loading.
- Exactly one backend is selected: OLED on PCH-1000, LCD on PCH-2000.
- Backend setup/live replacement is transactional with unwind/rollback.
- Userland writes follow copy -> validate -> serialized state transition -> commit/rollback.
- LUT persistence is kernel-authoritative and atomic relative to the source actually loaded.
- No polling threads, timers, periodic filesystem reads, framebuffer interception or per-frame software processing are introduced.
- Unknown firmware/layouts do not receive guessed kernel offsets.
- A control is exposed only when its implementation is technically supported.
- Packaged text assets must pass production parser semantics, not a separate looser validator.

See [`docs/V1.4-ARCHITECTURE.md`](docs/V1.4-ARCHITECTURE.md) and the recorded hardware evidence in [`docs/HARDWARE-RESULTS-2026-09-12.md`](docs/HARDWARE-RESULTS-2026-09-12.md).

## Current capabilities

| Capability | PCH-1000 OLED | PCH-2000 LCD |
|---|---|---|
| Extended brightness | transactional | transactional |
| Inactivity-dim workaround | implemented | implemented |
| Power-mode brightness hook | implemented | implemented |
| Panel/LUT selection | DDB-based | n/a |
| Live LUT editing | validation + rollback | monotonic validation + rollback |
| Atomic authoritative LUT save | implemented | implemented |
| Hardware colour invert | when SceDisplay export resolves | when SceDisplay export resolves |
| Panel color-space Get/Set | read-back verified | read-back verified |
| Persistent registry RGB-range mutation | not used | not used |
| Persistent active-scanout CSC | unsupported | unsupported |
| Gamma/nonlinear panel processing | unsupported without verified hardware model | unsupported without verified hardware model |

The old private `0x0FCBF457` IFTU path is removed. Current VitaSDK documents `ksceIftuCsc()` as a source/destination framebuffer conversion API under NID `0x67E37EFC`; that does not establish the persistent display-head setter assumed by v1.3.

## Production parser / CI contract

Physical testing found that the previous LCD parser required an entire physical line to fit in `char line[64]`, while CI ignored comment lines without that limit. The normal commented packaged LUT therefore passed workflow #114 but production rejected it before `SceLcd` initialization.

The corrected architecture fixes the class of mismatch:

- LCD and OLED LUTs use portable streaming C state machines with no fixed physical-comment-line buffer.
- Arbitrarily long full-line `#`/`;` comments are discarded as streams.
- LCD data remains strict: exactly 17 decimal bytes, `0..255`, monotonic nondecreasing, CRLF and EOF-without-final-newline supported.
- OLED remains exactly 17 x 21 two-digit hex bytes with strict row structure.
- Config parsing was audited too: long comments are streamed; an overlong non-comment directive is consumed/rejected as one physical line and can never become a second fake directive.
- Present-but-malformed authoritative files are errors; only missing preferred files may use documented fallback.
- CI compiles the **same parser C files linked into the kernel plugin** into a host regression executable and feeds it the exact source/package assets and malformed edge cases.

## Firmware/layout evidence

Raw table injection is separate from NID-resolved function support.

LCD candidates:

- 3.60 -> `0x1B00`
- 3.65/3.67/3.68/3.69/3.70 -> `0x1B48`

Before injection, loaded `SceLcd` must contain the exact Sony stock 17-byte table at the candidate segment-relative address. On the tested physical PCH-2000 / firmware `0x03650000`, this runtime exact-signature gate has **passed at `0x1B48`**, followed by successful injection/hooks and `last_error=0`.

That is physical runtime evidence, not static decrypted-image verification. Static evidence remains pending and is tracked in [`docs/FIRMWARE-LAYOUT-VERIFICATION.md`](docs/FIRMWARE-LAYOUT-VERIFICATION.md). 3.71–3.74 remain unsupported by the raw-table feature.

OLED requires a successful DDB read and validated loaded-module table address; DDB failure never falls through to an assumed panel.

## Authoritative load/save behaviour

The backend records the exact LUT source path. Live editor changes use kernel validation and transaction/rollback. Square Save calls the kernel persistence syscall, which writes a temporary file, syncs/closes it, then renames it over the authoritative path. The editor never guesses a `ur0`/`ux0` or OLED panel filename.

## Panel color-space and filter boundary

Matched Get/SetDisplayColorSpaceMode exports are used as a session-scoped optional capability on both panel families. v1.4 snapshots the original mode, accepts only 0/1, avoids redundant writes, verifies by read-back and restores the original firmware-owned mode on teardown. No persistent registry mutation or continuous processing is added.

Hardware invert remains capability-gated. Arbitrary CCT/gamma/contrast/nonlinear processing remains unsupported until the corresponding persistent scanout/physical transfer model is independently established.

## Configuration

Configuration lookup is `ur0:tai/vitabrightex.cfg`, then `ux0:tai/vitabrightex.cfg` only if the primary cannot be opened. Successful loads use defaults -> candidate -> validate -> commit. Malformed authoritative config is reported without preventing LiveArea from loading.

## Status ABI / companion editor

`vitabrightGetStatus()` ABI v2 reports model/firmware, panel, synchronization, raw-layout validation, brightness transaction state, invert, panel color-space, unsupported CSC/transfer stages and last error/detail.

The source-controlled editor is built against generated syscall stubs and uses status capabilities to gate controls. The LCD cursor is labelled **`LCD LUT entry X/16`** because it selects a table value; it is not the Vita system brightness slider.

## Build and validation

GitHub Actions gates:

1. architectural source contracts
2. parser/runtime/CI contract
3. exact source/package LUT and config equality
4. production-parser host regressions on exact packaged bytes
5. firmware-audit tool syntax
6. release plugin + syscall stubs under current VitaSDK with warnings as errors
7. diagnostic plugin from the same functional source
8. editor build against generated stubs
9. plugin/editor artifacts

## Hardware testing

Do not replace a known-good taiHEN setup without preserving the exact rollback config and a verified plugin-bypass recovery path. The current pass/fail record and next steps are in [`docs/HARDWARE-TEST-MATRIX.md`](docs/HARDWARE-TEST-MATRIX.md).

The next required check is intentionally narrow: install the corrected release SKPRX plus the **normal commented packaged LCD LUT** on the still-isolated PCH-2000/3.65 unit, perform a fresh cold boot without pressing Circle/reload, and require immediate `firmware_layout/core/table/brightness_hook/power_limit_hook=active` with `last_error=0`. Only then continue persistence, malformed/error paths, invert/color-space, suspend/resume, ioPlus, VitaGrafix and normal-stack compatibility.

## Power/performance

The kernel architecture is event-driven. Work occurs at boot, configuration/editor changes or OS brightness events; no new continuous worker exists. No measured `<0.01 W` claim is made without suitable instrumentation.
