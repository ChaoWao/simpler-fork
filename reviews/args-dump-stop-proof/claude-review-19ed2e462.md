# Review — PR #2677 `Fix: require producer stop proof for args dump recovery`

Base `fa6528f4e` → head `19ed2e462`. Read-only review; no files edited, nothing built or run, no GitHub writes.

## 0. Summary

The PR fixes a real ownership defect — the ordinary args-dump close recovered a producer-held buffer and ran its host-side merge/export with no evidence the device producer had stopped — by gating recovery on `device_execution_complete || device_producers_stopped` and, absent that proof, retiring the host transport before exporting only host-owned records. The mechanism is sound and correctly scoped to the non-retained path on both runner bases and both arch/runtime variants. Residual: the unproven path silently drops device-side loss accounting that the old path reported, the same unproven-recovery pattern survives untouched in `ScopeStatsCollector`, and several doc/comment sites still describe the old behaviour. **Verdict: needs discussion** — no Must-fix defect found, but three Should-fix items, one of which is a reporting regression rather than a known-gap deferral.

## 1. Background and problem

`ArgsDumpCollector` (non-retained, "ordinary" mode) runs a split transport: AICPU lanes write records into `DumpMetaBuffer`s, publish full buffers into a device ready ring, and the host's mgmt-drain / replenish / collector threads pull them into per-shard host storage. A lane's *in-progress* buffer is not in that transport at all — it is named only by `DumpBufferState::current_buf_ptr`, and the device's `dump_args_flush()` is what moves it in.

`reconcile_counters()` (`src/common/platform/shared/host/args_dump_collector.cpp:706`) exists to rescue exactly that buffer when the flush never ran — the documented case being an AICPU reaped by the hardware op-execute timeout. It bulk-reads the whole shm region into the host shadow, reads the held buffer, and feeds it through `on_buffer_collected`.

The problem: before this PR, `close_args_dump_run_boundary()` ran `quiesce(); reconcile_counters(); export_dump_files();` **unconditionally**, discarding both proof arguments it was handed. `quiesce()`'s own contract (`profiler_base.h:1240`) states the precondition "the device-side producers have stopped", and the line removed by this PR — *"Callers already satisfy this — the run is drained before teardown"* — asserted a guarantee the failure path does not deliver.

Concrete failure, which is also what the new test pins:

- Run fails without an observed fence (`device_execution_complete = false`). Lane 0 holds an unpublished buffer with one record whose payload bytes are still in the producer's arena.
- Old behaviour: the host copies that buffer back and exports the record; `export_dump_files()` also merges `collected_by_collector_` while the poll threads are still live; and the recovered write participates in the lifetime payload equation that `publish_arena_acks()` reads, so a *later*, genuinely published payload can be acknowledged before any host reader has copied it — the producer may then recycle an arena whose bytes nobody owns.

Stated goal (PR body) and the code agree, and the body is explicit that this is a bounded follow-up inside #2666, not completion of capture DFX. The invoking instruction's framing ("must be a meaningful complete fix, not an isolated scaffolding seam") is the one axis where I have a substantive observation — see §3 and §4.

## 2. Solution and rationale

**Mechanism.** One new method, `ArgsDumpCollector::close_ordinary_run(bool device_producers_stopped)` (`args_dump_collector.cpp:693`), takes the boundary decision the two runner bases used to hold inline:

- **Proof present** → `quiesce(); reconcile_counters();` then export. Byte-identical to the old path. Threads stay resident.
- **Proof absent** → `stop();` then export. `stop()` (`profiler_base.h:1278`) stops mgmt, lands its final drain pass into the host shards, sets `execution_complete_`, and joins the collector shards. Recovery is skipped entirely; the held buffer stays on the device.

Both `DeviceRunnerBase::close_args_dump_run_boundary` (`onboard/host/device_runner_base.cpp:4837`) and `SimDeviceRunnerBase`'s copy (`sim/host/device_runner_base.cpp:1495`) now call it with `device_execution_complete || device_producers_stopped`.

**Why it solves the problem.** Each of the three failure legs is closed by a different property:

