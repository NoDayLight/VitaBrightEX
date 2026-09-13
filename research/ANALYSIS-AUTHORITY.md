# VitaBrightEX LCD color-engine analysis authority

`research/vita_elf_legacy.py` preserves the original ARM.exidx-bounded implementation only for reproducibility of older evidence. It is not the current authority for function-local claims.

`research/vita_elf_audit.py` is the validated front door. Logical-function boundaries are closed over ARM.exidx starts, exports, module start/stop entries, import stubs, reachable direct BL/BLX targets, and explicit regression starts. Known no-return stack-check failures terminate paths. Import tables are count-aware and do not dereference absent function, variable, or TLS tables.

Derived function-local evidence must carry `start`, `logical_end`, `exidx_range`, `exidx_exact`, `boundary_sources`, and `termination_reason`. A known logical start may not be consumed as silent linear fallthrough; direct branches to another established start are recorded as shared/tail-entry edges.
