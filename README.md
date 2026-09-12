# VitaBrightEX pseudo-v1.4

VitaBrightEX is a hardening/rearchitecture fork of [devnoname120/vitabright](https://github.com/devnoname120/vitabright). This branch exists because VitaBrightEX 1.3 reproducibly hung a PCH-2000 / firmware 3.65 Ensō system at `PS logo → display on → no LiveArea`, while several v1.3 display paths relied on speculative or unverified assumptions.

> **Development status:** PR #1 remains open, draft and unmerged. The mandatory isolated PCH-2000 / 3.65 Ensō cold boot passed without reproducing the v1.3 hang. Physical 3.65 `SceLcd` runtime validation passed the exact stock-signature gate at segment-0 `0x1B48`. Static decrypted-image verification remains pending. The transaction/ownership/persistence/stop rewrite is software-verified only until a fresh final-bundle cold boot and later controlled fault-path testing.

## Design contract

- Optional enhancement failure at boot must never prevent LiveArea from loading.
- Exactly one brightness backend is selected: OLED on PCH-1000, LCD on PCH-2000.
- Runtime mutation starts only from a known ownership state; uncertain resource ownership stops further backend mutation.
- Backend setup/live replacement uses explicit CLEAN / ACTIVE / DEGRADED ownership and clean-vs-dirty transaction outcomes.
- taiHEN teardown is dependency-safe and stops at the first failed release.
- User writes follow checked copy -> validate -> serialized transition -> commit/rollback -> checked unlock.
- LUT source identity is `NONE`, `FILE + path`, or `COMPILED`; source identity commits only with the hardware transaction.
- LUT persistence is kernel-authoritative and transactional relative to the committed FILE source.
- Compiled LCD fallback has no guessed file target; normal Save returns `VBE_RESULT_NO_FILE_SOURCE`.
- Fallback occurs only when the preferred source is explicitly absent at OPEN; other open errors and all post-open errors are terminal.
- Independent error domains are authoritative internal state; legacy `last_error/detail` is derived output only.
- Module unload succeeds only after session display state, backend resources and synchronization ownership are confirmed restored/released.
- No polling thread, timer, periodic filesystem read, framebuffer interception or per-frame software processing is introduced.
- Unknown firmware/layouts do not receive guessed raw offsets.
- A control is exposed only where the hardware interface is sufficiently verified.
- Exact shipped bytes and transaction semantics must pass the same production-shared C cores exercised by CI.

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
| Live LUT editing | validation + clean rollback/degraded stop | monotonic validation + clean rollback/degraded stop |
| Authoritative LUT save | FILE source only | FILE source only; COMPILED explicitly has no save target |
| Hardware colour invert | when SceDisplay export resolves | when SceDisplay export resolves |
| Panel color-space Get/Set | read-back verified | read-back verified |
| Persistent registry RGB-range mutation | not used | not used |
| Persistent active-scanout CSC | unsupported | unsupported |
| Gamma/nonlinear panel processing | unsupported | unsupported |

The old private `0x0FCBF457` IFTU path remains removed. VitaSDK's documented `ksceIftuCsc()` does not establish the persistent display-head setter assumed by v1.3.

## Source authority and provenance

The production-shared source core distinguishes OPEN success, explicit no-entry, and other I/O errors. Once a file opens, read/parse/close failure is terminal.

```text
OPEN success -> authoritative source
OPEN VBE_SCE_IO_ERROR_NOT_FOUND -> fallback eligible
OPEN other error -> terminal
READ/PARSE/CLOSE after OPEN -> terminal
```

`VBE_SCE_IO_ERROR_NOT_FOUND` is the project-owned compatibility name for `0x80010002`. Current public VitaSDK generic headers do not expose a generic `SCE_ERROR_ERRNO_ENOENT` symbol used here; the value is corroborated by Vita ecosystem behavior including h-encore/Vita3K. The project does not claim SDK ownership of that symbol.

LCD has a compiled safe table only when both documented LUT files are explicitly absent. That candidate commits as `COMPILED`, not as a fake `ur0` path. OLED does not invent a compiled LUT.

## Parser / CI contract

LCD/OLED LUT and config parsing are portable streaming C cores shared by production and host CI. One newline decoder accepts LF/CRLF, rejects lone/interior CR, and never normalizes malformed bytes into valid tokens.

LCD remains exactly 17 decimal `0..255` monotonic-nondecreasing values; OLED remains exactly 17 × 21 two-digit hex bytes. Config numerics require complete tokens, clamp safely after parsing, and duplicate known keys use last-valid textual occurrence. `display_color_space_mode` and `lcd_color_space_mode` are compatibility aliases for the same field and follow that same ordering rule.

## Ownership and rollback

Backend ownership has three internal states:

```text
CLEAN     no backend taiHEN resource owned
ACTIVE    complete committed backend installed
DEGRADED retained/uncertain ownership; no new transaction may start
```

Transaction attempts are `TXN_OK`, `TXN_FAILED_CLEAN`, or `TXN_FAILED_DIRTY`. `ret < 0` alone never authorizes rollback.

Teardown order is:

```text
power hook -> brightness hook -> table injection
```

and stops at the first failed release. Handles are forgotten only after confirmed release.

Clean candidate failure may rollback the previous committed LUT/source. Dirty candidate cleanup immediately leaves DEGRADED ownership and forbids rollback/reinitialization. Clean rollback failure reports `VBE_ERR_LUT_ROLLBACK`; dirty cleanup/recovery failure reports `VBE_ERR_RESOURCE_RELEASE`. Recovery failure dominates the scalar public result.

## Persistence

Normal Save is legal only for an ACTIVE backend with a FILE source. The persistence transaction tracks fd/temp ownership and performs:

```text
prepare same-directory temp
-> open
-> write committed LUT
-> sync
-> close
-> rename to authoritative path
```

Rename is the commit point. Pre-commit failure cannot rename. fd/temp ownership is relinquished only after confirmed close/remove/rename. The committed source remains the authoritative final path; the temp path never becomes source identity.

A committed LCD COMPILED fallback returns positive `VBE_RESULT_NO_FILE_SOURCE` rather than silently creating `ur0:tai/vitabright_lcd_lut.txt`.

## Diagnostics and synchronization

`VitaBrightStatus` remains ABI v2 with unchanged layout. `VitaBrightDiagnostics` ABI v1 remains additive and exposes synchronization, brightness, config, color-space, filter and input domains.

Each subsystem owns its own domain. Runtime operations use one shared operation+unlock result rule: unlock failure dominates the scalar return while the operation's own domain truth remains stored independently.

The compatibility summary precedence remains:

```text
SYNC > BRIGHTNESS > CONFIG > COLOR_SPACE > FILTER > INPUT
```

## Stop safety

Module stop enters a quiesced synchronization state, restores invert, restores/read-backs original panel color-space, tears down persistence/backend resources, then unlocks/deletes the mutex. A tiny stop accumulator answers only whether unload is safe; detailed failures remain in their subsystem domains.

If any stop-critical restore/release cannot be confirmed, `module_stop()` returns `SCE_KERNEL_STOP_FAIL` and the plugin remains resident. Mutex identity is forgotten only after confirmed delete.

## Unsupported filter semantics

Advanced CCT/gamma/contrast/brightness/panel-enhance requests are explicit unsupported capability outcomes. `VBE_RESULT_UNSUPPORTED` is positive/nonzero, CSC/transfer remain `UNSUPPORTED`, FILTER remains clear, no speculative display write occurs, and the editor reports “unsupported capability” rather than generic failure.

Verified hardware invert remains separately capability-gated.

## Firmware/layout evidence

LCD raw candidates remain 3.60 -> `0x1B00` and 3.65/3.67/3.68/3.69/3.70 -> `0x1B48`. 3.71–3.74 remain unsupported by raw-table injection. Before injection the loaded `SceLcd` must contain the exact Sony stock 17-byte signature at the candidate address.

On the tested physical PCH-2000 / `0x03650000`, the runtime exact-signature gate passed at `0x1B48`. This is physical runtime evidence, not static decrypted-image verification.

OLED requires successful DDB and loaded-module layout validation; DDB failure never becomes an assumed default panel.

## Editor and build provenance

Plugin and editor independently embed an 8-character build ID generated from the build checkout. The editor displays `plugin=<id> editor=<id> MATCH/MISMATCH`; replacing `ur0:tai` files does not update the separately installed editor VPK.

After a LUT mutation the editor refreshes kernel status/diagnostics before choosing wording. It reports “previous committed table restored” only when the backend is operational and recovery diagnostics do not indicate rollback/resource-release failure. Degraded recovery and compiled-source persistence have separate truthful messages.

## Deployment

VitaShell FTP deployment uses absolute mount paths with `curl --ftp-method nocwd`: `...:1337//ur0:/...` and `...:1337//ux0:/...`.

## Build and validation

GitHub Actions compiles/runs production-shared regressions for LUT parser, config parser, source authority, transaction/ownership/source provenance/stop, persistence ownership/sequencing, synchronization lifecycle/result composition, diagnostics/error lifecycle and unsupported-filter policy. Structural checks remain tripwires only.

Release and diagnostic SKPRX, generated syscall stubs, matching editor and PCH-2000 bundle build under current VitaSDK with warnings treated as errors.

The exact final pre-hardware checkpoint SHA/run/hashes are recorded only after the last documentation/PR write and a fresh branch-exact workflow, avoiding self-invalidating provenance.

## Hardware evidence — unchanged

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
