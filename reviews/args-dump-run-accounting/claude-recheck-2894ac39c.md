I've completed the independent re-review of the final head. No edits, commits, pushes, GitHub messages, hardware runs, or other model CLIs were used — all verification was read-only.

# Final Review — PR #2671 @ `2894ac39c`

**Range:** `2522f02bf..2894ac39cab86bdd5f101e9a60defb1d81e62c2e` (single squashed commit, 14 files, +434/−89).
**Coverage:** base→`7448ed4e5` reviewed in round 1; `7448ed4e5`→final (4 files) read in full here. Complete coverage of the merge-base diff.

**Verdict: Approve the code change — merge gated on CI completing.** All three round-1 blockers are resolved or correctly re-classified. I verified each disposition from primary state, not from the author's summaries. CI at the final head is **not** green and I am not treating it as such: 14 IN_PROGRESS + 1 QUEUED, only 4 SUCCESS.

---

## Finding dispositions

| # | Round-1 finding | Disposition | Verified how |
| --- | --- | --- | --- |
| F1 | Guaranteed spurious lane ERROR | **Fixed — closed** | Code read + 4-target red/green + counter-assertion |
| F2 | Comment asserts unenforced precondition | **Fixed — downgraded to tracked follow-up** | Comment/docs read; behavioral gap confirmed pre-existing |
| F3 | Read failure reports `dropped_records: 0` | **Not fixed — documented, acceptable follow-up** | Product docs read at final head |
| F4 | `st-onboard-a2a3` red | **Pre-existing / environment — not introduced** | Independent control verified from git state |
| F5 | Minor style/comment gaps | **All fixed** | Header read at final head |
| **New** | Test global cleanup not RAII-guarded | **New minor nit** | Test read at final head |

---

### F1 — Fixed, and the regression test is genuinely load-bearing

The guard is now exactly the shape I proposed (`args_dump_collector.cpp:726-735`): error only when `current_buf_ptr != 0 || run_dropped_record_count != 0`.

I re-verified the structural premise rather than assuming it. `initialize()` (`:296-320`) pushes buffers into each lane's `free_queue` but **never sets `current_buf_ptr`** — it stays 0 from the memset. So an unused pool lane satisfies both clauses and goes silent. Correct.

The pre-fix evidence log reproduces F1 precisely as I diagnosed it, including the host/device divergence in one place:

```
[INFO] initialize: Args dump initialized: 2 threads ...
[INFO] dump_args_init: Initializing args dump for 1 threads
test_args_dump_run_identity.cpp:286: Failure
  unused.find("did not initialize")  Which is: 76
```

Two things make this test better than a pass/fail box-tick:

- **It failed on all four arch/runtime targets** (`a2a3_tmr`, `a2a3_hbg`, `a5_hbg`, `a5_tmr` — `0% tests passed, 4 tests failed out of 4`), then `100% tests passed, 0 tests failed out of 386` after. Red-then-green on every target, not just one.
- **It pins the counter-assertion**, which is what I'd have insisted on: `EXPECT_NE(stale.find("lane 0 did not initialize for run 71"), npos)`. A fix that merely silenced the message would pass my first assertion and fail this one. The pre-fix log shows the third assertion (`lane 1` silent even when a sibling is stale) also failing at `:293` — so the test discriminates three distinct behaviours.

The `ASSERT_NE`/`ASSERT_EQ` preconditions on `current_buf_ptr` are a nice touch: if someone later makes `initialize()` pre-pop buffers into every lane, this test breaks loudly instead of silently losing the guard.

I also checked the fix does not **over**-suppress. Case analysis for a mismatched lane: (a) never-a-producer pool lane → nothing at stake → silent, correct; (b) real producer lane that failed to init, with a held buffer or prior losses → ERROR fires; (c) real producer lane that failed to init with a clean predecessor → silent. Case (c) is the only blind spot, and it is strictly narrower than pre-PR behaviour, which had no epoch gate at all and would have *mis-attributed* the stale lane's counters to this run. No regression. Since `num_dump_threads_` and `active_sched_threads_` both derive from the same `aicpu_thread_num` and the runner rebuilds the collector when it changes, a lane is either always or never a producer — which makes the predicate exact for the stable-topology case.

Retained path is unaffected (`reconcile_counters` returns early for `retain_across_runs_`).

