> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# Ordinary async: real Worker.submit acceptance inventory and warm-prepare cost map

**Revision 2 — manager reconciled** — corrects R1–R4 of
`pipeline-a-manager/.docs/review/ordinary-async-acceptance-manager-review.md`.
Read-only source/design/evidence deliverable. **No product edit, no test authored or executed
(including previously executed ones), no build/install/device call, no runtime helper probe, no
CI POST/cancel, no commit/push/PR, no external message.** Worktree `dynamic-noodling-gizmo`,
original UUID, worktree changes preserved, merged branch not reset or checked out.

> **Delivery path.** Writes to `pipeline-a-manager/` are refused by this session's worktree
> isolation, so the artifact stays at
> `.claude/worktrees/dynamic-noodling-gizmo/.docs/review/ordinary-async-acceptance-and-cost-map.md`
> for the manager to collect. No other worktree modified.

**Baseline.** Main re-verified at the start of this revision: **`600540064cc99f99006c11524561d2568ea2bb06`**,
unchanged from the reviewed SHA. All source facts read with `git show upstream/main:<path>`.
Contracts read: `runtime-design-principles.md`, `review/stream-architecture-user-contract.md`,
`claude-managed/complete-feature-delivery-contract.md`, registry CURRENT CONTRACT.

**Evidence labels.** `[source]` current-main source fact · `[inference]` my reasoning from source ·
`[prev-exec]` an execution that happened, at the head named · `[untested]` no execution exists.
Per R4, three evidence kinds are kept apart throughout: **source presence**, **named-case actual
execution**, and **prior-head CI aggregate**.

---

## R1–R4 response map

