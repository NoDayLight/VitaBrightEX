# Pseudo-v1.4 hardware validation matrix

No physical-hardware row in this document is considered passed from source review or CI alone. Kernel/display behaviour must be observed on real hardware before the draft PR is merged.

Detailed observations from the first mandatory unit are recorded in [`HARDWARE-RESULTS-2026-09-12.md`](HARDWARE-RESULTS-2026-09-12.md).

## Software gate

These checks must be green on the exact commit installed for hardware testing:

- architectural source-contract verifier
- parser/runtime/CI contract verifier
- packaged OLED/LCD LUT + config validation **through the exact portable C LUT parser used by production**
- release kernel plugin + generated syscall stubs build with warnings as errors
- diagnostic plugin build from the same functional source paths
- companion editor build against those generated stubs

Static raw-layout evidence is tracked separately in [`FIRMWARE-LAYOUT-VERIFICATION.md`](FIRMWARE-LAYOUT-VERIFICATION.md). Runtime layout validation remains mandatory even after a static audit passes.

## Safety prerequisites

Before each target:

1. Back up `ur0:tai/config.txt` and the currently working plugin/LUT/config files.
2. Confirm the Vita boots with the current known-good plugin stack.
3. Confirm holding `L` during boot bypasses taiHEN plugins and restores a recovery path.
4. Test the release build first. Use `ENABLE_LOGGING=ON` only when a failure needs diagnostics; diagnostic and release builds use the same functional paths.
5. Never mark a firmware supported merely because it boots. The expected `vitabrightGetStatus()` capabilities must also match.

## Gate A — PCH-2000 / 3.65 Ensō (first mandatory target)

Target under test: physical PCH-2000, system software `0x03650000`, 3.65 Ensō. Initial hardware testing started from commit `b6eef0fe47dab918baa8edfcd2d0b20f1b275aff`.

### Recorded status

- **A1 isolated cold boot: PASS.** LiveArea loaded normally and the original VitaBrightEX 1.3 PS-logo hang was not reproduced.
- **Fail-open malformed authoritative LUT: PASS.** A hardware-discovered parser defect rejected the commented LUT before layout validation; status truthfully remained inactive/`layout=unknown` with `VBE_ERR_INVALID_USER_INPUT`, while LiveArea remained usable.
- **3.65 runtime exact-signature gate at segment-0 `0x1B48`: PASS.** After a numeric-only representation of the same 17 values removed the parser blocker, `layout/core/table/brightness_hook/power_limit_hook` all became active with `last_error=0`.
- **Stock/extended table A-B and repeated normal brightness-slider sweep: PASS.** Extended entry 0=`1` is materially darker than stock entry 0=`31`; direction is correct and both end at `255`.
- **Mid-brightness inactivity dim: PASS.** Very-low-level no-dim behavior is consistent with the anti-paradoxical-brightening threshold.
- **A2 overall: PARTIAL.** Explicit maximum-slider inactivity behavior and suspend/resume remain open.
- Wider plugin compatibility is **not yet tested**; the unit remains in A1-style isolation.

The `0x1B48` result is **physical runtime exact-signature evidence**, not static decrypted-ELF verification. Static status remains pending.

### A1. Boot isolation

Test in this order:

- v1.4 alone in `*KERNEL` — **passed on the first mandatory unit**
- v1.4 + ioPlus — pending
- v1.4 + VitaGrafix 5.0.2 + ioPlus — pending
- exact normal pre-existing plugin stack — pending

Pass:

- every boot reaches LiveArea normally
- no safe-mode recovery is required
- status reports `hardware=LCD`
- `state_lock=active`
- after authoritative input succeeds: `firmware_layout=active`
- `brightness_core/table/brightness_hook/power_limit_hook=active`
- `csc_filter=unsupported`
- `transfer_lut=unsupported`

Fail: PS-logo hang, reboot loop, black display, LiveArea timeout, corrupted brightness state, or any capability claiming active after initialization failure.

### A2. LCD brightness regression

- Sweep the **actual Vita system brightness slider** minimum -> maximum -> minimum repeatedly — **passed**.
- Test real system minimum, midpoint and maximum independently; do not use the editor LUT cursor as a brightness control.
- Trigger inactivity dim at low, middle and high system-slider positions — low/mid observed; explicit maximum remains pending.
- Enter/leave applications that change brightness — pending.
- Suspend/resume the console — pending.
- Invoke generic reload repeatedly from the editor — initial reload path passed during parser diagnosis; broader repetition pending.

