# VitaBrightEX pseudo-v1.4 — Gate-0 observer

**Target:** PS Vita PCH-2000 / Slim / LCD, retail firmware **3.65** (Ensō target).

**OBSERVATIONAL ONLY — NO DISPLAY-STATE MUTATION.**

This artifact exists to observe Sony's live display pipeline before any new color backend is attempted. It installs only four guarded observation points: private IFTU CSC A/B and the pinned retail-3.65 SceLcd panel writer/reader. It does not hook brightness/color-space/power setters; phase markers provide the experiment timeline without expanding the kernel interposition surface.

Safety contract:

- static, module-owned 512-record ring; no hook-path allocation;
- one atomic reservation per trace record; no waits, locks, worker threads, timers, or hardware polling in hook paths;
- full ring drops new trace records and increments `lost`; it never waits for a consumer;
- CSC copy is exactly `0x3C`, only for planes 0..4; NULL remains a NULL event;
- panel writer payload is copied only for non-NULL lengths 1..255; out-of-bound capture is recorded as `BOUNDS_REJECTED`, never truncated;
- panel reader payload is **never copied**. Invocation, arguments, ordering and raw return are retained with `READ_NOT_CAPTURED_UNPROVEN`;
- every hook invokes the Sony original exactly once with unchanged arguments and returns the raw Sony result unchanged;
- trace failure cannot suppress the Sony call;
- stop disables capture before checking quiescence; unhook occurs only with zero active hook calls, in reverse installation order;
- no CSC replacement, panel command, brightness write, color-space write, registry write, MMIO write, or gamma experiment exists in this module.

The kernel module fail-opens on unsupported firmware or missing/signature-mismatched hook sites. It enables capture only when all four required hooks are installed.

## Phase markers

The exported `vbeTraceMark()` accepts:

`BASELINE_IDLE`, `BRIGHTNESS_A_1`, `BRIGHTNESS_B`, `BRIGHTNESS_A_2`, `BRIGHTNESS_A_REPEAT`, `COLORSPACE_0_1`, `COLORSPACE_1`, `COLORSPACE_0_2`, `DISPLAY_OFF`, `DISPLAY_ON`, `PRE_SUSPEND`, `POST_RESUME`, `DIM_ENTRY`, `DIM_EXIT`.

Markers write only tracer-owned ring memory. They do not touch Sony display state.

## Drain

The supplied drain utility disables capture, waits outside the hook path for all active hooks to leave, snapshots only the zero/unsupported snapshot envelope, copies committed records, writes `ux0:data/vbe_gate0_trace.bin`, then starts a new empty trace epoch. A nonzero lost counter, missing hook, or bounds-rejected writer event makes the capture `PARTIAL` rather than authoritative.

Preserve the raw `.bin` and its SHA-256. Decode to a separate derivative with `gate0_trace_decode.py`; summarize marker-delimited evidence with `gate0_decision_audit.py`. Never overwrite the raw capture.

## Hardware sequence

Use a normal full-power cold boot. Do not use `L` plugin bypass for the successful path. After baseline activation, run the controlled sequence from the engineering handoff: brightness A/B/A, repeat A where useful, color-space 0/1/0, display off/on, suspend/resume, inactivity dim/restore. Drain between phases if needed to avoid ring overflow.

Conclusions from runtime data must remain tagged **OBSERVED**, **STATICALLY PROVEN**, **INFERRED**, or **UNKNOWN**. A changing panel command is not to be called “gamma” merely because a display action correlated with it; NULL CSC events are not to be called “cache replay” until evidence establishes that meaning.
