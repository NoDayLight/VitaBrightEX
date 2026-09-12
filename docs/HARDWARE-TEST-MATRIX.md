# Pseudo-v1.4 hardware validation matrix

No physical-hardware row is passed from source review or CI alone. Kernel/display behaviour must be observed on real hardware before draft PR #1 is merged. Detailed first-unit evidence remains in [`HARDWARE-RESULTS-2026-09-12.md`](HARDWARE-RESULTS-2026-09-12.md).

## Software gate

The exact commit installed for hardware testing must have a fresh branch-exact green workflow for:

- architectural/source structural contracts;
- parser/runtime/deployment structural contracts;
- byte-identical packaged LUT/config assets;
- compiled production LUT parser regressions;
- compiled production config parser regressions;
- compiled production source-authority regressions;
- compiled production transaction/ownership/source-provenance/stop regressions;
- compiled production persistence sequencing/ownership regressions;
- compiled production synchronization lifecycle/result regressions;
- compiled error lifecycle/domain/rollback/stop-domain regressions;
- compiled unsupported-filter policy regressions;
- firmware-audit tooling;
- release plugin/stubs under `-Werror`;
- diagnostic plugin under `-Werror`;
- generated-stub installation;
- matching editor build;
- PCH-2000 hardware-test bundle generation;
- branch-exact payload SHA-256 reporting and artifact uploads.

Semantic proof comes from compiled production-shared C cores, not Python shadow logic or source-text grep. Structural scripts are tripwires only.

## Source-authority rule

See [`SOURCE-AUTHORITY.md`](SOURCE-AUTHORITY.md).

```text
OPEN VBE_SCE_IO_ERROR_NOT_FOUND (0x80010002)
    -> fallback eligible

OPEN any other failure
    -> terminal

OPEN success, then READ/PARSE/CLOSE failure
    -> terminal
```

`VBE_SCE_IO_ERROR_NOT_FOUND` is project-owned compatibility knowledge, not a claimed current VitaSDK generic symbol.

Committed LUT source identity is `FILE`, `COMPILED`, or `NONE`. Only ACTIVE + FILE is normal persistence-eligible. A failed candidate never changes persistence destination; successful rollback restores the previous source identity. A degraded backend cannot persist historical source metadata.

## Ownership/diagnostics rule

See [`DIAGNOSTICS-ERROR-MODEL.md`](DIAGNOSTICS-ERROR-MODEL.md).

Backend ownership:

```text
CLEAN      no backend taiHEN resources owned
ACTIVE     complete committed backend installed
DEGRADED   ownership/recovery not proven clean; no new start
```

A clean failed candidate may be rolled back. A dirty candidate cleanup may not. taiHEN teardown is power hook -> brightness hook -> table injection and stops at the first failed release.

Error domains are independent. `last_error/detail` is only the derived compatibility summary. Runtime unlock failure belongs to SYNC and dominates the scalar syscall return while preserving the operation's own domain error.

## Safety and provenance prerequisites

1. Preserve the exact known-good `ur0:tai/config.txt` and existing plugin/LUT/config backups. Do not reconstruct the normal stack manually.
2. Confirm the Vita boots with that known-good stack and the established recovery/bypass path remains available.
3. Test release SKPRX first. Diagnostic SKPRX is used only when an observed failure needs trace data.
4. Use VitaShell FTP absolute mount syntax with `curl --ftp-method nocwd`: `//ur0:/...` or `//ux0:/...`.
5. Treat kernel plugin and editor VPK as separate components.
6. For diagnostics/provenance/UI gates, install the matching VPK and require `plugin=<sha8> editor=<sha8> MATCH`.
7. Record `WORKFLOW_COMMIT.txt`, workflow/run number and SHA-256 values before transfer; round-trip/hash Vita copies where practical.
8. Never mark firmware supported merely because LiveArea boots; required capability and diagnostic state must also match.
9. Do not use an engineering-intermediate commit when a later known software fix exists.

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

The `0x1B48` observation is physical runtime exact-signature evidence only, not static decrypted-image verification.

### A1. Boot isolation / compatibility progression

Progression remains:

`v1.4 isolated (passed) -> + ioPlus -> + VitaGrafix 5.0.2 + ioPlus -> exact preserved normal plugin stack`

Each stage must reach LiveArea, report LCD/firmware correctly, keep lock/layout/core/table/hooks active with valid input, and keep CSC/transfer explicitly unsupported.

### A2. LCD brightness regression

The real system-slider min -> max -> min sweep is passed. Remaining: true maximum inactivity behavior, application brightness transitions, explicit suspend/resume and repeated reloads.

The editor LUT cursor is not current system brightness. If true-maximum dim differs from prediction, use the separately built diagnostic SKPRX and inspect `[LCD:DIM]` / `[LCD:POWER]` before changing release behavior.

### A3. Final commented-LUT cold-boot checkpoint

Use one exact final green bundle containing matching release SKPRX, normal commented production LCD LUT, production config and matching editor VPK. Full power-off, normal cold boot, and **do not press Circle/reload before inspection**.

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

### A4. Live LCD transaction / source provenance / persistence

After A3 passes:

- make one valid monotonic RAM edit and verify immediate transactional apply;
- press Square and verify kernel-authoritative FILE persistence;
- reload and cold reboot, confirming exact value/source returns;
- test compiled fallback separately: Square must report no authoritative file rather than materializing one;
- test invalid user input;
- test preferred missing + fallback valid;
- test preferred malformed;
- where practical, distinguish preferred open/read/close failure from absence;
- exercise candidate failure with clean rollback success;
- exercise rollback/dirty cleanup failure only through a controlled safe fault-injection mechanism.

Pass semantics:

- malformed/unreadable opened authority never falls through;
- only OPEN-stage explicit absence permits fallback;
- invalid user input mutates no table and belongs to INPUT;
- candidate B failure + rollback A leaves active LUT/source/persistence identity A;
- dirty candidate cleanup forbids rollback/reinit and disables persistence;
- persistence failure leaves committed source identity unchanged and does not report Save success;
- clean rollback failure reports `VBE_ERR_LUT_ROLLBACK`;
- dirty cleanup/recovery reports `VBE_ERR_RESOURCE_RELEASE`;
- editor says previous table restored only when kernel status confirms it.

### A5. Persistence failure stages

Where safe fault injection exists, exercise temp open/write/sync/close/rename/cleanup failures. Required invariants:

```text
pre-commit failure -> authoritative file not replaced
rename failure -> authoritative file not reported committed
failed close -> fd ownership retained until confirmed cleanup
failed temp cleanup -> temp ownership remains unresolved and Save fails
successful rename -> source identity remains final FILE path, never .tmp
```

### A6. Error-domain and synchronization lifecycle

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

operation failure + successful unlock
=> operation failure returned

operation success + unlock failure
=> SYNC failure returned

operation failure + unlock failure
=> SYNC scalar result dominates while operation domain remains visible
```

`last_error` follows documented summary precedence; diagnostics remain full truth.

### A7. Layout conflict

Where safely reproducible, changed stock SceLcd signature must produce layout mismatch before injection.

### A8. Panel color-space transaction / restoration

When capability is usable, record mode, toggle 0 <-> 1 repeatedly, require read-back equality, cover suspend/resume/apps, then perform controlled teardown restoration. Original-state metadata must not be relinquished until restoration read-back confirms the original mode.

### A9. Invert / stop safety

Verify normal invert on/off first. Later controlled stop/fault testing must ensure a failed neutralizing setter cannot be represented as successful unload. No invented invert getter is part of the contract.

### A10. Unsupported filter semantics

Advanced CCT/gamma/contrast/brightness/panel-enhance requests must produce `VBE_RESULT_UNSUPPORTED`, keep CSC/transfer `unsupported`, keep FILTER error zero, and be described by the editor as unsupported rather than generic failure. No speculative framebuffer/IFTU mutation may occur.

## Gate B — PCH-1000 OLED

Use at least one known DDB type 4 or 5; type 6/other successful DDB is desirable. Require correct DDB/panel detection, loaded-module layout plausibility validation, complete transaction, brightness/auto-dim sweep, invalid >16 level rejection, panel/override FILE persistence, strict source authority, clean-vs-dirty rollback semantics, dependency-safe release and color-space read-back/restore.

Panel-specific source order must be tested semantically: absent panel file may advance; malformed/unreadable opened panel file may not fall through to generic. Explicit override never falls back. DDB failure never becomes an assumed default panel.

## Gate C — capability/editor/provenance truthfulness

On both hardware families verify unsupported controls stay disabled; invert/color-space are capability-gated; inactive/degraded backend operations are isolated; Square uses kernel source authority; diagnostics match induced failures; and matching components report `MATCH`. A deliberate old-editor or old-plugin pairing during a non-destructive provenance test must report mismatch.

## Gate D — parser and authority behavior

Host CI is authoritative for exhaustive grammar/decision semantics; hardware is a spot-check. Confirm normal commented LUT cold boot, CRLF acceptance, lone/interior CR rejection, EOF without final newline, long comments, malformed/range/count/non-monotonic failure, malformed preferred no-fallback, explicit missing preferred fallback, and fail-open LiveArea behavior.

Config hardware fault injection should remain conservative. Failed config parsing leaves prior committed config unchanged and sets CONFIG. A later successful config parse clears CONFIG immediately even if a later brightness stage fails.

## Gate E — firmware layouts

Raw-layout attempts remain limited to 3.60, 3.65, 3.67, 3.68, 3.69 and 3.70. **3.65 has physical runtime exact-signature evidence at `0x1B48`; static decrypted-binary evidence remains pending.** 3.71–3.74 remain unsupported.

## Power/performance observation

No continuous worker exists by design. Compare idle behaviour under equivalent screen/radio conditions, but do not publish a `<0.01 W` or similar bound without suitable instrumentation.

## Result recording

For every target record model/panel DDB, firmware/exploit, exact plugin stack, commit/build IDs, artifact hashes, release/diagnostic identity, full status + diagnostics snapshot, source identity/fault stage where relevant, runtime/static layout evidence separately, gate results and recovery action.

PR #1 remains draft until representative physical coverage is complete.