### F2 — Correctly re-classified; my blocker was the comment, and the comment is now honest

My round-1 objection was not that the gap existed, but that the comment asserted a precondition as fact. The replacement states it accurately:

> *This equation assumes recovery runs after producers stop. The ordinary close boundary does not yet check its producer-stop proof; recovery of unpublished payloads against a live producer can therefore over-credit this count. Epoch/read checks do not establish quiescence.*

I confirmed the behaviour is unchanged and that this is the right call: `close_args_dump_run_boundary` still ignores `device_producers_stopped` on the ordinary branch (verified at final head via `git show`), and `quiesce()` still does not stop `mgmt_replenish_loop`. Round 1 established the same over-credit shape existed pre-PR (`taken = written_payload_counts_[t]`), so this is **not introduced here**. Documenting a pre-existing gap you did not create, instead of silently widening it or claiming to have closed it, is the correct disposition. This satisfies `comments.md`: it states a present-tense constraint, not change narration.

The docs carry the same statement, so the limitation is discoverable without reading the source.

### F3 — Not fixed, explicitly documented, acceptable

`docs/dfx/args-dump.md` now states plainly:

> *Unread producer accounting is not represented as unknown in the current manifest, so `dropped_records: 0` alone does not prove no loss after a read failure.*

The defect stands — a read failure still yields a manifest asserting zero drops. But it is now a documented, bounded limitation rather than a silent lie, and the fix (a manifest `accounting_complete` signal) is a separable change. Reasonable as a tracked follow-up; I would not block on it.

### F4 — Disposed: pre-existing and environment-sensitive, with the strongest available evidence

The failure is `TestEarlyEnqueueDepthTwo::test_sustained_refill_overlaps_successive_run_pairs`:

```
AssertionError: the established pairs are not consecutive — no pair's predecessor
is another's successor, so this is not one chain of refills: [((1, 0), (2, 1)), ((3, 0), (4, 1))]
```

Outputs were correct; only the overlap chain degraded into two disjoint edges. I verified two independent discriminators myself rather than accepting the summary:

1. **The diff cannot reach the failing path.** `grep` for `dump_args|args_dump|enable_dump_args` across `tests/st/a2a3/host_build_graph/early_enqueue/test_early_enqueue.py` returns **nothing**. Every code path this PR touches is gated on `dfx.dump_args_enabled()`, so the PR is inert in that test.
2. **The control is genuinely untouched base.** I checked the control worktree's git state directly, not its logs: `HEAD == 2522f02bfead6071a70603d0ccb35431ca03139b`, `git status --porcelain` **empty**, `git diff 2522f02bf --stat` **empty**. The claim "no source or assertions were changed" holds against primary state.

On that untouched base, 2-CPU affinity reproduces the **byte-identical** assertion string *and* the byte-identical pair list `[((1, 0), (2, 1)), ((3, 0), (4, 1))]`; 4-CPU passes (`results.json`: `{cpus:[0,1,2,3], rc:0}`, `{cpus:[0,1], rc:1, chain_failure:true}`). A pipelining-overlap test starved of host CPUs failing to sustain a refill chain is a coherent mechanism, and the discriminating variable is demonstrably host CPU count, not the diff.

This clears `discipline.md` §5's bar, which asks for exactly this: show the diff doesn't touch the failing path, and show the failure reproduces on main. It was earned, not asserted.

Supporting evidence I also checked and agree with the framing of:
- The 16/16 alternating base/head comparison under normal resources. The author correctly notes it **does not establish a flake rate** — it shows neither tree fails when resources are adequate. Honest.
- The earlier local sweep failure is a *different* failure: driver `errno:16` (EBUSY) / `ret=13` on `Map device reserve address failed (devid=1)` at 03:54:23, i.e. a device-memory map before dump init could run. The occupancy snapshot shows a foreign python process (pid 2711509, 1482 MB) resident on NPU 1 chip 1 while the failing process was pid 2713681. The author's "interference evidence, not proof of root cause or an AIV" is the right level of claim — I'd have objected to anything stronger.
- NPU work went through `task-submit` with a device lock (`task_20261022_041302_35814497607`, device 6 lock acquired/released), per `running-onboard.md`.

### F5 — All addressed

