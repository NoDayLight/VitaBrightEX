# Gate-1F Campaign 2 — deep harness audit

This audit was triggered by physical PCH-2000 testing of the first Campaign-2 temporal witness builds. The production kernel remains frozen at `a637f54fec4e66a665874944fbea8af016d55f32`; every issue below is in the user-mode research discriminator and its evidence tooling.

## Physical symptom that exposed the observer-flow defect

A selected `YES` appeared to jump back to `NO` after `X`, while the title still read `PROBE D0-C2`. The old code was technically returning from one question and immediately constructing the next question with `int i=0`, whose first item was `NO`. Because the title did not change, there was no question number, there was no commit receipt, and the footer simultaneously said `X ANSWER` and `X CONFIRM`, a successful commit was visually indistinguishable from a rejected one.

That is a UI/state-contract defect, not acceptable observer tooling. It can cause the operator to re-answer the wrong semantic field and destroys confidence in the evidence even when the backend is correct.

## Defects found

1. **Implicit default answer on every question.** `session_choose()` initialized the cursor to item zero. A new question therefore looked like the previous answer had been reverted and could also be committed accidentally.
2. **No explicit question progression.** The probe title stayed constant across many semantic fields and the UI exposed no step number.
3. **Contradictory controls text.** The screen simultaneously advertised `X ANSWER` and `X CONFIRM` despite one action being used for both.
4. **Silent temporal-toggle ceiling.** Once the arbitrary press cap was reached, `SQUARE` became a no-op. That cap was not a hardware safety invariant and could strand the observer in the wrong A/B state.
5. **Over-broad startup recovery.** The first recovery patch accepted any otherwise-clean active matrix policy. A research VPK must not reset unrelated active policy merely because its status shape is valid.
6. **Evidence creation was destructive at launch.** Relaunching the app truncated the only Campaign-2 evidence path before a new campaign had completed, so an already-good result could be destroyed by opening the app again.
7. **Evidence I/O failures were not fail-closed.** Several `c2_log()` and `c2_log_status()` return values were ignored. Physical testing could continue after the evidence stream had become unusable.
8. **Backend validation was coupled too tightly to logging.** A failed status log could return before the caller had classified the physical Set/Reset result, weakening cleanup reasoning after an I/O fault.
9. **Answers were only durable after the whole probe.** If the app terminated mid-probe, there was no record of which semantic fields had actually been committed.
10. **Signed-probe schema mismatch.** The decoder required one `MATRIX2` record for `N02-C2`, while the original signed path did not emit it. A physically successful full run could therefore fail evidence invariants afterward.
11. **CI grepped source strings instead of testing observer behavior.** Presence of `SQUARE`, question names, and toggle tags did not test the choice state machine that the operator actually used.
12. **C probe constants were not mechanically tied to the host expected-object manifest.** A C/host divergence could reach physical testing and only be discovered by the decoder afterward.
13. **Cleanup observability was weaker than normal reset.** Abort cleanup called Reset through a less rigorous path instead of the same verified canonical-neutral transition used in the campaign.
14. **Recovery ownership did not require full active-generation coherence.** Exact matrix words alone are insufficient; recovery now also requires a clean active policy with `APPLIED` and both plane-forward generations equal to the active generation.
15. **Status-log formatting failures did not always latch evidence failure.** Buffer/format failures now poison the evidence stream so the campaign cannot silently continue.
16. **The audited harness outgrew Vita ELF metadata headroom.** After adding explicit evidence and observer checks, `vita-elf-create` reported a segment overlap while adding SCE metadata. The research VPK is now built with `-Os` rather than `-O2`; no production code or semantics changed.
17. **The first Campaign-2 decoder still encoded a shared-basis assumption in one cross-term case.** It could infer a positive cross term only when a primary became a different secondary hue. With independent `P_out` and `P_in`, an off-diagonal hardware slot may physically mean `R<-R`, `G<-G`, or `B<-B`; the correct witness is then `SAME_HUE_BRIGHTER`, not a secondary color. Leaving that unresolved would have silently biased the experiment toward `P_out == P_in` despite the mission explicitly forbidding that assumption.

