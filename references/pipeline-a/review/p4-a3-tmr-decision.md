> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# A3 TMR early enqueue — decision (rev 2)

Self-contained: reading this needs no other document. Design/evidence only — no product edit, test,
build, install, device call, behavioural helper, CI POST, commit, push, PR or external message. **No
external PR is edited, cherry-picked, taken over or contacted.** No new `PTO_`/`PTO2` identifier.
**This is not implementation authorization.**

**Baseline:** exact main `600540064cc99f99006c11524561d2568ea2bb06`, read with `git show`; 39 open
PRs at manager refresh (author draft counted 37); #2557 **not assumed merged**. Worktree preserved.
Labels: **[S]** source at this main · **[I]** inference · **[P]** previous execution · **[U]**
untested.

**Rev 2** addresses the review: R1 the submission order was wrong and is corrected (§4); R2 the
scope is rewritten as one complete capability with exact test evidence (§5); R3 "zero new device
allocation" is withdrawn (§6); R4 the external-author-reply prerequisite is withdrawn (§7).

> Manager review qualification: R1–R4 accepted for a concrete implementation decision. The run-result region and its first-use initialization already exist; count them as baseline activation, not a new per-run upload. Logical retirement makes retained storage reusable; it does not necessarily free its allocation. TMR DEVICE_SCRATCH is shared and ordered by the whole-operator boundary, not two independent arena banks. Borrowed caller HOST output/in-out storage remains needed through D2H; caller DEVICE allocations stay user-owned. Releasing a predecessor need not wait for the successor's entire execution (the consumed-wait proof exists for this). Deterministic Partial/unknown-stop validation may use existing native fixtures; public normal/refill/one- and two-endpoint validation is mandatory. SDK/runtime uncertainty or failed quiescence can stop the runner and retain resources; no claim of recovery, unconditional close success or speedup.

---

## 1. Decision

**Deliver A3 TMR ordinary early enqueue as one complete capability in one PR, in parallel with the
external PRs.** No new mechanism, no slot-number change, no new identifier, no new API. It is not
blocked by any open PR, and the earlier "wait for #2028" position is withdrawn on technical grounds:

- **#2028's stream half is already merged.** `run_stream_pair.h` exists (292 lines) and is already
  multi-owner — *"More than one run may have submitted and not yet retired, so a completion, a query
  and a retirement each name the run they belong to"* (`run_stream_pair.h:41-47`). a2a3's runner uses
  it with staleness-aware AICore replacement and `ready_to_join_launch()`
  (`a2a3/platform/onboard/host/device_runner.h:124`). **[S]**
- **#2028's status half was superseded.** Its `completed_runtime_status_impl` /
  `runtime_status_poisons_device_impl` exist nowhere in `src/`; main gives each pipeline slot its own
  epoch-guarded run-result region, published by the device before its kernel returns
  (`device_runner_base.cpp:2801-2908`). **[S]**

---

## 2. The user capability

Configure an a2a3 `tensormap_and_ringbuffer` level-3 Worker with `launch_depth=2` over one or two
local device endpoints, then call `Worker.submit` repeatedly and read each run's result — with a
successor's work reaching the device while its predecessor still executes, the device still running
one whole operator at a time, each run reporting its own outcome, and backpressure, failure isolation
and `close()` all behaving.

---

## 3. What is missing — two facts, one feature

**[S]** The two TMR runtime makers differ by **exactly one exported symbol**:
`joined_native_launch_supported_impl`. Both declare `pipeline_depth = 2`, the same six resources and
the same classes (`TASK_ARGS` = `HOST_PER_RUN`; `GM_HEAP`/`GM_SM`/`RUNTIME_IMAGE` = `DEVICE_SCRATCH`,
**one instance**), and both already answer `concurrent_native_prepare_supported_impl() = 1`.

