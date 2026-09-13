# SceLcd raw-layout verification

The PCH-2000 brightness extension replaces a 17-byte table inside segment 0 of `SceLcd`. A firmware version alone never authorizes injection: production resolves the candidate address in the loaded module and requires the exact Sony stock table before any write.

## Runtime signature

```text
31 37 43 50 58 67 77 88 100 114 129 147 166 182 203 227 255
```

Any mismatch fails open before injection. This runtime guard remains mandatory even for statically verified firmware because it detects unexpected module builds or conflicting pre-patches.

## Evidence table

| Firmware | Candidate segment-0 offset | Physical runtime evidence | Retail static evidence |
|---|---:|---|---|
| 3.60 | `0x1B00` | pending | pending |
| 3.65 | `0x1B48` | **PROVEN** | **PROVEN** |
| 3.67 | `0x1B48` | pending | pending |
| 3.68 | `0x1B48` | pending | pending |
| 3.69 | `0x1B48` | pending | pending |
| 3.70 | `0x1B48` | pending | pending |
| 3.71–3.74 | not accepted | n/a | unsupported by production raw injection |

Only the 3.65 row has both kinds of evidence. Repetition of inherited offsets in downstream source is not independent verification.

## Retail 3.65 static proof

The reproducible research workflow starts from the exact retail PUP, verifies hashes, decrypts packages/os0, extracts `SceLcd.skprx`, converts SELF to ELF, then checks the exact segment-relative location before deleting proprietary firmware bytes.

Pinned provenance:

```text
retail 3.65 PUP MD5
0a0f2a9ae58968ac5d1d2127049c3cba

retail 3.65 PUP SHA-256
86859b3071681268b6d0beb5ef691da874b6726e85b6f06a1cdf6a0e183e77c6

decrypted os0 SHA-256
35480a01ea783df859326d8698831afd3e850717f5b7785ab9964eab23712856

SceLcd retail ELF SHA-256
24752bb4c69f0c241701cab0364cde9cbe5c95128b6bfe02bf3263aad524246e
```

The exact stock signature above occurs at:

```text
SceLcd
segment 0
+ 0x1B48
firmware 3.65 retail
```

Research branch/checkpoint:

```text
research-iftu-static-audit
7e6eb480b9919365be31d73a64cac4d620e20c4b

IFTU static audit
run #16
run ID 34752217746
SUCCESS
```

The research workflow pins its extraction/analysis dependencies and removes extracted proprietary bytes at job completion.

## Physical 3.65 proof

On the physical PCH-2000 / firmware `0x03650000` / 3.65 Ensō target, the hardened backend selected segment-0 `0x1B48`, passed the same 17-byte signature gate, installed the extended table and hooks, reapplied current brightness, reached LiveArea and passed slider/A-B testing.

Therefore 3.65 `segment0 + 0x1B48` is deliberately classified:

```text
STATICALLY PROVEN
+
PHYSICALLY PROVEN
```

The two proofs are independent and neither removes the production runtime signature check.

## Static verifier

`tools/verify_scelcd_layout.py` accepts a decrypted SceLcd ELF or an explicitly identified raw segment-0 dump, records SHA-256, maps the segment-relative offset and requires the exact stock signature. Encrypted SELF/SKPRX input is not treated as equivalent evidence.

For ELF input the mapping is:

```text
file location = first PT_LOAD.p_offset + VitaBright segment offset
```

No other firmware row is promoted merely because it shares an inherited candidate offset.