## Repairs

- New questions begin **UNSELECTED**, never on `NO` or any other semantic value.
- Every answer screen displays an explicit `STEP nn` and a single control contract: `LEFT/RIGHT SELECT`, `X SAVE ANSWER`, `SQUARE A/B`, `TRI ABORT`.
- `X` cannot commit an unselected answer, an answer before the required temporal sequence, or an answer while physically neutral. The UI states the rejected condition.
- Every successful commit immediately writes `ANSWER2` with probe, step, semantic field, value, toggle count and `state=PROBE`.
- Every probe ends in a verified canonical Reset and an explicit `PROBE COMPLETE / X NEXT PROBE` screen.
- The artificial toggle ceiling is removed. Only integer-overflow protection remains.
- Startup recovery is allowed only when the clean active backend state exactly matches one of the seven frozen Campaign-2 matrices and its active/forwarded generations are coherent. Unknown active policy is refused without reset.
- The evidence format is `2` with observer contract `TEMPORAL_AB_EXPLICIT_COMMIT`.
- A running campaign writes only `ux0:data/vbe_gate1f_campaign2.partial.txt`. The previous completed `ux0:data/vbe_gate1f_campaign2.txt` is left untouched until the new run has written `COMPLETE2` and `FINALIZE2`; only then is the partial file promoted to the final path. Promotion failure preserves the partial file and restores the previous final when possible.
- Evidence writes and status serialization are fail-closed. Write, close, formatting and promotion failures prevent successful campaign completion.
- Backend transitions still classify the actual Set/Reset/GetStatus result even if evidence logging fails, so cleanup can prioritize physical neutralization rather than treating logging as the hardware result.
- Signed `N02-C2` emits the required `MATRIX2` record.
- The decoder cross-checks every `ANSWER2` sequence against the final `OBS2`/`SIGNED_OBS2`, requires contiguous steps, PROBE-state commits, non-regressing toggle counts, and the final `FINALIZE2` promotion marker.
- Positive cross-term decoding now explicitly accepts `SAME_HUE_BRIGHTER` as an evidence-backed same-component relation, so independent row and column permutations remain representable instead of being implicitly forced to a shared RGB basis.
- A pure-C observer state core is host-tested in CI, including the exact physical regression: after a committed answer the next question is unselected rather than silently displaying `NO`.
- CI parses all seven C probe constants and compares them against `generate_expected.py` before the VPK is built.
- CI freezes the production-kernel boundary, exact Gate-1E SKPRX hash, frozen Campaign-1 decoder, expected-object hashes, R3 package identity, evidence-lifecycle contract and absence of any `.skprx` from the artifact.

## What the repaired UI must look like

A valid R3 question begins with a value of:

```text
-- SELECT --
```

not `NO` or `YES`. After the observer uses LEFT/RIGHT and presses X while `STATE PROBE` is valid, the next screen must have a larger `STEP nn`, a different semantic field when appropriate, and again show `-- SELECT --`. Finishing a probe must reach a separate `PROBE COMPLETE` screen before the next probe begins.

This behavior is now an executable host-tested state-machine contract rather than a source-code convention.

## Remaining methodological boundary

The Campaign-2 decoder remains deliberately conservative. In particular, `D2-C2` only resolves a diagonal semantic pair when one pure primary changes clearly to dark/near-black. If physical data instead suggests a more complicated diagonal response, this experiment may remain unresolved rather than infer a missing mapping. For cross terms, however, both secondary-hue transitions and same-hue-brighter transitions are now represented so `P_out` and `P_in` remain independent.

Stage C remains blocked unless physical evidence independently determines the required row and column mappings. No CCT, saturation, gamma, additive-affine, panel-linearisation, direct-MMIO, Stage-A, Stage-B transaction, chain-reentry, authority, or production-kernel code is changed by this audit.
