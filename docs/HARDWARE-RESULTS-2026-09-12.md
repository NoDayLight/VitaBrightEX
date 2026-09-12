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

## Software correction checkpoint after the hardware finding

The permanent correction is broader than the single 77-character comment that triggered the failure:

- LCD and OLED LUT parsing use portable streaming production C state machines rather than fixed physical-line buffers.
- the same production parser sources are compiled and executed by host CI against the exact source/package bytes;
- one shared newline decoder accepts LF and CRLF only, rejecting interior or lone CR rather than silently deleting it;
- config loading now feeds a portable production parser core that is also executed by host CI;
- config numeric values are exact-token parsed, so suffix garbage is rejected rather than acquiring a prefix value accidentally;
- config long comments, overlong known directives, duplicate keys, missing `=`, unknown keys, EOF and CRLF behavior are behaviorally tested;
- deployment CMake now uses canonical absolute VitaShell FTP paths (`//ur0:` / `//ux0:`);
- the matching editor and plugin expose build IDs generated from the build checkout and show `MATCH`/`MISMATCH` on-device;
- `VitaBrightStatus` remains ABI v2, while additive diagnostics ABI v1 retains independent config/brightness/color/filter/input/synchronization error domains so unrelated successful operations cannot erase unresolved errors.

None of these software changes are counted as new hardware passes. A new release-artifact cold boot is still required.

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

Maximum system brightness remains explicitly unclosed. Source logic predicts that a genuine `brightness == 1` inactivity request at maximum maps to table value 255 and should be allowed. The diagnostic build logs the requested dim, prior raw brightness, derived table index/value and allow decision, plus ScePower max-brightness requests. Do not mark Gate A2 complete until actual minimum/mid/maximum slider positions are tested independently of the editor cursor.

## Editor terminology and component provenance

The editor cursor indexes the LUT; it is not current Vita system brightness. Corrected editor artifacts label it `LCD LUT entry X/16`.

The editor is a separate installed Vita application. Replacing `ur0:tai/vitabright.skprx`, LUT or config files does not update it. Hardware checkpoints that rely on current wording, diagnostics ABI v1 or build provenance must install the matching VPK. The current development editor displays an 8-character plugin build ID, its own build ID, and `MATCH`/`MISMATCH`.

## Still pending before wider compatibility testing

1. Install the matching corrected editor and release kernel-side bundle, then fresh-cold-boot with the normal commented packaged LUT. Before any Circle/reload action, require component `MATCH`, active layout/core/table/hooks, `last_error=0`, and zero unresolved diagnostics domains.
2. Live LUT edit -> Square kernel-authoritative persistence -> reload -> reboot exact-value persistence.
3. Malformed authoritative file and rollback/error paths, including missing-preferred/fallback versus present-malformed/no-fallback behavior.
4. Hardware invert and panel color-space read-back/restore.
5. Explicit suspend/resume and true maximum-slider inactivity behavior.
6. ioPlus, VitaGrafix, then exact normal plugin-stack restoration from the preserved known-good config backup.

PR #1 remains draft.
