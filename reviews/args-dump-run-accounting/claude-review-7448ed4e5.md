I've completed the independent review from the local checkout. No code or GitHub state was changed, and no hardware tests or other reviewer CLIs were run.

# Review — PR #2671 `Update: preserve args dump transport credits across invocations`

**Range reviewed:** `2522f02bf..7448ed4e5` (1 commit, 14 files, +384/−83). Design context read from `docs-kernel-mode/` (`ROADMAP.md` §C1, `kernel-eager-open-work.md`, `reviews/args-dump-run-accounting/REPORT.md`).

**Verdict: Request changes** — the protocol change itself is sound and well-evidenced, but it introduces a guaranteed false `LOG_ERROR` on the normal path for every `host_build_graph` run (and every TMR run with ≥2 AICPU threads), a required check is currently red, and one load-bearing comment asserts a precondition the code neither takes nor checks.

---

## 1. Is this a meaningful, complete feature?

Yes. It closes a real protocol defect rather than adding surface.

The defect: `ArgsDumpCollector::begin_run()` — host-side admission, run on *every* ordinary invocation — used to write `published_payload_count = completed_payload_count = dropped_record_count = 0` straight into the device's `DumpBufferState`. Those are the counters the AICPU's own arena barrier blocks on (`args_dump_aicpu.cpp:584`, `while (state->completed_payload_count < target_payload_count)`). Zeroing them from the host at the opening of a new window means a successor's admission mutates transport state a predecessor's producer is still reasoning about. For a Worker that submits repeatedly into a resident device side — the eager/kernel-mode direction C1 is heading for — that is not a theoretical hazard; it is the second invocation.

The fix is the right shape and the minimal one:

- **Host admission now clears only host-local state.** The whole device write-back block is gone from `begin_run`; everything it touched is now owned by the device.
- **Device entry establishes the boundary.** `dump_args_init` (`args_dump_aicpu.cpp:721-765`) snapshots `run_published_payload_base = published_payload_count`, zeroes `run_dropped_record_count`, and — after the lane's buffer is settled — publishes `run_epoch`.
- **Ordinary credit becomes `base + this window's disk completions`** (`args_dump_collector.cpp:891`), which is exactly the lifetime cursor the device compares against, with no reset on either side.

The ownership split is the part I'd single out as correct. Previously the host both opened the window and reset the transport; now the host owns `written_payload_counts_` / shards / `output_prefix_` and the device owns everything in `DumpBufferState`. That is a clean seam, and it is why the retained path needed no change at all — it was already lifetime-based, and this makes the ordinary path agree with it instead of contradicting it.

Three fields fit in existing padding; `sizeof(DumpBufferState)` stays 256 and every pre-existing offset is pinned by `static_assert` (`args_dump.h:233-241`). The added `std::is_standard_layout_v && std::is_trivially_copyable_v` assertion is what `codestyle.md` §8 asks for on a device-copied struct. No new env var, macro, or public API — `env-macro-gating.md` §1 is not triggered.

**Ordering is correct.** In `dump_args_init` the baseline is written, then `wmb()`, then the buffer is settled, then `wmb()`, then `run_epoch`, then `wmb()`. The host's mgmt thread is already running by then (`begin_run` → `start` precede kernel launch), so it *will* observe lanes mid-init — and every consumer gates on `run_epoch == run_epoch_` first, so a pre-init read is skipped rather than mis-credited. I also checked the non-atomic 48-byte device→host read in `publish_arena_acks` for field tearing across the init boundary: a read that picks up the new epoch with a stale (smaller) base yields a *smaller* `taken`, so it under-credits. The failure direction is safe.

**Epoch is non-zero where it matters.** `ensure_device_run_result_region` refuses `run_epoch == 0` outright (`device_runner_base.cpp:3372`) and the sim path notes `next_native_run_epoch` never returns 0. Kernel mode's `PersistentKernelArgs` does use epoch 0, but that path does not reach `start_shared_collectors_for_run`, so the gate is never vacuous on a path that actually collects. The new Python assertion (`len(epochs) == 1 and next(iter(epochs)) != 0`) pins this end-to-end.

---

## 2. Findings

### F1 — Guaranteed spurious `LOG_ERROR` on every HBG run; host and device disagree on lane count *(blocking)*

`src/common/platform/shared/host/args_dump_collector.cpp:726-731`

