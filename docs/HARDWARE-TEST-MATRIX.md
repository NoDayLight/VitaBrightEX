# Pseudo-v1.4 hardware validation matrix

No physical-hardware row in this document is considered passed from source review or CI alone. Kernel/display behaviour must be observed on real hardware before the draft PR is merged.

## Software gate

These checks must be green on the exact commit installed for hardware testing:

- architectural source-contract verifier
- packaged OLED/LCD LUT + config validator
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

This is the configuration that reproducibly hung stock VitaBrightEX 1.3 at `PS logo → display on → no LiveArea`.

### A1. Boot isolation

Test in this order:

- v1.4 alone in `*KERNEL`
- v1.4 + ioPlus
- v1.4 + VitaGrafix 5.0.2 + ioPlus
- normal pre-existing plugin stack, with NoPowerLimits still disabled for direct comparison with the original reproduction

Pass:

- every boot reaches LiveArea normally
- no safe-mode recovery is required
- status reports `hardware=LCD`
- `state_lock=active`
- `firmware_layout=active`
- `brightness_core/table/brightness_hook/power_limit_hook=active`
- `csc_filter=unsupported`
- `transfer_lut=unsupported`

Fail: PS-logo hang, reboot loop, black display, LiveArea timeout, corrupted brightness state, or any capability claiming active after initialization failure.

### A2. LCD brightness regression

- Sweep all 17 brightness positions repeatedly.
- Test minimum and maximum for at least five minutes each.
- Trigger inactivity dim at low, middle and high levels.
- Enter/leave applications that change brightness.
- Suspend/resume the console.
- Invoke generic reload repeatedly from the editor.

Pass: no brightness jump to a brighter value during low-level auto-dim, no crash, no stuck slider, and status stays coherent.

### A3. Live LCD LUT transaction / authoritative persistence

With the v1.4 editor:

- edit valid monotonic LCD values in RAM
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

Where safe, load a diagnostic build after another test patch has changed the stock SceLcd table signature.

Pass: v1.4 reports layout mismatch and leaves the brightness core inactive; it must not inject at the inherited offset.

### A5. LCD panel color-space transaction

If status reports `display_color_space` usable:

- record mode returned by the getter
- toggle mode 0 ↔ 1 in the editor repeatedly
- confirm status/editor read-back matches every successful request
- suspend/resume and launch multiple applications
- unload/stop the plugin in a controlled test and verify the original mode is restored

Pass: no set-only/assumed success, no persistent registry mutation, and teardown restores the firmware-owned mode captured at initialization.

## Gate B — PCH-1000 OLED

Use at least one known DDB type 4 or 5. A type-6/replacement/other successfully identified panel is highly desirable.

### B1. Panel detection / boot

Pass:

- LiveArea is reached normally
- status reports `hardware=OLED`
- `panel_type` matches the DDB result
- `firmware_layout=active`
- brightness table/core/hooks are active
- a failed DDB read never falls through to an assumed table offset
- an invalid/unresolvable inherited table location fails open before injection

### B2. OLED brightness / auto-dim

- Sweep brightness levels 0–16.
- Test inactivity dim at the lowest few levels.
- Exercise the power-mode maximum-brightness path.
- Suspend/resume and launch applications that modify brightness.
- Use a test client to submit an out-of-range level (>16).

Pass: existing VitaBright behaviour is retained without brightening during low-level dim, negative getter failures never become unsigned writes, and out-of-range level input is rejected.

### B3. Panel LUT selection / authoritative persistence

For each available panel:

- confirm the expected panel-specific file is selected
- if using `oled_panel_lut_override`, confirm that exact override is the authority
- make one small reversible byte edit
- verify live update immediately
- press Square to persist through the kernel syscall
- reload/reboot and verify the exact authoritative path is consumed
- restore the original LUT

Pass: no write is silently replaced by a disk reload, no cross-panel file is written, and a custom override is never saved to a guessed default filename.

### B4. OLED invalid-input / rollback

- send an all-zero candidate and an all-`FF` candidate from a test client
- make the authoritative file malformed/truncated and invoke reload
- where safely reproducible, force reinjection failure after a previously working LUT

Pass: malformed candidates never become live state; malformed existing files do not silently fall through; failed live replacement restores the previous table when possible.

### B5. OLED panel color-space transaction

Repeat A5 on OLED when the matched Get/Set exports resolve. Pass criteria are identical: read-back verified writes and restoration of the original mode on teardown.

## Gate C — capability/editor truthfulness

On both hardware families:

- open the v1.4 editor
- verify displayed status matches the kernel snapshot
- verify CCT/gamma/contrast/panel-linearisation controls are not editable
- verify invert appears only when the SceDisplay capability resolves
- verify panel color-space appears only when the matched getter/setter capability resolves
- verify unsupported/failed states cannot be invoked through the UI
- verify status refresh/reload does not initialize the inactive hardware backend
- verify Square uses kernel-authoritative persistence rather than direct userland file writes

## Gate D — fail-open / parser authority

On a known-supported test unit:

- temporarily remove the preferred LUT and confirm the documented fallback is used
- then restore the preferred path with malformed content and confirm fallback is **not** used
- test a malformed config value/file case where safe
- verify status/error detail reflects the failure while LiveArea remains usable

The distinction is intentional: **missing** may fall back; **present but malformed** is an explicit configuration error.

## Gate E — firmware layouts

Current source provenance permits raw-layout attempts only on 3.60, 3.65, 3.67, 3.68, 3.69 and 3.70.

For every firmware claimed in a release:

- preserve an audit record from `tools/verify_scelcd_layout.py` where a decrypted SceLcd image is available
- capture `firmware_layout` status
- require the LCD stock-signature gate before injection
- require successful OLED DDB + loaded-module layout validation before injection
- run the applicable brightness regressions above

3.71–3.74 remain unsupported by the raw brightness-table feature until their module layouts are independently established and hardware-tested.

## Power/performance observation

The implementation has no continuous worker by design. Hardware validation should still include idle comparison with plugin disabled/enabled and identical screen/radio conditions. Do not publish a ten-milliwatt bound unless measured with instrumentation capable of supporting that claim.

## Result recording

For each target record:

- model / panel DDB
- firmware and Ensō/h-encore state
- plugin stack
- exact pseudo-v1.4 commit SHA
- release or diagnostic build
- complete `VitaBrightStatus` snapshot
- static layout audit reference where applicable
- pass/fail for each applicable gate
- boot/recovery observations

Only after Gate A passes should the draft PR be considered for wider PCH-2000 testing. Only after representative A–E coverage should the PR leave draft status.
