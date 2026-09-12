# Diagnostics and error-domain model

Pseudo-v1.4 treats per-domain error slots as the authoritative internal diagnostic state. `VitaBrightStatus.last_error` and `last_error_detail` are compatibility output only; production control flow must not read them back to reconstruct state.

## Domains

The additive diagnostics ABI exposes independent slots for:

- synchronization;
- brightness/backend/LUT transaction;
- configuration;
- panel color-space;
- filter;
- user input.

`VitaBrightStatus` remains ABI v2 with unchanged layout. `VitaBrightDiagnostics` remains additive ABI v1.

## Ownership rule

A subsystem updates only the domain it owns, at the stage that actually succeeded or failed.

- `config_load()` owns CONFIG.
- LCD/OLED setup, replacement, persistence and rollback own BRIGHTNESS.
- color-space operations own COLOR_SPACE.
- filter runtime operations own FILTER.
- validated user-facing arguments own INPUT.
- state-lock failures own SYNC.

The outer reload orchestrator combines control-flow return values but does not clear multiple domains based on composite success/failure.

Therefore a successful config repair clears CONFIG even if the following LUT transaction fails. Conversely, a successful brightness replacement does not clear a still-broken config.

## Legacy summary precedence

For compatibility, `last_error/detail` is derived deterministically from the domain state using this precedence:

```text
SYNC > BRIGHTNESS > CONFIG > COLOR_SPACE > FILTER > INPUT
```

The summary is intentionally lossy; full truth is available only through `vitabrightGetDiagnostics()`.

## Rollback semantics

Live LCD/OLED replacement snapshots the requested BRIGHTNESS-domain failure directly, never the legacy summary.

If replacement fails and rollback succeeds:

- the previous committed table/backend becomes operational again;
- capability state returns to active where appropriate;
- the requested operation's failure remains in BRIGHTNESS so the caller can see that the requested edit did not commit.

If rollback fails:

- BRIGHTNESS becomes `VBE_ERR_LUT_ROLLBACK` with recovery detail;
- backend capability state remains degraded/failed rather than falsely active.

Resource teardown is also transactional. A failed taiHEN hook/injection release produces `VBE_ERR_RESOURCE_RELEASE`, leaves the unconfirmed handle owned, marks the backend degraded, and blocks reinitialization over uncertain ownership.

## Unsupported filter requests

Unsupported advanced filtering is a capability outcome, not a runtime error. `VBE_RESULT_UNSUPPORTED` is a positive nonzero result so negative values remain reserved for actual runtime/SCE/taiHEN failures.

For an advanced CCT/gamma/contrast/brightness/panel-enhance request:

- CSC and transfer capabilities remain `UNSUPPORTED`;
- FILTER diagnostics remain clear;
- the editor reports an unsupported capability rather than a generic failure;
- no speculative framebuffer/IFTU path is introduced.

Verified hardware invert remains separately capability-gated and can populate FILTER on real runtime failure.

## Regression proof

`tests/status_error_host.c` exercises the production lifecycle/error core, including stale-domain repair cases, summary precedence, rollback-success retention and rollback-failure replacement. `tests/filter_policy_host.c` exercises the production unsupported-capability policy. Structural CI also forbids backends from reading legacy `last_error/detail` as internal state.
