# Review — PR #2677 (final head `d9808101e`)

Base `fa6528f4e` → head `d9808101e`, single squashed commit *"Fix: close ordinary args dumps with proven producer ownership"*. Read-only: no edits, no builds, no hardware, no GitHub writes, no CLI delegation.

## 0. Summary

The expanded head now does three things that hang together: it gates ordinary args-dump held-buffer recovery on a producer-stop proof, it stops `stop()` from chasing a live publisher by adding an entry-boundary check plus a conditional final drain, and it makes the loss count honest with a new `dropped_records_complete` manifest field. The mechanisms are correct as far as I can verify by reading, including the memory ordering of the new flag and the arena-credit equation. Four Should-fix items remain — one verified accounting leak across the unproven close, one contract hole when nothing was collected, one latent retry-duplication asymmetry, and doc/comment drift the author believed closed. **Verdict: needs discussion** (no Must-fix; none of the four blocks the mechanism, but the first is a real introduced regression against an invariant stated in the same file).

## 1. Background and problem

Three facts about the ordinary (non-retained) args-dump path make the whole PR legible:

1. **A lane's in-progress buffer is outside the transport.** AICPU lanes fill a `DumpMetaBuffer` and publish it into a device ready ring; the host's drain / replenish / collector threads pull from there. The *current* buffer is named only by `DumpBufferState::current_buf_ptr`, and only the device's `dump_args_flush()` moves it in. `reconcile_counters()` exists to rescue it when that flush never ran.
2. **`quiesce()` ≠ `stop()`.** `quiesce()` drains to a quiescent point and leaves the threads resident; its documented precondition is *producers have stopped*. The base head asserted callers already satisfied that — the line this PR deletes (`profiler_base.h:1245`).
3. **`stop()`'s final drain pass is unconditional.** After the sweep loop exits it re-walks every device queue to empty (`profiler_base.h:2325`).

Three defects followed, and the final head addresses all three:

- **Unproven recovery.** `close_args_dump_run_boundary` ran `quiesce(); reconcile_counters(); export_dump_files();` while discarding both proof arguments. With `device_execution_complete=false` and a lane holding one unflushed record, the host copied that buffer back, exported it, merged `collected_by_collector_` while the poll threads were live, and — because `written_payload_counts_[t]` feeds the ordinary arena equation at `args_dump_collector.cpp:928` — let a *later*, genuinely published payload be acknowledged before any host reader copied it, so the producer could recycle an arena nobody owned.
- **A stop that chases live publishers** (found in GitHub review). `stop()` lowers `mgmt_running_`, but the drain thread then ran its *unconditional* final pass over the device ready queue. On an unproven close that pass consumes entries a still-live producer is publishing — the same ownership violation one layer down, now in the transport rather than in the held buffer.
- **Silently-zero loss accounting.** With recovery skipped, nothing accumulated `run_dropped_record_count`, so a failed run's manifest positively asserted `dropped_records: 0`.

Goal sources agree. The PR body scopes to the ordinary args-dump close inside #2666 and explicitly excludes the ScopeStats sibling; the invoking instruction confirms that disposition. No goal downgrade.

## 2. Solution and rationale

### Mechanism

**(a) Proof-gated close.** `ArgsDumpCollector::close_ordinary_run(bool producer_stop_proved)` (`args_dump_collector.cpp:698`) takes the decision both runner bases used to hold inline:

- proof → `quiesce(); reconcile_counters();` then export; threads stay resident.
- no proof → `stop(false)` then export; no recovery, no final device drain.

Both bases call it with `device_execution_complete || device_producers_stopped` (`onboard/host/device_runner_base.cpp:4838`, `sim/host/device_runner_base.cpp:1496`). Onboard passes `false` for the second at every call site — a host cannot observe an AICPU and its cores stopping — so completion of the fence is the only proof there, which the docs now say out loud.

**(b) Conditional final drain.** `ProfilerBase::stop(bool drain_device_queues = true)` (`profiler_base.h:1282`) stores the request into a new `drain_device_queues_on_stop_`, the sweep loop's outer *and inner* `mgmt_running_` loads become `acquire` (`:2207`, `:2229`), and the final pass is guarded by `if (!drain_device_queues_on_stop_.load(relaxed)) return;` (`:2327`).

