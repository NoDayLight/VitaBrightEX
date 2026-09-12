# Source authority

Pseudo-v1.4 treats configuration/LUT discovery and persistence provenance as explicit source transactions.

## Open authority

Only an explicit file-not-found result from the **OPEN stage** permits fallback. If OPEN succeeds, that source is authoritative; any later read, parse, or close failure is terminal. Any other OPEN failure is terminal as well.

The production decision core is `source_authority.c` / `source_authority.h`. It classifies results as `USE`, `FALLBACK`, or `FAIL` and records the stage that produced the outcome.

Current public VitaSDK generic headers do not expose a generic `SCE_ERROR_ERRNO_ENOENT` symbol used by this project. VitaBrightEX therefore owns one compatibility definition:

```text
VBE_SCE_IO_ERROR_NOT_FOUND = 0x80010002
```

The value is independently corroborated by Vita ecosystem implementations/usages such as h-encore and Vita3K, where `0x80010002` is used for no-entry/path-not-found behavior. The project does not present this compatibility constant as a VitaSDK-owned generic symbol.

A post-open error with the same numeric value remains terminal because fallback is determined by the OPEN classification, not by searching arbitrary later errors for `0x80010002`.

## Source identity

A committed LUT source is not represented by a bare pathname. `VbeSourceIdentity` distinguishes:

```text
NONE      no committed source identity
FILE      authoritative file + exact path
COMPILED  built-in table with no file path
```

This prevents a compiled fallback from pretending it came from `ur0:` or `ux0:`.

Source selection produces a validated candidate plus source identity. The backend transaction commits that identity only when the hardware transaction commits. A failed candidate cannot change the persistence destination. If source B fails and rollback restores source A, the committed identity is A again. If cleanup is dirty and rollback is forbidden, historical A metadata may remain for diagnosis, but persistence is disabled because the backend is not ACTIVE.

## Config

Order: `ur0:tai/vitabrightex.cfg` then `ux0:tai/vitabrightex.cfg`, then compiled defaults only if both files are explicitly absent. Parsing is candidate-based and atomic; failed input never partially mutates `g_config`. `config_load()` owns CONFIG diagnostics directly.

The compatibility aliases `display_color_space_mode` and `lcd_color_space_mode` target the same field and follow the same ordering rule as duplicate keys: last valid textual occurrence wins.

## LCD LUT

Order: `ur0:tai/vitabright_lcd_lut.txt`, then `ux0:tai/vitabright_lcd_lut.txt`, then the compiled safe LCD table only if both files are explicitly absent. A source that opened but later failed cannot fall through.

The compiled table commits as `COMPILED`, not as a guessed file source. Normal Square Save therefore returns `VBE_RESULT_NO_FILE_SOURCE`; it does not create a new LUT file implicitly. Materializing a compiled table, if ever implemented, must be a separate explicit operation.

## OLED LUT

For a recognized panel the normal order is panel-specific ur0, panel-specific ux0, generic ur0, generic ux0. Every transition requires explicit OPEN-stage absence. A configured `panel_lut_path` override is a single authoritative FILE source and never falls back. OLED has no invented compiled LUT fallback.

## Persistence

Normal persistence is legal only when:

```text
backend ownership == ACTIVE
source identity == FILE
```

The authoritative path remains the final FILE path. The same-directory `.tmp` path is an internal transaction resource and never becomes source identity.

Persistence sequence:

```text
prepare temp
-> open
-> write exact committed LUT
-> sync
-> close
-> rename temp over authoritative file   (commit point)
```

Pre-commit failure never advances to rename. fd ownership is relinquished only after confirmed close. temp ownership is relinquished only after confirmed remove/rename. Failed cleanup remains visible and blocks a false success.

Persistence errors belong to BRIGHTNESS and use the compact categories `VBE_ERR_PERSISTENCE_PREPARE`, `VBE_ERR_PERSISTENCE_COMMIT`, and `VBE_ERR_PERSISTENCE_CLEANUP`.

## CI proof

`tests/source_authority_host.c` compiles with the production authority core and verifies OPEN success, explicit-not-found fallback, other OPEN failure, READ/PARSE/CLOSE failure, post-open not-found terminality, valid fallback, and malformed-fallback terminality.

`tests/transaction_core_host.c` compiles the production transaction/source core and verifies source A survives a failed B replacement/rollback path, dirty failure does not commit B, degraded ownership is not persistence-eligible, successful B commits B, and COMPILED is never a normal file persistence target.

`tests/persistence_core_host.c` compiles the production persistence state/sequence core and fault-injects prepare/open/write/sync/close/rename/cleanup stages. Source-text checks are structural tripwires only.
