# Pseudo-v1.4 hardware validation matrix

No physical-hardware row is passed from source review or CI alone. Kernel/display behaviour must be observed on real hardware before draft PR #1 is merged. Detailed first-unit evidence remains in [`HARDWARE-RESULTS-2026-09-12.md`](HARDWARE-RESULTS-2026-09-12.md).

## Software gate

The exact commit installed for hardware testing must have green branch-exact checks for:

- architectural/source contracts;
- parser/runtime/deployment structural contracts;
- byte-identical packaged LUT/config assets;
- compiled production LUT parser regressions;
- compiled production config parser regressions;
- compiled production source-authority regressions;
- compiled error lifecycle/domain/rollback regressions;
- compiled unsupported-filter policy regressions;
- firmware-audit tooling;
- release plugin/stubs under `-Werror`;
- diagnostic plugin;
- generated-stub installation;
- matching editor build;
- PCH-2000 hardware-test bundle generation;
- branch-exact payload SHA-256 reporting.

Semantic proof comes from compiled production-shared C cores, not a Python shadow grammar or source-text grep. Structural scripts remain tripwires only.

## Source-authority hardware rule

See [`SOURCE-AUTHORITY.md`](SOURCE-AUTHORITY.md). Every hardware authority test must distinguish source absence from source failure:

```text
OPEN explicit ENOENT
    -> fallback eligible

OPEN any other failure
    -> terminal

OPEN success, then READ/PARSE/CLOSE failure
    -> terminal
```

A preferred file that exists but cannot be opened/read/parsed/closed must never silently select a lower-priority source. A post-open failure is terminal even if its raw error integer equals ENOENT.

## Diagnostics rule

See [`DIAGNOSTICS-ERROR-MODEL.md`](DIAGNOSTICS-ERROR-MODEL.md). Error domains are independent. Hardware observations must verify the domain repaired by an operation clears independently of later-stage failures. `last_error/detail` is only the derived compatibility summary.

## Safety and provenance prerequisites

1. Preserve the exact known-good `ur0:tai/config.txt` and existing plugin/LUT/config backups. Do not reconstruct the normal stack manually.
2. Confirm the Vita boots with that known-good stack and the established recovery/bypass path remains available.
3. Test release SKPRX first. Diagnostic SKPRX is used only when an observed failure needs trace data.
4. Use VitaShell FTP absolute mount syntax with `curl --ftp-method nocwd`: `//ur0:/...` or `//ux0:/...`.
5. Treat kernel plugin and editor VPK as separate components.
6. For diagnostics/provenance/UI gates, install the matching VPK and require `plugin=<sha8> editor=<sha8> MATCH`.
7. Record `WORKFLOW_COMMIT.txt`, workflow/run number and SHA-256 values before transfer; round-trip/hash Vita copies where practical.
8. Never mark firmware supported merely because LiveArea boots; required capability and diagnostic state must also match.

## Gate A — PCH-2000 / 3.65 Ensō

Target: physical PCH-2000, system software `0x03650000`, 3.65 Ensō.

### Recorded evidence — unchanged

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

The `0x1B48` observation is physical runtime exact-signature evidence only. It is not static decrypted-image verification.

### A1. Boot isolation / compatibility progression

Progression remains:

`v1.4 isolated (passed) -> + ioPlus -> + VitaGrafix 5.0.2 + ioPlus -> exact preserved normal plugin stack`

Each stage must reach LiveArea, report LCD/firmware correctly, keep lock/layout/core/table/hooks active with valid input, and keep CSC/transfer explicitly unsupported.

### A2. LCD brightness regression

The real system-slider min -> max -> min sweep is passed. Remaining: true maximum inactivity behavior, application brightness transitions, explicit suspend/resume and repeated reloads.

The editor LUT cursor is not current system brightness. If true-maximum dim differs from prediction, use the separately built diagnostic SKPRX and inspect `[LCD:DIM]` / `[LCD:POWER]` before changing release behavior.

### A3. Final pre-hardware cold-boot checkpoint

Use one exact green final bundle containing matching release SKPRX, normal commented production LCD LUT, config and matching editor VPK. Full power-off, normal cold boot, and **do not press Circle/reload before inspection**.

Required initial result:

```text
Build plugin=<sha8> editor=<sha8> MATCH
Hardware: PCH-2000 LCD
firmware: 0x03650000
ABI: 2
Core=active
table=active
layout=active
lock=active
Brightness hook=active
power hook=active
Last kernel error: 0
detail: 0x00000000
Errors cfg=0 bright=0 color=0 filter=0 input=0 sync=0
```

Any mismatch/unresolved error stops the gate. Circle must not be used to hide startup state.

### A4. Live LCD transaction and persistence

After A3 passes:

- make one valid monotonic RAM edit and verify immediate transactional apply;
- press Square and verify kernel-authoritative persistence;
- reload and cold reboot, confirming exact value/source returns;
- test invalid user input;
- test preferred missing + fallback valid;
- test preferred malformed;
- where practical, test preferred open/read/close failure separately;
- exercise replacement failure with rollback success;
- exercise rollback failure only by a controlled/safe injection mechanism.

Pass semantics:

- malformed or unreadable opened authority never falls through;
- only OPEN-stage explicit absence permits fallback;
- invalid user input mutates no table and belongs to INPUT;
- persistence failure belongs to BRIGHTNESS and retains the old persisted file;
- replacement failure + successful rollback restores active backend but retains the requested BRIGHTNESS failure;
- rollback failure reports `VBE_ERR_LUT_ROLLBACK` and degraded capability state;
- incomplete taiHEN teardown reports `VBE_ERR_RESOURCE_RELEASE` and must not stack a new transaction over uncertain handles.

### A5. Error-domain lifecycle integration

Induce only safe/recoverable faults and verify:

```text
old CONFIG error
-> fix config
-> later BRIGHTNESS failure
=> cfg=0, bright!=0

old BRIGHTNESS error
-> brightness repair
-> later COLOR_SPACE failure
=> bright=0, color!=0

CONFIG still broken
-> brightness succeeds
=> cfg!=0, bright=0

all repaired
=> relevant domains all clear
```

`last_error` should follow the documented summary precedence, but diagnostics remain the full truth.

### A6. Layout conflict

Where safely reproducible, a changed stock SceLcd signature must produce layout mismatch before injection.

### A7. Panel color-space transaction

When capability is usable, record mode, toggle 0 <-> 1 repeatedly, require read-back equality, then cover suspend/resume/apps and controlled teardown restoration. Color-space success must not clear CONFIG/BRIGHTNESS/INPUT errors it did not repair.

### A8. Unsupported filter semantics

Advanced CCT/gamma/contrast/brightness/panel-enhance requests must produce the named `VBE_RESULT_UNSUPPORTED` capability result, keep CSC/transfer `unsupported`, keep FILTER error zero, and be described by the editor as unsupported rather than generic failure. No speculative framebuffer/IFTU mutation may occur.

## Gate B — PCH-1000 OLED

Use at least one known DDB type 4 or 5; type 6/other successful DDB is desirable. Require correct DDB/panel detection, loaded-module layout plausibility validation, complete LUT transaction, brightness/auto-dim sweep, invalid >16 level rejection, authoritative panel/override persistence, strict source-authority fallback, rollback/release semantics and color-space read-back/restore.

Panel-specific source order must be tested semantically: absent panel file may advance; malformed/unreadable opened panel file may not fall through to generic. Explicit override path never falls back. DDB failure never becomes an assumed default panel.

## Gate C — capability/editor/provenance truthfulness

On both hardware families verify unsupported controls stay disabled; invert/color-space are capability-gated; inactive backend remains isolated; Square uses kernel authority; diagnostics domains match induced failures; and matching components report `MATCH`. A deliberate old-editor or old-plugin pairing during a non-destructive provenance test must report mismatch.

## Gate D — parser and authority behavior

Host CI is authoritative for exhaustive grammar/decision semantics; hardware is a spot-check. Confirm normal commented LUT cold boot, CRLF acceptance, lone/interior CR rejection, EOF without final newline, long comments, malformed/range/count/non-monotonic failure, malformed preferred no-fallback, explicit missing preferred fallback, and fail-open LiveArea behavior.

Config hardware fault injection should remain conservative. Failed config parsing leaves the prior committed config unchanged and sets CONFIG. A later successful config parse clears CONFIG immediately even if a later brightness stage fails.

The alias pair `display_color_space_mode` / `lcd_color_space_mode` is covered in host parser regression as textual last-valid-occurrence-wins behavior.

## Gate E — firmware layouts

Raw-layout attempts remain limited to 3.60, 3.65, 3.67, 3.68, 3.69 and 3.70. **3.65 has physical runtime exact-signature evidence at `0x1B48`; static decrypted-binary evidence remains pending.** 3.71–3.74 remain unsupported.

## Power/performance observation

No continuous worker exists by design. Compare idle behavior under equivalent screen/radio conditions, but do not publish a `<0.01 W` or similar bound without suitable instrumentation.

## Result recording

For every target record model/panel DDB, firmware/exploit, exact plugin stack, commit/build IDs, artifact hashes, release/diagnostic identity, full status + diagnostics snapshot, source-authority fault stage where relevant, runtime/static layout evidence separately, gate results and recovery action.

PR #1 remains draft until representative physical coverage is complete.
