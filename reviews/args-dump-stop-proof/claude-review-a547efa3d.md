# Re-review — PR #2677 at final head `a547efa3d`

Base `fa6528f4e` → head `a547efa3d`; delta reviewed against `d9808101e`, then the complete mechanism re-derived rather than inherited. Read-only: no edits, builds, installs, hardware, GitHub writes, or CLI delegation.

## 0. Summary

The `a547` delta closes the two findings that had mechanism behind them, with the right implementation in both cases: the unproven close now consumes its own host-only drain-loss counter after joining the threads that own it, and successful held recovery is remembered per run/lane by `(device buffer pointer, sequence)` so retry is idempotent for records as it already was for counters. I re-verified the delta's lifecycle, bounds, ordering and test assertions against the code, and re-checked the four previously-cleared invariants at this head. **One Should-fix remains, doc-only**; no Must-fix. **Verdict: approve**, with the doc row as a merge-blocking-optional follow-up at the parent's discretion and CI still outstanding.

## 1. What the delta changes and why it was needed

Two of my `d980` findings named a concrete mechanism; both are now fixed at the mechanism, not papered over.

**Drain-loss leak across an unproven close.** `report_drain_drops()` documented its own invariant — the read consumes the count so each run reports its own — but the unproven branch never called `reconcile_counters()`, its only consumer. Run N's retired ready buffer therefore survived into run N+1, where it both produced a `LOG_ERROR` naming run N's buffer against run N+1 and forced that clean run's `dropped_records_complete` to `false`.

**Additive held recovery under retry.** The counter snapshot became idempotent at `d980` (`store` rather than `fetch_add`), but record recovery did not: nothing cleared `current_buf_ptr` and nothing remembered a successful pass, so with two lanes where one read fails, the retry re-recovered the lane that had already succeeded — duplicate records and a second advance of `written_payload_counts_`.

## 2. Delta mechanism and verification

**(a) `report_drain_drops()` after `stop(false)`** (`args_dump_collector.cpp:709`). The placement is the load-bearing part and it is right: `stop(false)` joins the drain threads first, so no shard can increment the counter between the exchange and the end of the run. Had the call been placed before `stop(false)`, a shard's final retirement would have been lost instead. The base-class comment (`profiler_base.h:1362-1365`) was updated to name both consumers.

Re-derived consequence: the unproven path still cannot report `dropped_records_complete: true` — the flag is written `true` only at `args_dump_collector.cpp:809`, inside `reconcile_counters()`, which that path never calls, and is cleared at `begin_run` / `initialize` / both `export_dump_files` exits / `finalize`. So consuming the counter changes what the *next* run reports and nothing about this one, which is the intended effect.

**(b) Per-run, per-lane recovery receipts.** `std::vector<RecoveredCurrentBuffer> recovered_current_buffers_` (`args_dump_collector.h:638`), checked before `resolve_host_ptr` and written only after a successful `on_buffer_collected` (`args_dump_collector.cpp:762`, `:795`). I checked the five things that could make this subtly wrong:

- **Bounds.** The vector is `assign`ed at every site that can change `num_dump_threads_`, and `num_dump_threads_` has exactly two writers: `initialize()` at `:229`, immediately followed by the `assign` with no early return between them and after the `1..PLATFORM_MAX_AICPU_THREADS` validation; and `finalize()` at `:1240`, set to `0` *before* its `assign`. The loop bound and the vector size cannot diverge.
- **Failed reads are not marked.** Every `continue` above the receipt write — null host pointer, refused buffer copy, foreign buffer epoch, `count > PLATFORM_DUMP_RECORDS_PER_BUFFER` — leaves the receipt untouched, so a retry re-attempts exactly the lanes that did not land.
- **Counters stay non-additive and complete.** The skip sits *below* `dropped_total += state->run_dropped_record_count`, so a retried lane still contributes to the snapshot; and the skip does not touch `counts_complete`, which is correct because that lane is fully accounted. The two idempotence mechanisms compose rather than overlap.
- **Identity is sound under the stated precondition.** `current_buf_seq` is a monotonic per-lane counter and `switch_dump_meta_buffer` refuses to report success unless it changed (`args_dump_aicpu.cpp:570`), so within a run a reused pointer carries a new sequence. Across runs the device may reset the sequence and keep the same pointer — which is exactly why the receipts are cleared at `begin_run()`, and exactly what `NewRunRecoversReusedHeldBuffers` pins. The comment at `:762` states the precondition (a stopped producer holds pointer and sequence still) rather than asserting an unconditional invariant.
- **No new ownership.** The receipt is host-only POD; no device pointer is cleared, nothing is freed, and no incremental recovery against a live producer is introduced. The device-release invariant is intact on both branches.

