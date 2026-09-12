# Diagnostics and error-domain model

Pseudo-v1.4 treats per-domain error slots as authoritative internal diagnostic state. `VitaBrightStatus.last_error` and `last_error_detail` are compatibility output only; production control flow does not read them back to reconstruct state.

`VitaBrightStatus` remains ABI v2 with unchanged layout. `VitaBrightDiagnostics` remains additive ABI v1.

## Domains

The diagnostics ABI exposes independent slots for:

- SYNC — state-transition mutex/lifecycle;
- BRIGHTNESS — backend/LUT transaction, recovery and persistence;
- CONFIG — authoritative config loading;
- COLOR_SPACE — panel color-space operations/restoration;
- FILTER — verified filter/invert runtime operations/restoration;
- INPUT — user-facing argument validation.

A subsystem updates only the domain it owns, at the stage that actually succeeded or failed. The outer reload/stop orchestrators combine control-flow results but do not clear unrelated domains.

Therefore a successful config repair clears CONFIG even if the following LUT transaction fails. A successful brightness transaction does not clear a still-broken config. A later successful stop stage does not erase an earlier unresolved FILTER/COLOR_SPACE/BRIGHTNESS stop failure.

## Synchronization result composition

Runtime operations acquire the shared state lock and compose their operation result with the unlock result through the production synchronization core:

```text
operation fails + unlock succeeds
    -> return operation failure

operation succeeds + unlock fails
    -> return synchronization failure

operation fails + unlock fails
    -> scalar return is synchronization failure
       subsystem error remains in its own domain
       SYNC records the unlock failure
```

A failed unlock changes internal lock lifecycle to `DEGRADED`; ordinary runtime acquisition is no longer legal. A later confirmed synchronization cycle may clear a repaired SYNC fault. Status/LUT getters do not copy their snapshot to userland when unlock ownership cannot be confirmed.

## Module lifecycle boundary

Module orchestration records only whether startup crossed successful synchronization creation:

```text
INERT
    runtime initialization never committed, or clean stop completed

RUNTIME
    synchronization creation succeeded; normal runtime teardown responsibility exists
```

This is deliberately not another resource-ownership model. Mutex, backend, filter, color-space and persistence ownership remain authoritative in their existing subsystems.

If `state_lock_init()` fails, it records the SYNC failure and leaves lock lifecycle ABSENT. Startup returns fail-open `SCE_KERNEL_START_SUCCESS` before config-file loading, backend hooks/injection, invert/color-space programming or persistence resource creation. The resulting `INERT + ABSENT` module is clean and may unload directly.

That does not make ABSENT a generic success condition. `RUNTIME + ABSENT`, either module state with DEGRADED, and other inconsistent pairs are unload-unsafe.

## Legacy summary precedence

For compatibility, `last_error/detail` is derived deterministically from the domain state using this fixed precedence:

```text
SYNC > BRIGHTNESS > CONFIG > COLOR_SPACE > FILTER > INPUT
```

The summary is intentionally lossy. Full truth is available through `vitabrightGetDiagnostics()`.

## Backend ownership and transaction classes

Internal backend ownership has exactly three semantic states:

```text
CLEAN
    no backend taiHEN hook/injection resource remains owned

ACTIVE
    the complete committed backend stack is installed

DEGRADED
    ownership/recovery cannot be proven clean;
    no new backend transaction may start
```

Transaction attempts are classified independently:

```text
TXN_OK
    requested transaction completed

TXN_FAILED_CLEAN
    requested transaction failed but ownership is known clean

TXN_FAILED_DIRTY
    failure left retained/uncertain ownership
```

`ret < 0` alone is never used to infer rollback legality. Rollback is legal only after a `FAILED_CLEAN` candidate failure while ownership is CLEAN and a previous committed backend existed.

## Rollback semantics

If replacement fails and rollback succeeds:

- the previous committed table/backend/source identity becomes operational again;
- capability state returns ACTIVE;
- the requested operation's BRIGHTNESS failure remains visible because the requested edit did not commit;
- the public negative result reports the requested failure.

If rollback itself fails cleanly:

