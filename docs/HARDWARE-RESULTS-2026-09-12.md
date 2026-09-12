# PCH-2000 / 3.65 Ensō hardware results — 2026-09-12

Target: physical PCH-2000, system software `0x03650000`, 3.65 Ensō. This is the mandatory reproducer family for the VitaBrightEX 1.3 `PS logo -> display on -> no LiveArea` hang.

Tested starting point: branch `v1.4-rearchitecture`, commit `b6eef0fe47dab918baa8edfcd2d0b20f1b275aff`. The test stack intentionally disabled PSVshellPlus, PSVshell, NoPowerLimits, ioPlus and VitaGrafix; ShellBat and PNGShot remained enabled.

## Gate A1 — isolated cold boot: PASS

The console cold-booted normally into LiveArea. There was no PS-logo hang, loop, black screen, safe mode, L-button plugin bypass or manual recovery. The original v1.3 boot failure was not reproduced.

## Hardware-discovered parser/CI defect

The first editor status was fail-open rather than active:

```text
hardware=PCH-2000 LCD
firmware=0x03650000
ABI=2
core=inactive
table=inactive
layout=unknown
lock=active
brightness_hook=inactive
power_hook=inactive
last_error=8
last_error_detail=-1
```

`VBE_ERR_INVALID_USER_INPUT/-1` occurred before SceLcd export/layout work. Root cause: the production LCD parser buffered complete physical lines in `char line[64]`; descriptive comments in the exact packaged authoritative LUT exceeded that limit. The Python asset validator ignored comments without such a limit, so CI accepted bytes the kernel rejected.

Fail-open behavior itself passed: the plugin and status ABI remained operational and LiveArea loaded while the brightness backend stayed inactive.

The permanent correction replaces fixed physical-line LUT parsing with the same streaming pure-C grammar used by production and host CI. Full-line comments are discarded without buffering; actual data remains strict. OLED had the same fixed-line class (`PARSER_LINE_MAX 160`) and config parsing could split an overlong line into a second fake directive; both were audited/fixed as part of the same change.

## 3.65 SceLcd runtime layout evidence: PASS

For test continuation only, the on-device LUT was temporarily replaced with the same 17 values but without comments. Pressing Circle (`vitabrightReload()`) immediately produced:

```text
core=active
table=active
layout=active
lock=active
brightness_hook=active
power_hook=active
last_error=0
```

On `0x03650000`, production chooses SceLcd segment-0 offset `0x1B48` and requires the exact stock signature:

```text
31 37 43 50 58 67 77 88 100 114 129 147 166 182 203 227 255
```

Therefore `layout=active` is physical runtime exact-signature verification that this sequence exists at the proposed loaded-module address on the tested PCH-2000/3.65 console. This is meaningful runtime evidence, **not** static decrypted-ELF verification; the latter remains pending.

## Stock / extended A-B table: PASS

Stock table:

```text
31 37 43 50 58 67 77 88 100 114 129 147 166 182 203 227 255
```

Extended table:

```text
1 3 5 8 13 20 29 41 57 76 95 116 137 161 190 220 255
```

Both reloaded with backend/layout/hooks active and error 0. Stock entry 0 produced approximately normal Sony minimum brightness; extended entry 0 was materially darker. Repeated system-slider sweeps were smooth, correctly directed and stable. This supports the original VitaBright ascending table orientation: lower table code means lower physical luminance.

## Inactivity dim: PARTIAL PASS

At a real mid system-brightness setting, inactivity visibly dimmed the screen and activity restored it normally. At very low extended levels no visible dim was observed, consistent with the anti-paradoxical-brightening threshold (`table value < 25`).

Maximum system brightness remains explicitly unclosed. Source logic predicts that a genuine `brightness == 1` inactivity request at maximum maps to table value 255 and should be allowed. The diagnostic build now logs the requested dim, prior raw brightness, derived table index/value and allow decision, plus ScePower max-brightness requests. Do not mark Gate A2 complete until actual minimum/mid/maximum slider positions are tested independently of the editor cursor.

## Editor terminology

The editor cursor indexes the LUT; it is not the current Vita system brightness. Hardware testing exposed that `LCD brightness level X/16` was ambiguous. The intended UI wording for corrected artifacts is `LCD LUT entry X/16`.

## Still pending before wider compatibility testing

1. Fresh cold boot with the normal commented packaged LUT on the corrected parser build; backend must be active directly at boot with error 0, without Circle/reload.
2. Live LUT edit -> Square kernel-authoritative persistence -> reload -> reboot exact-value persistence.
3. Malformed authoritative file and rollback/error paths.
4. Hardware invert and panel color-space read-back/restore.
5. Explicit suspend/resume.
6. ioPlus, VitaGrafix, then exact normal plugin-stack restoration.

PR #1 remains draft.
