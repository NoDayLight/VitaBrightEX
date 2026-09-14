# Gate-0A v7 observational baseline

This package is a **PCH-2000 / firmware 3.65 research diagnostic**. It is observational only. It does not implement a production color backend and it does not narrow the pseudo-v1.4 feature target.

## Safety boundary

The kernel observer owns five taiHEN hooks until reboot: private SceLowio CSC A/B, `ksceIftuEnable`, and the private SceLcd panel writer/reader. Runtime `taiHookReleaseForKernel` is forbidden because pinned taiHEN v0.11 unlinks and immediately reclaims hook state without an in-flight callback grace period. Dynamic unload is therefore intentionally unavailable once any hook has been acquired.

Gate-0A copies **zero panel payload bytes**. Panel pointers are only represented as raw values plus non-dereferencing range provenance. CSC inputs are copied only for exact retail-3.65 valid-plane, non-NULL calls after the dedicated static audit proves Sony itself consumes the complete 0x3C input object. Reader output memory is never copied.

A partial hook installation is `PARTIAL_OWNED`: capture remains disabled, unload remains forbidden, and recovery is reboot-only. Never attempt to unload this diagnostic dynamically.

## Preflight

After installing the exact packaged `vbe_color_trace.skprx`, perform a clean reboot. Before creating experimental events, require firmware `0x03650000`, protocol 7, lifecycle `RUNNING`, `owned_hook_mask == required_hook_mask == 0x1F`, zero missing/fail masks, and `lost_records == 0`. If lifecycle is `PARTIAL_OWNED`, stop, correct configuration, and reboot.

## Controlled phase sequence

Use markers around ordinary Sony-controlled behavior only: `BASELINE_IDLE`, `BRIGHTNESS_A_1`, A->B, `BRIGHTNESS_B`, B->A, `BRIGHTNESS_A_2`, `COLORSPACE_0_1`, 0->1, `COLORSPACE_1`, 1->0, `COLORSPACE_0_2`, `DISPLAY_OFF`, normal display-off, `DISPLAY_ON`, normal display-on, `PRE_SUSPEND`, normal suspend/resume, `POST_RESUME`, `DIM_ENTRY`, `DIM_EXIT`.

Use only color-space operations legitimately exposed on the target. Do not invoke private CSC setters, panel commands, direct IFTU mutation, or MMIO writes.

## Capacity and draining

The ring has 512 records and never blocks. Full means drop + `lost_records++`; Sony still executes. An authoritative baseline requires zero loss. Split the experiment if needed. Drain via `RUNNING -> pause -> wait PAUSED && active_producers==0 -> read -> preserve raw dump -> reset(1) -> RUNNING`. The supplied drain writes `ux0:data/vbe_gate0a_trace.bin`. Preserve its raw SHA-256 before decoding.

A valid hardware baseline requires protocol 7, full ownership, committed/ordered records, valid ENTER/EXIT pairing, zero panel payload bytes, valid marker phases, and zero lost records. Malformed/lossy evidence is `EMPIRICAL_BASELINE_INCOMPLETE`. Equivalent-state comparisons report only `BYTE_IDENTICAL`, `DIFFERENT`, `NO_EVENT`, or `INCONCLUSIVE`; passive equality is not an ownership proof.