**(c) Tests.** I checked each new assertion against the implementation rather than taking the pass report:

- `UnknownCloseSettlesHostDrainLossBeforeTheNextRun` is a true boundary regression — it drives the real `close_args_dump_run_boundary`, asserts the counter reaches zero, then restarts the collector and runs a clean successor to `complete: true`. On `d980` the counter stays `1` and the successor reports `false`. It also happens to be the first test that exercises restart-after-unproven-close on the real boundary, which previously I could only verify by reading.
- The six two-lane cases are correctly specified. `UnreadHeldBufferKeepsKnownCounterSumIncomplete` expecting `dropped_records: 10` with `complete: false` is the one that proves the field's documented split — both lanes' *readable* counters sum into a known lower bound while the unreadable held buffer leaves its own potential loss unknown. `RetryRecoversEachHeldBufferExactlyOnce` runs three reconciles across both failure orders and asserts exactly one occurrence per `arg_index` plus a payload order that matches recovery order; that is the direct `d980` regression. `UnknownLanePreservesOtherLaneKnownLossCount` correctly expects the foreign-epoch lane's `3` to be excluded, matching the `continue` at `:745`.

**(d) Previously-cleared invariants, re-checked at this head rather than inherited.**

| Invariant | Status at `a547` |
| :--- | :--- |
| `drain_device_queues_on_stop_` release/acquire pairing | `profiler_base.h` delta is three comment lines only; the relaxed-store-then-release-store / acquire-load-then-relaxed-load pairing is unchanged and still correct |
| Retained manifest shape | `dropped_records_complete` is assigned at exactly one site, `export_dump_files():1059`, which early-returns for retained collectors; the retained writer never sets it |
| Stop ≠ device-release authority | the unproven branch adds only a host-atomic exchange and a log line; no device write, no free |
| Proof disjunction, onboard literal `false` | unchanged at `onboard:4838` / `sim:1496`; onboard still has no independent producer-stop proof, now documented in §5.5 and §8 |
| Arena-credit equation per-invocation | `written_payload_counts_` still zeroed in `begin_run`, device baseline still re-snapshotted at `dump_args_init`; recovery's deliberate over-credit stays confined to a window where producers are proven stopped |

**(e) Prior findings, dispositions verified.** Should-fix 1 and 3/5 fixed at the mechanism (above). Should-fix 2 resolved by documenting the convention explicitly at §7.3 (`docs/dfx/args-dump.md:1057-1061`) — the field now describes manifests that were written, and absence is stated not to be zero loss; no empty-artifact API was invented, which is the right call. Should-fix 4: the canonical example gained `"dropped_records_complete": true` (correct value for a successful `full` capture), a Key-fields bullet was added whose `§7.3` cross-reference I verified resolves to the right section (1015–1071), the onboard `teardown_shared_collectors_after_run` comment now describes the proof-aware close instead of `stop() → reconcile_counters() → export`, and both ASCII boxes are column-uniform again (71 and 28 columns, checked programmatically). The two Consider items I raised are also addressed: the reconcile comment now says quiesce drains published records while threads stay resident, and the gated fixture carries a comment that the observation counters are off in ordinary production.