```cpp
if (state->run_epoch != run_epoch_) {
    LOG_ERROR("Args dump: lane %d did not initialize for run %llu", t, ...);
    continue;
}
```

The loop bound is `num_dump_threads_`, which the host sets from `runtime.get_aicpu_thread_num()` (`a2a3/platform/onboard/host/device_runner.cpp:1764`). The device initializes `active_sched_threads_` lanes:

| runtime | `active_sched_threads_` | source |
| --- | --- | --- |
| host_build_graph | `aicpu_thread_num_ - 1` (and `>= 2` is enforced) | `a2a3/.../host_build_graph/runtime/scheduler/scheduler_cold_path.cpp:849-856`, a5 `:784` |
| tensormap_and_ringbuffer | `sched_thread_num_ ?: aicpu_thread_num_`, with `sched_thread_num_ = nthreads - 1` | `.../tmr/aicpu/aicpu_executor.cpp:321`, `scheduler_cold_path.cpp:1133` |

So the host always walks one lane further than the device ever initializes — **unconditionally for HBG** (which rejects `aicpu_thread_num < 2`), and for TMR whenever `aicpu_thread_num >= 2`. That trailing lane has no producer, so nothing is lost; but `reconcile_counters` now emits an ERROR for it on every single successful run.

**Failure scenario:** run any HBG scene test with `enable_dump_args=3`. The dump completes correctly, the manifest is right, and the log carries `Args dump: lane N did not initialize for run <epoch>` at ERROR level. An operator triaging a 507018 reads that as evidence the device-side dump init failed, which is precisely the diagnosis this subsystem exists to support. A guaranteed false alarm in a hang-triage log is worse than no log line.

This is not caught by CI: `test_worker_diagnostic_selection.py` leaves `aicpu_thread_num` unset, and an ERROR line fails no assertion.

**Suggested fix** — the honest predicate is "this lane had something to account for and no identity to account it under", not "epoch mismatch":

```cpp
if (state->run_epoch != run_epoch_) {
    if (state->current_buf_ptr != 0 || state->run_dropped_record_count != 0) {
        LOG_ERROR("Args dump: lane %d did not initialize for run %llu", t, ...);
    }
    continue;
}
```

A lane that genuinely failed to initialize still carries its predecessor's leftover `current_buf_ptr`, so the real case is still reported. Alternatively, have the device publish the lane count it initialized into `DumpDataHeader` and bound the host loop by it — more invasive, but it removes the host/device disagreement rather than silencing its symptom.

### F2 — `publish_arena_acks` asserts a precondition the code neither takes nor checks *(blocking, comment/contract)*

`src/common/platform/shared/host/args_dump_collector.cpp:876-878`

```
// Ordinary recovery requires stopped producers. Its unpublished
// payloads must not authorize arena reuse by an active execution.
```

Nothing enforces this. The ordinary close path is:

```cpp
// device_runner_base.cpp:4833-4841
int DeviceRunnerBase::close_args_dump_run_boundary(
    const DfxRunConfig &dfx, uint64_t run_epoch,
    bool device_execution_complete, bool device_producers_stopped) {
    if (!dump_collector_.retains_runs()) {
        dump_collector_.quiesce();
        dump_collector_.reconcile_counters();
        ...
```

`device_producers_stopped` is available on the signature and ignored on this branch. And `quiesce()` (`profiler_base.h:1251`) drains the shard pipeline — it does **not** stop `mgmt_replenish_loop`, which calls `publish_arena_acks()` on every tick (`profiler_base.h:2540`). So during `reconcile_counters`, recovery feeds unpublished records to the writer, which bumps `written_payload_counts_[t]`, which inflates `taken` past what the device ever published; the ack then advances `completed_payload_count` to `published` and releases arena bytes for payloads the host has *not* written. The retained branch's own comment names exactly this hazard and excludes `written_payload_counts_` for it.

To be clear about blame: **this over-credit is pre-existing**, not introduced here — the old `taken = written_payload_counts_[t]` had the same shape. What this PR adds is a comment stating the precondition as though it holds. The author's `REPORT.md` §"相关但独立的问题" item 2 records the gap candidly; the shipped comment does not. Per `comments.md` (a comment must state a fact that survives on its own) this one currently reads as a lie to the next person.

Minimum fix: reword to state what is true and what is owed, e.g. *"This equation assumes producers have stopped. `close_args_dump_run_boundary` does not yet consume `device_producers_stopped` on this path, so a recovery that runs against a live producer can over-credit — see <issue>."* Better: consume `device_producers_stopped` and skip recovery when it is false.

