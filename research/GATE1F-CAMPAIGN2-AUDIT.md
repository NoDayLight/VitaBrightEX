# Gate-1F Campaign 2 — deep harness audit

This audit was triggered by physical PCH-2000 testing of the first Campaign-2 temporal witness builds. The production kernel remains frozen at `a637f54fec4e66a665874944fbea8af016d55f32`; every issue below is in the user-mode research discriminator and its evidence tooling.

## Physical symptom that exposed the observer-flow defect

A selected `YES` appeared to jump back to `NO` after `X`, while the title still read `PROBE D0-C2`. The old code was technically returning from one question and immediately constructing the next question with `int i=0`, whose first item was `NO`. Because the title did not change, there was no question number, there was no commit receipt, and the footer simultaneously said `X ANSWER` and `X CONFIRM`, a successful commit was visually indistinguishable from a rejected one.

That is a UI/state-contract defect, not acceptable observer tooling. It can cause the operator to re-answer the wrong semantic field and destroys confidence in the evidence even when the backend is correct.

## Defects found

1. **Implicit default answer on every question.** `session_choose()` initialized the cursor to item zero (`NO`). A new question therefore looked like the previous answer had been reverted. It also permitted accidental default answers.
2. **No explicit question progression.** The probe title stayed constant across 8–11 semantic fields and the UI exposed no step number.
3. **Contradictory controls text.** The same screen said both `X ANSWER` and `X CONFIRM` although there was only one commit action.
4. **Silent temporal-toggle ceiling.** At the old 25/255 ceiling `SQUARE` became a no-op with no error or state transition. The cap was not a hardware safety property and could strand the operator in the wrong A/B state.
5. **Over-broad startup recovery.** The recovery path accepted any otherwise-clean active matrix policy, not only a matrix emitted by Campaign 2. A research VPK must not reset an unrelated active policy merely because it is structurally valid.
6. **Evidence-file creation was not transactional.** The harness removed a file and then appended to it; removal and subsequent log writes were not treated as campaign-critical operations.
7. **Evidence I/O failures were ignored.** Most `c2_log()`/`c2_log_status()` return values were discarded. A campaign could continue physically after losing the evidence stream.
8. **Answers were only emitted after the whole probe.** If the app was terminated mid-probe there was no durable record of which fields had already been committed.
9. **Signed-probe evidence/schema mismatch.** The decoder required one `MATRIX2` record for `N02-C2`, while the original `collect_signed()` did not emit one. A physically successful full run would therefore have failed decoder invariants at the signed probe.
10. **CI tested source-string presence, not observer behavior.** Grepping for `SQUARE`, toggle tags and question names did not test the choice state machine that the operator actually used.
11. **C probe constants were not mechanically compared with the host expected-object manifest.** The decoder would catch a mismatch after physical testing, but CI should prevent such a build from reaching the Vita.
12. **Cleanup had weak observability.** Abort cleanup invoked Reset but did not route it through the same verified neutral transition used by normal probe reset.

## Repairs

- New questions begin **UNSELECTED**, not `NO`.
- Every screen shows an explicit `STEP nn` and a single control contract: `LEFT/RIGHT SELECT`, `X SAVE ANSWER`, `SQUARE A/B`, `TRI ABORT`.
- `X` cannot commit an unselected answer, an answer before the required temporal sequence, or an answer while physically neutral. The UI explains the rejected condition.
- Every successful commit immediately writes `ANSWER2` with probe, step, semantic field, value, toggle count and `state=PROBE`.
- Every probe ends in a verified canonical Reset and an explicit `PROBE COMPLETE / X NEXT PROBE` screen.
- The artificial toggle ceiling is removed. Only integer-overflow protection remains; normal manual observation is not budget-limited.
- Startup recovery is allowed only when both forwarded planes exactly match one of the seven frozen Campaign-2 probe matrices. Unknown active policy is refused without reset.
- Evidence starts with `O_TRUNC`; logging is fail-closed and backend transition logs are campaign-critical.
- Signed `N02-C2` now emits its required `MATRIX2` record.
- Evidence format is bumped to `2` with observer contract `TEMPORAL_AB_EXPLICIT_COMMIT`.
- Decoder format 2 cross-checks every `ANSWER2` sequence against the final `OBS2`/`SIGNED_OBS2`, requires contiguous steps, requires each commit in PROBE state, and verifies non-regressing toggle counts.
- A production-shared pure-C observer state core is host-tested in CI, including the exact regression: after a committed answer, the next question is unselected rather than silently displaying `NO`.
- CI also parses the C probe constants and compares them to `generate_expected.py` output before building the VPK.

## Remaining methodological boundary

The Campaign-2 decoder remains deliberately conservative. In particular, the `D2-C2` hard-zero diagonal only resolves a semantic pair when one pure primary changes clearly to dark/near-black. If physical data instead suggests a separable but non-shared input/output basis, this experiment may remain unresolved rather than inferring a missing mapping. That is preferable to violating Gate-1F's no-inference rule; Stage C remains blocked in that case.

No CCT, saturation, gamma, additive-affine, panel-linearisation, direct-MMIO, Stage-A, Stage-B transaction, chain-reentry, authority, or kernel code is changed by this audit.
