# PCH-2000 / 3.65 Ensō hardware results — 2026-09-12

Target: physical PCH-2000, system software `0x03650000`, 3.65 Ensō. This is the mandatory reproducer family for the VitaBrightEX 1.3 `PS logo -> display on -> no LiveArea` hang.

Tested starting point: branch `v1.4-rearchitecture`, commit `b6eef0fe47dab918baa8edfcd2d0b20f1b275aff`. The test stack intentionally disabled PSVshellPlus, PSVshell, NoPowerLimits, ioPlus and VitaGrafix; ShellBat and PNGShot remained enabled.

> **Evidence boundary:** later parser/source/transaction/persistence/synchronization/stop changes are software-verified only. They do not create new physical PASS rows. The hardware evidence below remains unchanged until a fresh exact-head bundle is installed and tested.

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

Fail-open behavior itself passed: the plugin/status ABI remained operational and LiveArea loaded while the brightness backend stayed inactive.

## Software correction after the hardware finding

The permanent correction became broader than the triggering 77-character comment:

- LCD/OLED LUT parsing uses portable streaming production C state machines rather than fixed physical-line buffers;
- exact production parser sources execute in host CI against exact source/package bytes;
- one shared newline decoder accepts LF/CRLF and rejects lone/interior CR;
- config uses a production-shared parser core with strict complete-token numerics and behavioral host tests;
- source authority distinguishes explicit OPEN-stage absence from every other I/O/parse/close failure;
- source identity is `NONE`, `FILE + path`, or `COMPILED + no path`;
- LCD/OLED backend ownership is `CLEAN`, `ACTIVE`, or `DEGRADED`, with `TXN_OK`, `TXN_FAILED_CLEAN`, `TXN_FAILED_DIRTY` transaction attempts;
- dirty candidate cleanup forbids rollback/reinitialization over uncertain taiHEN ownership;
- dependency teardown is power hook -> brightness hook -> table injection and stops at the first failed release;
- source identity commits only with hardware transaction commit/rollback;
- compiled LCD fallback has no fabricated file source; normal Save returns `VBE_RESULT_NO_FILE_SOURCE`;
- persistence tracks fd/temp ownership and commits only at rename;
- synchronization has explicit RUNNING/STOPPING/DEGRADED lifecycle and unlock failure cannot be hidden by a successful operation;
- module stop refuses successful unload when invert/color-space/backend/mutex teardown cannot be confirmed;
- plugin/editor build provenance remains generated from the actual build checkout;
- VitaBrightStatus remains ABI v2 and additive VitaBrightDiagnostics remains ABI v1.

These are software architecture results only, not new hardware passes.

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

The editor is a separate installed Vita application. Replacing `ur0:tai/vitabright.skprx`, LUT or config files does not update it. Hardware checkpoints that rely on current wording, diagnostics or build provenance must install the matching VPK. The development editor displays plugin/editor build IDs and `MATCH`/`MISMATCH`.

After live mutation it refreshes kernel state before reporting recovery. “Previous committed table restored” is valid only when capability/diagnostic state confirms recovery. Degraded recovery and compiled-fallback Save have distinct messages.

## Current physical evidence summary — unchanged

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

## Still pending before wider compatibility testing

1. Install one matching final exact-head release/editor bundle and fresh-cold-boot with the normal commented packaged LUT. Before any Circle/reload action, require component `MATCH`, active layout/core/table/hooks, `last_error=0`, and zero unresolved diagnostics domains.
2. Live LUT edit -> Square FILE-authoritative persistence -> reload -> reboot exact-value/source persistence.
3. Compiled-fallback Save semantics (`VBE_RESULT_NO_FILE_SOURCE`) without implicit file creation.
4. Malformed/missing/unreadable authority behavior and clean-vs-dirty rollback paths, using controlled fault injection only where safe.
5. Persistence open/write/sync/close/rename/cleanup fault semantics where safely injectable.
6. Hardware invert and panel color-space read-back/teardown restore.
7. Explicit suspend/resume and true maximum-slider inactivity behavior.
8. ioPlus, VitaGrafix, then exact normal plugin-stack restoration from the preserved known-good config backup.

PR #1 remains draft.