### F3 — A failed state read makes the manifest claim zero drops *(medium)*

`src/common/platform/shared/host/args_dump_collector.cpp:707-710`

On `profiling_copy_from_device` failure the function logs and returns before touching `total_dropped_record_count_`. `export_dump_files` then writes `meta.dropped_records = total_dropped_record_count_` = **0** (`:1009`), and the new Python assertion `data["dropped_records"] == 0` treats that as the healthy value.

**Failure scenario:** on a5, the post-hang `rtMemcpy` of the shm region fails. The manifest is published stating `"dropped_records": 0` — a positive claim of no loss — when the accounting was never read. Returning early is the right call; reporting `0` for it is not. Suggest a distinct manifest signal (`"dropped_records": null`, or an `accounting_complete: false` field) so "unknown" is distinguishable from "none". The same applies per-lane to the `continue` paths at `:731` and `:744`.

### F4 — Required check `st-onboard-a2a3` is red *(blocking until triaged)*

`st-onboard-a2a3 / st-onboard-a2a3` → **FAILURE**, step 9 `Run pytest scene tests (a2a3)` (run `39765241516`, job `119780564148`). I could not pull the log — `gh run view --log-failed` reports the run is still in progress (`ut (macos)`, `st-sim-a5 (macos)`, `ut-a5` were still running at the time of this review), so **the cause is unknown to me and I am not calling it related or unrelated.** `discipline.md` §5 makes this the author's to read and name before merge; "unrelated to my change" has to be earned, and the PR body already says hardware/final-head CI is pending.

### F5 — Minor

- `args_dump.h:217-224`: the three new fields carry no trailing comments while every other field in the struct does, and `pad1/pad2/pad3` lost the arithmetic comment (`// Pad to 256 bytes (188 + 68 = 256)`) the old `pad1` had. The header block above documents the fields, so this is cosmetic, but it breaks the file's own convention.
- `args_dump.h:233`: the new `static_assert(std::is_standard_layout_v<...> && ...)` is the only message-less `static_assert` in the file.
- `docs/dfx/args-dump.md:998-1000`: "Device initialization snapshots the lifetime published cursor into `run_published_payload_base`" — `dump_args_init` does this unconditionally, including on the retained path (where the field is unused). Harmless, but the doc reads as if it were ordinary-only.
- `docs/dfx/args-dump.md:566-572`: now that ordinary counters are also lifetime-monotonic, the 2⁶⁴ headroom check remains retained-only (`args_dump_retained_runs.cpp:390`). The doc correctly says "retained admission checks", so this is consistent — flagging only so the asymmetry is a known choice rather than an oversight.

---

## 3. Dump collection across repeated invocations

I traced both paths against the code rather than the description.

**Ordinary.** Per invocation: host `begin_run` resets shards / `written_payload_counts_` / `total_dropped_record_count_` and latches `run_epoch_`; device `dump_args_init` snapshots `base`, clears `run_dropped_record_count`, publishes `run_epoch`. Credit is `base + written`, drops are `run_dropped_record_count`, recovery requires both the lane's and the buffer's `run_epoch` to match. `published`/`completed`/`arena_write_offset`/lifetime `dropped_record_count` are never reset by anyone after `initialize()`.

I checked the boundary case where a window closes with `published > written` (a drain drop, or a ready-queue entry never consumed). The next invocation's `base` absorbs the gap, so the new window's equation stays internally consistent and the stale bytes — already lost — are not re-credited. Correct.

**Retained.** Untouched. It still differences lifetime `published`/`dropped` against the per-run `admit_published`/`admit_dropped` snapshots (`args_dump_retained_runs.cpp:409-410`, `:724`) and still credits from `received + discarded`. The 20-byte admission read at `:370-382` remains valid under the new layout — `published`/`completed`/`dropped` are still contiguous at 168..188. `publish_arena_acks` keeps its 8-byte read and skips the epoch gate via `!retain_across_runs_`, so the two protocols stay genuinely separate rather than one being bolted onto the other.

**Failure handling.** Both new read checks fail *closed* in the right direction: an unreadable state or buffer skips recovery rather than falling back to the stale host shadow — which is a real bug class, since on a5 that shadow holds the *previous* run's records for the same reused buffer. The new UT demonstrates exactly that. Device-side, `dump_args_init`'s early returns (null base, bad thread count) leave `run_epoch` unpublished, so the host declines the lane rather than mis-attributing it. The gaps are F2 (no quiescence proof) and F3 (unknown reported as zero).

