# Actual Claude re-review

Session `5a72d87d-1265-4f3d-b815-476b402df029`; read-only review at `77aba9a8a`.

## Re-review — PR #2684 @ `77aba9a8a` vs base `71077b04a`

Verified independently against the code, not against the summary. Read-only: no edits, builds, hardware, GitHub writes, or sub-agents.

---

### 0. Summary

All three blocking findings from my `25d31c653` review are closed by mechanisms I verified in the diff, each with a test that genuinely fails against the old head's logic. One new, small doc-consistency defect appeared *as a side effect of fixing my Consider #6*. **Verdict: approve**, with one Should-fix one-liner that does not block the chain.

Diff base confirmed: `git merge-base 71077b04a 77aba9a8a` → `71077b04a`, single commit ahead, and the PR's 14 files contain no comm-domain / `CommBufferSpec` / `window_size` paths — disjoint from #2672 as stated.

---

### 1. Prior Must-fix #1 — successor-run contamination: **CLOSED**

**Mechanism** (`src/common/platform/shared/host/scope_stats_collector.cpp:227-240`):

```cpp
if (!terminal_banks_.opened() && ordinary_run_epoch_ && run_epoch != *ordinary_run_epoch_) {
    foreign_epoch_records_ += n;
    …  // LOG_ERROR once, set foreign_epoch_pending_report_
    return;
}
```

Four things I checked rather than assumed:

- **`std::optional<uint64_t> ordinary_run_epoch_` is the right carrier, and `expected_producer_epoch_` would have been wrong.** `begin_run` binds it unconditionally (`:186`), while `expected_producer_epoch_` is deliberately `0` for `required=false` (`:183`). Reusing the latter would have left producerless bindings unguarded — which is precisely why `ProducerlessCapacityDoesNotAdmitForeignEpochRecordsOnRestart` parametrizes the new test over both settings. The claim is load-bearing and the test proves it.
- **Legacy no-`begin_run` behavior is preserved and is actually exercised.** `ordinary_run_epoch_` stays `nullopt` → guard off. `test_scope_stats_collector.cpp:215-227` (`collected_for_run(33)`/`(44)` after re-stamping a buffer) and `:232-256` (`TwoPartialRunsAccountWithoutACapacityTerm`) both drive the collector with no admission; neither appears in the `begin_run` call-site list for that file. Real constraint, not a hypothetical.
- **`total_collected_` is *not* bumped** for a rejected batch — unlike the retained branch at `:258`. I verified this is the correct asymmetry: `total_collected_` feeds both `host_received_records` and the `total_collected_ + dropped == total_device` identity in `reconcile_counters`, and the device counters are reset by the AICPU leader at the successor's entry. Counting a predecessor's records would have manufactured the very mismatch the guard exists to prevent.
- **No lock inversion.** `foreign_epoch_pending_report_` is set under `records_mutex_` (same as the retained branch); `report_foreign_epoch_records()` (`scope_stats_retained_runs.cpp:70-84`) takes `records_mutex_`, releases it, *then* calls `note_host_failure` → `retained_mu_`. It is invoked from `close_ordinary_run:468`, before `write_jsonl` takes `records_mutex_`. No nesting on any path.

**The test is the scenario, not a proxy** (`test_scope_stats_ordinary_close.cpp:268-325`). It uses **the same site string across both epochs** — the exact input that produced the fabricated pair — asserts the entry is still queued at close (`queue_heads[0] == head`), then after restart asserts the queue is fully drained (`heads == tails`, so the transport entry is retired and the buffer recycled — no leak) while `collected_for_run(kRunEpoch) == 0`, the artifact contains only `run_epoch: 8201` with `phase: "end"` and **no** `"phase": "begin"`, and the sticky host error is raised (`status.has_error`, `host_failures > 0`). Against the old head, `collected_for_run(8200)` would have been 1 and both phases would be in the file — it was red on `25d31`, as stated.

**One regression risk I chased and cleared.** `append_buffer_records` is also the held-buffer recovery path in `reconcile_counters`, so I asked whether a proved close could now reject a legitimately-held buffer. It cannot: `read_terminal_checked` validates the device's `run_epoch` against `expected_producer_epoch_` and fails the whole reconcile first — `test_scope_stats_collector.cpp:336-362` (producer at 11, `begin_run(22)`) still ends at `total_collected() == 0` and a refused export, unchanged.