Field comments on all three new members, `pad3` arithmetic comment restored (`// Pad to 256 bytes (216 + 40)`), and the `static_assert` now carries a message. I re-verified at final head that this was comment-only: `sizeof == 256`, offsets 192/208, and every pre-existing offset assertion are intact, and `dump_args_init`'s barrier ordering (baseline → `wmb` → buffer settle → `wmb` → `run_epoch` → `wmb`) is unchanged.

### New minor — test global state is not RAII-guarded

`test_args_dump_run_identity.cpp:267-298`. The new free `TEST()` sets `set_dump_args_enabled(true)` and `set_platform_dump_base(shm)`, then cleans up only on the success path. If one of its `ASSERT_*` preconditions fires, gtest returns early and leaves `dump_args_enabled` true with `platform_dump_base` pointing at memory `finalize()` never freed — which can cascade into later tests in the same binary and obscure the original failure. The fixture-based tests in the same file use `SetUp`/`TearDown` for exactly this. Exposure is narrow (only the two structural preconditions are `ASSERT`; the three behavioural checks are `EXPECT` and do run to cleanup), so this is a nit, not a merge blocker. A small RAII guard or converting it to a fixture would close it.

---

## Remaining issues: tracked follow-ups, not introduced blockers

I assessed each against "does this PR create it, or inherit it?":

- **Ordinary close stop-proof (F2)** — inherited. Same over-credit shape existed pre-PR; `device_producers_stopped` was already on the signature and already unused. Now documented in both code and product docs with no quiescence claim.
- **Unknown-loss manifest (F3)** — inherited. The read checks this PR adds are what *created the ability to detect* the condition; representing it in the manifest is the separable next step.
- **Early-enqueue chain under CPU starvation (F4)** — inherited, reproduced on untouched base.
- **PMU owned-HBG AIV** — pre-existing, explicitly not claimed fixed by this PR's green results.

None of these is a blocker introduced by this change.

## Merge gate

The code review is **approve**. The merge is not mine to sign off, and I am explicitly not doing so: CI at `2894ac39c` is **unfinished** — `ut` (both), `ut-a2a3`, `ut-a5`, `st-sim-a2a3`/`a5` (both), `st-onboard-a2a3`, `st-onboard-a5`, `st-network1-onboard-a2a3`, `st-deepseek-onboard-a2a3`, `profiling-flags-smoke`, and `packaging-matrix` (both) are IN_PROGRESS or QUEUED. Before merge:

1. All required checks complete and green at `2894ac39c`.
2. If `st-onboard-a2a3` reds again on the final head, confirm it is the same `test_sustained_refill_overlaps_successive_run_pairs` chain assertion; a *different* failure would need fresh triage and should not inherit F4's disposition.

## Assessment of the change

The protocol is the right one and the ownership seam it draws — host owns the collection window, device owns the transport — is what makes the ordinary path finally agree with the retained path instead of contradicting it. The round-1 fixes are minimal and targeted: a predicate narrowed to "is anything attributable at stake", and two comments made honest. Nothing was papered over, and the one finding the author declined to fix is the one that genuinely belongs in a separate change.

What raises my confidence most is the shape of the evidence rather than its volume: a regression test that fails on all four targets before the fix and carries an assertion that would catch over-suppression; and a control experiment on a verifiably clean base tree that reproduces a CI failure byte-for-byte under a named, testable variable. Both are falsifiable. That is the standard this area should be held to.

**Verification caveats, stated plainly.** I ran no builds, no tests, and no hardware; every runtime claim comes from reading logs. The git worktree state for the F4 control (`HEAD`, clean `status`, empty `diff`) and the CI rollup are the two things I confirmed from primary sources rather than author-produced artifacts — those I consider established. The `/tmp` log files I take at face value as to provenance: they are internally consistent and consistent with the code, but I cannot prove they came from the runs described. The final-head SDK rebuild, local hardware validation, and CI remain outstanding and unverified by me.

## Author verification note

The process-table interpretation in the local driver-failure paragraph needs correction: NPU1/chip1 maps to physical3, so pid2711509 is not the competing process on physical1. The saved board table maps NPU0/chip1 to physical1, where pid2693254 held about43GiB near the failure. This remains interference evidence, not proof of root cause. See the [disposition](review-disposition.md#device-mapping); the review's code verdict is unchanged.
