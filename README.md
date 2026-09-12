# VitaBrightEX pseudo-v1.4

VitaBrightEX is a hardening/rearchitecture fork of [devnoname120/vitabright](https://github.com/devnoname120/vitabright). This branch exists because VitaBrightEX 1.3 reproducibly hung a PCH-2000 / firmware 3.65 Ensō system at `PS logo → display on → no LiveArea`, while several v1.3 display paths relied on speculative or unverified assumptions.

> **Development status:** PR #1 remains open, draft and unmerged. The mandatory isolated PCH-2000 / 3.65 Ensō cold boot passed without reproducing the v1.3 hang. Physical 3.65 `SceLcd` runtime validation passed the exact stock-signature gate at segment-0 `0x1B48`. Static decrypted-image verification remains pending. A fresh final-bundle cold boot is still required after the current source-authority/error-lifecycle hardening.

## Design contract

- Optional enhancement failure must never prevent LiveArea from loading.
- Exactly one brightness backend is selected: OLED on PCH-1000, LCD on PCH-2000.
- Backend setup/live replacement is transactional with checked teardown and rollback.
- User writes follow checked copy -> validate -> serialized transition -> commit/rollback.
- LUT persistence is kernel-authoritative and atomic relative to the source actually loaded.
- Fallback occurs only when the preferred source is explicitly absent at OPEN; other open errors and all post-open errors are terminal.
- Independent error domains are authoritative internal state; legacy `last_error/detail` is derived output only.
- No polling thread, timer, periodic filesystem read, framebuffer interception or per-frame software processing is introduced.
- Unknown firmware/layouts do not receive guessed raw offsets.
- A control is exposed only where the hardware interface is sufficiently verified.
- Exact shipped bytes must pass the exact production parser semantics exercised by CI.

See:

- [`docs/V1.4-ARCHITECTURE.md`](docs/V1.4-ARCHITECTURE.md)
- [`docs/SOURCE-AUTHORITY.md`](docs/SOURCE-AUTHORITY.md)
- [`docs/DIAGNOSTICS-ERROR-MODEL.md`](docs/DIAGNOSTICS-ERROR-MODEL.md)
- [`docs/HARDWARE-RESULTS-2026-09-12.md`](docs/HARDWARE-RESULTS-2026-09-12.md)
- [`docs/HARDWARE-TEST-MATRIX.md`](docs/HARDWARE-TEST-MATRIX.md)
- [`docs/FIRMWARE-LAYOUT-VERIFICATION.md`](docs/FIRMWARE-LAYOUT-VERIFICATION.md)

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
| Gamma/nonlinear panel processing | unsupported | unsupported |

The old private `0x0FCBF457` IFTU path remains removed. VitaSDK's documented `ksceIftuCsc()` does not establish the persistent display-head setter assumed by v1.3.

## Authoritative source model

The production-shared `source_authority` core classifies file operations into `USE`, `FALLBACK`, or `FAIL` and records the failing stage.

```text
OPEN succeeds -> source is authoritative
OPEN explicit SCE_ERROR_ERRNO_ENOENT -> fallback eligible
OPEN any other error -> terminal
READ/PARSE/CLOSE error after successful OPEN -> terminal
```

This applies consistently to config, LCD LUT, OLED panel/generic LUT search and explicit OLED override. A broken preferred `ur0` source can no longer silently select stale `ux0` data.

LCD has a compiled safe table only when both documented files are explicitly absent. OLED does not invent a compiled LUT if every documented source is absent.

## Production parser / CI contract

LCD/OLED LUT and config parsing are portable streaming C cores shared by production and host CI. One newline decoder accepts LF and CRLF, rejects lone/interior CR, and never normalizes malformed bytes into valid tokens.

LCD remains exactly 17 decimal `0..255` monotonic-nondecreasing values; OLED remains exactly 17 × 21 two-digit hex bytes. Config numerics require complete tokens, clamp safely after parsing, and duplicate known keys use last-valid textual occurrence. `display_color_space_mode` and `lcd_color_space_mode` are compatibility aliases for the same field and follow that same ordering rule.

## Transaction and rollback semantics

LCD/OLED replacement retains the previous committed table. taiHEN hook/injection releases are checked; handles are forgotten only after confirmed release. Incomplete teardown reports `VBE_ERR_RESOURCE_RELEASE`, leaves the backend degraded and blocks reinitialization over uncertain ownership.

If replacement fails and rollback succeeds, the previous backend returns operational while the requested BRIGHTNESS failure remains visible. If rollback fails, BRIGHTNESS reports `VBE_ERR_LUT_ROLLBACK` and capability state remains degraded.

Rollback uses the actual error-domain state, never the lossy legacy `last_error` summary.

## Diagnostics and reload ownership

`VitaBrightStatus` remains ABI v2 with unchanged layout. `VitaBrightDiagnostics` ABI v1 is additive and exposes synchronization, brightness, config, color-space, filter and input domains.

Each subsystem owns its own truth:

- config load owns CONFIG;
- LCD/OLED transaction and persistence own BRIGHTNESS;
- color-space owns COLOR_SPACE;
- verified filter operation owns FILTER;
- user argument validation owns INPUT;
- state lock owns SYNC.

Generic reload combines return values but does not clear unrelated domains. A repaired config therefore clears CONFIG even if the later LUT transaction fails.

The compatibility summary precedence is:

```text
SYNC > BRIGHTNESS > CONFIG > COLOR_SPACE > FILTER > INPUT
```

## Unsupported filter semantics

Advanced CCT/gamma/contrast/brightness/panel-enhance requests are explicit unsupported capability requests. `VBE_RESULT_UNSUPPORTED` is positive/nonzero so negative results remain actual runtime failures. CSC/transfer stay `UNSUPPORTED`, FILTER diagnostics remain clear, no speculative display write occurs, and the editor reports “unsupported capability” instead of generic failure.

Verified hardware invert remains separately capability-gated.

## Firmware/layout evidence

LCD raw candidates remain 3.60 -> `0x1B00` and 3.65/3.67/3.68/3.69/3.70 -> `0x1B48`. 3.71–3.74 remain unsupported by raw-table injection. Before injection the loaded `SceLcd` must contain the exact Sony stock 17-byte signature at the candidate address.

On the tested physical PCH-2000 / `0x03650000`, the runtime exact-signature gate passed at `0x1B48`. This is physical runtime evidence, not static decrypted-image verification.

OLED requires successful DDB and loaded-module layout validation; DDB failure never becomes an assumed default panel.

## Editor, persistence and build provenance

The backend records the exact LUT source path. Square Save calls kernel persistence, which writes a same-directory temporary file, syncs/closes it, then renames it over the authoritative target. The editor never guesses a `ur0`/`ux0` or OLED panel filename.

Plugin and editor independently embed an 8-character build ID derived from the exact Git SHA. The editor displays `plugin=<id> editor=<id> MATCH/MISMATCH`. Updating `ur0:tai` files does not update the editor VPK.

## Deployment

VitaShell FTP deployment uses absolute mount paths with `curl --ftp-method nocwd`: `...:1337//ur0:/...` and `...:1337//ux0:/...`.

## Build and validation

GitHub Actions gates production-shared LUT parsing, config parsing, source authority, error lifecycle/rollback and unsupported-filter policy; structural source invariants; firmware audit tooling; release/diagnostic SKPRX under warnings-as-errors; generated syscall stubs; matching editor; and a PCH-2000 hardware bundle. The branch-exact workflow also prints SHA-256 for release SKPRX, diagnostic SKPRX, editor VPK and the bundle manifest.

## Hardware evidence — do not over-promote

```text
A1 isolated cold boot: PASS
v1.3 hang: not reproduced
old malformed-LUT fail-open: PASS
3.65 SceLcd segment 0 / 0x1B48: physical runtime exact-signature PASS
static decrypted-image verification: PENDING
stock-vs-extended A/B: PASS
brightness slider sweep: PASS
mid inactivity dim: PASS
very-low behavior: consistent with design
true maximum inactivity: PENDING
suspend/resume: PENDING
normal plugin-stack compatibility: PENDING
A2 overall: PARTIAL
```

The next physical gate is a fresh full-power cold boot using one matching final release bundle with the normal commented production LCD LUT and matching editor, with no Circle/reload before inspection.

## Power/performance

The architecture is event-driven. Work occurs at boot, explicit config/editor changes or OS brightness events; no new continuous worker exists. No measured `<0.01 W` claim is made without suitable instrumentation.
