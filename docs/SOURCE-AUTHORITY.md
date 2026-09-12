# Source authority

Pseudo-v1.4 treats configuration and LUT discovery as an authority transaction.

Only an explicit file-not-found result from the OPEN stage permits fallback. If OPEN succeeds, that source is authoritative; any later read, parse, or close failure is terminal. Any other OPEN failure is terminal as well.

The production decision core is `source_authority.c` / `source_authority.h`. It classifies results as `USE`, `FALLBACK`, or `FAIL`, records the stage that produced the outcome, and uses VitaSDK's `SCE_ERROR_ERRNO_ENOENT` definition rather than a guessed numeric constant. A post-open error with the same numeric value as ENOENT is still terminal because fallback is determined from the stage/outcome, not raw equality.

## Config

Order: `ur0:tai/vitabrightex.cfg` then `ux0:tai/vitabrightex.cfg`, then compiled defaults only if both files are explicitly absent. Parsing is candidate-based and atomic; failed input never partially mutates `g_config`. `config_load()` owns CONFIG diagnostics directly.

The compatibility aliases `display_color_space_mode` and `lcd_color_space_mode` target the same field and follow the same ordering rule as duplicate keys: the last valid textual occurrence wins.

## LCD LUT

Order: `ur0:tai/vitabright_lcd_lut.txt`, then `ux0:tai/vitabright_lcd_lut.txt`, then the compiled safe LCD table only if both files are explicitly absent. A source that opened but later failed cannot fall through.

## OLED LUT

For a recognized panel the normal order is panel-specific ur0, panel-specific ux0, generic ur0, generic ux0. Every transition requires explicit OPEN-stage absence. A configured `panel_lut_path` override is a single authoritative source and never falls back. OLED has no invented compiled LUT fallback.

## Persistence

The backend records the source associated with the committed LUT. Save writes a same-directory temporary file, syncs/closes it, then renames it over the authoritative path. Persistence errors belong to BRIGHTNESS, not CONFIG.

## CI proof

`tests/source_authority_host.c` compiles with the production authority core and verifies OPEN success, OPEN-not-found fallback, other OPEN failure, READ/PARSE/CLOSE failure, post-open ENOENT terminality, valid fallback, and malformed-fallback terminality. Parser suites separately exercise the production grammars; source-text checks are only structural tripwires.