| | Required correction | What changed here | Verdict |
|---|---|---|---|
| **R1** | Partial is neither sole-producer nor untested; enum is NotStarted/Partial/Complete; nonzero rc cannot authorize discharge; remove the unsafe oracle | §3 **I1 withdrawn in full**; §2.1 is a new accurate Partial inventory (multiple Partial paths, 5 grading cases, retirement predicate, 2 drain-separation cases, 5 lane cases); row 7 reclassified from "missing at every level" to **covered**; the `Launched, rc != 0` control is deleted as unconstructible | **Accepted — my claim was wrong** |
| **R2** | Row 6 conflates four claims; do not globally label data chaining missing | Row 6 split into **four rows** (growth / incompatible config / launch opt-out / host-address restriction), each with evidence that proves its own cell; row 1 split into generic DEVICE chaining (**covered**, A3 `:1395` + A5 `:1436`) vs production same-request autoregressive feedback (**#2511's**) | **Accepted** |
| **R3** | H2D is not every non-output tensor; pipeline_depth alone does not select early launch | §4 transfer rule rewritten with both bypasses and `written_spans`; §5 plan rebuilt on **separate resource and launch axes**, equal workload, actual receipts, warm-up, endpoint frame occupancy | **Accepted** |
| **R4** | Update #2511 to `259481f3`; add #2557; distinguish the three evidence kinds; I1/I2 are obligations of complete features | §1.1 updated; §1.2 added for #2557; evidence labels split as above; §3 states I2 as an obligation **inside P4's complete A3 TMR delivery**, not a standalone PR | **Accepted** |

**Delivery-contract compliance.** Per `complete-feature-delivery-contract.md`, nothing here is
proposed as an isolated acceptance or capacity PR. The one surviving invariant (§3) is named as an
acceptance obligation **inside** an already-owned complete feature. No fragmented PR is proposed,
and no new architecture implementation is requested.

---

## 0. Two corrections to the standing picture

**0.1 `[source]` The Graph Definition device block no longer exists — my earlier audit was
superseded by a merged PR.** `#2487` (`ef38a0ed8`, *"carry a run's Graph Definition in the AICPU
launch arguments"*): *"No rtMalloc, no rtMemcpy and no rtFree for a Definition remains, and there
is no manual fallback: the block, its grow rule and its release path are deleted."* At main,
`bind_graph_definitions` calls `acquire_graph_staging` (host-only, grow-only, per slot) and
`publish_graph_section`; the `HostApi` contract says *"a Definition's bytes reach the device inside
the run's AICPU launch package, so nothing here allocates, uploads or frees device storage"*
(`host_api.h:87-105`). `TaskDescriptor` carries `graph_definition_offset`, not an address.
`HostPhaseKind::BindGraphPack` is *"Packing, not an upload."*

Consequence: the "one H2D of `block_bytes`" cost I quantified against `c605b03ca`, and the entire
per-slot *device* residency half of `.docs/hbg-definition-registration-reuse-decision.md`, **do not
describe main.** That artifact's §4.2/§4.3 are void. Its §1 (collision with the withdrawn
cross-invocation preparation cache) and §2 (what is keyed vs. only declared) survive, now applying
to host packing and launch-package bytes rather than to a device upload. Manager review accepts
this.

**0.2 `[source]` A user-facing docstring on the API under inventory is stale.** `Worker.submit`
(`python/simpler/worker.py:12245-12247`) says *"A5 tensor-map-and-ring-buffer publishes depth two …
**A5 host-build-graph publishes no contract and stays at depth one**."* At main,
`tests/ut/py/test_pipeline_contract_capacity.py::test_a5_host_build_graph_declares_three_program_sets`
asserts a5 HBG's program depth `== 3`, and `#2531` changed that declaration from literal `2` to
`PTO_PIPELINE_MAX_DEPTH` — so a5 HBG published a contract even before `#2531`.
`PTO_PIPELINE_MAX_DEPTH = 3` (`runtime_c_api.h:134`). **Both clauses are wrong at main.** Reported,
not fixed; a `doc-consistency.md` §1 item for whoever next touches that docstring, not a behaviour
defect and not a PR of its own.

---

## 1. Acceptance inventory — real public `Worker.submit`

Routes: **A3 HBG** = onboard a2a3 `host_build_graph`; **A5 HBG**; **A5 TMR**. A3 TMR is P4's design
scope and appears only where it already has coverage.

**Execution evidence, the three kinds kept apart.**

- `[prev-exec]` **Prior-head CI aggregate.** At the `#2531` pre-merge head
  **`ca08f636900cfd5a8440ed60d838ca6d3141f343`**: `st-onboard-a2a3`, `st-onboard-a5`, `ut-a2a3`,
  `ut-a5`, `ut (ubuntu/macos)`, `st-sim-a2a3sim`, `st-sim-a5sim`, `st-deepseek-onboard-a2a3`,
  `st-network1-onboard-a2a3` all `success`. This is an **aggregate**, not per-case evidence.
- `[prev-exec]` That is **attempt 2**. An earlier attempt at the same head failed in
  `BootstrapDispatcher` with `aclrtSynchronizeStream` 507018 on device 8 before any workload, on
  both HBG and TMR (job `114140756920`, workflow `38027072918`, runner `infra021-npu-4`,
  `2026-10-10T05:27:11Z`; same signature at heads `ece4631ca` and `9ed1c25f8` per
  `ci-failure-2531-a3-bootstrap-device8.json`). Root cause undetermined; per §G8 the green attempt
  is **not** evidence it is fixed.
- `[source]` **At `600540064` itself no scene or unit job ran** — 7 `success`
  (`report`×5, `deploy`, `build`), 10 `skipped` (`lane`, `gate-and-resolve`). So "last known
  execution" everywhere below means *at `ca08f6369`, as part of that aggregate* — **no named case
  below has verified per-case execution at current main.**

### 1.0 Matrix

| # | Invariant | A3 HBG | A5 HBG | A5 TMR |
|---|---|---|---|---|
| 1a | **generic DEVICE result chaining** — a run consumes a predecessor's device output by address | `TestDeviceResultChain::test_a_device_result_feeds_the_next_two_runs` (`early_enqueue:1395`) | `TestA5ThreeRunCapacity::test_a_device_result_feeds_the_next_two_runs` (`:1436`) | `[untested]` |
| 1b | **production same-request autoregressive control/KV feedback** | **owned by `#2511`** — its own declared follow-up, not mine | — | — |
| 2 | **independent requests** accepted at once, device serial | `test_two_runs_are_launched_and_accepted_at_once`; `TestThreeRunCapacity::test_three_runs_…` | `TestA5EarlyEnqueueDepthTwo::test_two_runs_…`; `TestA5ThreeRunCapacity::test_three_runs_…` | `TestA5TmrEarlyEnqueueDepthTwo::test_two_runs_…` |
| 3 | **sustained refill** | `test_sustained_refill_overlaps_successive_run_pairs`; `TestThreeRunCapacity::test_sixteen_runs_keep_refilling_the_sets_they_retire` | `test_sixteen_runs_keep_refilling_both_sets`; `TestA5ThreeRunCapacity::test_runs_keep_refilling_the_third_set` | `test_sixteen_runs_keep_refilling_both_sets` |
| 4 | **wait(A) vs successor** | `tests/st/run_retention/…::test_retained_synchronize_waits_for_the_successor_hbg` — **private entries, not `Worker.submit`** | same case | `…_tmr` sibling |
| 5 | **delayed result read** | `test_result_reads_while_successor_runs_hbg` — **private entries**; public half partial (row 2 asserts each run's own result after both submitted) | row 1a covers the device→device shape | `[untested]` for the delayed-read shape |
| **6a** | **retained-capacity growth** (a wider run outgrows a retained block) | `tests/ut/cpp/common/platform/test_retained_temp_bump.cpp::{ALargerRunGrowsTheSlotAndFreesTheOldBuffer, ARunThatFitsReusesTheRetainedBufferWithNoAllocation, AFailedGrowClearsTheSlotRatherThanLeavingTheFreedBuffer}`; `test_retained_scheduler_storage.cpp`; `test_arena_replacement_transaction.cpp` — **cpput, route-independent** | same | same |
| **6b** | **incompatible prepared config → depth-one fallback** | `[untested]` on this route | documented at `early_enqueue:1650` (`PreparedRunIncompatible` named as *the* depth-one fallback) | `TestWorkerAsyncWholeRunFifoTmr::test_incompatible_runtime_env_falls_back_to_depth_one`; lane path via `test_chip_run_lane.cpp:72,397` |
| **6c** | **launch-axis opt-out** (no `launch_depth` ⇒ no overlap) | `TestEarlyEnqueueDepthOneControl::test_no_overlap_without_the_opt_in` | `TestA5EarlyEnqueueDepthOneControl::…` | `TestA5TmrEarlyEnqueueDepthOneControl::…` |
| **6d** | **host-address restriction on a control value** | `[untested]` | `test_a_device_control_tensor_is_refused_until_it_is_copied_to_the_host` (`#2531`) | `[untested]` |
| 7 | **partial launch** (`LaunchProgress::Partial`) | **covered — see §2.1**, route-independent | same | same |
| 8 | **fail then close** | lane `test_three_run_capacity.cpp::AMiddleRunsFailureKeepsItsOwnReferencesAndLeavesThePredecessorAlone`; `test_chip_run_lane.cpp::{FinalizeFailurePoisonsAdmissionAndCloseReportsIt, LaunchFailureWhoseFinalizeFailsReportsThePoisonedLane, EarlierFailedRunRetainsItsErrorAfterLaterRunPoisonsLane, CloseDrainsAndRejectsNewSubmissions}`; worker `test_error_propagation.py::{test_failure_does_not_wedge_worker, test_subsequent_submit_raises_after_failure}` | same lane cases (`#2531` extended to capacity three) | `TestA5TmrEarlyEnqueueErrorAttribution::test_a_failing_successor_reports_its_own_status` |

Unless stated otherwise each scene case is in
`tests/st/<arch>/<runtime>/early_enqueue/test_early_enqueue.py`, gated on its own platform.
**Rows 6a and 7 are route-independent by construction** — they exercise shared code below the
platform split, so they are not per-route cells and are not claimed as such.

**Note on rows 4–5 `[source]`.** `tests/st/run_retention/test_run_retention.py` drives
`ChipWorker._prepare_native_run_with_pipeline_lease`, `_launch_native_run` and
`_probe_run_retention` — private L2 entries plus a fixture-only probe, not `Worker.submit`. Its own
`_overlap_established` docstring calls the overlap *"a **precondition**, not the claim"* and says
nothing pins the successor there. That is the concrete reason a sleep-based "successor still
Pending" assertion is the wrong shape. **`run_retention_probe.{h,cpp}` is in `#2557`'s diff** (§1.2),
so this surface has an owner and I propose nothing on it.

**A3 TMR `[source]`:** no `early_enqueue` directory. Its async coverage is `worker_async_fifo` (via
the `TestWorkerAsyncWholeRunFifoTmr` subclass), `pipeline_slots`, `run_stream_reuse`,
`prepared_callable`, `test_l3_launch_acceptance.py`. Row 2's device-order evidence has no A3 TMR
counterpart; that is P4's complete-delivery scope, and §3 is its acceptance half.

### 1.1 `#2511`, at current head

`[source]` **OPEN at head `259481f3fa2e94bf8b8c8fb28a4f8cfb2a6610b3`** (was `e0472bdee`), a single
commit whose parent is main `600540064`, 6 files — `examples/a2a3/host_build_graph/qwen3_14b_serving_effective/{README.md,clone_worker_submit.py,reference_worker_submit.py}`
and `tests/ut/py/{test_qwen_clone_worker_submit,test_qwen_resident_kv_lifetime,test_qwen_standalone_adapter}.py`.
`.github/workflows/_st-deepseek-a2a3.yml` is **no longer in the diff** — the earlier head's
DeepSeek 60-minute timeout bump is reverted to 30. **Workload source unchanged** between the two
heads.

`[prev-exec]` Its hardware qualification — *two requests, 127 decode pairs, 127 joined launches,
token/logit/KV gates passed, serial device ticks 1–3* (686 `task_20261008_185616_98029523118`) and
*depth=1 reference and depth=2 fallback each passed 127 steps*
(`task_20261008_190002_110848685`) — was collected at **original head `47628cfe`, base
`60167bcc`**, and its own body says the 127-pair qualification was not rerun. **The 127 pairs are
therefore prior-head execution, cited nowhere as current-head or as a named-case result at
`259481f3`.** I do not gate any other work on its head state.

`[source]` Its declared follow-ups — *"Same-request DEVICE feedback joined enqueue"* and
*"Continuous request refill, retirement, backpressure, and failure cleanup"* — are the workload
owner's. Row 1b is theirs; §3 selects neither.

### 1.2 `#2557`, noted as an owned overlap

`[source]` **OPEN at head `26d8abc38e511ef5f2022e5cefbfec5fcafafd29`**, *"Refactor: bind execution
streams to each native run"*, author `ChaoWao` (original owner). Files:
`docs/design/run-completion-fence.md`, `src/a2a3/platform/onboard/host/device_runner.{cpp,h}`,
`src/a5/platform/onboard/host/device_runner.cpp`,
`src/common/platform/onboard/host/device_runner_base.{cpp,h}`,
`src/common/platform/onboard/host/run_retention_probe.{cpp,h}`,
`tests/ut/py/support/{run_execution_streams_probe,runtime_publication_launch_probe}.cpp`,
`tests/ut/py/test_runtime_publication_launch.py`.

`[inference]` It binds streams to `PreparedExecution` and takes query/drain/wait from saved
handles. Two consequences for this artifact, both boundaries rather than findings: it **owns**
`run_retention_probe`, the fixture rows 4–5 depend on, so the "private entries" note above may be
restated by that PR rather than by me; and it touches the `launch_execution` neighbourhood that
grades `Partial`, which is a further reason §3 does not propose work there. **No duplicate work is
proposed on any file in that list.**

---

## 2. What the inventory is actually short of

`[inference]` After R1 and R2, rows 2, 3, 6a, 6b, 6c, 7 and 8 are genuinely covered, most at two or
three layers. The remaining gaps are narrow:

1. **Rows 4–5 have no public-API owner** — only private entries plus a probe whose precondition is
   admitted unpinnable, and that surface is `#2557`'s.
2. **Row 1b** — production same-request autoregressive feedback — is `#2511`'s declared follow-up.
3. **Rows 6b/6d have route-shaped holes** (6b untested on A3 HBG; 6d only on A5 HBG). Each belongs
   to whichever complete feature next owns that route's admission path, not to a standalone case.
4. **A3 TMR's predecessor-error read is documented in production source as unsound once a
   successor launches** (§3).

**Row 7 is no longer on this list.** §2.1 replaces my earlier claim.

### 2.1 `Partial`: the accurate inventory (R1)

`[source]` **The enum is `NotStarted` / `Partial` / `Complete`** (`native_run_execution.h:138-142`).
There is no `Launched`. My previous §3 used `Launched` and proposed a `Launched, rc != 0` positive
control — **an unconstructible state**, since `Complete` is set only after `result.rc == 0`. That
oracle is deleted.

`[source]` **Partial has multiple failure paths**, all in
`ExactLaunchTransaction::run` (`:222-256`):

1. an exception escaping `submit_aicore` → `rc = -1`, `Partial`;
2. `submit_aicore` returning non-zero **with** `sink.submitted()` → `Partial`;
3. `progress` is set to `Partial` **unconditionally** once AICore submission succeeds, before
   `submit_aicpu` is called — so any non-zero return from `submit_aicpu` leaves `Partial`;
4. an exception escaping `submit_aicpu` → `rc = -1`, still `Partial`;

`Complete` + a receipt occurs only when both submissions return 0; this is the success path, not a Partial producer.

`submit_aicore` returning non-zero **without** a marked sink stays `NotStarted` — the documented
safe pre-launch rollback.

`[source]` **Grading is tested, by five cases** in
`tests/ut/cpp/common/worker/test_native_run_execution.cpp` (10 cases total):
`AicoreFailureAfterMarkedSubmissionPoisons`, `AicpuFailureAfterMarkedSubmissionStaysPartial`,
`AicoreExceptionPoisonsWithoutTryingAicpu`, `AicpuFailureAfterAicorePoisonsWithoutReceipt`,
`AicpuExceptionAfterAicorePoisonsWithoutReceipt`. Each asserts `rc`, `progress == Partial`,
`poisoned()`, `receipt.valid() == false`, and whether the second callback was attempted. Companions
`AicoreFailureIsSafePrelaunchFailure` and `ArmingFailureReportedAsRcStaysSafe` pin the
`NotStarted` side.

`[source]` **Ownership and discharge are tested, and are not rc-derived.** `WorkspaceManager`
records *positive facts reported where they become true*, never interpreting a return code
(`workspace_manager.h:168-190`). Retirement is a conjunction:

```cpp
static bool retired(const RunRecord &r) {
    const bool device_settled = r.drain_proved || r.no_submission;
    return device_settled && r.copyback_returned && r.bindings_released;
}
```

`NoDeviceSubmission` is **ignored once `launched` is set** — *"a partial unwind can leave the
caller's pointer null, and that is not the same fact as never having reached the device"* (`:468-471`).
`DrainProvedComplete` is reported only when `drain.device_rc == 0`
(`c_api_shared.cpp:1930,2099`). `[inference]` So a Partial run, which reported `Launched`, can only
retire through `drain_proved`; absent that it never retires and its references are never dropped —
which is exactly the transaction's stated contract, *"resources stay retained and admission is
poisoned until a later drain or teardown proves quiescence."* Covered by
`test_workspace_manager.cpp::{ALaunchedRunKeepsThatFactAgainstALateNoSubmission,
ADrainThatDidNotProveCompletionDoesNotRetire, ARunThatSubmittedNothingRetiresWithoutADrain,
ARunIsLiveFromItsFirstReferenceUntilItRetires}`.

`[source]` **Device-rc and diagnostics-rc are deliberately separate.** `DrainOutcome` keeps
`device_rc` and `diagnostics_rc` apart because composing them *"would classify a run whose device
work demonstrably finished as one whose completion was never proved, which permanently withholds
every workspace block that run referenced"* (`:180-199`). Covered by
`test_run_drain_result_separation.cpp::{ADiagnosticsFailureFailsTheCallerAndStillRetiresTheRun,
ADeviceErrorKeepsPriorityAndQuarantinesTheRunsBlocks}`.

`[source]` **The runner's response to its own Partial is in production:**
`if (transaction.poisoned()) recover_device_or_mark_unusable(transaction.rc)` (a2a3
`device_runner.cpp:701`, a5 `:694`); sim uses `poison_launch()` (a2a3 `:648`, a5 `:659`).

`[source]` **`LaunchProgress` does not cross the C API.** The lane's stub seam is
`int launch_run(void *, void *runtime)` — a plain rc (`test_chip_run_lane.cpp:77-81`). Progress is
consumed inside the runner; the lane only ever sees the resulting rc. `[inference]` **So my
proposed "lane stub returning `LaunchOutcome{Partial}`" is not constructible at that seam** — there
is no lane-level Partial injection point, and I am not proposing that one be added. Lane behaviour
on a launch rc failure is already covered
(`LaunchFailureWhoseFinalizeFailsReportsThePoisonedLane`, with `g_launch_rc[0] = -6`), as is error
isolation (`EarlierFailedRunRetainsItsErrorAfterLaterRunPoisonsLane`).

`[untested]` **What is left is a sliver, and it is an obligation, not a proposal.** This finite inventory did not identify a case that connects a graded Partial through
`recover_device_or_mark_unusable` / `poison_launch` to subsequent public submit and close.
That is an unverified integration boundary, not proof that no indirect case exists. It is
not injectable without a new seam, so it belongs to whichever complete feature next owns launch and
recovery ownership — `#2557` is the nearest such feature. **Not proposed as work here.**

---

## 3. The one remaining selected invariant

My earlier **I1 is withdrawn in full** (§2.1). One invariant remains, and per R4 and the
delivery contract it is stated as an acceptance obligation **inside P4's complete A3 TMR
early-enqueue delivery**, not as a standalone PR.

### A predecessor that published no terminal record keeps its own execution error and never reports a successor's

`[source]` a2a3 TMR `runtime_maker.cpp:1018-1035` reads run status in two steps.
`read_published_run_status` is the **run-owned** error tail and is already successor-safe: *"it
cannot have been overwritten: the device copied it before its kernel returned, hence before the
fence that releases a successor to reset the header"* (`:240-249`). The **fallback**
`read_runtime_status` reads the **shared header**, and production source states its own
precondition:

> *"That read is only sound under the current single-launched contract: this run still holds its
> execution claim, no successor has been launched, and nothing has reset or rebuilt the shared
> memory since this run wrote it. Once P4 admits a launched successor the branch has to be
> reworked — an unproven read could then report the successor's header as this run's error."*

`[source]` **a5 TMR has already removed that fallback.** At `:1028-1039` there is no shared-header
read: *"Every platform serving this runtime hands each pipeline slot its own result region instead,
and the device side publishes into it before its kernel returns … A run that published none keeps
the execution error its caller already holds."*

`[inference]` So the end state is already shipped next door, and the invariant is **differential**:
it holds on a5 TMR at main and must hold on a3 TMR once a successor can launch. The fallback is
reached **only** when the run published no snapshot, which is why no green run exercises it.

**Acceptance shape, for P4 to own inside its delivery.** A stubbed `HostApi` in which `run_result()`
returns `nullptr` (or a wrong byte count), so `read_published_run_status` returns 0 by construction
and the fallback branch is the one under test; the shared-header read planted with a
**distinguishable** successor error code; `execution_rc != 0` and `launched != 0`, the two gating
conditions. Assert the reported status is the run's **own** execution error, never the planted code.
On a5 TMR this passes at main — the built-in positive control. No timing, no sleep, no device.

**Boundary.** The approved A3 TMR design is **P4's**: retain the existing per-run result region and
delete the unsafe shared-header fallback; no new result-region allocation is proposed. I supply the invariant, the a5 precedent and the oracle. This
is not a standalone PR and not a product change.

---

## 4. Warm prepare cost map

`[source]` A3 HBG ordinary warm path in execution order, from
`c_api_shared.cpp::simpler_prepare_run` (`:1591-1635`) and
`src/a2a3/runtime/host_build_graph/host/runtime_maker.cpp`.

### 4.0 The argument transfer rule, stated exactly (R3)

`[source]` `bind_callable_to_runtime_impl` (`:1311-1399`) decides per tensor, **in this order** —
so "H2D of every non-pure-OUTPUT caller tensor" was wrong on two counts:

1. **`t.is_device_memory()` ⇒ pass-through by address.** No retained-temp slice and **no H2D**. The
   address is asserted below `HEAP_VIRTUAL_BASE`; a signature entry is **mandatory** (absent ⇒
   `SIMPLER_ERROR_INVALID_ARGS`, because direction is the only thing a pass-through's handling
   depends on); `OUT`/`INOUT` records a `CallerBufferSpan` into `written_spans`, which feeds
   `api->declare_caller_device_writes(...)` before orchestration.
2. **`t.transfer == TensorTransfer::NONE` ⇒ added as-is.** No slice, no H2D. (Transfer request and
   storage location are independent axes since `#2454`.)
3. **Otherwise — a HOST-space tensor with a transfer request — is the host copy path.** It takes a
   `RetainedTempBump` slice; `size == 0` takes none and carries a null address; otherwise H2D
   unless the signature marks it pure `OUT`; the lease records `needs_copy_in` /
   `needs_copy_back` (copy-back skipped only for provably `IN`).

So the retained-temp slicing and the H2D belong to **case 3 only**, and `h2d_bytes` / `h2d_tensors`
in the `BindArgs` attributes count that path alone.

### 4.1 Stages

| Stage | Span / phase | What a **warm** run still does |
|---|---|---|
| compatibility | `chip.run.bind.compatibility` | TMR only: `prepared_run_config_compatible_impl` probes the prebuilt-arena cache; HBG has no such probe |
| bind → args | `BindArgs` (`:1406`) | the §4.0 three-way decision; slices come from a **retained** buffer (grow-only); H2D on case 3 only |
| bind → arena | `BindArenaBuild` (`:1430`) | **`DeviceArena host_arena; runtime_reserve_layout(...); host_arena.commit(...)` — a host allocation per bind** |
| bind → runtime init | `BindRuntimeInit` (`:1463`) | **`runtime_init_data_from_layout` + `runtime_wire_arena_pointers` rewrite the whole runtime-arena image into that host arena, every bind** |
| bind → orchestration | `BindHostOrch` (`:868`) | host graph build; per-bind `GraphHostState`; recorder storage per-thread and retained |
| bind → graph pack | `BindGraphPack` (`:613`) | pack into **retained** `acquire_graph_staging`; headers + spill copies; `publish_graph_section` into the AICPU launch package. **No H2D** (§0.1) |
| bind → commit | `BindStaticArena` (`:974`), `BindGmHeap` (`:994`) | `setup_static_arena` *"short-circuits a request an existing one already covers, so a repeated workload pays for neither twice and the heap is grow-only"* |
| bind → SM | `BindSharedMem` (`:989`) | `compact_live_image` restacks the live SM prefixes, pitched to this run's `total_tasks`, heap-rebased, into **retained** `acquire_run_image_staging` |
| publish | `chip.run.publish_image` / `BindArenaH2d` (`:1570`) | N prerequisite `copy_to_device` + **one H2D of `copied_bytes + sm_image_bytes`** |
| stage inputs | `chip.run.stage_inputs` | HBG: `copy_in_run_inputs_impl` is a documented **no-op** |
| prepare exec | `chip.run.prepare_execution` | kernel-args pack, boundary-event reservation, collector arm |

a5 HBG adds one `add_pending_metadata` prerequisite for scheduler state (a5
`runtime_maker.cpp:1281,1396`) against the per-slot storage `#2443` retained — `[source]` still
present at main.

### 4.2 Classification

**Unavoidable mutation.** Case-3 caller-input H2D; host orchestration; `compact_live_image`'s SM
image (pitched to this run's `total_tasks`, heap-rebased); `total_tasks`;
`rt->prebuilt_layout.sched.capacities`, which `derive_ready_queue_capacities` computes from **this
run's** task population (`:880-908`); Definition section bytes for new Graph keys.

**Safe reuse already delivered — do not re-propose.** Registration-owned callable tables borrowed
per bind (`set_callable_tables` after a mandatory `clear_callable_tables`,
`device_runner_base.cpp:2036-2065`); `setup_static_arena`'s grow-only short-circuit;
`acquire_run_image_staging`, `acquire_graph_staging`, `acquire_sm_mirror`, retained-temp bump — all
per-slot, grow-only, handed over uninitialized; a5 HBG scheduler-state storage (`#2443`); the
Definition's RTS launch-package carriage (`#2487`); TMR's `PrebuiltRuntimeArenaCache` hit path.

**Unproven candidate — correctness evidence first, not an automatically justified optimization.**
The per-bind host runtime-arena rebuild (`BindArenaBuild` + `BindRuntimeInit` + the `memcpy` of
`copied_bytes`, `:1050`). `[source]` TMR consults
`lookup_prebuilt_runtime_arena_cache` / `mark_prebuilt_runtime_arena_cached` and on a hit skips the
host arena, the image build and the wiring (`bind_cached_runtime_image`, a2a3 TMR `:698-722`);
**HBG consults neither on either arch** — the only consumers are the two TMR makers.
`[inference]` A direct port is **unsound**: HBG writes `sched.capacities` into that image from this
run's task population, so the image is not key-invariant the way TMR's is. What is needed first is a
field-level split of the copied zone into layout-invariant vs per-run plus a patch-in-place
protocol — a correctness contract, not an edit, and if ever pursued it is one complete feature with
its own acceptance, not a byte-shaving PR. **No measurement supports doing it** (§5.2).

Cross-run Definition reuse is now a host-packing question (§0.1) and still collides with the
withdrawn *"any automatic cross-invocation preparation cache"* (`#2270`/`#2281`, 2026-09-17), so it
remains inadmissible standalone.

---

## 5. Measurement plan (R3: separate axes)

`[source]` **Existing sources only; no new instrumentation and none authorized.** `[STRACE]`
markers via `python -m simpler_setup.tools.strace_timing --rounds-table`; the ten-kind
`HostPhaseKind` breakdown behind `SIMPLER_HBG_BIND_BREAKDOWN_ENABLE=1`, parsed by
`simpler_setup/tools/hbg/bind_phases.py`; `chip.run.runner_run.{device_wall,aicpu_launch,device_boundaries}`;
`write_host_phase_records_artifact`; `SIMPLER_SKIP_DEVICE_RUN=1`;
`simpler_setup/tools/{phase_time_split,critical_path,sched_overhead_analysis}.py`; docs
`dfx/{hbg-bind-phases,host-trace,l2-timing}.md`. Memory surfaces:
`tests/ut/py/test_worker/{test_workspace_budget_surface,test_device_memory_info}.py`, plus the
per-bind `minflt`/`hblkhd` method in
`docs/investigations/2026-08-host-orch-phase-tail-is-page-faults.md`.

### 5.1 The three bounds are three axes `[source]`

`_validated_run_depths` returns **three** numbers (`worker.py:623-634`), and only the third selects
early launch:

- **`pipeline_depth`** — the native **resource-set** capability requested; granted as
  `min(request, the chip module's declaration)`, and an explicit request that is under-granted
  **fails startup** (`_granted_chip_pipeline_depth`).
- **`pending_run_depth`** — the logical admission FIFO bound. *"That cap is a run count: it is not
  a memory budget, and a terminal run whose RunHandle is unreleased does not occupy it."* `0`
  derives it from the negotiated pipeline depth.
- **`launch_depth`** — how many admitted runs may have device work launched at once. **Defaults
  `1`**, which *"keeps a successor's work off the device until its predecessor is terminal"*;
  requires level ≥ 3; effective value is
  `min(launch_depth, direct_chip_pipeline_depth, *chip_task_frame_counts)` (`:8952-8962`) — so a
  **single-frame endpoint carries one launched run whatever depth was requested.**

`[source]` The scene tests confirm the separation: the depth-two classes set `launch_depth: 2`
only; `TestThreeRunCapacity` sets **both** `launch_depth: 3` and `pipeline_depth: 3`, its docstring
saying *"`launch_depth` alone cannot produce this: a launched run holds its resource set for its
whole …"*; and `TestEarlyEnqueueDepthOneControl` is the **launch**-axis opt-out (`launch_depth`
unset). `[inference]` My previous plan's "two arms differing only in `pipeline_depth`" was
therefore wrong: that axis alone changes no launch behaviour.

### 5.2 The plan

One workload, held **identical** across arms (same callable, same shapes, same task count, same
round count), on a pinned die, with `task-submit --list` and `pgrep` recorded in every arm. Four
arms, moving one axis at a time:

| Arm | `pipeline_depth` | `launch_depth` | Isolates |
|---|---|---|---|
| **A** baseline | default (2) | default (1) | serial reference; no early launch |
| **B** launch axis | default (2) | 2 | early launch at unchanged resource capacity |
| **C** resource axis | 3 | 2 | extra retained capacity at the same launch bound |
| **D** launch axis at capacity three | 3 | 3 | third simultaneous launched run, at unchanged resource capacity |

Read the same capture three ways, and **report the endpoint frame occupancy alongside each arm**,
since the effective launch depth is clamped by `chip_task_frame_counts`:

| Question | Read | Discriminator |
|---|---|---|
| **host prepare bottleneck** | warm per-phase `HostPhaseKind` medians inside `chip.run.bind`, plus `chip.run` | compare phase distributions across A/B/C/D; measure the critical path rather than equating a subtotal with elapsed time |
| **device queue gaps** | each run's **actual receipts** — the joined-launch records and whole-operator boundaries the children emit, the same evidence rows 2–3 already assert — against the next run's launch | `device_wall` per run flat across arms while the inter-run gap closes in **B** |
| **memory retention** | `test_workspace_budget_surface` / `device_memory_info` high-water, per-bind `minflt`/`hblkhd` | compare C vs B for retained-capacity effects and D vs C for actual third-run occupancy; report observed rather than assumed allocation |

Attribution is only valid if the arms are read against receipts rather than wall-clock: B vs A
isolates launch overlap, C vs B holds launch depth fixed while changing resource capacity,
and D vs C holds resource capacity fixed while changing launch depth. Receipt and allocation
measurements are required; configuration alone does not prove actual occupancy or causality.

**Five traps, each already on the record `[source]`.**

1. **Discard the cold binds.** `host_orch` per-bind `minflt` runs 989, 983, then 114, 173, 54, 3,
   13, 13, 11, 8, reaching 0 by the sixth bind (page-faults entry, corrected 2026-08-26). Any mean
   over a 3–6-round run is mostly warm-up; discard ≥5 binds per arm.
2. **The `bind_phases` "control plane" subtotal is `host_orch + graph_pack + arena_h2d` — a sum of
   measured costs, not an interval.** Graph task binding, a5's resident scheduler preparation and
   the inter-segment gaps are outside it. *"The < 1 ms target for the complete control plane cannot
   be accepted from this subtotal alone."*
3. **Batch, not invocation, is the reporting unit.** Pooling invocations is pseudo-replication —
   the error `2026-09-prepare-execution-h2d-measurement.md` records and retracts. Its ABBA over
   `runtime_device_copy_size()` (51,584 vs 17,536 B, 400 rounds × 4 batches) found the between-arm
   difference **smaller than the spread between two batches of the same arm**, so a byte delta of
   that size is below this harness's resolution.
4. **Serialize and record occupancy** in all four arms.
5. **Reduced bytes or allocations ≠ reduced latency** (§G8). This plan yields counts and per-phase
   distributions. **No causal or performance claim is made here, and none may be derived from
   source**; execution is not authorized by this assignment.

### 5.3 Does any evidence warrant an upload or allocator product change?

**No — stated explicitly.** `[inference]` Three reasons from current main:

1. The upload target I previously quantified **no longer exists** (`#2487`, §0.1). A warm run's
   device uploads are its case-3 caller inputs, one arena+SM H2D, and a5 HBG's scheduler-state
   prerequisite.
2. Every retained allocator surface on the warm path is already grow-only and per-slot, with
   `setup_static_arena` short-circuiting (§4.2) and growth behaviour covered by row 6a.
   `MemoryAllocator` has no cumulative counters, so an "allocations fell" claim is not measurable
   today without adding counters first.
3. The one remaining candidate is blocked on a **correctness** question (`sched.capacities` is
   per-run), not a measurement — and the closest prior measurement of this harness could not
   resolve a 34 KB upload delta.

So no upload or allocator product change is warranted by present evidence, and no capacity-constant
or byte-shaving PR is proposed.

---

## 6. Overlap re-check and boundaries

`[source]` Re-verified for this revision at main `600540064` (unchanged): `#2511` → `259481f3`
(§1.1), `#2557` → `26d8abc3` (§1.2), both OPEN and both the original author's. `#2446` (device
result without a host round trip) and `#2487` are merged and in the baseline. `#2531` is merged and
inventoried, not duplicated. Nothing here overlaps `#2493`/`#2494`, `#2533`, `#2536`,
`#2538`/`#2540`, `#2545`, `#2532` or `#2546`.

A3 TMR design is P4's (§3 is its acceptance half only); DFX collector/host-output lifetime is
temporal's; `#2511` and `#2557` retain their author. No `PTO_`/`PTO2` identifier introduced.
Capture is a future constraint only. No product change, no new env var, macro, injection seam or
runtime helper probe is proposed — including for §2.1's residual sliver, which is explicitly left
to the complete feature that owns launch and recovery.

**Carried for an owner, not acted on:** the stale `Worker.submit` docstring (§0.2); the undetermined
device-8 bootstrap 507018 (§1); `.docs/hbg-definition-registration-reuse-decision.md` §4.2/§4.3 are
void against current main (§0.1) and must not be read as a live contract.
