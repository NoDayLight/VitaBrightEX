# Diagnostics and error-domain model

Pseudo-v1.4 treats per-domain slots as authoritative internal state. `VitaBrightStatus.last_error/last_error_detail` are compatibility output only; control flow never reconstructs state from them.

`VitaBrightStatus` remains ABI v2. `VitaBrightDiagnostics` remains additive ABI v1. `VitaBrightDisplayFilterState` ABI v1 separately exposes requested/committed filter truth.

## Error domains

- **SYNC** — mutex lifecycle/acquire/unlock/delete;
- **BRIGHTNESS** — backend/LUT transaction, rollback and persistence;
- **CONFIG** — authoritative config loading;
- **COLOR_SPACE** — panel color-space mutation/restoration;
- **FILTER** — runtime failures in a supported generic filter backend;
- **INPUT** — invalid user arguments.

A capability being unsupported is not a runtime FILTER error.

Legacy summary precedence is fixed:

```text
SYNC > BRIGHTNESS > CONFIG > COLOR_SPACE > FILTER > INPUT
```

## Multi-stage reload result

Reload uses one production-shared severity composer:

```text
negative runtime failure
>
positive capability/partial result
>
zero success
```

Examples:

```text
OK + UNSUPPORTED                -> UNSUPPORTED
UNSUPPORTED + negative failure  -> negative failure
negative failure + UNSUPPORTED  -> retain negative failure
```

This prevents an early capability limitation from hiding a later real failure. Domain diagnostics remain independent, so both facts are still observable.

## Synchronization boundary

Unlock/release ownership is stricter than ordinary stage composition:

```text
operation failure + unlock succeeds
    -> operation failure

operation success + unlock failure
    -> synchronization failure

operation failure + unlock failure
    -> synchronization failure scalar return
       + operation's original domain remains recorded
       + SYNC records unlock failure
```

Failed unlock makes the lock lifecycle DEGRADED and blocks ordinary future acquisition. Userland snapshots are not copied after an unconfirmed unlock.

## Brightness ownership and rollback

Backends use:

```text
CLEAN
ACTIVE
DEGRADED
```

Attempt outcomes are `TXN_OK`, `TXN_FAILED_CLEAN`, `TXN_FAILED_DIRTY`. A clean candidate failure may roll back a previous state only while ownership is CLEAN. A dirty failure makes ownership DEGRADED and forbids rollback/new mutation.

If rollback succeeds, the previous committed backend/state becomes ACTIVE again while the requested BRIGHTNESS failure remains visible. Clean rollback failure becomes `VBE_ERR_LUT_ROLLBACK`; dirty cleanup/recovery failure becomes `VBE_ERR_RESOURCE_RELEASE`.

OLED rollback copies one complete `VbeOledLutState`, so base/runtime/source/panel/requested-transform/applied-transform cannot drift independently.

## Persistence diagnostics

Persistence uses:

- `VBE_ERR_PERSISTENCE_PREPARE` — setup/write/sync/close failure before commit;
- `VBE_ERR_PERSISTENCE_COMMIT` — rename commit failure;
- `VBE_ERR_PERSISTENCE_CLEANUP` — temp/fd cleanup cannot be confirmed.

`VBE_RESULT_NO_FILE_SOURCE` is positive capability/control output for a COMPILED LCD state; it is not an error.

## Generic filter diagnostics

Pseudo-v1.4 currently has no mutation-safe generic filter backend:

```text
INVERT     unsupported
AFFINE_CSC unsupported
TRANSFER   unsupported
```

Unsupported requests are retained in `VitaBrightDisplayFilterState.requested` and `unsupported_domains`; committed hardware state remains neutral and `failed_domains` remains clear. FILTER diagnostics remain zero because no runtime mutation was attempted.

Invert is unsupported because original-state acquisition is unproven. Persistent CSC is unsupported because original-state restoration/lifecycle is unproven. Transfer is unsupported because no nonlinear hardware stage is proven.

Panel color-space is not part of this generic filter system; it has its own COLOR_SPACE domain and proven original-state/read-back restoration flow.

## Stop transaction

Stop classification is:

```text
INERT + ABSENT    -> clean direct stop
RUNTIME + RUNNING -> serialized runtime teardown
anything else     -> STOP_FAIL
```

The runtime teardown resets generic requested filter state (no generic hardware state is owned), restores/read-backs panel color-space, cleans persistence/backend taiHEN resources, then unlocks/deletes the mutex. Any unresolved owned resource or synchronization state returns `SCE_KERNEL_STOP_FAIL` and leaves the module resident.

## Regression coverage

Production-shared tests cover:

- result severity, including `UNSUPPORTED` followed by a negative failure;
- synchronization lifecycle and unlock dominance;
- module lifecycle/inert/runtime stop classification;
- independent diagnostic domains and summary precedence;
- rollback legality/recovery classification;
- persistence error sequencing;
- generic filter requested/committed/unsupported truth;
- config source rollback and OLED complete-state rollback.

Structural CI remains a guardrail, not semantic proof.
