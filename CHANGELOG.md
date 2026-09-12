# Changelog

## VitaBrightEX pseudo-v1.4 (development)

### Hardware validation checkpoint — 2026-09-12

- First isolated physical PCH-2000 / 3.65 Ensō cold boot passed without reproducing the v1.3 PS-logo hang.
- Production runtime exact-signature validation passed for the loaded 3.65 `SceLcd` table at segment-0 `0x1B48`; static decrypted-image verification remains separate/pending.
- Stock-vs-extended LCD table A/B and repeated system brightness-slider sweep passed.
- Mid-brightness inactivity dim passed; explicit true-maximum dim and suspend/resume remain open.

### Fixed — production/CI parser contract

Physical testing found that the prior LCD kernel parser required a complete physical line to fit `char line[64]`, while CI ignored arbitrary-length comments. The normal commented packaged LUT therefore passed workflow #114 but was rejected by production before layout validation.

The fix removes the mismatch class rather than shortening comments:

- LCD and OLED LUTs use portable streaming C parser state machines with no fixed physical-comment-line limit.
- Full-line comments are discarded as streams; data grammar remains strict.
- LCD requires exactly 17 monotonic decimal bytes in `0..255`, with CRLF and EOF-without-final-newline support.
- OLED requires exactly 17 x 21 two-digit hexadecimal bytes.
- Config parsing was audited for the same class: long comments stream safely and an overlong non-comment directive is consumed/rejected as one physical line instead of being split into fake directives.
- Existing malformed authoritative `ur0` input remains an error; only missing preferred files permit fallback.
- CI now compiles the exact production LUT parser C files into a host regression executable and feeds them the exact packaged bytes plus long-comment/malformed/count/range/CRLF/EOF cases.

### Editor

- LCD table cursor wording changed from `LCD brightness level` to `LCD LUT entry`, clarifying that it is not the current Vita system brightness setting.

### Architecture

- OLED/LCD startup and live replacement are transactional and fail-open.
- LUT Save is kernel-authoritative and atomic relative to the source actually loaded.
- Status ABI v2 exposes hardware/layout/capability/error state.
- Verified panel color-space Get/Set is session-scoped, read-back verified and restored on teardown.
- Speculative private IFTU scanout calls and persistent handheld RGB-range registry mutation remain removed.

---

## LUT Editor v3.1

Historical v3.x/v4.x editor binaries are retained only as project history. Their source was not committed alongside those VPKs; pseudo-v1.4 uses the new source-controlled `editor/` implementation.

---

## VitaBrightEX v1.3 and earlier

See repository history/releases for the historical changelog. Statements from those versions are not treated as authoritative for pseudo-v1.4 when they conflict with current source, VitaSDK evidence or physical validation.