## 3. Implementation–claim consistency

Every stated goal traces to its implementation; I found no ❌ and no ➕ row at this head. The delta is exactly the two fixes, their tests, and the documentation they invalidate — no scope creep, no new public API or ABI, no new production type (the `RecoveredCurrentBuffer` struct is a private member of the collector). The `d980` traceability rows that were ⚠️ are now ✅ or documented limitations with the limitation written down.

One consistency note worth recording rather than filing: the PR body's testing claims for this head are *not* transferred from `d980`, which is the correct handling, and the dispositions say so explicitly.

## 4. Residual issues

### Should fix

**1. §7.4's loss-condition matrix does not carry the new condition or the new flag.** *(doc-consistency §1; verified)*
`docs/dfx/args-dump.md:1074-1080` is the one table that enumerates loss conditions against the flag that signals them:

| Condition | Flag | Metadata | Payload | … |

Its only loss row is *"Record buffer full, no free buffer → `dropped_records` summary → Lost → Lost"*. This PR introduces a second condition with a different signature — an ordinary close without a producer-stop proof, where a lane's held records are not recovered and the manifest says `dropped_records_complete: false` — and the matrix does not list it. A reader who goes to the limitations summary to ask *"what tells me I lost records?"* is told only about the full-buffer case. The existing row's Flag cell is also now narrower than reality.

This is the table I did not name in the prior round, so it is outside the "all identified" disposition rather than contradicting it. Adding one row (and widening the existing Flag cell to mention the completeness field) is the whole fix, and per `doc-consistency.md` §4 it belongs in this commit.

### Consider

- **The two paths consume the same counter with different bookkeeping.** The proven path sets `ordinary_drain_loss_` from `drain_dropped_buffers()` and then calls `report_drain_drops()`; the unproven path calls `report_drain_drops()` alone. That is correct today precisely because the unproven path's completeness flag is already `false` and cannot become `true`. It is a coupling that depends on a fact two functions apart, so a comment at `close_ordinary_run`'s `report_drain_drops()` naming why the unproven branch does not also set the sticky flag would keep a future edit from making the loss invisible.
- **The proven path reads the drain counter in two steps** — a `load()` into `ordinary_drain_loss_`, then an `exchange()` in `report_drain_drops()` — while the drain threads are still resident, since `quiesce()` does not retire them. This is not a defect: under the stop proof the queues are drained and no shard has anything left to retire, which is the precondition the path is gated on. Having `report_drain_drops()` return the value it exchanged would collapse the pair into one atomic read and remove the dependence on that reasoning; a one-line change if the file is open anyway.
- **`report_drain_drops()`'s updated comment lives in the generic base** but now names an ArgsDump-specific caller ("an unproven ordinary close"). Accurate for ArgsDump, silent about the other four collectors for which reconciliation remains the only consumer. Fine as a disjunction; worth a clause if the base class's comments are meant to be subsystem-neutral.

### Explicitly re-checked and clean at this head

Receipt vector bounds and lifecycle; receipt identity under pointer reuse across runs; non-additivity of both the counter snapshot and the record recovery; the `count == 0` lane never being marked (harmless, re-read each pass, consistent); `drain_dropped_buffers()` already public before this PR, so no access widening; retained path untouched; `dropped_records_complete` unreachable as `true` from the unproven branch; all six multi-lane assertions matching the implementation line for line; core changes confined to `src/common/platform`, so a2a3 and a5 receive them identically through the existing `PER_ARCH PER_RUNTIME` / `hbg_case` + `tmr_case` registrations (no CMake delta in this head).

Out of scope by explicit record, re-confirmed present and unchanged: the ScopeStats sibling held-recovery gap (`onboard:4899-4903`, `sim:1517-1521`, `scope_stats_collector.cpp:433`). I take no position on the shape of its follow-up beyond noting the gap still exists.

