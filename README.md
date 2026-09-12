# VitaBrightEX pseudo-v1.4

VitaBrightEX is a hardening/rearchitecture fork of [devnoname120/vitabright](https://github.com/devnoname120/vitabright). The v1.4 branch prioritises boot safety, explicit capability reporting and verifiable hardware behaviour over preserving v1.3 controls that were speculative or ineffective.

> **Development status:** the current branch builds against current VitaSDK in CI but is **not release-ready until hardware regression testing is completed**. The first required target is PCH-2000 / firmware 3.65 Ensō, because stock VitaBrightEX 1.3 reproducibly hung that configuration at the PS logo.

## Design contract

- Optional enhancement failure must never prevent LiveArea from loading.
- Exactly one backend is selected: OLED on PCH-1000, LCD on PCH-2000.
- Backend setup is transactional: inject/hook resources are committed only after every required step succeeds; partial setup is unwound.
- Userland writes follow copy → validate → serialized state transition → commit/rollback.
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
| Hardware colour invert | available when SceDisplay export resolves | available when SceDisplay export resolves |
| Live LCD colour-space call | n/a | optional, session-scoped |
| Persistent registry RGB-range mutation | not used | unsupported |
| Persistent active-scanout CSC | unsupported | unsupported |
| Gamma correction | unsupported | unsupported |
| Nonlinear panel linearisation | unsupported | unsupported |

The old `0x0FCBF457` IFTU call has been removed. Current VitaSDK documents `ksceIftuCsc()` as an explicit source/destination framebuffer conversion API under NID `0x67E37EFC`; that is not evidence of the persistent display-head setter assumed by v1.3. Gamma and true panel-linearisation additionally require a verified nonlinear transfer stage.

## Firmware/layout policy

Function exports are resolved by NID, but raw table injection still depends on module layout. Those are separate claims.

The raw-layout whitelist currently preserves only the firmware families present in original VitaBright:

- 3.60
- 3.65
- 3.67
- 3.68
- 3.69
- 3.70

For LCD, the inherited table offsets are:

- 3.60: `0x1B00`
- 3.65/3.67/3.68/3.69/3.70: `0x1B48`

v1.4 additionally resolves that address in the loaded `SceLcd` image and requires the stock 17-byte table signature before injection. A mismatch fails open. **3.71–3.74 are not claimed supported by the brightness-table enhancement until their layouts are independently verified.**

For OLED, a successful `ksceOledGetDDB()` read selects the upstream panel layout:

- DDB low byte `4` → `0x1AB8`
- DDB low byte `5` → `0x1C20`
- other successfully identified DDB types → `0x1E00`

A failed DDB read no longer falls through to a guessed default offset.

## OLED LUT semantics

The 17 × 21-byte OLED table is treated as opaque panel-control/calibration data. v1.4 does **not** perform the old byte-level “RGB bias”, “night mode” or white-point arithmetic because there is insufficient evidence that those bytes are linear RGB samples.

Panel-specific LUT files and raw editor changes are preserved verbatim after structural sanity checks. Legacy configuration keys are still parsed so old files do not break, but they are not advertised as functional processing stages.

## Configuration

Configuration lookup is deterministic:

1. `ur0:tai/vitabrightex.cfg`
2. `ux0:tai/vitabrightex.cfg` only if the `ur0` file is absent

Every reload starts from defaults, parses into a temporary candidate, validates/clamps it, then publishes one complete snapshot. Removing a key therefore restores its default rather than retaining stale runtime state.

See [`vitabrightex.cfg`](vitabrightex.cfg) for the current supported settings.

## Status ABI

`vitabrightGetStatus()` returns ABI version 2 and reports:

- selected hardware and firmware
- detected OLED panel type
- synchronization state
- firmware-layout validation
- brightness core/table/hooks
- invert capability
- LCD live color-space capability
- CSC/nonlinear transfer support
- last error and detail code

The companion editor uses this object as the authority for enabling controls.

## Companion editor

The old v3/v4 editor source was never committed to this repository; only opaque VPK binaries were stored. Pseudo-v1.4 therefore adds a new source-controlled editor under [`editor/`](editor/) instead of pretending to patch an unreproducible binary.

The editor supports capability-gated OLED/LCD LUT editing, verified hardware invert, status/error display, disk persistence and reload. Unsupported CCT/gamma/contrast/linearisation controls are disabled rather than simulated.

## Building

A GitHub Actions workflow builds both plugin and editor against current VitaSDK. Locally:

```sh
export VITASDK=/usr/local/vitasdk
export PATH="$VITASDK/bin:$PATH"

cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake"
cmake --build build --parallel
```

The plugin output is `build/vitabright.skprx`.

To build the editor, install the generated syscall stubs and `libvita2d`, then build `editor/`:

```sh
cmake --install build --prefix "$VITASDK/arm-vita-eabi"
cmake -S editor -B editor-build -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake"
cmake --build editor-build --parallel
```

## Installation for hardware testing

Do not replace a known-good plugin setup without a recovery path. Keep a backup of `ur0:tai/config.txt` and verify that holding `L` during boot bypasses taiHEN plugins on the target setup.

Copy `vitabright.skprx`, the relevant LUT files and `vitabrightex.cfg` into `ur0:tai/`, add this under `*KERNEL`, then reboot:

```text
ur0:tai/vitabright.skprx
```

The exact pre-merge test sequence and pass/fail criteria are in [`docs/HARDWARE-TEST-MATRIX.md`](docs/HARDWARE-TEST-MATRIX.md).

## Power/performance

The kernel architecture is event-driven: work occurs at boot, configuration/editor changes, or when the OS itself changes brightness. There is no newly introduced continuous worker. This makes steady-state software overhead structurally minimal, but the project does **not** claim a measured `<0.01 W` power delta without instrumented hardware measurement.

## Evidence / provenance

Primary implementation references include:

- [VitaSDK vita-headers](https://github.com/vitasdk/vita-headers) for current kernel structures and NIDs
- [devnoname120/vitabright](https://github.com/devnoname120/vitabright) for the original brightness-table layouts and hooks
- [vitabright PR #39](https://github.com/devnoname120/vitabright/pull/39) for the DDB-dependent OLED table selection

Historical VitaBrightEX README statements are not treated as authoritative when they disagree with source or current VitaSDK evidence.