I accept the scoping note: these are collector-restart contracts, an onboard failed fence poisons the runner, and no silicon race is claimed.

### 2. Prior Should-fix #2 — stale artifact at a reused prefix: **CLOSED**

`write_jsonl:717-727` now computes `path` before the empty-unknown branch and calls `std::filesystem::remove(path, ec)`, logging and returning `PTO_RUNTIME_ERR_INTERNAL` on failure. Checked: `remove` reports no error for a non-existent path, so `UnknownCloseWithoutHostRecordsEmitsNoArtifact` (fresh dir) still returns 0 — and that test is unchanged and still present. `create_directories` correctly moved below the branch so the removal path creates nothing. The proved-close metadata-only artifact is untouched (gate is `ordinary_stop_unproved_ && records_.size() == 0`), confirmed by `ProducerlessNormalRunPreservesMetadataOnlyArtifact` passing unmodified.

`EmptyUnknownCloseRemovesOnlyThePreviousArtifactAtTheSamePrefix` (`:459-481`) seeds a *real* proved artifact containing `previous.cpp`, writes a sibling `unrelated.txt`, then runs an empty unproven close and asserts the JSONL is gone and the sibling is byte-identical. Narrow deletion of one known path is the right call — deleting unrecognised files in a user-supplied output dir would be worse — and the doc now says so explicitly.

### 3/4. Prior Should-fix #3 and #4 — doc drift: **CLOSED**

- `docs/dfx/scope-stats.md:257` — "in this mode only" replaced by "used by background exports and by unproven ordinary closes. Proven ordinary exports keep their original seven-key metadata line." Accurate against `render_jsonl_to`'s `extra` argument.
- `:262` — `partial_cut_unknown` now lists "unproved producer stop" alongside the saturation/mismatch causes.
- `:264` — `host_received_records` qualified with "held-buffer recovery is included only when producer stop is proved".
- `:265` — `host_retained_records` now says the 256 MiB budget "applies only to background collection". I verified this against `charge_record_block` (`scope_stats_retained_runs.cpp:91-95`), which returns `true` unconditionally unless `retain_across_runs_ && retained_ready_`. This corrects a pre-existing inaccuracy that was not mine to raise — good.
- `chip_swimlane_runs.h:82-92` — `CutUnknown`'s comment is now `counts unknown; resource safety comes from the owner's protocol`, with the block comment scoping the handshake requirement to retained collection and adding "A verdict describes the artifact, not device termination: resource release still requires its owner's own proof." This resolves the contradiction without renaming a wire token or touching retained semantics, which is the better of the two options I offered.
- Capacity all-or-none is now stated positively (`:225`), and the foreign-epoch rejection is documented at `:211-214`.

The decision to leave the discarded-export-return item unexpanded is reasonable — it preserves the existing best-effort precedence and was a Consider, not a request.

### 5. Self-reported additional regressions — both verified real

- **Positive fatal evidence under unknown counts** (`scope_stats_plot.py:379-390`): `status_label` becomes `"Fatal; device counts unknown"` with class `danger`, `Device fatal: true`, and the note drops the false "fatal status is unknown" clause. Correct polarity — a `null` fatal still reads "unknown", only a confirmed `true` is retained. Covered by `test_unknown_counts_preserve_a_confirmed_fatal_latch` (×2).
- **Epoch-aware pairing** (`:120-131`): `scope_key = (rec.get("run_epoch"), site)`. Legacy records without the field all key on `None`, reproducing the old behavior exactly. I worked the `140 → 40` claim by hand: with LIFO stack keyed on site alone, the first `end` (epoch 8200, `task_window_end=25`) pops epoch 8201's begin (`start=100`) and the second pops epoch 8200's (`start=0`), yielding a peak of `140 - 0 = 140`; with epoch keying the pairs are `25-0` and `140-100`, peak `40`, capacity use `40.00%`. `test_interleaved_epochs_keep_their_own_scope_boundaries` asserts exactly `<td>40 slots</td>` present and `<td>140 slots</td>` absent. The fabricated-peak class is genuinely eliminated, not merely labelled.

Counts check out independently: 15 `TEST_F` × 4 targets = **60** boundary cases; **12** Python tests after expansion. `tests/ut/cpp/common/platform/CMakeLists.txt` is unchanged since the prior head — still the same two `hbg_case`/`tmr_case` entries, no new targets. No new `getenv`/`#ifdef`/`extern "C"`/public Python names anywhere in the diff; `collected_records`, `collected_for_run` and `retained_run_stats_for_test` all pre-date this PR.