## 5. Merge recommendation

Problem is real and was three-layered (§1 of the prior review) → the method addresses each layer and the delta fixes the two findings with mechanism rather than with assertions (§2) → implementation matches the claims with no ❌ or ➕ rows (§3) → residual is one documentation row plus three optional clarity items (§4) → **approve**.

The chain does not break. The one Should-fix is a table that should carry the PR's own new condition; it is a five-line edit in a file this PR already rewrites, so my recommendation is to land it in this commit rather than as a follow-up. Nothing here should gate on it if the parent prefers to ship — it misleads no reader into believing a loss did not occur, it only fails to tell them where to look.

## Appendix A — Change breakdown

⚠️ **Oversized PR by total churn: 1049 lines.** The warning is mechanical and I do not think it should bias the verdict: Core is **177 lines across 7 files**, and 871 of the 1049 are tests (750) and documentation (122). The reviewability concern the threshold exists to catch is not present — the core mechanism fits on two screens.

| Bucket | Files | + | − | churn |
| --- | --- | --- | --- | --- |
| Core | 7 | 123 | 54 | 177 |
| Test/Ex | 3 | 749 | 1 | 750 |
| Docs | 1 | 75 | 47 | 122 |
| **Total** | **11** | **947** | **102** | **1049** |

Nothing landed in Uncategorized. `tests/ut/cpp/common/platform/CMakeLists.txt` buckets as Test by path precedence, correctly — it only registers the ordinary-close targets and is unchanged since `d980`.

## Appendix B — pto-isa pin check (advisory)

ℹ️ **pto-isa pin:** `pto_isa.pin` is pinned to `b1d460ee8c001585ede36b68ba3678dc5a226bbf`, untouched by this PR, and no pto-isa header reference was added, removed, or relocated in any head of it. Confirm the pinned commit is still adequate; bump + rebuild onboard a2a3 `host_runtime.so` (`SIMPLER_PTO_ISA_BUILD_COMMIT`) only if needed.

## Appendix C — Independent reviewer notes

Not run — no `codex` / `gemini` / `all` opt-in, and recursive CLI use is forbidden by the invocation. Every finding is my own, verified against the diff and the surrounding call graph at this head.

## Missing evidence (separate)

- **No validation is attached to `a547`.** The `d980` results — local C++ 390/390, Python 3525/192, A3 task-submit, A3/A5 hardware CI, macOS pending — are explicitly not transferred, and the full build/UT/CI and rebuilt A3 checks for this head are still running. I built nothing and ran nothing. In particular the eight integrated targets, including the new cross-run boundary case and the six two-lane cases, are verified by me only by reading their assertions against the implementation.
- **The `report_drain_drops()` placement is unexercised for the concurrent case.** `UnknownCloseSettlesHostDrainLossBeforeTheNextRun` retires the buffer from the test thread before the close, so it pins the consumption but not the ordering property that makes the placement correct (no shard can increment after the join). That property is structural and I verified it by reading; no test distinguishes the two placements under contention, and I do not think one is worth building.
- **Onboard held recovery remains unreachable and therefore untested on silicon.** `device_producers_stopped` is a literal `false` at both onboard sites, so the receipt mechanism, the per-lane completeness arithmetic and the recovery path itself are exercised only through the sim boundary and the CPU producer. This is the accepted, now-documented trade rather than a gap in the PR, but it bounds what any amount of hardware CI on this head can confirm.
- **No reproduction of a hardware hang or a device-side fault is claimed or shown**, and none is implied by my reading. The published-malformed-record injection in the read-failure suite exercises existing post-transport content rejection, not a normal-producer path; I have not treated it as evidence about producer behaviour.
- **The ScopeStats sibling is unmeasured here.** I confirmed the code shape is unchanged; I did not evaluate its recorded follow-up.
