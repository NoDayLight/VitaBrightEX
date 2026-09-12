# SceLcd raw-layout verification

The PCH-2000 brightness extension replaces a 17-byte table inside **segment 0 of `SceLcd`**. Function NIDs and raw module offsets are separate claims and are tracked separately.

## Production runtime rule

A firmware version being whitelisted is never sufficient by itself. Before injection, v1.4 resolves `segment 0 + offset` in the loaded `SceLcd` image and requires the exact Sony stock table:

```text
31 37 43 50 58 67 77 88 100 114 129 147 166 182 203 227 255
```

Any mismatch fails open before injection. This guard remains mandatory even after a static binary audit because it also detects an unexpected module build or conflicting pre-patch.

## Evidence table

| Firmware | Candidate segment-0 offset | Physical runtime exact-signature evidence | Static decrypted-image evidence |
|---|---:|---|---|
| 3.60 | `0x1B00` | pending | pending |
| 3.65 | `0x1B48` | **PASS: PCH-2000 / `0x03650000` / 3.65 Ensō** | pending |
| 3.67 | `0x1B48` | pending | pending |
| 3.68 | `0x1B48` | pending | pending |
| 3.69 | `0x1B48` | pending | pending |
| 3.70 | `0x1B48` | pending | pending |
| 3.71–3.74 | not accepted | n/a | unsupported |

The inherited candidates come from original VitaBright provenance. Repetition in downstream forks is not independent verification.

## 3.65 physical runtime result

On 2026-09-12, the mandatory physical PCH-2000 / 3.65 Ensō target reported firmware `0x03650000` through status ABI v2. The first isolated cold boot reached LiveArea but a separate authoritative-LUT parser defect stopped backend startup before layout validation, leaving `firmware_layout=unknown` and the backend inactive as intended.

For diagnosis only, the same 17 extended values were uploaded without comments. After `vitabrightReload()`, status became:

```text
firmware_layout=active
brightness_core=active
brightness_table=active
brightness_hook=active
power_limit_hook=active
last_error=0
```

On this firmware the production path selects segment-0 `0x1B48`; `firmware_layout=active` can only be reached after all 17 stock bytes above match at that loaded-module address. This is therefore **physical runtime exact-signature verification of `0x1B48` on the tested 3.65 console**.

It is deliberately **not** labelled static verification. No independently decrypted 3.65 `SceLcd` ELF or exact segment-0 dump has yet been passed through the repository verifier.

The same console also passed stock-vs-extended A/B testing: stock entry 0=`31` produced roughly Sony's normal minimum while extended entry 0=`1` was materially darker; both tables remained active and ended at `255`. This supports the original VitaBright ascending table direction.

## Static verifier

`tools/verify_scelcd_layout.py` accepts a decrypted SceLcd ELF or an explicitly identified raw segment-0 dump, records SHA-256, maps the VitaBright segment-relative offset correctly, and requires the exact stock signature.

For ELF input:

```text
file location = first PT_LOAD.p_offset + VitaBright table offset
```

Examples:

```sh
python3 tools/verify_scelcd_layout.py --firmware 3.65 SceLcd.elf
python3 tools/verify_scelcd_layout.py --firmware 3.65 --raw-segment-0 SceLcd.seg0.bin
```

Encrypted SELF/SKPRX input is intentionally rejected unless the caller explicitly supplies a known raw segment-0 dump.

## Static acquisition status

Public investigation established viable PUP acquisition/extraction routes, but no independently sourced decrypted `SceLcd` image suitable for an auditable repository record was obtained. Team Molecule tooling and current Vita3K both demonstrate the required extraction/decryption machinery, but the repository does not embed firmware/decryption material and does not manufacture a static result from encrypted module bytes.

A row may be called `static verified` only when an audit record contains firmware/PUP provenance, decrypted image SHA-256, segment mapping, exact expected/actual bytes and surrounding context.

Static verification never replaces the production runtime signature gate; physical runtime success never retroactively becomes a static decrypted-binary audit.
