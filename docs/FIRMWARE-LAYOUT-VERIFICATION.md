# SceLcd raw-layout verification

VitaBrightEX uses NID-resolved functions wherever possible, but the PCH-2000 brightness extension still replaces a 17-byte table inside **segment 0 of `SceLcd`**. That makes the raw table offset a separate firmware-layout claim and it must be treated accordingly.

## Production safety rule

A firmware version being on the source whitelist is never sufficient by itself. Before injection, v1.4 resolves `segment 0 + offset` in the *loaded* `SceLcd` module and requires the exact stock table:

```text
31 37 43 50 58 67 77 88 100 114 129 147 166 182 203 227 255
```

If any byte differs, the backend reports a layout mismatch and fails open without injecting. This runtime check remains mandatory even after static binary verification because it also detects conflicting pre-patches or an unexpected module build.

## Current inherited offsets

| Firmware | Candidate segment-0 offset | Runtime exact-signature gate | Static decrypted-binary verification |
|---|---:|---|---|
| 3.60 | `0x1B00` | required | pending binary evidence |
| 3.65 | `0x1B48` | required | pending binary evidence |
| 3.67 | `0x1B48` | required | pending binary evidence |
| 3.68 | `0x1B48` | required | pending binary evidence |
| 3.69 | `0x1B48` | required | pending binary evidence |
| 3.70 | `0x1B48` | required | pending binary evidence |
| 3.71–3.74 | not accepted | n/a | unsupported |

The first six candidates come from original VitaBright provenance. Repetition in downstream forks is **not** counted as independent static verification.

## Repository verifier

`tools/verify_scelcd_layout.py` verifies a decrypted SceLcd image and records its SHA-256. The injection offset is relative to module segment 0, so for ELF input the verifier parses the 32-bit little-endian program headers, selects the first `PT_LOAD` segment and maps:

```text
file location = segment0.p_offset + VitaBright table offset
```

It then requires the exact 17-byte stock signature.

Example:

```sh
python3 tools/verify_scelcd_layout.py --firmware 3.65 SceLcd.elf
```

For a known exact dump of module segment 0:

```sh
python3 tools/verify_scelcd_layout.py --firmware 3.65 --raw-segment-0 SceLcd.seg0.bin
```

Encrypted SELF/SKPRX data must not be passed off as a decrypted image; the verifier intentionally rejects non-ELF input unless `--raw-segment-0` is explicitly selected.

## PUP acquisition / extraction path investigated

Two public routes were checked before hardware testing:

1. **VitaDeploy firmware payloads.** Current VitaDeploy source publishes split PUP payload names for 3.60, 3.65 and 3.68 (`360.01/.02`, `365.01/.02`, `368.01/.02`) together with CRC32 values and concatenates each pair into `PSP2UPDAT.PUP`. This establishes a reproducible public acquisition route for those three versions.
2. **Team Molecule `sceutils`.** `pup_fiction.py` can split/decrypt a PUP, build `os0.bin`/`vs0.bin`, extract the filesystem and convert SELF modules to ELF. The public repository deliberately omits `keys.py`, however, so a clean automated checkout does not contain the retail key material required to finish this route.
3. **Vita3K.** Current Vita3K has a `--firmware <PUP>` installation path and contains current PUP/SELF decryption code/key material internally. Its normal firmware-install CLI extracts the Vita filesystem for the emulator, but it does not expose a documented command that emits an arbitrary decrypted `SceLcd` ELF for this audit. Treating the installed encrypted SKPRX as if it were segment bytes would be invalid.

Because this repository does not embed Sony firmware or private decryption keys, and no public checked-in decrypted `SceLcd` image was found, the table above remains explicitly **pending static binary evidence** rather than manufacturing a verification result.

## What counts as completion

For a firmware row to move to `static verified`, commit or attach an audit record containing:

- firmware version and PUP source/digest,
- decrypted `SceLcd` image SHA-256,
- verifier output showing the segment-0 mapping,
- exact 17 expected/actual bytes,
- surrounding byte context.

The firmware can then proceed to physical hardware regression. Static verification does not replace the runtime signature gate or hardware tests.
