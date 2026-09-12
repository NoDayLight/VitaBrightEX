# Pseudo-v1.4 hardware validation matrix

No physical-hardware row is passed from source review or CI alone. Kernel/display behaviour must be observed on real hardware before the draft PR is merged. Detailed first-unit evidence is in [`HARDWARE-RESULTS-2026-09-12.md`](HARDWARE-RESULTS-2026-09-12.md).

## Software gate

The exact commit installed for hardware testing must have green checks for architectural source contracts; parser/runtime/deployment structural contracts; byte-identical packaged LUT/config assets; compiled production LUT parser regressions; compiled production config parser regressions; compiled error-domain regressions; firmware-audit tooling; release plugin/stubs under `-Werror`; diagnostic plugin; generated-stub installation; matching editor build; and hardware-test bundle generation.

LUT/config parser semantics are not accepted from a separate Python grammar. CI executes the portable C cores used by production. Text newline policy is LF or CRLF only; interior/lone CR must fail.

Static raw-layout evidence is tracked in [`FIRMWARE-LAYOUT-VERIFICATION.md`](FIRMWARE-LAYOUT-VERIFICATION.md). Runtime layout validation remains mandatory even after a static audit passes.

## Safety and provenance prerequisites

1. Preserve the exact known-good `ur0:tai/config.txt` and existing plugin/LUT/config backups. Do not reconstruct the normal stack manually.
2. Confirm the Vita boots with that known-good stack and that the established recovery/bypass path remains available.
3. Test the release SKPRX first. Diagnostic builds are reserved for an observed failure requiring trace data.
4. Use VitaShell FTP absolute mount syntax with `curl --ftp-method nocwd`: `//ur0:/...` or `//ux0:/...`.
5. Treat the kernel plugin and editor VPK as separate installable components. Uploading `ur0:tai` files does not update the editor app.
6. When a checkpoint depends on current diagnostics/provenance/UI wording, install the matching VPK from the same hardware-test bundle and require the editor's plugin/editor build IDs to show `MATCH`.
7. Record the bundle's `WORKFLOW_COMMIT.txt` and `SHA256SUMS` before transfer, then round-trip/hash the Vita copies before reboot.
8. Never mark firmware supported merely because LiveArea boots; required status/capability state must also match.

## Gate A — PCH-2000 / 3.65 Ensō

Target: physical PCH-2000, system software `0x03650000`, 3.65 Ensō. Initial hardware testing began from `b6eef0fe47dab918baa8edfcd2d0b20f1b275aff`.

### Recorded evidence

- **A1 isolated cold boot: PASS.** LiveArea loaded normally; the VitaBrightEX 1.3 PS-logo hang was not reproduced.
- **Old malformed-authority fail-open path: PASS.** The hardware-discovered commented-LUT parser defect left backend/layout inactive/unknown with `VBE_ERR_INVALID_USER_INPUT`, while the console remained usable.
- **3.65 runtime exact-signature gate at segment-0 `0x1B48`: PASS.** With a numeric-only representation of the same 17 values, `layout/core/table/brightness_hook/power_limit_hook` became active with `last_error=0`.
- **Stock/extended A-B and real system-slider sweep: PASS.** Extended entry 0=`1` is materially darker than stock entry 0=`31`; direction is correct and both end at `255`.
- **Mid-brightness inactivity dim: PASS.** Very-low-level behavior is consistent with the anti-paradoxical-brightening threshold.
- **A2 overall: PARTIAL.** True maximum-slider inactivity, application brightness transitions and suspend/resume remain open.
- Wider plugin compatibility remains untested; the unit is still intentionally isolated.

The `0x1B48` result is **physical runtime exact-signature evidence**, not static decrypted-ELF verification.

### A1. Boot isolation / compatibility progression

Order remains: v1.4 isolated (**passed**) -> v1.4 + ioPlus -> v1.4 + VitaGrafix 5.0.2 + ioPlus -> exact preserved normal plugin stack.

Every stage must reach LiveArea without recovery, report the correct LCD hardware/firmware and active lock/layout/core/table/hooks after valid authoritative input, while CSC and transfer LUT remain explicitly unsupported.

### A2. LCD brightness regression