1. **The capability is never declared.** `c_api_shared.cpp:193` supplies
   `__attribute__((weak)) int joined_native_launch_supported_impl(void) { return 0; }`; a2a3 TMR does
   not override it. Every gate above it already passes on a2a3 — `ready_to_join_launch()` is a
   platform-level override already serving a2a3 HBG **[P]**, and
   `ChipWorker::supports_joined_native_launch()` needs only `launch_depth > 1` (the caller's existing
   key) and `pipeline_depth > 1` (already 2). **[S]**
2. **The failure-status read is unsound with a launched successor, and the tree says so.**
   `a2a3/.../tensormap_and_ringbuffer/host/runtime_maker.cpp` ~:1019-1035: *"That read is only sound
   under the current single-launched contract… **Once P4 admits a launched successor the branch has
   to be reworked** — an unproven read could then report the successor's header as this run's
   error."* **[S]**

**These are one feature, not two PRs.** Declaring the capability without fixing the read ships wrong
error attribution; fixing the read alone ships nothing a user can reach.

**Prior findings.** F1 (shared-header clobber) is real; main already has `read_published_run_status`
(`:239-258`) and **a5 TMR already ships the joined-safe form** — *"The shared memory header is not
read… A run that published none keeps the execution error its caller already holds."*
(`a5/.../tensormap_and_ringbuffer/host/runtime_maker.cpp:1022-1040`) **[S]** F2 ("D2H unknown treated
as success") is **stale as phrased**: the status block is gated on `execution_rc != 0 && launched !=
0`, so a failed D2H loses the *detail*, never the verdict **[S]**. The residual — `0` means both
"published nothing" and "snapshot absent" — is a diagnostic nicety, not a correctness defect, so
**no new log or observability API is proposed**.

---

## 4. The exact ordering (R1 correction)

**Rev 1 put the own-AICore wait before the AICPU payload. That was wrong.** Actual, **[S]**
`a2a3/platform/onboard/host/device_runner.cpp:805-885`:

```text
AICore stream  (closure 1)                  AICPU stream  (closure 2)
arm; run_streams_.mark_submitted
[joining] aclrtStreamWaitEvent(aicore,
     predecessor WHOLE-OPERATOR boundary)   :823,905-914
          record_cross_run_proof(aicore)    :928  (completes when the WAIT is consumed)
mark AicoreStart (passive)        :831
launch_aicore_kernel(aicore)      :834
record_run_boundary(Aicore)       :843
                                            launch_aicpu_payload(aicpu, RunName)   :849  ◀ FIRST
                                            [joinable_boundary] aclrtStreamWaitEvent(aicpu,
                                                 OWN AICore boundary)              :872,890
                                                 mark WholeOperatorEnd (passive)   :879
                                            record_run_boundary(Aicpu)             :882
                                            note_whole_operator_boundary()         :883
```

**Why AICPU must be first [I] from the comment at `:858-863` [S]:** *"The AICPU kernel spins in the
handshake until the AICore workers publish, but it returns on its own schedule: the AICore kernel may
still be in its tail."* The two kernels of one operator cooperate **during** execution. A wait on
this run's own AICore-kernel-*end* placed ahead of the AICPU launch would hold the AICPU payload
until the AICore kernel had already finished — but the AICore kernel cannot finish without the AICPU
dispatching to it. Circular wait. **Production ordering is correct and is not changed.**

**Both successor orderings.** (a) The successor's **AICore** stream takes an *explicit event wait* on
the predecessor's whole-operator boundary, then records a proof event immediately behind it so the
predecessor can retire its boundary without waiting for the successor's kernels **[S]** `:905-931`.
(b) The successor's **AICPU** stream takes **no explicit wait** — ordering is FIFO on the shared
AICPU stream, behind the predecessor's payload, own-AICore wait, marker and boundary record. **[I]**
So the successor's AICPU op — the one that *resets the single shared arena* — is dispatched only
after the predecessor's AICore kernel has ended. That is the whole safety argument for one shared
`DEVICE_SCRATCH` arena.

**What makes (b) sound [S]:** `simpler_launch_run_joined` (`c_api_shared.cpp:1744-1775`) refuses
`UNSUPPORTED` — absorbed by launching ordinarily — when the predecessor **published no whole-operator
boundary** (`has_whole_operator_boundary`). A predecessor whose boundary covers only its AICPU kernel
can never be joined behind.

**Second-order effect [S]:** `c_api_shared.cpp:1634` sets `joinable_boundary =
descriptor.joinable_boundary != 0 && joined_native_launch_supported_impl() != 0`. So declaring the
impl is also what makes a2a3 TMR runs *construct* the whole-operator boundary, at `launch_depth > 1`
only.

**SDK property, named:** in-order stream semantics for `aclrtStreamWaitEvent`. **[P]** a2a3 HBG
already ships joined launch on the same pair and calls, so the primitive is exercised on a2a3
silicon — **but that is not A3 TMR proof**: HBG has `HOST_PER_RUN` arena banks while TMR has one
shared arena, a different consumer of the same ordering. **Default-off is a control, not evidence.**

---

## 5. The one PR (R2)

**Production — two changes in one file, shipped together:** declare
`joined_native_launch_supported_impl() = 1` with a2a3's own justification; and remove the
shared-header fallback, keeping `read_published_run_status` plus a `WARN` naming the device log.

**Untouched:** declared depth (stays 2), `PipelineSlotPool`, `chip_run_lane.*`, `chip_worker.*`,
`runtime_c_api.h`, `device_runner_base.*`, all a5 and all HBG files, the mailbox layout. No ABI, no
config key, no new identifier, no new observability API.

**Declared scope, closed inside it:** a2a3 onboard TMR, level-3 root, one **or two** local endpoints,
`launch_depth=2`, `pipeline_depth` at its default. Two endpoints is scope rather than widening
because nothing on the admission path counts endpoints and `configure_pipeline_depth` already mins
across children **[S]** (`worker.py:8937-8941`), each child being its own process with its own
context, arena and slots. **Explicitly not delivered and not promised:** TMR depth 3, HBG, kernel
mode, sim, remote/MPI, comms, capture, and **diagnostics-enabled runs keep today's no-early-enqueue
behaviour** — `joined_diagnostic_launch_supported_impl` is a deliberately separate declaration
(`a2a3 HBG maker:162-174`) that this PR does not add.

**Existing invariants (regression cover, not proof) [S]/[P]:**
`tests/st/a2a3/tensormap_and_ringbuffer/pipeline_slots/test_pipeline_slots.py::TestPipelineSlotsTmr::test_slot_one_keeps_tmr_per_run_state_separate`;
`tests/ut/cpp/common/hierarchical/test_run_stream_pair.cpp`;
`tests/ut/cpp/common/hierarchical/test_chip_run_lane.cpp`;
`tests/st/a2a3/host_build_graph/early_enqueue/` (**HBG, not TMR**);
`tests/st/a5/tensormap_and_ringbuffer/early_enqueue/` (**a5, not a2a3**). The last two are **not**
substitutes.

**Missing — the PR's own acceptance, all [U]**, in a new
`tests/st/a2a3/tensormap_and_ringbuffer/early_enqueue/`: (1) two runs launched and accepted at one
coherent mailbox instant with correct goldens; (2) sixteen submits showing **sustained** refill of a
*different* pair; (3) successor `AicoreStart` after predecessor `WholeOperatorEnd` from passive
markers; (4) a third submit blocks in `begin_run` and completes after the first retires; (5) HOST
input lifetime — distinct buffers verified in results, `release_buffer` refused in flight and
accepted after drain; (6) DEVICE output lifetime — `Worker.free` refused in flight, accepted after
retire; (7) delayed read of a predecessor's output after a successor launched; (8) growth fallback —
a prebuilt-arena-cache miss falls back to depth one and still produces the right result; (9) error
attribution — a failing *successor* beside a live predecessor, predecessor's result intact;
(10) absent snapshot still reports the execution failure; (11) Partial/unknown stop — borrows
retained, lane refuses further admission, `close()` reports; (12) `close()` with work in flight, and
a poisoned lane's `close()` raising; (13) two endpoints each two-deep; (14) depth-one control with no
overlap. Where a scene test cannot make a state deterministic (10, and the
`has_whole_operator_boundary`-false refusal), the proof goes to
`tests/ut/cpp/common/hierarchical/` with injected native outcomes.

---

## 6. Cost (R3) — "zero new device allocation" withdrawn

**Static capacity unchanged [S]:** depth stays 2, so `PipelineSlotPool`, `runtime_bufs_`, mailbox
frames, `QueuedStreamWaits::entries_` (`std::array<Entry, PTO_PIPELINE_MAX_DEPTH * 2>`) and every
slot-indexed array keep their extent.

**Already active today, so not this change's cost [S]:**
`ChipWorker::supports_concurrent_native_prepare()` gates on `pipeline_depth > 1` **and not on
`launch_depth_`** (`chip_worker.cpp:918-921`), so a2a3 TMR **already prepares a successor into slot
1** — slot 1's `RuntimeStorage`, task-args copy, `sm_mirror` and result region are already activated.

**Lazy resources touched by this operation [S] (not all incremental to this change):** one **cross-run proof event** per `QueuedStreamWaits` CrossRun
entry, created once when `entry->proof_event == nullptr` and then retained
(`queued_stream_waits.h:100-105`); an Own-shape queued-wait entry plus its reserved fence reference
per joinable run (the role boundary events already exist — both are recorded unconditionally today);
and, on a slot's first touch, `allocate_tensor(device_run_result_bytes())` plus a **one-time 8-byte
H2D** zeroing, with steady-state reuse epoch-distinguished and paying no H2D
(`device_runner_base.cpp:2810-2855`).

**Overlap occupancy — the real change [I]:** at `launch_depth=2` two runs are simultaneously
*launched* where one was launched and one merely prepared, so the predecessor's task-args copy,
caller borrows and declared writes, fence events, device-wall buffer, result region and mailbox frame
may overlap the successor's submission and are released or made reusable at their own last-consumer boundaries. **Peak live ≈ two per-run sets** — the
count the slots already provision; what changes is the duration both are held. **No performance,
footprint or fragmentation claim; there is no measurement.**

---

## 7. Overlap and ownership (R4) — evidence only, no contact

| PR | relation | disposition |
| -- | -------- | ----------- |
| **#2028** `46e3adf1`, last commit 2026-08-28 | same **file** among its 32; stream half merged, status half superseded | A stale delta is **not** a blocking dependency. Record overlap, write the modern solution, reuse none of its code. No contact; **no reply is a prerequisite.** |
| **#2557** `26d8abc38`, not assumed merged | **Beneficial, non-blocking, zero file overlap.** Binds actual streams into `PreparedExecution` at launch and uses that binding for completion/discharge instead of re-reading `run_streams_.aicpu()/aicore()` later — exactly the hazard when two runs share one pair. **[S]** overlap diff. **Does not change the §4 order** (it only drops stream arguments) and **does not declare A3 TMR joined capability** or implement collector retirement. | Correct in either merge order. No takeover. |
| **#2545** `7c698515` | a2a3 **device-side** AICore exit acknowledgements; file-disjoint | **The one semantic dependency to watch**: it governs when the AICore kernel's end is observable, which `core_done` in §4 names. Evidence of impact, not a veto — re-read §4 if it lands. |
| #2511 `259481f3` | delta restores DeepSeek timeout 60→30; workload source unchanged **[S]** | unrelated |
| #2533, #2493, #2494, #2536, #2538, #2540, #2546 | HBG publication / kernel collectors / replay / diagnostics / graph cache / graph boundary | no overlap |
| #2534 `8300e34c` | this author's separate DFX track | this PR proposes **no** diagnostics capacity change |

**Blocked semantic dependencies today: none**, with evidence — #2545 is the only candidate, is
unmerged and file-disjoint, and nothing in §4 or §5 requires it first.

**Parallel tracks.** Nothing requested from temporal: diagnostics behaviour is unchanged here.
From dynamic, the a2a3 TMR prepare/bind cost would inform whether a caller should take the opt-in —
**useful, not a prerequisite.** Neither track changes the lane or runner; neither does this design.

---

## 8. Complete contract

**Operation.** `Worker(level=3, platform="a2a3", runtime="tensormap_and_ringbuffer",
device_ids=[1 or 2], launch_depth=2)` → `init()` → repeated `submit()` → `RunHandle.wait()` → read
outputs. No new key, no new call.

**Continuous running.** Up to two runs hold sets; a successor prepares into its own slot and launches
behind the predecessor's whole-operator boundary (§4); the device runs one whole operator at a time;
each retirement frees a set for the next admitted run.

**Full backpressure.** A third `submit` blocks in `begin_run` until an entry frees — no failure, no
reorder. A bind that misses the prebuilt-arena cache falls back to depth one at the FIFO head.

**Retirement.** Per run on its own proven finalize: arena bank, fence events, mailbox frame, task
args, result region, caller borrows. An unproven finalize retains borrows and latches
`write_unproven` **on the allocation**, never on the slot.

**Error attribution.** Each run reports its own status from its own per-slot region, written by the
device before its kernel returned; the shared header is not read; a run that published nothing keeps
the execution failure its caller holds.

**Failure stop.** A throwing joined launch or unproven finalize poisons the lane, which stops
admitting and whose `close()` reports the poison. A partial launch retains resources until quiescence
is proven. A failing successor leaves a live predecessor's result correct.

**Close.** `close()` drains in-flight runs and reclaims buffers; on a poisoned lane it raises.

**Limits.** As §5's declared scope, including diagnostics keeping today's behaviour.

**Cost.** §6 — no static capacity change, one retained proof event, one 8-byte H2D per first-touched
slot, peak ≈ two per-run sets held concurrently for longer. No performance claim.

**Acceptance.** §5's fourteen scene cases plus the native cases, on the real `Worker.submit` route.
**[U]** Nothing has been executed; new-head CI would be the first execution. HBG green is not A3 TMR
evidence; default-off is a control.

---

## 9. Review response

- **R1 accepted and corrected** — §4 gives the real order (AICPU payload first, then own-AICore wait,
  then `WholeOperatorEnd`, then the AICPU boundary record) from `device_runner.cpp:805-885`, with the
  handshake deadlock reason, both successor orderings, the `has_whole_operator_boundary` refusal that
  makes the AICPU-FIFO argument sound, and the second-order effect of declaring the impl. Production
  ordering unchanged. HBG passing and default-off are explicitly not accepted as proof.
- **R2 accepted; scope rewritten** — §2 states the capability, §5 shows why both production changes
  are one feature, names existing invariants by exact file and test, says why HBG's and a5's suites
  are not substitutes, and replaces the six-item list with fourteen cases plus native coverage. Port
  size is no longer the goal.
- **R3 accepted; claim withdrawn** — §6 separates static capacity from overlap occupancy, names
  `ensure_device_run_result_region`'s first-use allocation and 8-byte H2D and
  `QueuedStreamWaits::open`'s first-use proof event, and corrects a fact against my own earlier
  framing: concurrent prepare does not gate on `launch_depth`, so slot 1 is already active today.
- **R4 accepted** — no contact, no reply prerequisite; §7 records #2028 as a stale non-blocking
  one-file overlap, #2557 as beneficial, file-disjoint and order-independent with its actual delta
  read, #2545 as the single dependency to re-read if it lands, and #2511 as unrelated. No blocked
  dependency exists today.
- **No new API added** — §3 shows the status-read residual is diagnostic, not functional.
- **Not implementation authorization.**

**Full design with every citation: `p4-a3-tmr-current-main-design.md`.**