---

### Residual issues

#### Should fix

**The a5 scene-test docstring was left behind by this PR's own a2a3 edit.**
`tests/st/a5/tensormap_and_ringbuffer/dfx/scope_stats/test_scope_stats.py:20`.

The two files' header blocks were byte-identical before this PR; `diff` of lines 10-30 now reports exactly one difference:

```
< Output (``scope_stats.jsonl``): line 1 is run metadata        # a5
> Successful output (``scope_stats.jsonl``): line 1 is run metadata   # a2a3
```

`codestyle.md` §10 ("finish each identifier across architecture siblings in one change") and `doc-consistency.md` §1 both apply. The PR *created* this divergence — previously both were uniformly unqualified. One word, same commit.

#### Consider (stated for completeness; not a request)

`ordinary_run_epoch_` is written in `begin_run` outside `records_mutex_` (`:186`) and read inside it on the collector thread (`:229`) — the first of the ordinary-close fields to be read cross-thread. I traced whether it is reachable: on the proved path `quiesce()` guarantees both queues drained with the producer proved stopped, so no append can occur before the next `begin_run`; on the unproven path `stop(false)` has already joined every reader. **No interleaving exists**, so this is not a Must-fix under the review's own standard — it is a one-line tightening (move the assignment inside the existing scoped lock) that the author may take or leave.

---

### Merge recommendation

Problem is real → the method solves it, and the two gaps I found in the method are now closed by mechanisms I re-derived from the code rather than from the summary → the implementation matches every claim, including the three I spot-checked hardest (`ordinary_run_epoch_` vs `expected_producer_epoch_`, `total_collected_` exclusion, `140→40`) → residual is one word in one docstring → **approve**.

The chain holds at every link. Land the a5 docstring with it.

### Evidence still outstanding (not findings)

Stated and accepted as such — I am recording it so the approval is not read as more than it is:

- Fresh full qualification and CI at `77aba9a8a` are **running**. No pending check is treated as green by this review, and the `25d31c653` results (CPP 394 / Python 3533+192 / A3sim 65 / A5sim 61 / A3 task-submit sessions / 18 unchanged hashes / CI 19✓+1 skip) are old-head evidence that does not transfer.
- I built nothing and ran nothing. Every "passes / fails" statement above is a reading of test logic against implementation logic, not an execution result.
- No a2a3/a5 silicon run of the new boundary; the four UT targets inject the real ScopeStats producer even into HBG builds, which exercises production code but is neither an HBG device producer nor A5 hardware. No silicon race is claimed by the PR and none is asserted here.

### Appendix A — change breakdown

| Bucket | Files | + | − | churn |
| :-- | --: | --: | --: | --: |
| Core (`src/`, `simpler_setup/`) | 8 | 206 | 76 | 282 |
| Test/Examples | 4 | 817 | 1 | 818 |
| Build (`tests/.../CMakeLists.txt`) | 1 | 20 | 0 | 20 |
| Docs | 1 | 62 | 20 | 82 |
| **TOTAL** | **14** | **1105** | **97** | **1202** |

⚠️ Total churn 1202 > 1000. This triggers the advisory threshold, but **Core is 282 lines** — well under the 1000-line core limit that would bias the verdict. 68% of the churn is the test file, and splitting a boundary-semantics change from the tests that prove it would be strictly worse. No split recommended.

### Appendix B — pto-isa pin check (advisory)

ℹ️ `pto_isa.pin` is pinned to `b1d460ee8c001585ede36b68ba3678dc5a226bbf`, untouched by this PR, and no pto-isa `#include` was added, removed or relocated. Confirm the pinned commit is still adequate; a bump plus an onboard `a2a3` `host_runtime.so` rebuild (`SIMPLER_PTO_ISA_BUILD_COMMIT`) is only needed if it is not.

### Appendix C — independent reviewer notes

Not run — no `codex` / `gemini` opt-in token in the invocation, and the review was constrained to no recursive agent use. Nothing surfaced, nothing dropped.
