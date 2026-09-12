# Pseudo-v1.4 hardware validation matrix

No row in this document is considered passed from source review or CI alone. Kernel/display behaviour must be observed on physical hardware before the draft PR is merged.

## Safety prerequisites

Before each target:

1. Back up `ur0:tai/config.txt` and the currently working plugin/LUT/config files.
2. Confirm the Vita boots with the current known-good plugin stack.
3. Confirm holding `L` during boot bypasses taiHEN plugins and restores a recovery path.
4. Test the release build first. Use `ENABLE_LOGGING=ON` only when a failure needs diagnostics; diagnostic and release builds must use the same functional code paths.
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

Fail:

- PS-logo hang, reboot loop, black display, LiveArea timeout, corrupted brightness state, or any capability claiming active after an initialization error

### A2. LCD brightness regression

- Sweep all 17 brightness positions repeatedly.
- Test minimum and maximum for at least five minutes each.
- Trigger inactivity dim at low, middle and high levels.
- Enter/leave applications that change brightness.
- Suspend/resume the console.
- Invoke generic reload repeatedly from the editor.

Pass: no brightness jump to a brighter value during low-level auto-dim, no crash, no stuck slider, and status stays coherent.

### A3. Live LUT transaction/rollback

With the v1.4 editor:

- edit valid monotonic LCD values in RAM
- persist and reload them
- attempt a non-monotonic table using a deliberately modified test client
- force a table-reapply failure if a safe reproducible method is available

Pass: invalid input is rejected before live mutation. If reapply fails after teardown, the previous working table is restored or status reports `LUT_ROLLBACK` without pretending the core is active.

### A4. Layout conflict test

Where safe, load a diagnostic build after another test patch has changed the stock SceLcd table signature.

Pass: v1.4 reports layout mismatch and leaves the brightness core inactive; it must not inject at the inherited offset.

## Gate B — PCH-1000 OLED

Use at least one known DDB type 4 or 5. A different/aftermarket DDB type is highly desirable.

### B1. Panel detection / boot

Pass:

- LiveArea is reached normally
- status reports `hardware=OLED`
- `panel_type` matches the DDB result
- `firmware_layout=active`
- brightness table/core/hooks are active
- a failed DDB read never falls through to an assumed table offset

### B2. OLED brightness / auto-dim

- Sweep brightness levels 0–16.
- Test inactivity dim at the lowest few levels.
- Exercise the power-mode maximum-brightness path.
- Suspend/resume and launch applications that modify brightness.

Pass: existing VitaBright behaviour is retained without brightening during the low-level dim workaround.

### B3. Panel LUT selection

For each available panel:

- confirm the panel-specific file is selected
- make one small reversible byte edit
- verify the live editor update appears immediately
- persist it, reboot/reload, and verify the same panel-specific file is read
- restore the original LUT

Pass: no write is silently replaced by a disk reload during the same live transaction; no cross-panel file is written.

### B4. OLED invalid-input / rollback

- send an all-zero candidate and an all-`FF` candidate from a test client
- verify both are rejected
- where safely reproducible, force reinjection failure after a previously working LUT

Pass: malformed candidates never become live state; a failed live replacement restores the previous table when possible.

## Gate C — capability/editor truthfulness

On both hardware families:

- open the v1.4 editor
- verify its displayed status matches the kernel snapshot
- verify CCT/gamma/contrast/panel-linearisation controls are not editable
- verify invert appears only when the SceDisplay capability resolves
- verify unsupported/failed states cannot be invoked through the UI
- verify status refresh/reload does not initialize the inactive hardware backend

## Gate D — firmware layouts

Current source provenance permits raw-layout attempts only on 3.60, 3.65, 3.67, 3.68, 3.69 and 3.70.

For every firmware claimed in a release:

- capture `firmware_layout` status
- verify the LCD stock signature gate succeeds before injection (LCD)
- verify successful OLED DDB selection before panel injection (OLED)
- run the appropriate brightness regression above

3.71–3.74 remain unsupported by the raw brightness-table feature until their module layouts are independently established and then hardware-tested.

## Power/performance observation

The implementation has no continuous worker by design. Hardware validation should still include idle comparison with plugin disabled/enabled and identical screen/radio conditions. Do not publish a ten-milliwatt bound unless measured with instrumentation capable of supporting that claim.

## Result recording

For each target record:

- model / panel DDB
- firmware and Ensō/h-encore state
- plugin stack
- exact v1.4 commit SHA
- release or diagnostic build
- complete `VitaBrightStatus` snapshot
- pass/fail for each applicable gate
- boot/recovery observations

Only after Gate A passes should the draft PR be considered for wider PCH-2000 testing. Only after A–D pass on representative hardware should the PR leave draft status.