The ordering is sound, and it is the one thing in this PR that would be silently wrong if it weren't: the writer does a relaxed store of the flag *then* a release store of `mgmt_running_=false`; the reader's acquire load that observes `false` establishes the happens-before, so the subsequent relaxed load cannot see a stale `true`. `start()` re-arms the flag to `true` (`:1196`) before spawning, so restart is clean. Every other subsystem (`chip_swimlane`, `pmu`, `scope_stats`, `dep_gen`) calls the default-argument `stop()` and is unaffected; the inner-loop check only shifts which loop consumes an entry, since with `drain=true` the final pass still runs.

**(c) `dropped_records_complete`.** A new `std::optional<bool>` on `ManifestMeta` (`args_dump_manifest.h:138`), emitted only when engaged, so the retained manifest shape is unchanged — verified: `export_dump_files()` early-returns for retained collectors and the retained writer builds its own `ManifestMeta` that never sets the field. The flag is `true` only out of a completed reconcile, and `reconcile_counters()` now clears it on entry and sets it from `counts_complete && dropped_total <= UINT32_MAX`, with `counts_complete` falsified by: a prior host-side transport retirement (`ordinary_drain_loss_`), an uninitialized lane, a saturated per-lane counter (`== UINT32_MAX`, matching `diagnostic_terminal_add_u32`'s clamp), an unresolvable or unreadable held buffer, a foreign buffer epoch, and a malformed `count`. The counter write became a **store** of the per-run sum rather than a `fetch_add`, which is what makes a retried reconcile a snapshot instead of a double-count.

### Why it solves §1

Threading the §1 example through: lane 0 holds one unflushed record, the fence failed.

- Before: that record is exported, the shard merge races the poll threads, and `written_payload_counts_[0]` is advanced for a payload `published_payload_count` never counted — so `taken = run_published_payload_base + written` can reach `published` for the *next* publication and `publish_arena_acks()` releases arena bytes nobody copied.
- After: `stop(false)` joins the readers before `merge_collector_shards()`, leaves the held buffer and any later publication on the device, and the equation only ever sees what the device enqueued. The manifest records `dropped_records_complete: false`.

I checked the inflation does **not** survive the run: `begin_run()` zeroes `written_payload_counts_` (`:98`) and the device re-snapshots `run_published_payload_base` at `dump_args_init` (`args_dump_aicpu.cpp:726`), with `publish_arena_acks()` suppressed until `host_state->run_epoch == run_epoch_` (`:888`). So the proven path's deliberate over-credit is confined to a window in which producers are proven stopped. That was my largest open hypothesis from the previous head and it does not hold.

Residual by design, correctly: `stop(false)` leaves entries in the device ready queue. The next run's drain consumes them, and each record carries its own `run_epoch` into the manifest (`args_dump_manifest.h:186`), so they are labelled rather than misattributed.

### Tests

The two gated-drain tests are the strongest part of the diff. A thread factory gates the *first* spawned thread — the drain shard, since `start()` creates drains, then replenish, then collectors — so the test can observe `mgmt_running_` actually drop, publish a second finite buffer afterwards, then release the gate. `stop(false)` leaves `queue_heads[0]` where it was; the default-`stop()` positive control advances it and exports both records. That is a real determinism gate, not a sleep.

Cleanup on assertion failure is handled deliberately and correctly: `GatedClose` is declared *after* the `rc` it writes through, so destruction order joins the thread before `rc` dies; its destructor releases the gate so a failed `ASSERT_TRUE(wait_until(...))` cannot deadlock; the fixture's `TearDown` releases again (idempotent) for the path where `GatedClose` was never constructed; and `ASSERT_FALSE(HasFatalFailure())` catches fatals raised inside the void helper.

## 3. Implementation–claim consistency

| Stated goal | Design choice | Location | Assessment |
| :--- | :--- | :--- | :--- |
| Recover held buffers only on proof | proof-gated branch | `args_dump_collector.cpp:698` | ✅ |
| Join host readers before export | `stop(false)` precedes export | same | ✅ |
| Recovered writes must not credit later publications | recovery skipped; equation per-run | test + `:928` | ✅ |
| Stop ≠ device-release authority | no free/unregister on either branch | vs `finalize()` | ✅ |
| Final drain must not chase live publishers | acquire entry check + conditional pass | `profiler_base.h:2229`, `:2327` | ✅ |
| Loss count completeness, observed lower bound | `dropped_records_complete` | `:797-801`, manifest `:168` | ⚠️ absent when nothing collected — [1] |
| Non-additive retry snapshot | `store()` not `fetch_add` | `:797` | ⚠️ counter only; recovery still additive — [2] |
| Resident collectors on the proof path | `EXPECT_TRUE(collector_threads_started())` | tests `:277`, `:289` | ✅ |
| Retained manifest shape unchanged | `optional` engaged only on ordinary export | `:1049` | ✅ |
| Stale diagrams / A5 text / comments corrected | docs §5.4/§5.5/§5.6/§8 rewritten | docs diff | ⚠️ three sites missed — [3] |
| Both arch/runtime variants | 4 close + 4 read-failure targets | `CMakeLists.txt:656` | ⚠️ all single-lane — [4] |

[1] §4 Should-fix 2. [2] §4 Should-fix 3. [3] §4 Should-fix 4. [4] §4 Should-fix 5.

Stated and real goals match; this is a bugfix implemented as a bugfix, with regression tests that fail on the old head by construction (the unknown-close head assertion and the held-payload exclusion). No scope creep found — the `dropped_records_complete` addition was requested in review and is declared in the body.

## 4. Residual issues and risks

### Should fix

**1. The host drain-drop count leaks past an unproven close into the next run.** *(verified; introduced; safe-direction)*
`report_drain_drops()` documents its own invariant at `profiler_base.h:1360-1365`: *"Derived::reconcile_counters() is the only caller: the read consumes the count, which is what makes each run report its own rather than the collector's cumulative total across a resident lifetime."* `drain_dropped_buffers_` is reset nowhere else — not in `start()`, not in `begin_run()`, not in `export_dump_files()`. The unproven branch never calls `reconcile_counters()`, so the count is never consumed.

*Failure scenario:* run N retires one undeliverable ready buffer (`profiler_base.h:1486`) and then closes unproven. Run N+1 is clean and closes with proof. Its `reconcile_counters()` reads `drain_dropped_buffers() != 0`, sets `ordinary_drain_loss_`, and therefore emits `dropped_records_complete: false` for a run that lost nothing — and `report_drain_drops()` logs the `LOG_ERROR` naming run N's buffer against run N+1's manifest.

The direction is conservative (never a false `true`), which is why this is Should-fix and not Must-fix. The remedy is cheap and needs no proof: consume the count on the unproven path too — it is a pure host-side counter — or clear it in `begin_run()`. The new test `NewRunDoesNotInheritCompleteLossAccounting` covers the opposite direction only.

**2. An unproven close that collected nothing writes no manifest, so the completeness signal is missing exactly where it matters most.** *(verified; pre-existing shape, newly contract-relevant)*
`export_dump_files()` returns early at `args_dump_collector.cpp:991-1000` when `collected_` is empty — `LOG_WARN("No args dump data to export")`, no `args_dump.json`. The docs added by this PR (`docs/dfx/args-dump.md:1040-1050`) state that *"Without completed reconciliation, including when producer stop is unknown, it also remains `false`"*, which presumes a manifest exists.

*Failure scenario:* a run hangs before any lane publishes; the fence fails; close takes `stop(false)`; nothing was collected. The reader gets no artifact and no `dropped_records_complete: false` — the same "absence of evidence reads as absence of loss" the field was introduced to remove, one level up. Either emit a records-free manifest carrying the flag on the ordinary path, or state the exception in the doc paragraph.

**3. Record recovery is still additive on retry while the counter snapshot is now idempotent.** *(verified mechanism; not reachable from production callers today)*
`reconcile_counters()` (`args_dump_collector.cpp:736-792`) has no "already recovered this buffer" guard: it re-reads `state->current_buf_ptr`, which nothing clears, and calls `on_buffer_collected` again. Contrast `ScopeStatsCollector::reconcile_counters` (`scope_stats_collector.cpp:455-460`), which keys recovery on `recovered_current_buf_` / `recovered_current_total_` precisely to make a second call a no-op.

The PR now *establishes retry as a supported shape* for the counter (`RetryReplacesCounterSnapshotAndCanCompleteLossCount`). With more than one lane, a partial failure plus retry duplicates: lane 0's read succeeds and lane 1's fails on attempt 1; the retry recovers lane 0 a second time, appending its records twice and advancing `written_payload_counts_[0]` twice — which also inflates `taken` in the arena equation. The single-lane tests cannot see it. No production caller retries today (`close_ordinary_run` is the only path and calls it once), so this is latent, not live — but the counter and the records should agree on whether reconcile is idempotent.

**4. Doc and comment drift the disposition reports as closed.** *(verified)*

- `docs/dfx/args-dump.md:325-345` — the canonical example manifest for a `full` ordinary capture still lists `"dropped_records": 0,` with no `"dropped_records_complete"`, which the code now always emits for ordinary runs immediately after it. The "Key fields" list at `:364` does not document the new field either (only the §8 "Effect" paragraph does).
- `src/common/platform/onboard/host/device_runner_base.h:2605-2612` — the contract comment for `teardown_shared_collectors_after_run` still says each block *"does: stop() → reconcile_counters() → export step … `dump` writes dump files"*. The dump block now calls `close_ordinary_run`, which may take `stop(false)` and skip reconcile entirely. (The sim header has no equivalent paragraph, so this is one site.)
- The §5.4 and §5.5 ASCII diagrams (`docs/dfx/args-dump.md:747`, `:857`) are now one column wider on the `│ close_ordinary_run(proof) │` line than on every other line of the same box, so the right border and the box's own top/bottom rules no longer line up.

**5. Every new and amended test runs a single dump lane.** *(coverage gap)*
Both fixtures call `initialize(1, …)` (`test_args_dump_ordinary_close.cpp:155`, `test_args_dump_read_failure.cpp:74`). The entire per-lane structure introduced by this PR — the cross-lane `dropped_total` summation, one lane saturating among several, one lane's held-buffer read failing while another's succeeds, and the duplication in Should-fix 3 — is unexercised. A two-lane case in the read-failure fixture would cover the arithmetic and the partial-failure shape at near-zero cost.

### Consider

- **The new reconcile comment overstates its precondition.** `args_dump_collector.cpp:722-723` now reads *"Producer stop and host transport quiescence precede this shared-state snapshot"*, but on the proof path `quiesce()` leaves the drain and replenish threads **running**; the bulk `profiling_copy_from_device(shm_host_, shared_mem_dev(), shm_size_)` then overwrites the whole host shadow, including free-queue cursors those threads own — the exact hazard the retained early-return two lines above cites as its reason to refuse the read. This is pre-existing (the base head did the same after `quiesce()`), only reachable on the a5 host-shadow transport where the two regions are distinct, and I have **not** proven a concrete corruption — the clobbered cursors are re-read from the device. But given that this PR's entire subject is the distinction between quiesce and stop, the comment should not claim a quiescence the code does not have.
- **The gated fixture runs the drain with `set_run_counters(true)`** (`test_args_dump_ordinary_close.cpp:46`), needed because `ring_processed_` is gated on it (`profiler_base.h:2684`). Ordinary production collectors never enable it — only retained admission does (`args_dump_retained_runs.cpp:201`) — so the positive control exercises a drain configuration production's ordinary path does not have. Observability-only, but worth a comment in the fixture.
- **Two distinct definitions of `profiling_common::ProfilerBaseTestPeer`** now exist (`test_profiler_base.cpp:45` and `test_args_dump_ordinary_close.cpp:38`) against one `friend` declaration at `profiler_base.h:1037`. Safe only because the two files are never linked into the same executable; a shared test header would remove the ODR footgun.
- **`close_ordinary_run` returns `export_dump_files()`**, which returns `0` on every ordinary exit, so nothing propagates today. The neighbouring comments at `onboard/host/device_runner_base.cpp:4925` and `sim/host/device_runner_base.h:818` still say only the *retained* close reports anything — true in effect, now imprecise in shape.
- **Comment wrap.** Several new lines in `profiler_base.h:1267-1273` run noticeably past the paragraph's own wrap (still under the 120-col cpplint limit and clang-format-stable, so no lint failure).

### Explicitly checked and clean

Worth recording because they were the listed concerns: memory ordering of `drain_device_queues_on_stop_`; no post-stop device write from the replenish loop (`profiler_base.h:2533` has no final pass); `report_drain_drops()` does not write `total_dropped_record_count_`, so the new `store()` cannot clobber a host-side contribution; `ordinary_drain_loss_` is read before `report_drain_drops()` clears the source; saturation detection matches the device's clamp; all three new flags reset in `initialize` / `begin_run` / both `export_dump_files` exits / `finalize`; the retained path, PMU, chip-swimlane and scope-stats closes are untouched and keep the default-argument `stop()`; the new manifest key is additive and no scene test asserts a manifest key set; core changes live entirely under `src/common/platform`, so a2a3 and a5 get them identically.

## 5. Merge recommendation

Problem is real and now three-layered (§1) → the method addresses all three and the one piece that could have been silently wrong, the flag's memory ordering, is correct (§2) → implementation matches the claims, with four ⚠️ rows and no ❌ or ➕ (§3) → residual contains one verified accounting leak and one contract hole, both conservative in direction and both cheap to close (§4) → **needs discussion**.

The chain does not break on correctness; it breaks on the completeness contract the PR itself introduces. Should-fix 1 and 2 are the two places where `dropped_records_complete` does not yet mean what the docs say it means — one attributes a previous run's loss to a clean one, the other omits the signal entirely in the worst case. Both are small and in-scope for this PR. Should-fix 3 and 5 can land as a follow-up if the author prefers, but a two-lane test would also be the cheapest way to make 3 moot. Should-fix 4 is doc-only and should ride along per `doc-consistency.md` §4.

## Appendix A — Change breakdown

| Bucket | Files | + | − | churn |
| --- | --- | --- | --- | --- |
| Core | 6 | 97 | 43 | 140 |
| Test/Ex | 3 | 564 | 0 | 564 |
| Docs | 1 | 66 | 47 | 113 |
| **Total** | **10** | **727** | **90** | **817** |

No oversized-PR warning (total 817 < 1000; Core 140). The ratio is right for a bugfix: core logic is 140 lines, the rest is regression coverage and the documentation the behaviour change invalidated. `tests/ut/cpp/common/platform/CMakeLists.txt` buckets as Test by path precedence, which is correct here — it only registers the new targets.

## Appendix B — pto-isa pin check (advisory)

ℹ️ **pto-isa pin:** `pto_isa.pin` is pinned to `b1d460ee8c001585ede36b68ba3678dc5a226bbf`, unchanged by this PR, and no pto-isa header reference was added, removed, or relocated. Confirm the pinned commit is still adequate; bump + rebuild onboard a2a3 `host_runtime.so` (`SIMPLER_PTO_ISA_BUILD_COMMIT`) only if needed.

## Appendix C — Independent reviewer notes

Not run. The invocation contains no `codex` / `gemini` / `all` opt-in and explicitly forbids recursive CLI use. Every finding above is my own, verified against the diff and the surrounding call graph; nothing was carried over unverified from the earlier head's review.

## Missing validation (separate, as requested)

- **No CI verdict for this head.** The 390/390 CPU C++, Python 3525/192, and A3 task-submit results belong to `19ed2e462` and, per the instruction, are not transferred. I did not build, install, or run anything. The `hbg_case`/`tmr_case` × a2a3/a5 build of the new gated test is therefore unverified here; the build *wiring* is correct by inspection (`args_dump_aicpu.cpp` and `device_run_result_base_aicpu.cpp` come from `{arch}_tmr_objs` for the tmr target and the hbg target adds the former explicitly because hbg keeps it in `hbg_orch_objs`, which this target does not link — so no duplicate symbol).
- **No multi-lane coverage anywhere**, as in Should-fix 5. The per-lane logic this PR adds is validated at N=1 only.
- **No hardware, and none claimed.** The gated tests emulate producer state on a real CPU AICPU producer and the real `SimDeviceRunnerBase` boundary; nothing reproduces an op-timeout reap, and nothing in the diff bounds device-side producer behaviour. The onboard `close_args_dump_run_boundary` remains a textual duplicate of the sim one and is exercised only through that duplication.
- **The onboard stop proof is still absent by construction**, now documented rather than fixed: `device_producers_stopped` is a literal `false` at both onboard call sites, so held-buffer recovery has no reachable trigger on real silicon. That is the accepted trade, not a gap in this PR, but it means the recovery path's correctness on hardware is untested and untestable until such a proof exists.
- **ScopeStats** retains the identical unproven-recovery pattern (`onboard/host/device_runner_base.cpp:4899-4903`, `sim/host/device_runner_base.cpp:1517-1521`, `scope_stats_collector.cpp:433`). Confirmed scoped out by the PR body and tracker; recorded here only so the reviewer can see it was re-checked at this head rather than assumed.