---

## 4. Test coverage

Strong, and notably more adversarial than typical.

- `test_args_dump_run_identity.cpp` adds five cases: admission leaves the whole `DumpBufferState` byte-identical (`memcmp` over the struct — a good choice, it catches any field anyone adds later); three successive invocations with real payload bytes read back from `args.bin`, checking the ack does *not* advance before the write and does after; device init preserving a saturated lifetime drop counter and non-zero credits while clearing only per-run accounting; and two negative cases where an uninitialized invocation can neither recover its predecessor's held buffer nor acknowledge its baseline.
- `test_args_dump_read_failure.cpp` is the best piece here. It forces a detached host shadow on both arches (`ASSERT_NE(device, host)`, with its own `profiling_copy_*` rather than the arch stubs), poisons the shadow's `run_epoch` to *match* the current run so the copy-result check cannot be satisfied by the epoch check, parameterises over both refusal sites (shared state / held buffer), and verifies the retry path afterwards exports only `42`. Without the fix this test fails by *producing* a manifest containing the previous run's record — a genuine regression barrier, not a tautology.
- The Python UT now parameterises `borrowed ∈ {False, True}` and asserts per-record `run_epoch` is non-zero and unique across invocations — the one check that actually exercises the host↔device epoch agreement end-to-end on silicon.
- The `REPORT.md` describes ten mutant variants (dropped read checks, removed epoch gate, missing baseline, missing per-run drop reset) all caught. I did not re-run these.

Gaps:

- **F1 is uncovered.** No test runs with `aicpu_thread_num >= 2`, which is where host and device lane counts diverge. A UT that initializes the collector with N lanes and calls `dump_args_init(N-1)` would pin it in a few lines.
- **F3 is uncovered.** `test_args_dump_read_failure` asserts no manifest is written on failure, but not what a manifest written *after* a partial failure claims about `dropped_records`.
- `test_args_dump_read_failure` calls `export_dump_files()` three times on one collector. It passes, but it leans on undocumented re-entrancy (`total_dropped_record_count_` is reset inside export at `:1044`). Worth a comment saying that is deliberate.

## 5. CI

Gating is correct and needs no change: `src/` and `tests/` both fall outside `NON_CODE`, so every axis flips true and the full matrix runs — consistent with `ci-change-detection.md` §4. The new `platform_case(test_args_dump_read_failure.cpp PER_ARCH PER_RUNTIME ...)` is explicitly registered and correctly omits `sim/host/profiling_copy.cpp` (the test supplies its own), matching the convention of the neighbouring `test_args_dump_run_identity` entry.

Status at review time: `detect-changes`, `pre-commit`, `ut (ubuntu)`, `ut-a2a3`, `st-sim-a2a3` (both), `st-sim-a5 (ubuntu)`, `st-onboard-a5`, `st-network1-onboard-a2a3`, `st-deepseek-onboard-a2a3`, `packaging-matrix` (both), `profiling-flags-smoke` all green. `st-onboard-a2a3` **failed** (F4). `ut (macos)`, `st-sim-a5 (macos)`, `ut-a5` still in progress.

---

## What I'd need to flip to approve

1. **F1** — stop the guaranteed false ERROR on HBG / multi-thread TMR, and add a test at `N` host lanes vs `N-1` device lanes.
2. **F2** — reword the `publish_arena_acks` comment to match what the code enforces, or consume `device_producers_stopped`.
3. **F4** — read the `st-onboard-a2a3` log and name the cause.
4. **F3** — nice to have in this PR, acceptable as a tracked follow-up if F1/F2/F4 land.

The underlying protocol change I'd take as-is. The ownership boundary it draws — host owns the window, device owns the transport — is the right one, the padding reuse is disciplined, the retained contract is genuinely untouched, and the negative testing is better than most changes in this area get. The blockers are about a newly-loud log on the happy path and a comment that over-claims, not about the accounting itself.

**Uncertainties, stated plainly:** I could not read the `st-onboard-a2a3` failure log (run still in progress), so F4 is "untriaged", not "caused by this PR". I ran no builds, no unit tests, and no hardware — all pass/fail claims about the suites come from the GitHub check rollup and the author's `REPORT.md`, not from my own execution. F1 and F2 are derived by reading the call chains cited above, not by observing a run.
