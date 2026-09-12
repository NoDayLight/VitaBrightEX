# VitaBrightEX pseudo-v1.4

VitaBrightEX is a hardening/rearchitecture fork of [devnoname120/vitabright](https://github.com/devnoname120/vitabright). This branch exists because VitaBrightEX 1.3 reproducibly hung a PCH-2000 / firmware 3.65 Ensō system at `PS logo → display on → no LiveArea`, while multiple v1.3 display features were speculative, ineffective, or based on incorrect assumptions.

> **Development status:** PR #1 remains draft. The mandatory isolated **PCH-2000 / 3.65 Ensō** cold boot passed without reproducing the v1.3 hang, and the physical 3.65 `SceLcd` image passed the production exact-signature gate at segment-0 `0x1B48`. That run also exposed a real shipped-LUT/parser/CI contract defect. The branch now executes the same LUT/config parser cores used by the SKPRX in host CI, uses strict LF/CRLF semantics, exposes build provenance and independent error domains, and requires another fresh cold boot with the normal commented packaged LUT before proceeding.

## Design contract

- Optional enhancement failure must never prevent LiveArea from loading.
- Exactly one brightness backend is selected: OLED on PCH-1000, LCD on PCH-2000.
- Backend setup/live replacement is transactional with unwind/rollback.
- User writes follow checked copy -> validate -> serialized transition -> commit/rollback.
- LUT persistence is kernel-authoritative and atomic relative to the source actually loaded.
- No polling thread, timer, periodic filesystem read, framebuffer interception or per-frame software processing is introduced.
- Unknown firmware/layouts do not receive guessed raw offsets.
- A control is exposed only where the hardware interface is sufficiently verified.
- The exact bytes shipped must pass the exact production parser semantics exercised by CI.

See [`docs/V1.4-ARCHITECTURE.md`](docs/V1.4-ARCHITECTURE.md), [`docs/HARDWARE-RESULTS-2026-09-12.md`](docs/HARDWARE-RESULTS-2026-09-12.md) and [`docs/HARDWARE-TEST-MATRIX.md`](docs/HARDWARE-TEST-MATRIX.md).

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

The old private `0x0FCBF457` IFTU path is removed. VitaSDK's documented `ksceIftuCsc()` is a source/destination framebuffer conversion API and does not establish the persistent display-head setter assumed by v1.3.

## Production text-parser / CI contract

Physical testing found that the previous LCD parser required an entire physical line to fit in `char line[64]`, while CI ignored comment lines without that limit. The normal commented packaged LUT therefore passed workflow #114 but production rejected the same bytes before `SceLcd` initialization.

The correction removes the contract class rather than shortening comments. LCD/OLED LUT and config parsing are portable streaming C cores used by both production and host CI. Full-line `#`/`;` comments are streamed without a physical-line limit. A shared newline decoder accepts LF and CRLF, but rejects lone/interior CR; malformed sequences such as `2\r55` and `A\rF` are never normalized into valid tokens.

LCD remains exactly 17 decimal `0..255` monotonic-nondecreasing values; OLED remains exactly 17 x 21 two-digit hex bytes. Existing malformed authoritative files fail instead of falling through; only a missing preferred source may use documented fallback.

Config values are also production/host-shared. Known numeric values require complete tokens, so suffix garbage is rejected. Extreme valid numbers are safely clamped. Unknown keys and missing-`=` lines are ignored for compatibility; an empty numeric value fails; duplicate known keys use the last valid occurrence.

CI compiles and executes the exact production LUT parser, config parser and error-domain cores against exact packaged assets plus long-comment/directive, LF/CRLF, lone/interior CR, EOF, range/count/monotonicity, malformed numeric and duplicate-key regressions.

## Firmware/layout evidence

LCD raw candidates remain 3.60 -> `0x1B00` and 3.65/3.67/3.68/3.69/3.70 -> `0x1B48`. 3.71–3.74 remain unsupported by the raw-table feature. Before injection the loaded `SceLcd` must contain the exact Sony stock 17-byte table at the candidate address.

On the tested physical PCH-2000 / `0x03650000`, this runtime exact-signature gate **passed at `0x1B48`**, followed by successful injection/hooks and `last_error=0`. This is physical runtime evidence, not static decrypted-image verification; static evidence remains pending in [`docs/FIRMWARE-LAYOUT-VERIFICATION.md`](docs/FIRMWARE-LAYOUT-VERIFICATION.md).

OLED requires a successful DDB read and validated loaded-module table address; DDB failure never falls through to an assumed panel.

## Authoritative load/save behaviour

The backend records the exact LUT source path. Live editor changes use kernel validation and transaction/rollback. Square Save calls the kernel persistence syscall, which writes a temporary file, syncs/closes it, then renames it over the authoritative path. The editor never guesses a `ur0`/`ux0` or OLED panel filename.

## Panel color-space and filter boundary

Matched Get/SetDisplayColorSpaceMode exports are a session-scoped optional capability on both panel families. v1.4 snapshots the original mode, accepts only 0/1, avoids redundant writes, verifies by read-back and restores firmware-owned state on teardown. No persistent registry mutation or continuous processing is added.

Hardware invert remains capability-gated. Arbitrary CCT/gamma/contrast/nonlinear processing remains unsupported until the corresponding persistent scanout/physical transfer model is independently established.

## Status, diagnostics and build provenance

`vitabrightGetStatus()` remains **ABI v2 with unchanged struct layout**. Its legacy `last_error/detail` is a deterministic summary of independent synchronization, brightness, config, color-space, filter and user-input error domains. A success clears only the domain it actually repairs, so a later display/editor success cannot erase an unresolved authoritative config failure.

`vitabrightGetDiagnostics()` adds diagnostics ABI v1 for the individual domains without enlarging status ABI v2. Development builds also expose `vitabrightGetBuildId()`: plugin and editor embed an 8-character ID derived from the actual Actions `GITHUB_SHA` or local Git HEAD. The matching editor displays both IDs and `MATCH`/`MISMATCH` on-device.

The editor is a separate VPK application. Updating `ur0:tai` kernel files does not update it; tests that rely on current provenance/diagnostics/UI must install the matching VPK from the same hardware-test bundle.

## Deployment contract

Project FTP deployment and user-facing hardware instructions use VitaShell absolute mount paths with `curl --ftp-method nocwd`: `...:1337//ur0:/...` and `...:1337//ux0:/...`. CMake constructs destinations from centralized absolute roots; single-slash relative forms are source-contract failures.

## Build and validation

GitHub Actions gates architectural/source invariants; parser/runtime/deployment structure; byte-identical packaged LUT/config copies; compiled production LUT/config/error-state host regressions; firmware-audit tooling; release plugin/stubs with warnings-as-errors; diagnostic plugin; generated syscall stubs; matching editor; and a PCH-2000 bundle containing release SKPRX, normal commented LUT, config, matching VPK, commit marker, README and SHA-256 manifest.

## Hardware testing

Preserve the exact known-good taiHEN rollback config and recovery path. The next required check remains narrow: install the matching release kernel-side bundle **and matching editor VPK** on the still-isolated PCH-2000/3.65 unit, use the normal commented packaged LCD LUT, then fresh cold boot without Circle/reload. Require plugin/editor build IDs `MATCH`, active layout/core/table/hooks, `last_error=0`, and zero unresolved diagnostics domains. Only then continue persistence, malformed/error paths, invert/color-space, suspend/resume, ioPlus, VitaGrafix and exact normal-stack compatibility.

## Power/performance

The kernel architecture is event-driven. Work occurs at boot, configuration/editor changes or OS brightness events; no new continuous worker exists. No measured `<0.01 W` claim is made without suitable instrumentation.