Current source predicts: LUT entries below `25` may suppress a `brightness == 1` inactivity request to avoid paradoxical brightening; midpoint and true maximum (`255`) should permit it. If maximum does not dim, use the diagnostic build and inspect `[LCD:DIM]`/`[LCD:POWER]` logs before classifying the behavior as plugin or Sony policy.

Pass: no brightness jump to a brighter value during low-level auto-dim, no crash, no stuck/reversed slider, and coherent status.

### A3. Live LCD LUT transaction / authoritative persistence

Before this gate, perform a **fresh cold boot with the corrected build and the normal commented packaged LUT**. It must activate directly at boot without Circle/reload:

```text
commented packaged LUT -> accepted at boot
firmware_layout=active
table/core/hooks=active
last_error=0
```

Then:

- edit a valid monotonic LCD value in RAM
- press Square to persist through the kernel persistence syscall
- reload and verify exactly the committed table returns
- reboot and verify the same source is loaded
- attempt a non-monotonic table using a deliberately modified test client
- create an existing-but-malformed authoritative LUT and invoke reload
- force a table-reapply failure if a safe reproducible method is available

Pass:

- invalid live input is rejected before mutation
- malformed authoritative files produce a reload error instead of silently falling through
- failed live replacement restores the previous working table when possible
- Save never guesses/writes a different `ur0`/`ux0` path than the backend's authoritative source
- temporary-write failure leaves the previous persisted file intact

### A4. Layout conflict test

Where safe, test after another controlled patch has changed the stock SceLcd signature. Pass: v1.4 reports layout mismatch and never injects at the inherited offset.

### A5. LCD panel color-space transaction

If status reports `display_color_space` usable:

- record getter mode
- toggle mode 0 <-> 1 repeatedly
- require read-back to match every successful request
- suspend/resume and launch multiple applications
- unload/stop in a controlled test and verify restoration of the captured original mode

Pass: no assumed set-only success, no persistent registry mutation, teardown restores firmware-owned mode.

## Gate B — PCH-1000 OLED

Use at least one known DDB type 4 or 5. A type-6/replacement/other successfully identified panel is highly desirable.

### B1. Panel detection / boot

Require LiveArea, correct `hardware=OLED`/DDB panel type, active layout/core/table/hooks, no DDB-failure fallback, and fail-open behavior for an invalid inherited table location.

### B2. OLED brightness / auto-dim

Sweep 0–16, test lowest-level inactivity behavior, power maximum path, suspend/resume/app brightness changes and reject a >16 test-client level.

### B3. Panel LUT selection / authoritative persistence

Verify panel-specific or explicit override authority, live reversible byte edit, Square kernel persistence, reload/reboot of the same path, then restore original LUT. No cross-panel guessed save path is acceptable.

### B4. OLED invalid-input / rollback

Reject all-zero/all-FF live candidates; reject malformed/truncated authoritative files without fallback; safely force reinjection failure where possible and require rollback/status truthfulness.

### B5. OLED panel color-space transaction

Repeat A5 on OLED when matched Get/Set exports resolve.

## Gate C — capability/editor truthfulness

On both hardware families verify displayed kernel status, disabled unsupported CCT/gamma/contrast/panel-linearisation, capability-gated invert/color-space, inactive-backend isolation, and kernel-authoritative Square persistence. The LCD editor line is a **LUT cursor/value**, not current system brightness; corrected artifacts should label it `LCD LUT entry X/16`.

## Gate D — fail-open / parser authority

On a supported unit:

- missing preferred LUT + valid fallback -> fallback permitted
- malformed preferred LUT + valid fallback -> explicit error, no fallback
- CRLF and EOF-without-final-newline valid files -> accepted
- long full-line comments -> accepted without fixed-buffer limits
- long malformed data lines, >255 values, wrong count and non-monotonic LCD values -> rejected
- verify LiveArea remains usable and status/error detail remains truthful

## Gate E — firmware layouts

Raw-layout attempts remain limited to 3.60, 3.65, 3.67, 3.68, 3.69 and 3.70. Track static verifier evidence separately from physical runtime signature evidence. **3.65 currently has physical runtime exact-signature evidence at `0x1B48`; static decrypted-binary evidence remains pending.** 3.71–3.74 remain unsupported.

## Power/performance observation

There is no continuous worker by design. Hardware validation should still compare idle behavior under identical screen/radio conditions. Do not publish a ten-milliwatt bound without instrumentation capable of supporting it.

## Result recording

For each target record model/panel DDB, firmware/exploit state, plugin stack, exact commit SHA, release/diagnostic build, complete status snapshot, static/runtime layout evidence separately, applicable gate results and recovery observations.

PR #1 remains draft until representative hardware coverage is complete.
