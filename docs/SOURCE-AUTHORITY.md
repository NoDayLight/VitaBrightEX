# Source authority

Pseudo-v1.4 treats configuration/LUT discovery and persistence provenance as explicit transactions rather than path guesses.

## Authority rule

Only an explicit not-found result from the **OPEN stage** permits fallback:

```text
OPEN succeeds
    -> source is authoritative
    -> READ/PARSE/CLOSE failure is terminal

OPEN VBE_SCE_IO_ERROR_NOT_FOUND (0x80010002)
    -> source absent
    -> documented fallback is eligible

OPEN any other failure
    -> terminal source I/O failure
```

`VBE_SCE_IO_ERROR_NOT_FOUND` is project-owned compatibility knowledge, not a claimed current VitaSDK generic symbol. A later READ/PARSE/CLOSE error with the same numeric value remains terminal because fallback depends on the stage, not the number alone.

## Identity model

`VbeSourceIdentity` is one of:

```text
NONE
FILE + exact path
COMPILED + no path
```

A compiled candidate never pretends to be `ur0:` or `ux0:`. Source identity is copied as part of the state it belongs to and becomes committed only with that state.

## Config source transaction

Config search order is:

```text
ur0:tai/vitabrightex.cfg
-> ux0:tai/vitabrightex.cfg
-> COMPILED defaults only when both FILEs are explicitly absent
```

`config_load()` parses into a candidate and records the corresponding candidate identity. The serialized reload coordinator snapshots the previously committed config and identity before load. If a later backend replacement fails, the previous snapshot is restored before the lock is released.

The regression that defines this contract is:

```text
committed config/source = FILE ux0:A
candidate config/source = FILE ur0:B
backend replacement      = clean failure
backend rollback          = success

final config/source       = previous config + FILE ux0:A
future file decision      = ux0:A, never ur0:B
```

A malformed preferred `ur0` file is terminal and does not fall through to `ux0`; a failed reload leaves the previous committed source intact. If both files are absent, source identity is COMPILED with an empty path.

Legacy panel-color aliases share one canonical setting. `display_color_space_mode`, `lcd_color_space_mode`, `lcd_ips_enhance` and `lcd_saturation_boost` obey textual last-assignment semantics rather than creating independent source/state fields.

## LCD LUT

Order:

```text
ur0:tai/vitabright_lcd_lut.txt
-> ux0:tai/vitabright_lcd_lut.txt
-> compiled safe table only when both FILEs are explicitly absent
```

The compiled table commits as COMPILED. Normal persistence therefore returns `VBE_RESULT_NO_FILE_SOURCE`; materialization would have to be a separate explicit feature.

## OLED LUT

Recognized panels search panel-specific `ur0`, panel-specific `ux0`, generic `ur0`, then generic `ux0`, advancing only on OPEN-stage absence. An explicit `panel_lut_path` is one authoritative FILE and never falls back. OLED has no invented compiled LUT.

The committed source identity lives inside `VbeOledLutState` beside base/runtime LUT, panel type, requested transform and applied transform. This makes a rollback snapshot coherent: restoring a previous OLED state restores its exact source at the same time as its base/runtime data.

## Persistence

Normal persistence requires:

```text
backend ownership == ACTIVE
source identity    == FILE
```

Sequence:

```text
prepare same-directory temp
-> open
-> write authoritative committed payload
-> sync
-> close
-> rename temp over FILE path   # commit point
```

For OLED the serialized payload is always the **base LUT**, never the derived runtime LUT. The temp path is internal transaction state and never becomes source identity. fd/temp ownership clears only after confirmed close/remove/rename; cleanup failure remains a failure.

## CI evidence

Production-shared tests cover:

- OPEN success/not-found/other error and post-open terminal failures;
- malformed preferred authority with no fallback;
- config FILE `ux0:A` surviving a failed `ur0:B` reload;
- COMPILED config having no fabricated path;
- source A surviving backend replacement/rollback;
- dirty ownership disabling persistence;
- FILE versus COMPILED persistence eligibility;
- persistence prepare/open/write/sync/close/rename/cleanup fault sequencing.

Source-text checks guard structure only; source authority and rollback semantics are exercised by the same C cores used by production.
