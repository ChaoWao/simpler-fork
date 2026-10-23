## Review — PR #2684 `Fix: close ordinary scope stats with proven producer ownership`

Range reviewed: `07eb531d5` → `25d31c653` (read-only worktree; no builds, no hardware, nothing posted to GitHub).

---

### 0. Summary

The PR makes the ordinary (non-retained) ScopeStats close respect the runner's completion / producer-stop facts instead of unconditionally quiescing and reading the live shared header, and it teaches the artifact and the HTML report to say "unknown" instead of "healthy zeroes". The mechanism is right, the shape matches the merged #2677 precedent exactly, and the tests are real. **Verdict: request changes** — the unproven branch stops draining the device ready queue but nothing was added to stop the *next* ordinary run from ingesting what it left there, and three doc statements the PR's own change falsified were not updated.

---

### 1. Background and problem

ScopeStats' device producer (the AICPU orchestrator, TMR only) fills `ScopeStatsBuffer`s and publishes full ones onto a shared ready queue; host mgmt/collector threads drain that queue and append each buffer's records into one in-memory accumulator, `records_`. At the run boundary the collector reads the shared header for the run's accounting (`fatal`, `total`, `dropped`) and per-ring capacities, and writes `scope_stats/scope_stats.jsonl` — a metadata line plus one line per record.

Before this PR that boundary was unconditional:

```cpp
scope_stats_collector_.quiesce();
scope_stats_collector_.reconcile_counters();
scope_stats_collector_.write_jsonl(dfx.output_prefix);
```

The runner already knew, and passed in, `device_execution_complete` and `device_producers_stopped` — and threw both away. Concretely: a run whose execution fence fails (device error, timeout, recovery path) still had `reconcile_counters()` copy the producer-owned `current_buf_ptr` buffer back to host while an AICPU may still be writing it, and still had `write_jsonl()` read the live shared header. The artifact that came out said `{"fatal": false, "dropped": 0, "total": 0}` — a statement about device state that nobody observed, rendered by the plot as **"Healthy: no fatal, no dropped"**.

This is the ScopeStats instance of the boundary fix #2677 landed for ArgsDump, under tracker #2666.

### 2. Solution

`close_ordinary_run(output_dir, producer_stop_proved)` replaces the three-call sequence on both runner backends, with `producer_stop_proved = device_execution_complete || device_producers_stopped`:

- **Proved** → unchanged behavior: `quiesce()` (threads stay resident), `reconcile_counters()` (held-buffer recovery, count cross-check), seven-key artifact.
- **Unproved** → `stop(false)` (#2677's finite join: mgmt exits without a final device-queue drain, collectors finish only what was already handed to them), `report_drain_drops()`, `device_snapshot_ = {}`. No held-buffer read, no live-header read, no device free.

Supporting changes, all coherent:

- `device_snapshot_valid_` (bool) → `device_snapshot_` (full `DeviceSnapshot`). `read_terminal_checked()` is now the only writer, so `write_jsonl` renders a **frozen** checked snapshot instead of re-reading the mirror via `snapshot_unchecked()`. `test_scope_stats_read_failure.cpp:ExportUsesTheCheckedSnapshotInsteadOfTheLiveHeader` mutates the header after reconcile and proves the export ignores it.
- `ordinary_capacities_` is copied **by value** from the admission-time `CapacityMetadata` in `begin_run()`, so the unknown artifact's capacities come from this run's host binding, never from a successor's header.
- `render_jsonl_to` gains a `host_only_capacities` parameter: non-null forces `fatal/dropped/total` to JSON `null`, and emits either the owned capacities or four `null`s.
- `write_jsonl` takes `records_mutex_` earlier and returns `0` without opening the file when an unproven close has zero host records.
- The plot overrides the status label on `counts_unknown` / `partial_cut_unknown`, adds host received/retained, and adds an "Unpaired evidence" panel. `_resource_size` already returned `None` for a null capacity and `ratio = peak / cap if cap else None` already guarded division, so **no capacity ratios are invented** — verified, and `test_ordinary_unknown_metadata_preserves_samples_and_only_known_capacity[None]` asserts `n/a` rather than `0.00%`.

**On the vocabulary-reuse question you raised: no unauthorized release.** The `Collection{CutUnknown, …}` is constructed as a local in `write_jsonl` and flows only into `render_jsonl_to`. `close_ordinary_run` returns before the runner's `settle_failed_run_terminal_bank` / credit path, which is guarded by `retains_runs()`. No device buffer is freed, no bank is settled, no credit is released on the unproven path. `finalize()` is only reached at runner destruction. The invariant "host-thread join never grants device release" holds. (But see Should-fix #4 — the enum's *documentation* now lies about it.)