Repeated actual Vita system-slider min -> max -> min is **passed**. Continue by testing real minimum, midpoint and maximum independently, application brightness transitions, explicit suspend/resume and repeated reloads. Do not treat the editor LUT cursor as current system brightness.

Source predicts table values below `25` may suppress a `brightness==1` request to avoid paradoxical brightening, while midpoint and true maximum (`255`) should permit it. If maximum genuinely differs, deploy the separately built diagnostic SKPRX and inspect `[LCD:DIM]` / `[LCD:POWER]` before changing release behavior.

### A3. Corrected parser cold-boot prerequisite

Before persistence testing, install **both** the corrected release kernel-side files and the matching editor VPK from one branch-exact bundle. Use the normal commented packaged LCD LUT—not the numeric-only workaround. Fresh power-off/on, no Circle/reload.

Initial editor screen must show plugin/editor build IDs as `MATCH`, then:

```text
Hardware: PCH-2000 LCD
firmware: 0x03650000
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

Neutral optional capabilities may remain inactive/unsupported as documented. Any mismatch or unresolved error domain stops this gate; do not press Circle to mask startup behavior.

### A4. Live LCD transaction / authoritative persistence

After A3 passes: make one valid monotonic RAM edit, verify immediate transactional apply, press Square, verify kernel-authoritative atomic persistence, reload, then cold reboot and confirm the exact value/source returns. Then test invalid live input, malformed authoritative input, missing preferred/fallback, and safe rollback/error paths.

Pass requires invalid input to be rejected before mutation; present malformed preferred source to fail instead of falling through; missing preferred source to permit fallback; failed replacement to preserve/restore the previous working table where possible; Save to use the actual source path; and temporary-write failure to retain the old persisted file.

### A5. Layout conflict

Where safely reproducible, a changed stock SceLcd signature must produce layout mismatch before injection.

### A6. LCD panel color-space transaction

When capability is usable, record mode, toggle 0 <-> 1 repeatedly, require read-back equality, then cover suspend/resume/apps and controlled teardown restoration. A successful color-space operation must not clear unrelated config/brightness diagnostics.

## Gate B — PCH-1000 OLED

Use at least one known DDB type 4 or 5; type 6/replacement/other successful DDB is desirable. Require correct DDB/panel detection, loaded-module layout plausibility validation, complete LUT transaction, brightness/auto-dim sweep, invalid >16 level rejection, authoritative panel/override persistence, malformed/no-fallback behavior, rollback where safely inducible, and color-space read-back/restore. No DDB failure may become an assumed default panel.

## Gate C — capability/editor/provenance truthfulness

On both hardware families verify unsupported controls stay disabled; invert/color-space are capability-gated; inactive backend remains isolated; Square uses kernel authority; `LCD LUT entry X/16` is clearly a table cursor; diagnostics domains agree with induced failures; and matching bundle components report `MATCH`. Deliberately using an old editor or mismatched plugin during a non-destructive provenance test should visibly report mismatch rather than silently appearing current.

## Gate D — parser and authority behavior

Hardware spot-checks supplement, but do not replace, host parser proof. Confirm a normal commented packaged LUT cold-boots; long comments do not fail merely due length; CRLF is accepted; interior/lone CR is rejected; EOF without final newline is accepted when otherwise complete; long malformed data/range/count/non-monotonic failures are rejected; malformed preferred source does not fall through; missing preferred source may fall back; and LiveArea/status remain truthful under failure.

Config-specific hardware fault injection should be conservative: malformed authoritative config must leave safe initialized defaults in use and retain a config-domain error even if brightness initializes successfully or a later optional/editor action succeeds.

## Gate E — firmware layouts

Raw-layout attempts remain limited to 3.60, 3.65, 3.67, 3.68, 3.69 and 3.70. **3.65 has physical runtime exact-signature evidence at `0x1B48`; static decrypted-binary evidence remains pending.** 3.71–3.74 remain unsupported.

## Power/performance observation

No continuous worker exists by design. Compare idle behavior under equivalent screen/radio conditions, but do not publish a `<0.01 W` or similar bound without suitable instrumentation.

## Result recording

For every target record model/panel DDB, firmware/exploit, exact plugin stack, commit/build IDs, artifact hashes, release/diagnostic identity, full status + diagnostics snapshot, runtime/static layout evidence separately, gate results and any recovery action.

PR #1 remains draft until representative physical coverage is complete.