1. *Held-buffer recovery* — simply not attempted without proof, so no read of storage a live producer owns.
2. *Export racing host readers* — `export_dump_files()` calls `merge_collector_shards()` over `collected_by_collector_`, which the poll threads append to. Under the old `quiesce()` that merge ran with the shards live; `stop()` joins them first. This is a genuine host-side data-race fix independent of the device question.
3. *Arena over-credit* — `process_dump_buffer()`'s `transport_published` flag already excludes a *forced* recovery from the payload equation (`args_dump_collector.cpp:447`), but the ordinary recovery path went through the normal entry. With recovery skipped, the equation only ever sees publications the device actually enqueued. The test's final assertions verify precisely this: after the unproven close, a new publication does not move `completed_payload_count`.

**Why `||` is the right disjunction.** On onboard, both call sites pass `device_producers_stopped = /*literal*/ false` — nothing a host can do observes an AICPU and its cores stopping — so completion of the fence is the only available proof, matching how `chip_swimlane` and `pmu` already treat it. On sim the runner propagates a real joined-producers signal. Importantly, the proof is used only to authorize a *read*; no free/unregister happens on either branch (device storage is still released in `finalize()`, `args_dump_collector.cpp:1150+`). The new comment says so explicitly, and I verified it.

**Restart semantics.** `ProfilerBase::start()` (`profiler_base.h:1155`) resets `execution_complete_` and both quiesce-epoch handshakes, and the ordinary arming path calls `begin_run()` + `start()` per run (`onboard/host/device_runner_base.cpp:4562`). So a collector stopped by an unproven close restarts cleanly on the next invocation. Verified by reading, not by running.

**Checks I ran that came back clean** (listed because they were the ones asked for):

- *Retained path preserved* — `close_ordinary_run` is reached only under `!retains_runs()`; `reconcile_counters()` still early-returns for retained; `run_close` / `settle_failed_run_terminal_bank` untouched.
- *Successful collectors stay resident* — the proof branch never calls `stop()`.
- *Could an unproven close retire the transport of an in-flight successor?* My initial hypothesis. It does not hold: retention is one switch for all collectors (`set_retain_runs`, `onboard/host/device_runner_base.h:400`, unconditional in `configure_retained_runs`), so `retains_runs()==false` implies the non-pipelined, exclusive-execution-claim path (the `begin_run()` comment at `:4506` states the same serialization). Recorded as cleared, with the caveat in §5.
- *Build wiring* — the new `hbg_case` adds `args_dump_aicpu.cpp` (which lives in `hbg_orch_objs`, not linked here) while `tmr_case` does not (it is already in `{arch}_tmr_objs`). Asymmetric but correct; no duplicate symbol.

## 3. Implementation–claim consistency

| Stated goal | Design choice | Location | Assessment |
| :--- | :--- | :--- | :--- |
| Recover held buffers only on proof | proof-gated branch | `args_dump_collector.cpp:693` | ✅ |
| Without proof, join host readers before export | `stop()` before `export_dump_files()` | same | ✅ |
| Recovered writes must not credit later publications | recovery skipped ⇒ equation untouched | test asserts | ✅ |
| Stop ≠ device-release authority | no free/unregister on either branch | verified vs `finalize()` | ✅ |
| Retained settlement preserved | `!retains_runs()` guard | runner bases | ✅ |
| Both arch/runtime variants | `hbg_case` + `tmr_case` × a2a3/a5 | `tests/.../CMakeLists.txt:656` | ✅ (sim boundary only — [1]) |
| "Explicit manifest representation of incomplete loss accounting is a separate follow-up" | — | — | ⚠️ understates: this PR *removes* accounting that previously landed — [2] |
| — | `close_ordinary_run` parameter named `device_producers_stopped` but fed a disjunction | `args_dump_collector.h:283` | ➕ naming — [3] |

[1] The test subclasses `SimDeviceRunnerBase`. The onboard `close_args_dump_run_boundary` is a textual duplicate, and onboard can never pass `stopped=true`, so the uncovered combination is only the duplicate itself.
[2] See §4 Should-fix #1.
[3] See §4 Should-fix #3.