**Test quality is high and worth saying so:** the four C++ targets drive the real `close_scope_stats_run_boundary` on a real `SimDeviceRunnerBase` subclass with the production AICPU producer API (`scope_stats_aicpu_begin_run` / `scope_stats_begin` / `scope_stats_aicpu_flush_buffers`), not a mock. The forbidden-read assertions use a `refused_source` probe on the real `profiling_copy_from_device` seam, so "did not read the held buffer" is observed, not assumed. The late-publication race uses a `condition_variable` gate plus bounded `wait_until` predicates — deterministic, no sleeps, no spin-forever producer. Failure cleanup is in `TearDown` and the gated fixture releases its gate first.

---

### 3. Implementation–claim consistency

Stated goal (PR body, commit message, and your invocation) and real goal **match**: this is a complete behavior change across the full call graph — both runner backends, the collector, the schema, the renderer, and the only consumer — not scaffolding or a one-line gate.

| Stated goal | Design choice | Location | Assessment |
| :--- | :--- | :--- | :--- |
| Completion / producers_stopped disjunction | `close_ordinary_run(dir, complete \|\| stopped)` | `onboard/host/device_runner_base.cpp:4899`, `sim/…:1518` | ⚠️ [1] |
| `stop(false)` entry-boundary join inherited from #2677 | identical branch shape to `ArgsDumpCollector::close_ordinary_run` | `shared/host/scope_stats_collector.cpp:438` | ✅ |
| No unproven shared/held reads, no late-publication chase | `device_snapshot_ = {}`; probe tests | `…collector.cpp:445`; tests 325, 365, 425 | ✅ |
| Host-owned samples with immutable admission capacities | `ordinary_capacities_` copied in `begin_run` | `…collector.cpp:184`, `…collector.h:452` | ✅ |
| Explicit null device total/drop/fatal | `host_only_capacities != nullptr` branch | `…collector.cpp:617` | ✅ |
| Empty-unknown ⇒ no artifact | `records_.size() == 0 → return 0` | `…collector.cpp:701` | ⚠️ [2] |
| No stale prior-run export | `records_.release()` in `begin_run` | `…collector.cpp:187` | ⚠️ [2] |
| Known snapshots frozen | `write_jsonl` renders `device_snapshot_` | `…collector.cpp:721` | ✅ |
| Restart state | `begin_run` resets all four new fields | `…collector.cpp:181-195` | ❌ [3] |
| Seven-key schema on success, resident threads | `extra = nullptr` when proved; `quiesce()` not `stop()` | `…collector.cpp:439`, `:720` | ✅ |
| Producerless HBG / OFF / unstarted controls | `required=false` path | test `ProducerlessNormalRunPreservesMetadataOnlyArtifact` | ✅ |
| Retained quarantine/settlement untouched | early return on `retains_runs()` | `device_runner_base.cpp:4904` | ✅ |
| Plot identifies unknown/partial and unmatched | status override + unpaired panel | `scope_stats_plot.py:374`, `:924` | ✅ |
| Collection uses existing wire names | `Verdict::CutUnknown` → `partial_cut_unknown` | `chip_swimlane_runs.h:103` | ⚠️ [4] |
| Both architectures / runtimes | `hbg_case` + `tmr_case` × a2a3/a5 = 4 targets | `tests/ut/cpp/common/platform/CMakeLists.txt:674-692` | ✅ |

[1] On the **onboard** runner, `device_producers_stopped` is hardcoded `false` at every call site, so in production-on-silicon the disjunction reduces to `device_execution_complete`. That is correct and consistent with the sibling collectors (a host cannot observe an AICPU stopping), and the sim runner does supply a real value — but the second term is exercised only by sim and UT. Worth one sentence in the PR body so nobody reads the onboard path as having a stop proof.

[2] See Should-fix #2.

[3] See Must-fix #1 — the collector's *own* state resets correctly; what is not reset is the shared ready queue the unproven close deliberately left loaded.

