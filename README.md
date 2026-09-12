# VitaBrightEX pseudo-v1.4

VitaBrightEX is a hardening/rearchitecture fork of [devnoname120/vitabright](https://github.com/devnoname120/vitabright). This branch exists because VitaBrightEX 1.3 reproducibly hung a PCH-2000 / firmware 3.65 Ensō system at `PS logo → display on → no LiveArea`, while multiple v1.3 display features were either speculative, ineffective, or built on incorrect assumptions.

> **Development status:** software/CI hardening is being completed before physical regression. The PR remains draft until the hardware matrix passes, beginning with the reproducing PCH-2000 / 3.65 Ensō console.

## Design contract

- Optional enhancement failure must never prevent LiveArea from loading.
- Exactly one backend is selected: OLED on PCH-1000, LCD on PCH-2000.
- Backend setup and live replacement are transactional with rollback/fail-open behaviour.
- Userland writes follow copy → validate → serialized state transition → commit/rollback.
- LUT persistence is kernel-authoritative and atomic relative to the source actually loaded.
- No polling threads, timers, periodic filesystem reads, framebuffer interception or per-frame software processing are introduced.
- Unknown firmware/layouts do not receive guessed kernel offsets.
- A control is exposed only when its implementation is technically supported.

See [`docs/V1.4-ARCHITECTURE.md`](docs/V1.4-ARCHITECTURE.md).

## Current capabilities

| Capability | PCH-1000 OLED | PCH-2000 LCD |
|---|---|---|
| Extended brightness | implemented, transactional | implemented, transactional |
| Inactivity-dim workaround | implemented | implemented |
| Power-mode brightness hook | implemented | implemented |
| Panel/LUT selection | DDB-based | n/a |
| Live LUT editing | validated + rollback | monotonic validation + rollback |
| Atomic authoritative LUT save | implemented | implemented |
| Hardware colour invert | available when SceDisplay export resolves | available when SceDisplay export resolves |
| Panel color-space Get/Set | optional, read-back verified, restored on stop | optional, read-back verified, restored on stop |
| Persistent registry RGB-range mutation | not used | not used |
| Persistent active-scanout CSC | unsupported | unsupported |
| Gamma correction | unsupported | unsupported |
| Nonlinear panel linearisation | unsupported | unsupported |

The old `0x0FCBF457` IFTU call has been removed. Current VitaSDK documents `ksceIftuCsc()` as an explicit source/destination framebuffer conversion API under NID `0x67E37EFC`; that is not evidence for the persistent display-head setter assumed by v1.3. Gamma and true panel linearisation additionally require a verified nonlinear transfer stage.

## Firmware/layout policy

Function exports are resolved by NID, but raw brightness-table injection still depends on module layout. Those are separate claims.

The raw-layout whitelist currently preserves only firmware families present in original VitaBright:

- 3.60
- 3.65
- 3.67
- 3.68
- 3.69
- 3.70

For LCD, the inherited candidate offsets are:

- 3.60: `0x1B00`
- 3.65/3.67/3.68/3.69/3.70: `0x1B48`

v1.4 resolves the address in loaded `SceLcd` and requires the **exact stock 17-byte table signature** before injection. A mismatch fails open. 3.71–3.74 are not claimed supported by this raw-table feature.

Static decrypted-binary verification is tracked separately in [`docs/FIRMWARE-LAYOUT-VERIFICATION.md`](docs/FIRMWARE-LAYOUT-VERIFICATION.md). `tools/verify_scelcd_layout.py` correctly maps a segment-0-relative taiHEN offset through a decrypted ELF before comparing bytes; encrypted SKPRX data is not falsely treated as verification evidence.

For OLED, a successful `ksceOledGetDDB()` read selects the inherited panel layout:

- DDB low byte `4` → `0x1AB8`
- DDB low byte `5` → `0x1C20`
- DDB low byte `6` / another successfully read type → `0x1E00`

Before injection the address is resolved in loaded `SceOled` and the 357-byte object must pass structural plausibility checks. A failed DDB read never falls through to a guessed default panel.

## OLED LUT semantics

The 17 × 21-byte OLED table is not a normal framebuffer RGB LUT. Panel documentation establishes that each row contains seven RGB gamma-reference triplets, but an independently verified register-code → voltage/transfer model for those references is still missing.

Therefore pseudo-v1.4 does **not** perform the old white-point normalization, byte-level RGB bias or night-mode arithmetic. Those operations were not removed merely because they are difficult; they remain disabled because their physical mapping cannot currently be verified.

Panel-specific LUT files and explicit raw editor changes are preserved after structural checks. Legacy configuration keys remain parseable so old files do not break.

## Authoritative load / save behaviour

LUT files use one authority at a time:

- a missing preferred file may use its documented fallback
- an existing malformed preferred/override file is an explicit failure and does not silently fall through
- the backend records the exact source path that produced the committed table
- live editor updates go through kernel validation and rollback
- Square in the editor calls the backend's kernel persistence syscall
- the kernel writes a temporary file, synchronizes it, then renames it over the authoritative target

This prevents custom OLED overrides or `ur0`/`ux0` fallback state from being saved to the wrong guessed filename.

## Panel color-space control

VitaSDK independently names matched Get/SetDisplayColorSpaceMode exports for both `SceOled` and `SceLcd`. Pseudo-v1.4 uses those as a single optional capability:

- snapshot the original firmware-owned mode
- only accept defined modes 0/1
- avoid redundant writes
- set and immediately read back the requested mode
- restore the original mode on plugin teardown

No registry value is persistently changed and no continuous processing is introduced.

`rgb_range_mode` is intentionally not restored: independent registry documentation identifies it as a PSTV-oriented setting with `0=Auto`, `1=Limited`, `2=Full`, contradicting the old VitaBrightEX claim that `1` meant full range on PCH-2000.

## Configuration

Configuration lookup is deterministic:

1. `ur0:tai/vitabrightex.cfg`
2. `ux0:tai/vitabrightex.cfg` only if the `ur0` file is absent

Every reload starts from defaults, parses into a temporary candidate, validates/clamps it, then publishes one complete snapshot. Removing a key therefore restores its default instead of retaining stale state.

See [`vitabrightex.cfg`](vitabrightex.cfg) for current supported settings.

## Status ABI

`vitabrightGetStatus()` ABI v2 reports selected hardware/firmware, OLED panel, synchronization state, firmware-layout validation, brightness transaction state, invert, panel color-space, unsupported CSC/transfer stages, and the last error/detail. The companion editor treats this object as the authority for enabling controls.

## Companion editor

The historical later editor versions in this repository were shipped as VPK binaries without corresponding reproducible source. Pseudo-v1.4 adds a new source-controlled editor under [`editor/`](editor/) and builds it against generated syscall stubs.

It provides capability-gated OLED/LCD LUT editing, kernel-authoritative atomic save, hardware invert, panel color-space Get/Set, status/error display and reload. Unsupported CCT/gamma/contrast/linearisation controls are not simulated.

## Build and validation

GitHub Actions now gates more than compilation:

1. architectural source-contract checks
2. packaged OLED/LCD LUT structure and source/package equality
3. packaged config equality
4. release plugin + syscall stubs under current VitaSDK with warnings as errors
5. diagnostic plugin from the same functional code
6. editor build against generated stubs
7. plugin/editor artifacts

Local plugin build:

```sh
export VITASDK=/usr/local/vitasdk
export PATH="$VITASDK/bin:$PATH"
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake"
cmake --build build --parallel
```

Then install generated stubs and build the editor:

```sh
cmake --install build --prefix "$VITASDK/arm-vita-eabi"
cmake -S editor -B editor-build -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake"
cmake --build editor-build --parallel
```

## Installation for hardware testing

Do not replace a known-good plugin setup without a recovery path. Keep a backup of `ur0:tai/config.txt` and verify that holding `L` during boot bypasses taiHEN plugins on the target setup.

Copy the CI-built `vitabright.skprx`, relevant LUT files and `vitabrightex.cfg` into `ur0:tai/`, add this under `*KERNEL`, then reboot:

```text
ur0:tai/vitabright.skprx
```

Do not use the historical prebuilt binaries in `release/` as evidence for the current pseudo-v1.4 source. The exact pre-merge sequence is in [`docs/HARDWARE-TEST-MATRIX.md`](docs/HARDWARE-TEST-MATRIX.md).

## Power/performance

The kernel architecture is event-driven: work occurs at boot, configuration/editor changes, or when the OS itself changes brightness. There is no newly introduced continuous worker. This makes steady-state software overhead structurally minimal, but the project does **not** claim a measured `<0.01 W` delta without instrumented hardware measurement.

## Evidence / provenance

Primary implementation references include:

- [VitaSDK vita-headers](https://github.com/vitasdk/vita-headers) for current kernel structures and NIDs
- [devnoname120/vitabright](https://github.com/devnoname120/vitabright) for original brightness-table layouts/hooks
- [vitabright PR #39](https://github.com/devnoname120/vitabright/pull/39) for DDB-dependent OLED table selection
- Team Molecule `sceutils`, VitaDeploy and Vita3K as reproducible firmware-audit tooling routes documented in the firmware-layout note

Historical VitaBrightEX README/changelog claims are not treated as authoritative when they disagree with source or independently verifiable platform evidence.