Stated goal and real goal otherwise match: this is a bugfix, it is implemented as a bugfix, and the diff contains no unrelated functionality. The PR body's validation claims match what I can see in-tree (4 targets, real AICPU producer, real runner boundary); I did not and could not verify the run results.

## 4. Residual issues and risks

### Should fix

**1. Unproven close now exports a manifest that claims zero device-side loss.** *(correctness of reported evidence; medium)*
`reconcile_counters()` does two separable things: recovery (unsafe without proof) and accounting — `report_drain_drops()` plus accumulating each lane's `run_dropped_record_count` into `total_dropped_record_count_` (`args_dump_collector.cpp:712`, `:750`). The new `else` branch skips both. `export_dump_files()` then writes `meta.dropped_records = total_dropped_record_count_` (`:1027`), which is 0.

*Failure example:* a run that drops 400 records on the device, then fails its fence. Before: `args_dump.json` reports `dropped_records: 400` and a `WARN`. After: `dropped_records: 0`, no warning — the artifact for the failed run positively asserts no loss. The PR body files this under "separate follow-up", but the follow-up it names is *representing unknowns*; this is a regression from a correct number to a wrong one.

The accounting half is a counter read with no ownership transfer and no arena credit, and after `stop()` the drain/replenish threads that own the host-shadow free-queue cursors are joined — which is the hazard the retained path cites as its reason to refuse the bulk read (`:708`). So splitting reconcile into "accounting" (always) and "recovery" (proof-gated) looks feasible within this PR's scope. At minimum the manifest should not emit `0`.

**2. The identical defect survives in `ScopeStatsCollector`, unmentioned.** *(scope/completeness; medium)*
`ScopeStatsCollector::reconcile_counters()` (`scope_stats_collector.cpp:433`) performs the same producer-held-buffer recovery — `current_buf_ptr` → `append_buffer_records()` → artifact. Its ordinary close branch still ignores both proofs:

- `src/common/platform/onboard/host/device_runner_base.cpp:4899-4903`
- `src/common/platform/sim/host/device_runner_base.cpp:1517-1521`

Both functions already receive `device_execution_complete` and `device_producers_stopped` and use neither in that branch — the exact pre-PR args-dump shape. `PmuCollector` is *not* affected (it explicitly never recovers: `pmu_collector.cpp:623`). So after this PR one of the two recovering collectors is gated and the other is not, which is the sharpest answer to the "complete fix vs. isolated seam" question: complete for ArgsDump, incomplete for the defect class. Scope-stats has no arena-credit equation, so only the read-a-live-producer's-buffer half applies there — lower severity, but it writes those records into the exported artifact. Either extend the gate or state in the PR body why scope-stats is deliberately out of scope.

**3. Documentation and comments still describe the removed behaviour.** *(doc-consistency §1/§3; low-medium)*
The PR updated three sites but left at least four that now contradict the code:

- `docs/dfx/args-dump.md:551` — *"A run that dies without an observed device fence is not recovered. **The default path reads a hung run's un-flushed buffer back**; the retained path does not, because nothing there proves the device producers stopped."* This sentence is the direct contradiction: after this PR the default path does not either, for the same reason.
- `docs/dfx/args-dump.md:892` — "If a hang/op-timeout reaps AICPU before that flush runs, `reconcile_counters` recovers a non-empty `current_buf_ptr` host-side before export" — unreachable on onboard now.
- `docs/dfx/args-dump.md:748` / `:772` / `:913` — the lifecycle diagram and the a2a3-vs-a5 table still give `stop() → reconcile_counters() → export`, which now matches neither branch (the proof branch uses `quiesce()`, the unproven branch has no reconcile).
- `src/common/platform/shared/host/args_dump_collector.cpp:727-737` — the body comment still opens "After stop()…" and concludes "so a hung run still yields its dumped inputs/outputs", which is the one outcome the gate now prevents on onboard.

Related, and worth deciding rather than just documenting: because onboard hardcodes `device_producers_stopped = false`, **the hang-recovery backstop has no reachable trigger on real silicon** — its only entry is `device_execution_complete`, and a completed run has normally flushed. That may be the intended safety trade, but it should be stated as such (in `docs/dfx/args-dump.md` §"Host-side recovery (backstop)", `:1168`), or an onboard stop proof should be plumbed in a follow-up.