[4] See Should-fix #4.

---

### 4. Residual issues

#### Must fix

**1. An unproven ordinary close leaves device publications in the ready queue, and the next ordinary run ingests them into its artifact.**
`src/common/platform/shared/host/scope_stats_collector.cpp:438-451` (mechanism), `:235-270` (missing guard).

Pre-PR, the unconditional `quiesce()` guaranteed the shared ready queue was empty at every ordinary close — which is exactly the premise `append_buffer_records` is written against:

> `src/common/platform/shared/host/scope_stats_collector.cpp:252` — *"Without one there is only ever one open run, and the accumulator is left exactly as it was"*

The unproven branch breaks that premise and nothing replaces it. Failure scenario:

- Ordinary mode (`collect_across_runs=False`), TMR, two `run()` calls in one process sharing an `output_prefix`.
- Run N's fence fails. A full buffer is already published on the ready queue but not yet drained. `close_ordinary_run` takes the unproven branch; `stop(false)` retires the host readers without draining it. The entry stays at `queue_heads[0]` — this is precisely the state your own test asserts: `test_scope_stats_ordinary_close.cpp:446`, `EXPECT_EQ(header->queue_heads[0], head)`.
- Run N+1 calls `begin_run()` (`records_.release()`, `total_collected_ = 0`) and `start()`. The restarted drain pops run N's entry; `append_buffer_records` stores it into run N+1's `records_` with **no epoch check** — the foreign-epoch rejection at `:243-266` is gated on `terminal_banks_.opened()`, i.e. retained mode only.
- Result: run N+1's `scope_stats.jsonl` carries run N's records. `reconcile_counters` logs a count mismatch (the AICPU leader resets the device counters at the next run's entry, so `total_collected_ > total_records`), but the **artifact is a normal seven-key line with no verdict field** — the file looks clean.
- The plot makes it worse: `_pair_by_ring` keys on `(ring, site)` only (`scope_stats_plot.py:120-133`), so run N's orphaned `begin` at a site pairs with run N+1's `end` at the same site, fabricating a scope pair and a bogus `end.head - begin.tail` high-water peak. This is the one thing the PR's own §"Plot must identify unmatched samples" goal was meant to prevent.

Suggested shape: the ordinary path already holds the answer — `expected_producer_epoch_` is set at `begin_run` and `buf->run_epoch` is read at `:250`. Reject-and-report a mismatch in ordinary mode the way retained mode already does, rather than silently appending. (Care needed for the producerless `expected_producer_epoch_ == 0` case.)

No test covers this. `NextCompletedRunClearsUnknownCollectionState` is the closest, but its `collect_published()` quiesces first, so the queue is empty and the contaminating path is never entered. The gated test stops one assertion short: it proves the entry is left behind, then tears down.

#### Should fix

**2. A zero-record unproven close leaves the predecessor's artifact on disk at the same path.**
`src/common/platform/shared/host/scope_stats_collector.cpp:698-701`.

The early `return 0` happens before `fopen(path, "w")`, so nothing truncates an existing file. Run N (proved, healthy) writes `scope_stats/scope_stats.jsonl`; run N+1 fails unproven with no host samples and writes nothing; a reader inspecting the output dir after run N+1 finds run N's healthy seven-key artifact and attributes it to run N+1. The doc text added at `docs/dfx/scope-stats.md:224` — *"artifact absence proves neither zero samples nor zero loss"* — describes a condition the code does not actually produce here. Either unlink the stale path or write a record-free unknown line. `UnknownCloseWithoutHostRecordsEmitsNoArtifact` starts from a freshly-created empty dir, so it cannot catch this.

**3. `docs/dfx/scope-stats.md:250-258` is falsified by this PR and was not updated.**
Three concrete statements:
- `:250` — *"Four keys are added to the metadata line **in this mode only**"* (background mode). The PR adds all four to the ordinary unproven close.
- `:255` — `partial_cut_unknown` is defined as *"a saturated counter or an accounting mismatch"*. The new producer is neither; it is "no producer-stop proof".
- `:257` — `host_received_records` is defined as *"including one recovered unpublished buffer"*. On the unproven close the held buffer is explicitly **not** recovered.

Per `.claude/rules/doc-consistency.md` §1/§4 these belong in this commit, and the PR already edits this file.

**4. `Verdict::CutUnknown`'s own documentation now contradicts one of its producers.**
`src/common/platform/include/host/chip_swimlane_runs.h:82-91` says `CutUnknown` means *"count unreachable, **references proved released**"* and *"the reference handshake proved the records were safe to move"*. On the unproven ordinary close, references are precisely what is **not** proved — the retained-mode analogue of that state is `Quarantined` ("references not proved; nothing may be moved or freed"). I confirmed no release actually happens (§2), and the inline comment at `scope_stats_collector.cpp:719` says so. But the enum comment is the one a future reader greps, and a future consumer that tests `verdict == CutUnknown` to conclude "references released" would be wrong. Either scope the enum comment to retained collection, or give the ordinary unproven close its own verdict token.

#### Consider

5. `docs/dfx/scope-stats.md:221` — *"Each absent capacity is `null`"* reads as per-field, but `Capacities` is a complete struct inside one `std::optional`, so emission is all-four-or-none. The following sentence ("An admitted binding normally supplies all four together") half-corrects it; merging them would be clearer.
6. `tests/st/a2a3/tensormap_and_ringbuffer/dfx/scope_stats/test_scope_stats.py:21` still documents the metadata line as `{"fatal":bool,"dropped":uint,"total":uint}`. The assertions at `:136-137` remain correct for a successful run; only the docstring is now narrow.
7. `close_ordinary_run`'s return value is discarded by both runners (the boundary is `void`). Consistent with the prior code, but it means a `write_jsonl` failure on the proved path is invisible except in the log. Non-blocking.

---

### 5. Merge recommendation

Problem is real and well-identified (§1) → the method is the right one and matches the #2677 precedent line for line (§2) → the implementation delivers every claim in the PR body (§3) → but the residual contains one correctness regression the PR introduces and does not close (§4.1), plus three stale documentation statements the PR's own change falsified (§4.2–4.4).

**Request changes.** The chain breaks at §4: the unproven branch is correct about what it refuses to read, and incomplete about what it leaves behind for the next run to read. Fix #1 with an ordinary-mode foreign-epoch guard plus a test that actually runs a successor after a loaded-queue unproven close, land #2–#4, and this is a clean approve — the design and the test engineering are both well above the bar.

### Missing evidence (separate from the findings)

These are not defects; they are things no artifact in front of me establishes:

- Full C++/Python UT, runtime rebuild, sim and onboard qualification, and CI — stated in progress, not passed. None of the 48 new cases have been observed green by me (read-only worktree, no build).
- No a2a3/a5 **silicon** execution of the new boundary. The four UT targets inject the real ScopeStats producer even into HBG build targets, which exercises the production code path but is not an HBG device producer and is not A5 hardware.
- No reproduction of a device-side hang or a real failed fence driving the unproven branch end-to-end; the unproven path is reached in tests by passing `complete=false, stopped=false` directly to the boundary.
- The §4.1 contamination scenario is derived from reading the call graph (queue state asserted by the PR's own test at `:446` + absence of an ordinary-mode epoch guard at `:235-270`). I have not executed it.
- `reviews/scope-stats-proof-audit/REPORT.md` was treated as prior static evidence, not as validation, per your instruction.

### Appendix A — change breakdown

| Bucket | Files | + | − | churn |
| :-- | --: | --: | --: | --: |
| Core (`src/`, `simpler_setup/`) | 7 | 160 | 65 | 225 |
| Test / build (`tests/`) | 4 | 694 | 0 | 694 |
| Docs | 1 | 49 | 15 | 64 |
| **TOTAL** | **12** | **903** | **80** | **983** |

No oversized-PR warning (Core 225 lines, total 983). The 3:1 test-to-core ratio is appropriate for a boundary-semantics change.

### Appendix B — pto-isa pin check (advisory)

ℹ️ `pto_isa.pin` is pinned to `b1d460ee8c001585ede36b68ba3678dc5a226bbf`. No pto-isa header references were added, removed or relocated in this PR, and the pin file is untouched — confirm the pinned commit is still adequate; a bump plus an onboard `a2a3` `host_runtime.so` rebuild (`SIMPLER_PTO_ISA_BUILD_COMMIT`) is only needed if it is not.

### Appendix C — independent reviewer notes

Not run. `codex` / `gemini` cross-check is opt-in and the invocation did not request it (and would conflict with the no-recursive-invocation constraint).