- BRIGHTNESS becomes `VBE_ERR_LUT_ROLLBACK`;
- the backend is not reported ACTIVE;
- recovery failure dominates the public result.

If candidate cleanup or rollback cleanup fails dirty:

- BRIGHTNESS becomes `VBE_ERR_RESOURCE_RELEASE`;
- ownership is `DEGRADED`;
- no rollback/reinitialization is attempted over the uncertain resources;
- recovery/ownership failure dominates the public result.

Resource handles are invalidated only after confirmed taiHEN release. Dependency teardown is power hook -> brightness hook -> table injection and stops at the first failed release so a surviving hook does not lose resources it may still depend on.

## Persistence diagnostics

Persistence uses the compact BRIGHTNESS errors:

- `VBE_ERR_PERSISTENCE_PREPARE` — pre-commit setup/write/sync/close failure;
- `VBE_ERR_PERSISTENCE_COMMIT` — rename commit failure;
- `VBE_ERR_PERSISTENCE_CLEANUP` — temporary-resource cleanup could not be confirmed.

Detailed failing stages remain available to diagnostic logging/production outcome state without adding one public enum per syscall. Failed persistence never changes committed LUT source identity.

`VBE_RESULT_NO_FILE_SOURCE` is a positive capability/control result for a committed compiled LCD fallback. It means no authoritative file exists to overwrite; it is not a runtime fault.

## Stop transaction

Stop first classifies module orchestration state together with the lock lifecycle:

```text
INERT + ABSENT
    -> no runtime teardown responsibility
    -> SCE_KERNEL_STOP_SUCCESS

RUNTIME + RUNNING
    -> enter the serialized runtime stop transaction

all other pairs
    -> SCE_KERNEL_STOP_FAIL
```

The runtime stop then attempts, under STOPPING serialization:

```text
invert/filter restoration
-> panel color-space restoration/read-back
-> backend persistence-resource cleanup + taiHEN teardown
-> mutex unlock/delete
```

Each subsystem retains its own error domain. A tiny stop accumulator answers only whether unload is safe. Any unresolved runtime stop-critical failure returns `SCE_KERNEL_STOP_FAIL`; the module remains resident. Successful later teardown stages do not erase earlier domain failures.

Color-space ownership is relinquished only after original mode is confirmed by read-back. Invert has no verified getter; therefore setter failure is treated as unconfirmed restoration and blocks successful unload. Mutex ID/lifecycle are relinquished only after confirmed unlock/delete. Module orchestration returns to INERT only after confirmed mutex deletion.

If stop teardown fails and cancellation unlock also fails, SYNC remains failed/DEGRADED and future unload cannot take the inert path. If mutex deletion fails after a successful stop unlock, runtime responsibility remains rather than being discarded.

## Unsupported filter requests

Unsupported advanced filtering is a capability outcome, not a runtime error. `VBE_RESULT_UNSUPPORTED` is positive/nonzero so negative values remain actual runtime/SCE/taiHEN failures.

Advanced CCT/gamma/contrast/brightness/panel-enhance requests keep CSC/transfer capabilities `UNSUPPORTED`, leave FILTER diagnostics clear, perform no speculative hardware write, and are reported by the editor as unsupported rather than generic failure. Verified hardware invert remains separately capability-gated.

## Regression proof

Production-shared host suites cover distinct layers:

- `tests/transaction_core_host.c` — ownership transitions, rollback legality/result dominance, source commit/persistence eligibility and stop accumulator;
- `tests/persistence_core_host.c` — persistence sequence plus fd/temp ownership under injected failures;
- `tests/state_lock_core_host.c` — runtime/STOPPING/DEGRADED mutex lifecycle and operation+unlock precedence;
- `tests/module_lifecycle_core_host.c` — inert startup/stop symmetry, runtime stop eligibility, DEGRADED rejection, failed-delete responsibility and clean-stop idempotence using the production module/lock lifecycle cores;
- `tests/status_error_host.c` — independent domains, stop-domain preservation, rollback diagnostics and summary precedence;
- `tests/filter_policy_host.c` — unsupported-capability policy.

Structural CI guards the startup ordering and stop classification boundary in addition to the existing ownership tripwires; it remains a guardrail rather than semantic proof.