### Consider

- **Parameter name.** `close_ordinary_run(bool device_producers_stopped)` is called with `device_execution_complete || device_producers_stopped`, so a completed run passes `true` for "producers stopped" though no caller observed that. This codebase is otherwise careful to keep the two proofs distinct (the swimlane close uses completion for *reads* and stop for *bank release*). `stop_proved` / `producer_stop_proved` would not mislead.
- **Test does not pin residency on the proof path.** `CompletionRecovers…` and `ProducerStopRecovers…` assert the artifact but never `EXPECT_TRUE(collector_threads_started())`. A regression that stopped the collector on the proof path — the exact property "successful ordinary collectors remain resident" — would pass. One line each.
- **Header comment style.** The new `close_ordinary_run` declaration uses a bare `//` block while every neighbour in `args_dump_collector.h` uses `/** … */`.
- **Return value.** `close_args_dump_run_boundary` now returns `export_dump_files()`'s rc instead of `0`. Inert today — the ordinary path of `export_dump_files()` returns `0` on every exit, including the "No args dump data to export" and manifest-written paths. Harmless and arguably more honest, but it does not currently propagate anything, and the surrounding comment at `:4925` ("Only ArgsDump's close reports anything here") describes the *retained* ownership failure, not this.

### Residual risk (not a defect in this PR)

After an unproven close the host has no readers, so a producer that is in fact still alive can publish into the device ring and have those buffers drained by the *next* invocation's collector. The ordinary path does not epoch-gate in `process_dump_buffer()` (only the retained path does, via `slot_for()`, `:396`), so such records land in the next run's artifact — but each record carries its own `run_epoch` in the manifest (`args_dump_manifest.h:186`), so they are labelled, not silently misattributed. Pre-existing in kind; the explicit `stop()` boundary just makes the window legible.

## 5. Missing evidence

Separately from the findings, these are things I could not establish read-only:

- **No CI verdict.** Per instruction I did not treat any pending check as green; the PR body's 390/390 CPU result, the Python UT counts and the A3 task-submit matrix are author-reported and unverified here. Fixed-HEAD Python/board validation is still running.
- **No hardware-stop guarantee is claimed or demonstrated.** The new test drives a real CPU AICPU producer and the real `SimDeviceRunnerBase` boundary and *emulates* producer state; it does not reproduce an op-timeout reap, and nothing in the diff bounds device-side producer behaviour. The PR body states this correctly.
- **Serialization of the non-retained path** is established from the retention switch being collector-wide plus the `begin_run()` execution-claim comment, not from an explicit guard that refuses a pipelined launch with non-retained DFX. If such a configuration is reachable by some path I did not find, Should-fix #2's sibling hazard and the `stop()`-on-shared-collector question would both need re-examination.
- **Onboard runner boundary is untested** (sim duplicate only); correctness there rests on the two functions being textually identical, which I checked by reading.

## Appendix A — Change breakdown

| Bucket | Files | + | − | churn |
| --- | --- | --- | --- | --- |
| Core | 5 | 31 | 19 | 50 |
| Test/Ex | 2 | 221 | 0 | 221 |
| Docs | 1 | 6 | 2 | 8 |
| **Total** | **8** | **258** | **21** | **279** |

No oversized-PR warning. Core logic is 50 lines; the PR is overwhelmingly its regression test, which is the right ratio for a bugfix.

## Appendix B — pto-isa pin check (advisory)

ℹ️ `pto_isa.pin` is pinned to `b1d460ee8c001585ede36b68ba3678dc5a226bbf`. No pto-isa header references were added, removed or relocated in this PR, and the pin file is untouched — confirm the pinned commit is still adequate; bump + rebuild onboard a2a3 `host_runtime.so` (`SIMPLER_PTO_ISA_BUILD_COMMIT`) only if needed.

## Appendix C — Independent reviewer notes

Not run: the invocation did not include `codex`, `gemini`, or `all`, and the instruction forbids recursive CLI invocation. All findings above are my own, verified against the diff and the surrounding call graph.
