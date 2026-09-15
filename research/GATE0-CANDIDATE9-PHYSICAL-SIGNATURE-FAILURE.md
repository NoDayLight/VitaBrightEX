# Candidate 9 physical signature failure — PCH-2000 / firmware 3.65

This document preserves the physical evidence that authorized Candidate 10. Raw private hardware dumps are not committed.

## Candidate and status evidence

- Candidate-9 research SHA: `5a1a1ac29340f4a5345b08fc74ac5942559b9428`
- Target: PCH-2000
- Firmware: `0x03650000` / 3.65
- Read-only status snapshot SHA-256: `62d36f658e7c278c43ad6184da85c4bc38a42461d3cccf379ab1ec434b654d5b`
- `vbeTraceGetStatus()`: success
- protocol: 7
- lifecycle: INERT
- owned hook mask: `0x00`
- required hook mask: `0x1F`
- missing hook mask: `0x1F`
- hook failure mask: `0x00000020` = `VBE_TRACE_FAIL_PANEL_WRITE_SIG`
- slots / committed / lost: `0 / 0 / 0`

This physically verifies the Candidate-9 Modulemgr repair: the kernel module loads and the `VbeColorTrace` syscall library resolves. The failure occurs later in `prepare_lcd()` before any hook is installed.

## Read-only SceLcd runtime probe

The zero-hook runtime probe reported all setup calls successful and the following module layout:

```text
SceLcd modid: 0x000100F7
segment[0]: base 0x009BE000 size 0x1E64
segment[1]: base 0x009B7000 size 0xD8
writer:     0x009BEA54 = segment0 + 0xA54
reader:     0x009BE5B4 = segment0 + 0x5B4
```

The user-side raw runtime-probe SHA-256 was not included in the conversation transcript. The complete decoded 180-byte structure was preserved in the test transcript; a deterministic reconstruction from those fields hashes to `258ee65a2ce054ac2cde5fe46f0d0a20b5b812dfe9dd4eb1d2fd08d0a29ffe57`, but this value is explicitly **not** represented as an independently read raw-file hash.

## Writer evidence

Static retail first 16 bytes:

```text
2D E9 F8 43 42 F2 00 07 C8 F2 00 17 05 46 89 46
```

Physical runtime first 16 bytes:

```text
2D E9 F8 43 47 F2 00 07 C0 F2 9B 07 05 46 89 46
```

The prefix `2D E9 F8 43` and suffix `05 46 89 46` are invariant. The middle pair remains MOVW/MOVT to `r7` but is relocated:

```text
static target:   0x81002000
runtime target:  0x009B7000
segment[1].base: 0x009B7000
```

## Reader evidence

Static retail first 16 bytes:

```text
2D E9 F8 4F 42 F2 00 06 C8 F2 00 16 81 46 0F 46
```

Physical runtime first 16 bytes:

```text
2D E9 F8 4F 47 F2 00 06 C0 F2 9B 06 81 46 0F 46
```

The prefix `2D E9 F8 4F` and suffix `81 46 0F 46` are invariant. The middle pair remains MOVW/MOVT to `r6` and resolves independently to the same runtime segment:

```text
static target:   0x81002000
runtime target:  0x009B7000
segment[1].base: 0x009B7000
```

## Engineering conclusion

The physical offsets and function identity are correct. Candidate 9's invalid invariant was the literal comparison of relocation-bearing instruction bytes. Candidate 10 is authorized only to replace that raw comparison with:

```text
exact invariant prefix
+ valid Thumb-2 MOVW
+ valid Thumb-2 MOVT
+ exact expected destination register
+ decoded target == runtime SceLcd segment[1].vaddr
+ exact invariant suffix
```

No hook-set, protocol, ring, CSC, IFTU, panel-payload, drain, Modulemgr-resolver, or production change is authorized by this evidence.
