> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# Resource-driven admission for ordinary `Worker.submit` — complete design (r21)

Design only. No product or test edit, no build, no test, no device operation, no git mutation, no publication, no external comment. Implementation is not authorized, and **no approval is requested**: the mechanisms below are stated so they can be checked against source, not ratified.

**Main rechecked: `cc6c4d51f79e98af5ef07829e3ca71c36f752a43`, unchanged.** Open PRs: **33**; `#2593` is now `25be88ec` and own `#2588` `d5911fa0`. Down from 35 earlier — **`#2591` and `#2028` were CLOSED without merging** (2026-10-15T12:36:13Z and 12:38:22Z, `mergedAt: null`, `mergeCommit: null`), which changes one of this design's premises (§0, finding 4). `#2588` is at `d5911fa0` (own DFX writer, active; its diff is PMU/diagnostic collection and launch gating — it touches the lane but implements none of this admission); every other head is unchanged. None implements automatic admission; none touches the arena transaction, the workspace ledger, the lane's admission paths or the retained-temp path (§15). (S) = source fact at that head. Nothing was executed; every byte figure is illustrative arithmetic.

---

## 0. Resolved findings

| # | Finding | Resolution |
|---|---|---|
| **1** | **Pure DEVICE TMR never grows admission.** `packed_temp_bytes` counts only `transfer == TensorTransfer::H2D` tensors with `nbytes() != 0` (`temp_buffer_plan.h:43-53`), so an all-DEVICE run has `required == 0`, `RetainedTempBump::begin` grows nothing (`retained_temp_bump.h:76-92`), and r18's staging-driven `slot_materialized_` left TMR's `proven(0)` false forever ⇒ capacity pinned at 1 | **§8.1 rewritten.** The proof is now a **completed phase C**, not held bytes: `slot_proven_[s]` is set when a run on slot `s` commits successfully — *including when its private byte demand is legitimately zero* — and cleared by trim, quarantine, teardown or loss of a shared prerequisite. Never-prepared stays false, so nothing is vacuous. New traces §12.2a (all-DEVICE TMR to multiple runs), §12.2b (HOST progression on HBG), §12.2c (mixed-input change) |
| **2** | **Sim route contradiction** — §1 said unconfigured sim admission changes, §2.3 said sim never enters automatic mode | **Automatic mode is onboard-only.** §1 and §2.3 now say the same thing: the parent does not request it for a simulated platform, and sim keeps today's behaviour in full — no token, no park, no trim, no derived capacity, no admission change. The earlier "sim admission changes" claim is withdrawn |
| **3** | **Explicit-budget contradiction** — §2.4 had explicit budgets following the new park logic while §2 excluded them from the route and promised them unchanged | **Excluded means excluded.** §2.4 now states that an explicit `workspace_budget_bytes` context is not in automatic mode: it gets no phase-S token, no park, no trim and no derived capacity, and its `OverQuota` refusal stays the immediate failure it is today |
| **4** | **`D` arithmetic described dishonestly** — the formula subtracted `MAILBOX_REQUEST_FRAMES` while the prose claimed "no transport-frame term" | **§3 corrected.** The `+K` entries are the entry table's own **hand-off reservation** — *"the entry exists from the moment the parent publishes, and the run it names waits for a set without holding a 64 KiB frame to do it"* (`worker_manager.h:120-127`) — sized by `MAILBOX_REQUEST_FRAMES` because that is how many hand-offs can be outstanding. They are table entries at 384 B each, not 64 KiB request frames. The byte budget is on the table; solving it for `granted` subtracts the reservation |
| **5** | **`#2591` closed unmerged** | Its control/admission lifetime split is **not in main** and is no longer an in-flight external PR, so this design no longer defers to it. §10.4 and §14 now describe main's actual behaviour: a lane poison breaks the child's serve loop and the `finally` closes the lane, which reaches every parked plan through `drain_front`'s QUEUED branches. Hook coverage is unchanged; only the premise is |

Retained from r17/r18 with their mechanisms restated here rather than referenced: phase S bringing retained HOST staging under the growth token (§4.2), token continuity and self-recognition on resume (§4.4–4.5), transaction-local claim rollback (§6), the two cleanup paths and the typed wait/fatal taxonomy (§7), the phase-sensitive parked-plan abort hook on every terminal path (§10).

### 0.1 r19 and r20 rounds — six further corrections

| # | Finding | Resolution |
|---|---|---|
| **6** | **Capacity progression did not follow the real retirement and slot-acquisition mechanics.** The fold publishes capacity *and* releases that run's lease in the same critical section (`orchestrator.cpp:227-255`), and `try_acquire` returns the **first free** slot — so after R1 retires the next run takes **slot 0**, not slot 1, and a serial chain never reaches a second slot at all | **§12.2a rewritten in two parts.** Part 1 shows steady serial reuse: slot 0 is reused indefinitely and capacity stays at **2** (one working slot plus one slot of exploratory headroom) — the earlier claim that it climbs on every serial run is withdrawn. Part 2 shows the concurrent ratchet: a second outstanding run takes slot 1, proves it, and the *next* retirement publishes 3. §12.2b gets the same treatment. The commit-to-visibility lag is stated explicitly: `slot_proven_` is set at phase C, but the parent only learns at the whole-run fold |
| **7** | **"No allocation" was unscoped.** §3 already noted that fence and caller-order events are created lazily per set, so "no allocator calls / never allocates after the first run" was false for a new set | **§11.1 added.** Every zero-allocation statement is now scoped to **ledger-managed bytes** — the three arena regions and the per-slot staging. The separately fallible per-set resources are named with their creation points and failure policy: the fence's two and caller-order's three events (created at **launch**, in `arm` — *"creating its events can fail, and here that failure costs the run nothing"*, a2a3 `device_runner.cpp:752-759`), `runtime_bufs_[slot]`, and the per-slot host scratch. None is ledger-backed, so none is parkable: each is a fatal prepare or a rolled-back launch failure. A launch-time event failure on a new set **clears** `slot_proven_`, so capacity stops offering a slot that cannot carry a run |
| **8** | **`releasable >= shortfall` was a sufficiency test used as an admission test.** With request 40, free 30 and a launched holder of 20, the allocator refuses 40 now but releasing 20 would let it succeed — the rule fatally refused a satisfiable request | **§9.1 added.** The condition is now existential — `park_admissible(e)` holds while a non-excluded launched holder set exists that the plan has not already waited through — because *free + release* is what matters and free is the quantity this design deliberately never queries. The bound is the holder set captured at the first park: each park consumes one real retirement and no holder is waited for twice. (The cardinality bound stated in this row was itself corrected by finding 9 below: it is `D − 1`, not `admission_depth_ − 1`.) The worked 40/30/20 example and the stated limitation (a plan may wait through every holder and still fail) are both in §9.1 |
| **9** | **The holder-set state machine was not a fixed budget.** The pseudocode reassigned `pending_holders` from live holders on each park, never removed the just-retired identity, and bounded the waits by `admission_depth_ − 1` — which §8.3 explicitly allows grandfathered runs to exceed | **§9.1 rewritten.** An **immutable** `holder_snapshot` captured once per phase-park-episode, a monotone per-plan `consumed` set, `pending = snapshot − consumed`, resume **only** on an identity drawn from the snapshot, and the bound stated as the captured cardinality ≤ `D − 1` — the fixed envelope, not the shrinkable depth. Scope stated: one snapshot per phase, `consumed` spanning both, so parks per plan ≤ `2(D − 1)`. Four regression cases tabulated (two holders retiring separately, unrelated retirement, shrink while parked, exhaustion with no new holder appended) |
| **10** | **The attempt bound contradicted itself.** §§13–15 said "at most two allocator attempts per growth phase" while §9.1 re-ran the two-attempt sequence after each retirement | **§9.3 added**, separating three units that were conflated: a **backend allocator call** (≤ 3 per arena transaction attempt, 0 for a reuse), a **transaction attempt** (2 per activation), and an **activation** (≤ `1 + |snapshot|` ≤ `D` per phase). Totals: ≤ `2D` transaction attempts per phase, ≤ `4D` per plan, ≤ `8D` backend calls per plan. The "no allocation-retry loop" wording is replaced by the accurate statement — no timer, and every activation paid for by a distinct consumed retirement |
| **11** | **Cost claims were wrong in two ways and the exclusion list was self-defeating.** §14 claimed no new steady-state cost while the capacity capture, prefix scan, fold and records all run per run; the holder snapshot had no named owner; a launch-event failure was missing from the proof-clear table; and §9's "last consumer unproven" literally excluded every launched holder | **§14 costs rewritten** as two tables of added per-run CPU work and added host state, with the snapshot explicitly owned by `SlotPrepareState` and **no performance benefit claimed**. §8.1's clear-table gains the launch-event failure. **§9.2 added**, distinguishing *running with a known retirement path* (eligible — the normal releaser) from *quarantined / release-unconfirmed / retained-mapping / unproven-stop-after-failure* (excluded) |
---

## 1. What the feature is

On a local level-3 Worker with nothing configured, `submit` admits against the run-resource sets the context actually has; a submission past current capacity parks before a run id exists and resumes when a run ahead of it retires; concurrency starts at one set and follows what the context has actually materialized. There is **no byte quota**: the allocator's own refusal is the only capacity fact used, and memory this ledger does not own is never modelled. `pipeline_depth`, `launch_depth` and `workspace_budget_bytes` keep today's behaviour at every value.

Still a proposal, not asked for here: making this the default for an unconfigured local L3 Worker, which changes observable enqueue order on **onboard** platforms. **A simulated platform is outside the mode entirely and keeps today's behaviour in full** (§2.3) — the r18 text claiming sim admission changes is withdrawn.

---

## 2. Mode routing (complete mechanism)

### 2.1 Why inference cannot select the mode

`#2589` put owned L2 on the shared helper: `_init_level2` calls `_initialize_chip_runtime(self._chip_worker, device_id, binaries, …, workspace_budget_bytes=workspace_budget)` directly (`worker.py:8853-8882`), while the public `ChipWorker` wrapper — kept on the forked chip-child path — calls the same helper from its own `init` (`task_interface.py:1674-1682`). `_init_level2` passes no `requested_pipeline_depth` and a budget that is **0 when the key is absent** (`:8788-8797`), and it is not a borrowed context. So "non-sim, non-borrowed, request 0, budget 0" selects ordinary L2 as well.

### 2.2 The route value

One keyword on that helper — `automatic_admission: bool = False` — beside `manage_workspace`, which is already a routing-only boolean on the same signature that the native side accepts and no longer decides anything (`chip_worker.cpp:535-540`). It is forwarded **only** by the public wrapper's `init`, and only because `_chip_process_loop` sets it; that fork-child entry already carries `pipeline_depth_request` (`worker.py:4160`) and `launch_depth` (`:4158`) as explicit COW context. **`_init_level2` never calls that wrapper**, so owned L2 is excluded by construction.

Parent-side authorization uses a predicate that already exists:

```
self._pipeline_depth_scope_refusal() is None          # worker.py:12903-12932
    ├─ self._topology_parent is None                  #   not nested under an L4 parent
    ├─ self._is_startup_root                          #   remote/MPI-hosted inner Workers get a
    │                                                 #   _startup_deadline, so this is False
    └─ int(self.level) == 3
and not self._pipeline_depth_requested_explicitly
and "launch_depth" not in self._config
and "workspace_budget_bytes" not in self._config      # already rejected on this route (:8911)
and the platform is not simulated                     # §2.3: automatic mode is onboard-only
```

### 2.3 Onboard-only, reaching `simpler_init`, and failing closed

**Automatic mode is onboard-only, and the parent is where that is decided.** The platform is known parent-side — `_validated_workspace_budget` already keys on it (`worker.py:8788`) — so a simulated platform simply never has the route value set. The reason is not a preference: every mechanism in §4–§9 is ledger-driven, and the sim platform has no `WorkspaceManager`, no `stage_workspace_management` and no `install_staged_workspace` anywhere under `src/common/platform/sim/`, while `ChipWorker::init` requests nothing on a simulated backend (`chip_worker.cpp:525-558`). With no ledger there is no commit to prove a slot with (§8.1), so a sim context in "automatic" mode would sit at capacity 1 forever — strictly worse than the fixed default it has today. So:

> **On a simulated platform nothing in this design applies**: no route value, no phase-S token, no park, no trim, no derived capacity, and no change to admission, enqueue order or observed concurrency. Device memory remains host `malloc` and an allocation failure remains `malloc` failure.

For an onboard automatic-mode child the route value reaches the native side through the existing staging mechanism:

| Step | Where | What |
|---|---|---|
| 1 | `ChipWorker::init`, inside the existing `!simulated && !borrowed_context_generation.has_value()` block (`chip_worker.cpp:539-558`) | when the route value is set and `workspace_budget_bytes == 0`, call a new internal staging entry resolved by `dlsym` beside `simpler_enable_workspace_management_ctx` and `set_workspace_budget_fn_` (`:318-322`) |
| 2 | `stage_workspace_management` (`c_api_shared.cpp:2405`, `:2414`) | records a new internal `Install::ManageWithAutomaticAdmission` beside `Nothing` / `ManageOnly` / `ManageWithLimit` (`workspace_staging.h:40-47`). **Staging only — nothing installed, nothing allocated** (`device_runner_base.cpp:675-681`) |
| 3 | `install_staged_workspace`, inside `run_context_install` (`c_api_shared.cpp:796-820`) | `configure(backend)` as today, and records the mode; cleared by `clear_staged_workspace` on the install failure path (`:805-808`) |

That `!simulated` guard at step 1 is a second, independent reason a sim context cannot enter the mode even if the route value reached it.

Failing closed, mirroring the tree's own rule for an explicitly requested budget (`chip_worker.cpp:542-550`): a missing staging symbol **throws**, a non-zero return **throws**, an install failure fails `simpler_init`. A child's init failure is published into the mailbox and flagged `_INIT_FAILED`, which the parent's readiness barrier reports as a bounded error (`worker.py:4195-4202`). So an endpoint either reaches INIT_READY in automatic mode or the whole startup fails — never half-automatic. Owned L2, nested, remote/MPI, explicit depth, explicit launch depth, explicit budget, kernel/borrowed and sim never set the route value, so the symbol is never required of them and its `dlsym` result is never examined.

### 2.4 No byte quota, and explicit budgets are genuinely excluded

The automatic mode installs **no** `set_limit`, and no code path consults a fraction of device capacity.

An explicit `workspace_budget_bytes` context is **not in automatic mode** — the route value is cleared by §2.2's predicate — so it receives **no phase-S token, no park, no trim and no derived capacity**. Its limit stages and latches exactly as today (`:542-550`), and an over-quota growth is refused by the ledger at `OverQuota` and fails the prepare immediately, which is today's behaviour unchanged. The r18 sentence saying such a context "then follows the same park/fail logic" is withdrawn: that would have been a behaviour change to a configuration this design promises not to touch.

---

## 3. Bounds

| Bound | Derivation | Nature |
|---|---|---|
| naming envelope `D` | a **byte budget on the entry table**: `table_bytes ≤ 64 KiB`. The table holds `mailbox_run_entry_count(granted) = granted + MAILBOX_REQUEST_FRAMES` entries of `MAILBOX_RUN_ENTRY_STRIDE` each (`worker_manager.h:120-127`, `:193-195`), so solving the budget for `granted` gives `D = floor(64 KiB / MAILBOX_RUN_ENTRY_STRIDE) − MAILBOX_REQUEST_FRAMES` — **168** at the current stride. The subtracted term is the table's own **hand-off reservation**, not a transport count: *"the entry exists from the moment the parent publishes, and the run it names waits for a set without holding a 64 KiB frame to do it."* Those spare entries are 384 B table rows, and they are dimensioned by `MAILBOX_REQUEST_FRAMES` because that is how many hand-offs can be outstanding at once — the 64 KiB request frames themselves are a separate, fixed region and are not in this budget. An explicit `pipeline_depth=n` sets `D = n` | fixed before the fork |
| tables sized by `D` | mailbox entry table; `RunResourceArena::regrant(D)`; the child's ~25 `RunResourceSlots` tables; `WorkspaceManager::build(D)`; retained-record threshold | fixed before the fork |
| retained records | refuse at `terminal_unreleased() >= D`; `runs_.size() ≤ 2D − 1` for every history | fixed |
| host per **used** slot | `get_runtime_size()` runtime storage; SM mirror `sm_size`; host arena `layout.arena_size`; graph-definition staging; run-image staging; `#2566`'s `OrchestrationScratch` | **count** ≤ `D` and only materialized slots allocate; each **size** is the workload's graph, which this design does not bound and does not claim to |
| device bytes | **no quota** | bounded by what the allocator grants |

Every per-set device resource is lazy: arena regions at the first `setup_static_arena`, fence and caller-order events in `arm` — *"committing both events on first use"* (`run_completion_fence.h:156-181`, `run_caller_order.h:33-52`) — and the slot's retained temp at the first grow.

---

## 4. The complete prepare path

### 4.1 Phases

| Phase | Work | First device allocation? |
|---|---|---|
| **S — pre-staging** | compute `required_temp_bytes` from the args; decide growth; acquire the token if needed; grow the slot's retained temp; **then** copy HOST inputs (H2D) | **yes — this is the earliest one** |
| **P — plan** | build the graph into per-slot retained host storage; run the user's orchestration callback **once**; close `tensor_access`; derive the arena demand; stamp the plan | none new |
| **C — commit** | `arena_region_action` for every region; the staged transaction | yes |
| **B — bind** | acquire bases, rebase, assemble into run-image staging | none new |
| **U — publish** | upload | none new |

r17 placed the token at C and so left S unguarded. The corrected rule is one sentence: **the token is acquired at the first phase that needs growth — S or C — and held continuously until the prepare terminates.**

### 4.2 Phase S in detail

```
S0  required = packed_temp_bytes(args)                       // host-side, no allocation
    api->get_retained_temp_buffer(&raw, &size)               // reads the slot's current block
    wanted = required ? required + kAlignment - 1 : 0         // RetainedTempBump's own arithmetic
    needs_staging_growth = (wanted > size)
S1  if needs_staging_growth and this plan does not already hold the token:
        acquire the endpoint's growth token
        unavailable -> PARK ON TOKEN (phase S park), naming the holder
S2  if needs_staging_growth:
        one acquire_retained_temp(wanted) attempt, recorded as a claim (§6)
        failure -> rollback claims; trim idle sets; reclaim; ONE re-attempt
        second failure ->
            park_admissible(e)       -> PARK ON BYTES (phase S park), naming the holder set (§9.1)
            otherwise                -> typed fatal: RESOURCE_EXHAUSTED (§7)
S3  bump.begin succeeds against the now-sufficient block; copy HOST inputs (H2D)
```

`required_temp_bytes` is `packed_temp_bytes(orch_args)` (a2a3 HBG `runtime_maker.cpp:1322`), a pure host computation over the already-snapshotted args, and `get_retained_temp_buffer` only reads the slot's recorded address and size (`c_api_shared.cpp:299-307`). So **the growth need is decidable before any allocation**, which is what makes S1 possible at all. On a refusal the slot keeps the block it had — *"a refusal must leave the slot naming the block it already had"* (`retained_temp_bump.h:82-87`) — so a phase-S park leaves the slot's previous staging intact and reusable.

**Input lifetimes across a phase-S park.** The H2D copy has not happened, so the source bytes must stay valid. On the ordinary async path the child already owns them: `stage_request` *"verifies it, copies the payload out, and gives the frame back"* (`worker.py:3812`), so the HOST input bytes live in child memory before the prepare begins. On the direct L2 path the caller's buffer is in-process behind an outstanding run handle. A park therefore extends a window that already exists and does not create a new lifetime obligation for the caller.

### 4.3 Phase C in detail

```
C0  if needs_growth_arena(candidate) and this plan does not already hold the token:
        acquire; unavailable -> PARK ON TOKEN (phase C park)
    action(r) for every applicable region, from unmutated state
C1  every applicable region Keep  -> no allocation; proceed to B
C2  shortfall = Σ requested_r over Replace regions
    one staged transaction attempt
      success -> claims become references; proceed to B (token released at U)
      failure -> rollback_staged_claims()
C3  trim idle sets; reclaim_workspace_obsolete()  (frees trimmed sets AND C2's rollback)
    exactly ONE further transaction attempt; failure -> rollback
C4  park_admissible(e)  -> PARK ON BYTES (phase C park), keeping the token (§9.1)
    otherwise            -> typed fatal: RESOURCE_EXHAUSTED
```

`needs_growth_arena(candidate) = ∃ applicable region r : arena_region_action(r) == Replace`, computed from state the transaction has not mutated (`arena_replacement_transaction.h:108-116`). A region the candidate sizes at zero yields `Keep` by that function's own rule (`:110-113`) and is **applicable-but-satisfied** — this is what keeps HBG's zero-byte `GmSm` from being treated as a gap.

**Peak**: the transaction stages **all** `Replace` regions and only then publishes atomically (`:168-200`), and `release_arena_backing` returns early for a managed block, so every superseded `old_r` stays charged until `note_published` plus the **next** entry reclaim. Therefore `shortfall = Σ requested_r`, never `new − old`.

### 4.4 Token lifetime across the whole prepare

| Event | Token |
|---|---|
| S1 or C0 first detects a growth need | acquired, and **recorded on the slot's prepare state** |
| S → P → C transition | **retained**; C0 sees `holds_token(slot) == true` and does not re-acquire |
| park (on token or on bytes), at S or at C | **retained** if already held; a token-park by definition does not hold it |
| commit success, through B to U | released at U, after publication |
| typed fatal at S2/C4, or `Degraded` | released |
| `abort_plan` (§10) | released |

Holding it across S→P→C is what makes the peak single-plan: a candidate that grew its staging in S and then grows its arena in C is **one** growth attempt, not two, and no other candidate can interleave a second peak between them.

### 4.5 Resume must recognise its own token

A resumed plan re-enters at its park phase and **first** asks whether it already holds the token:

```
resume(plan):
    if plan.parked_on == Token:
        if token_free(e): acquire; plan.parked_on = None; continue at plan.phase
        else: stay parked (the holder is still the named releaser)
    else if plan.parked_on == Bytes:
        assert holds_token(slot)                 // it never released it
        plan.parked_on = None; continue at plan.phase   // S2 or C0, not S0 and never P
```

The assertion is the answer to "resume must not wait on itself": a bytes-parked plan is the token holder, so it must never consult `token_free`. Only a token-parked plan does, and a token-parked plan holds nothing to deadlock against. **Phase P is never re-entered**, so the user's orchestration callback runs exactly once per submission.

---

## 5. Prepare state per slot

```cpp
struct SlotPrepareState {
    enum class Phase : uint8_t { None, Staging, Planned, Committing };
    Phase    phase{Phase::None};
    uint64_t epoch{0};          // 0 ⇒ no live prepare on this slot
    uint64_t dispatch_id{0};
    bool     plan_stamped{false};   // phase P completed: mirror + host arena + PODs valid
    bool     holds_token{false};
    enum class Parked : uint8_t { No, OnToken, OnBytes } parked{Parked::No};
    uint64_t releaser_epoch{0};     // the named holder, for the refusal message
};
```

`phase` is what makes cancellation safe **before the plan stamp exists**: a park at S has `phase == Staging`, `plan_stamped == false`, and §10's hook keys on `epoch` rather than on the stamp. The stamp exists only from the end of P and is what prevents the next occupant of the slot from resuming someone else's plan.

---

## 6. Claim ownership and rollback

### 6.1 The defect being fixed

`abort_replacement` calls `free_(ctx_, staged_raw_)` (`device_arena.h:356-364`) → `arena_free_trampoline` → `release_arena_backing` → **`if (workspace_.owns(p)) return;`** (`device_runner_base.cpp:666-673`), a no-op for a managed block. `note_unpublished` clears only `current` and *"drops no run reference"* (`workspace_manager.h:407-433`), while `acquire` added the candidate's epoch (`:346`, `:1124`). So a staged block abandoned by the transaction stays charged **and** referenced: `reclaimable_locked` fails on `ref_count != 0` (`:1075-1079`) and `acquire`'s reuse scan skips it too. Without an explicit rollback, retries accumulate.

### 6.2 The record

`g_workspace_plan` is already the thread-local naming the region each workspace request belongs to (`device_runner_base.cpp:569-589`). It gains a fixed-capacity record — `kMaxArenaTransactionRegions + 1` entries, covering three arena regions plus one staging grow — appended by **every** acquire this prepare causes, in both S2 and C2:

```cpp
struct StagedClaim { void *base; WorkspaceManager::RegionKey region; uint64_t epoch; bool was_region_backing; };
```

`was_region_backing` is `base == arena->raw_backing()` at stage time; the runner can evaluate it because the transaction names each region immediately before staging it (`:1148-1153`) and the announce context carries the bank (`:1147`). For a staging claim it is false by construction (the slot's block is replaced, not re-handed).

### 6.3 Commit and rollback

| Outcome | Action |
|---|---|
| success | the claims **become** the live references (`reference_bank_arenas`, `:1178-1186`, for the arena; the slot's recorded block for staging). Record cleared **without** releasing |
| failure, park, or abort | `rollback_staged_claims()`: in reverse, `workspace_.release_ref(base, epoch)`, plus `workspace_.note_unpublished(base)` **only when `!was_region_backing`**. Record cleared |

- **Only new, unpublished claims are released.** Committed old blocks are untouched — the transaction left every arena as it found it (`arena_replacement_transaction.h:129-139`) — and `was_region_backing` suppresses the `current` clear in the one case where the allocator handed back the region's own live backing, which the staged guard already recognises (`:158-163`).
- **Never `drop_run_refs(epoch)`**, which would also drop the parked plan's retained-temp reference.
- **A rolled-back reuse is restored**: `acquire` sets `b.current = true` even on the reuse path (`:351`), so the rollback's `note_unpublished` puts it back.
- **The bytes come back in the same phase**: C3 and S2 run `reclaim_workspace_obsolete()` after the rollback and before the re-attempt, so a rolled-back block (now `ref 0`, `current false`) is freed and the re-attempt sees it.

A `false` return from either ledger call means the runner's record and the ledger disagree about ownership; it is treated as a **fatal prepare failure that poisons the lane**, never swallowed.

---

## 7. C API cleanup seams, and the error taxonomy

### 7.1 Why a park cannot use `cleanup_failed_prepare`

`cleanup_failed_prepare` notes `NoDeviceSubmission`, `CopybackReturned` and — when `release_run_bindings_impl` returned 0 — `BindingsReleased` (`c_api_shared.cpp:1336-1358`). Since `retired(r) = (drain_proved || no_submission) && copyback_returned && bindings_released` (`workspace_manager.h:875-878`), a successful cleanup **retires the epoch** and `drop_run_refs` releases every reference it held. That is exactly right for a fatal failure and exactly wrong for a park: it would drop the retained-temp reference the resumed plan still needs and would record "no device submission" for a run that is going to run.

### 7.2 The two cleanup paths

| | `cleanup_failed_prepare` (existing, fatal) | `park_prepare` (new) |
|---|---|---|
| RunFacts noted | `NoDeviceSubmission`, `CopybackReturned`, `BindingsReleased` ⇒ epoch retires ⇒ all refs dropped | **none** — the epoch stays live |
| bindings | `release_run_bindings_impl` | **kept**, so the resume does not re-record them |
| claims | implicitly recovered by `drop_run_refs` | `rollback_staged_claims()` explicitly |
| per-slot plan | discarded | **kept** (mirror, host arena, graph staging, PODs, stamp) |
| reservation / execution claim | released | released |
| token | released | **kept** if the plan holds it |
| native-run entry | dropped by `prepare_native_run_on_slot`'s `rc != 0` path | same |

Both run on the prepare thread with the execution claim still held, and both release it before returning.

### 7.3 Typed wait versus fatal

Two new internal rc values beside `PTO_RUNTIME_ERR_PREPARED_INCOMPATIBLE = PTO_RUNTIME_ERR_BASE - 2` (`runtime_c_api.h:111`) — neither is user-facing:

| Condition | rc | c_api cleanup | `ChipWorker` throws | Lane |
|---|---|---|---|---|
| growth refused, releaser named (S2 or C4) | `RESOURCE_PARKED` (`BASE - 3`) | `park_prepare` | `PreparedRunResourceParked` | mark parked, keep QUEUED, keep plan and token |
| growth refused, **no** releaser; or request impossible | `RESOURCE_EXHAUSTED` (`BASE - 4`) | `cleanup_failed_prepare` | `PreparedRunResourceExhausted` | fail the run with the typed message |
| shared-bank layout mismatch | `PREPARED_INCOMPATIBLE` | `cleanup_failed_prepare` | `PreparedRunIncompatible` | existing depth-one fallback |
| `Degraded`, ledger/record inconsistency, any other | `INTERNAL` | `cleanup_failed_prepare` | `std::runtime_error` | fail and poison |

`prepare_native_run_on_slot` already drops the native-run entry for **every** non-zero rc before throwing (`chip_worker.cpp:1060-1064`), so all four paths leave `native_runs_` clean — which is the proof §10 relies on.

---

## 8. Capacity: materialization, prefix, token, fold

### 8.1 The proof is a completed commit, not held bytes

Two wrong predicates have now been tried, and the second is why this section is rewritten:

- r17 inferred "proven" from non-zero cached sizes, which a **never-used** slot satisfies vacuously.
- r18 inferred it from provisioned storage (`slot_materialized_`), which a **pure DEVICE workload never acquires**: `packed_temp_bytes` counts only tensors whose `transfer == TensorTransfer::H2D` and whose `nbytes() != 0` (`temp_buffer_plan.h:43-53`), so an all-DEVICE-input run has `required == 0`, `RetainedTempBump::begin` performs no growth (`retained_temp_bump.h:76-92`), and the slot never holds a staging block. Under r18's TMR rule `proven(0)` would stay false forever and capacity would stay 1 for a sustained DEVICE chain — a normal supported input form.

Both failed for the same reason: they asked *what the slot holds* instead of *what the slot has demonstrated*. The predicate is therefore an observed event:

```cpp
// per slot, on the runner
bool slot_proven_[s];   // a run on slot s completed phase C successfully, and nothing has
                        // since invalidated the resources that commit established
```

| Set true | Cleared |
|---|---|
| phase C completes successfully for a run on slot `s` — every applicable region `Keep`-satisfied or published, and its staging demand satisfied (**including a demand of zero**) | the trim releases slot `s`'s private resources, or a shared prerequisite it depended on |
| | any block of slot `s`'s region keys becomes quarantined or release-unconfirmed |
| | **the first launch on slot `s` fails to create its lazily-armed events** (§11.1) — the slot could not in fact carry a run, so the proof is withdrawn |
| | context teardown, or a regrant |

This separates the three cases the review asked to distinguish:

| Slot state | `slot_proven_` | Why |
|---|---|---|
| never prepared | **false** | no commit has completed; nothing is inferred from the absence of bytes |
| commit completed with **zero private bytes** (all-DEVICE inputs; a declared region sized 0) | **true** | the demand was satisfied — a zero demand is satisfied by definition, which is a positive fact about a completed commit, not a vacuous one about an untouched slot |
| previously proven, then trimmed or quarantined | **false** | the resources the proof rested on are gone |

**What the proof does and does not claim.** It claims that a run has committed on this slot, so the slot is a legitimate admission target. It does **not** claim that any future candidate fits: a larger candidate still fails the per-candidate `Keep` check (§4.3), still takes the growth token, and still runs the two-attempt / park / typed-fatal path of §4.2 and §4.3. Being proven buys admission, never bytes.

**Shared-bank prerequisite.** Under a `DEVICE_SCRATCH` declaration every slot maps to bank 0, so part of what a commit established is shared:

```
declared arena is HOST_PER_RUN :  proven(s) = slot_proven_[s]
                                  (the bank is slot s's own, so its release or quarantine
                                   already clears slot_proven_[s])
declared arena is shared       :  proven(s) = slot_proven_[s] AND shared_prereq_ok
                                  shared_prereq_ok = bank 0 committed and usable
                                  (a trim or quarantine of bank 0 clears the prerequisite for
                                   every slot at once, without touching their private state)
```

A **growth** of the shared bank does not invalidate the proof: the transaction publishes the new generation atomically and the old one is superseded, so the prerequisite remains satisfied at the new size. A **release** of it does invalidate the proof, for every slot, which is correct because no slot can then host a run without allocation.

### 8.2 `proven_prefix`, because `try_acquire` scans a prefix

`try_acquire(admission_depth)` scans `slot = 0 … depth − 1` and returns the first free one (`run_resource_arena.h:95-111`). A *count* of sparse proven slots does not map onto that: with slots 0 and 2 proven and slot 1 trimmed, a count of 2 hands out slot **1**.

> **`proven_prefix(e)` = the largest `k` such that `proven(0) … proven(k−1)` all hold.**

The prefix property is maintained rather than assumed: the trim releases candidates in **descending** slot order with bank 0 last, so it clears the highest materialized index first. A quarantine on a middle slot caps the prefix at that index — the conservative answer, and the one that keeps the parent's prefix scan honest.

### 8.3 Capacity, fold and launch

```
affordable_sets(e) = clamp( proven_prefix(e) + (token_free(e) ? 1 : 0), 1, D )
admission_depth_   = clamp( min over endpoints with a sample of affordable_sets(e), 1, D )
pending_run_limit_ = admission_depth_
```

The `+1` is **derived**, not asserted: exactly one slot beyond the proven prefix is offered because exactly one growth attempt can be in flight, and that exclusivity is the token's (§8.4). At startup nothing is materialized, so `proven_prefix = 0` and capacity is `clamp(0 + 1, 1, D) = 1` — the one exploratory candidate, now actually produced by the predicate.

**Sample → fold → launch**, unchanged in mechanism and restated:

| Hop | Source |
|---|---|
| the child writes `affordable_sets(e)` into the run-state entry beside rc and text, **then** stores the terminal state — *"rc and text first, the terminal state last"* | `worker.py:3969-3976` |
| `LocalMailboxEndpoint::terminal_completion` reads it after the acquire load of that state, beside `entry->error`, tagged with `caps_.worker_id` | `worker_manager.cpp:1179-1195` |
| `on_complete_` → `scheduler_.worker_done` | `worker_manager.cpp:927`, `worker.cpp:173-175` |
| `Scheduler::worker_done`'s first statement resolves `TaskSlotState &s` from `completion.task_slot`, giving `s.run_id` — the capture point | `scheduler.cpp:147-148` |
| captures accumulate in the `RunState`; `decrement_run_tasks` → `finish_run_if_ready` only at `active_tasks == 0` | `orchestrator.cpp:1656`, `:703-716`, `:200-217` |
| `retire_terminal_run` folds under `runs_mu_`, stamped by `run_id`, then the existing `refresh_leases_locked` + `notify_all` + `ready_notify_cb_` | `orchestrator.cpp:227-255` |

The fold therefore never acts on partial information. A decrease grandfathers **admission and leases, never bytes**: `try_acquire(k)` grants no new lease above `k` while existing leases stay valid (`release` by slot id, `owns` by generation), and a grandfathered QUEUED run's later prepare runs S and C in full.

**Launch**: static cap `D` configured before `ChipWorker::init` (the only time it may be set — `configure_launch_depth` throws afterwards, `chip_worker.cpp:994-1000`); dynamic bound is the FIFO, which no admission may push past `pending_run_limit_`; capability gate is `permits_joined_launch` per pair. So `launch_window = supports_joined_native_launch() ? admission_depth_ : 1` **eventually**, with `launched_prefix ≤ min(launch_depth_, run_fifo_.size())` instantaneously — they differ exactly during the grandfathering window. An explicit `launch_depth=n` replaces the static cap.

### 8.4 The exclusive growth token

One per endpoint, acquired by **any** candidate whose S or C needs growth, held across S→P→C and across a park, released at U, at a typed fatal, or by `abort_plan`. It caps neither bytes nor concurrency — `Keep`-only candidates proceed in parallel with a holder — it orders *attempts*, which is what makes the §4.3 peak single-plan and an allocator refusal attributable to one shortfall. It also bounds the obligation inventory to **one** bytes-parked plan per endpoint.

**Acyclicity with three edge kinds.** Edges: *token-parked* → token holder; *bytes-parked* → runs launched on the same endpoint. A token holder is either progressing (bounded by at most two allocator calls per phase) or bytes-parked, discharged by an endpoint-local retirement (§9). Chains are *token-parked → holder → (commit | launched run)*, length ≤ 3, every head discharged. No cycle.

---

## 9. Releasable, and endpoint-local progress

```
releasable(e) = Σ over slots s held by a run LAUNCHED on e, over s's applicable region keys,
                of block bytes, excluding any block that is
                  · in an arena bank shared with another live run on e   (both TMR backends)
                  · quarantined or release_unconfirmed
                  · held by an epoch whose last consumer is unproven
                  · still host-mapped (mapping_retained)
                  · in the requesting run's own slot
park on bytes iff park_admissible(e)  -- see §9.1; NOT releasable >= shortfall
```

### 9.1 `park_admissible` — an immutable holder snapshot, consumed one retirement at a time

`releasable(e) >= shortfall` was a **sufficiency** condition, and using it as the admission test under-utilised capacity. The review's example is decisive: request **40**, current free **30**, one launched holder of **20**. The allocator refuses 40 now; releasing the holder's 20 makes free 50 and the next attempt succeeds — yet `20 < 40` would have declared it fatal. The error is comparing the shortfall against the *release* alone when what matters is *free + release*, and **free is exactly the quantity this design refuses to query**. So sufficiency cannot be decided in advance; the condition is existential and the bound must come from a captured set.

**State, and where it lives.** The snapshot and its consumption record live in `SlotPrepareState` (§5), one per slot: `holder_snapshot` is at most `D − 1` epoch identities (8 B each), `consumed` is a subset of it.

```
eligible_holders(e) = launched runs on e whose sets contribute a non-zero, non-excluded amount
                      to releasable(e)                     // §9's exclusion list, as corrected in §9.2

FIRST park within a phase (S or C):
    plan.holder_snapshot[phase] = eligible_holders(e)       // captured ONCE; IMMUTABLE thereafter
                                                            // never recomputed, never appended to
pending(plan, phase) = plan.holder_snapshot[phase] minus plan.consumed

park_admissible   <=>  pending(plan, phase) is non-empty

on an endpoint-local retirement of epoch h on e:
    if h in pending(plan, phase):
        plan.consumed += { h }                              // the identity is consumed HERE
        clear the park; re-activate this phase's sequence once
    else:
        ignore — h is not an eligible identity for this plan; the plan stays parked

re-activation refused and pending(plan, phase) now empty  ->  typed fatal RESOURCE_EXHAUSTED
```

Three things the r20 pseudocode got wrong and this fixes: the snapshot is **captured once and never reassigned** from live holders; the retired identity is **removed from `pending`** by being added to `consumed`, not merely recorded; and a resume happens **only for an identity drawn from the snapshot**, so an unrelated retirement does not wake the plan.

**Scope of the snapshot and the budget.** One snapshot **per phase-park-episode**, with `consumed` **per plan and monotone across phases**. A plan that parks in S, resumes, commits S and later parks in C takes a fresh snapshot for C — the holder population has genuinely changed by then — but it can never re-wait on an identity it already consumed in S. Hence:

```
parks per phase  <=  |holder_snapshot[phase]|  <=  D - 1
parks per plan   <=  |snapshot_S| + |snapshot_C|  <=  2 (D - 1)
```

**The bound is `D − 1`, not `admission_depth_ − 1`.** §8.3 grandfathers runs through a capacity decrease: a lease granted before the decrease stays valid, so the number of launched runs on an endpoint can exceed the current `admission_depth_`. The only quantity that cannot be exceeded is the fixed naming envelope `D`, because every lease comes from the `D`-sized `RunResourceArena` and at most one run occupies a slot. So the captured cardinality is bounded by `D − 1` (every other slot), and the snapshot's own size — not a live, shrinkable number — is what bounds the waits.

**Why it terminates, and why it is not a retry loop.** Every re-activation is caused by a *distinct consumed identity* drawn from a finite immutable set; there is no timer, no sleep, no poll, and no re-activation that is not paid for by a real retirement the progress owner performed. A front park still fails immediately: launched runs are a prefix, so a QUEUED front has an empty `eligible_holders(e)`.

**Worked example (request 40, free 30, holder 20).**

| step | event |
|---|---|
| 1 | shortfall 40; the transaction attempt refuses; the trim finds no idle set; the one post-trim attempt refuses |
| 2 | `eligible_holders(e) = {R7}` ⇒ **park**; `holder_snapshot[C] = {R7}`, `consumed = {}`, `pending = {R7}` |
| 3 | R7 retires endpoint-locally; `R7 ∈ pending` ⇒ `consumed = {R7}`; the trim converts its 20 ⇒ free 30 + 20 = 50 |
| 4 | re-activation: one attempt for 40 ⇒ **succeeds**. The `20 < 40` comparison never happens |

**Regression cases.**

| case | behaviour |
|---|---|
| **two named holders retiring separately** | snapshot `{H1, H2}`. H1 retires ⇒ consumed, re-activate; still refused ⇒ `pending = {H2}` non-empty ⇒ park again. H2 retires ⇒ consumed, re-activate. Two parks, two real retirements, then success or fatal with `pending` empty |
| **unrelated retirement** | an endpoint-local retirement of `H3 ∉ snapshot` does **not** clear the park and does **not** consume anything; the plan stays parked waiting for `pending` |
| **capacity shrinks while parked** | a fold lowers `admission_depth_`; the snapshot is unaffected because it was captured by identity, and grandfathered launched runs remain eligible. The bound was never `admission_depth_ − 1` |
| **exhaustion with no new holder appended** | after the last snapshot identity is consumed and the re-activation still refuses, a *newly* launched run is **not** appended — the snapshot is immutable — so the plan takes the typed fatal naming the shortfall, the consumed identities and their released bytes |

**Stated limitation.** The design cannot prove in advance that a release will be enough, because it will not query free capacity; it proves only that a named holder exists and that waiting for it is bounded. A plan may wait through its whole snapshot and still fail. That is a deliberate exchange of one unobservable number for a bounded, attributable sequence of real events.

### 9.2 "Unproven last consumer" — running is not unproven

The exclusion list in §9 said "held by an epoch whose last consumer is unproven", which read literally excludes **every** launched holder: a running run's consumers are by definition not yet proven finished. That is self-defeating, and the ledger already draws the distinction this needs:

| Holder state | Eligible? | Ledger evidence |
|---|---|---|
| **launched and running, with a known retirement path** — its lane will reach `finish` → `finalize_native_run` → `note_run_fact` → `drop_run_refs` | **yes** — this is the normal eligible holder | the run is in `native_runs_` in a launched phase, and its epoch holds ordinary references |
| **quarantined** — its context was destroyed before its facts completed, so no further fact can arrive (`quarantine_run_refs`, `workspace_manager.h:934-956`) | no | `BlockState::Quarantined` |
| **release-unconfirmed** — a platform free was attempted and failed | no | `BlockState::ReleaseUnconfirmed` |
| **failed or unknown-stop** — a lane poison or a failed `finalize_native_run`, where the device stop is unproven (§10.4) | no | the lane's poison, and the plan is failed rather than parked |

So the exclusion is **quarantined, release-unconfirmed, retained-mapping, or an epoch whose stop is unproven after a failure** — never "a run that has not finished yet", which is precisely what a releaser is.

### 9.3 The actual attempt bound

Three units were being conflated. Stated apart:

| Unit | Definition | Bound |
|---|---|---|
| **backend allocator call** | one `backend_.acquire(bytes)` inside `WorkspaceManager::publish_new_block` | ≤ 3 per arena transaction attempt (one per `Replace` region of the bank), ≤ 1 per staging attempt. A region `acquire` satisfies by **reuse** makes no backend call at all |
| **transaction attempt** | one `run_arena_replacement_transaction` over the bank's regions — staged together, published atomically — or one `acquire_retained_temp` for staging | **2 per activation**: one before the trim, one after it |
| **activation** | one entry into a phase's attempt sequence: the initial entry, plus one per consumed retirement | `1 + |holder_snapshot[phase]|` ≤ `1 + (D − 1)` = **`D`** per phase |

Honest totals, per plan:

```
transaction attempts, per phase   <=  2 * D
transaction attempts, per plan    <=  4 * D                       (phases S and C)
backend allocator calls, per plan <=  3*2*D (arena) + 1*2*D (staging)  =  8 * D
```

The earlier "at most two allocator attempts per growth phase" was true only of a *single activation*, and is corrected wherever it appeared. **There is still no unbounded retry and no timer**: `D` is fixed before the fork, the snapshot is immutable, and every activation beyond the first is paid for by a distinct consumed retirement. The common case is one activation and one or two transaction attempts; the `D`-scaled figure is the worst case a plan can reach before taking the typed fatal.

Byte release is **endpoint-local**: the lane's `progress → poll_native_run → finish

Byte release is **endpoint-local**: the lane's `progress → poll_native_run → finish → finalize_native_run → note_run_fact → drop_run_refs` runs inside one child, driven by that endpoint's device completion alone (`chip_run_lane.cpp:525-559`, `:255-277`), while the lease and the capacity fold are whole-run. **Retirement alone returns no bytes** — it makes the holder's set trim-eligible; the trim plus `reclaim_obsolete` performs the platform free.

A run parked at the FIFO **front** has `releasable == 0` by construction: launched runs are a prefix and `launch_front` launches nothing while the front is QUEUED (`:301-315`, `:420-441`). It takes the typed fatal. This is an intrinsic boundary, not a narrowing — at the front there are no other runs whose resources could be waited for.

---

## 10. Abort on every terminal path

### 10.1 What a parked plan owns

| Phase-S park | Phase-C park |
|---|---|
| lease, borrow, mailbox entry, token (if growth was needed), the slot's **previous** staging block (unchanged by the refusal), args in child memory | all of that, plus the retained-temp reference with HOST inputs copied in, the stamped per-slot plan (mirror, host arena, graph staging, PODs), and the recorded tensor leases the bind has made so far |
| **no** claim record, **no** native-run entry, **no** lane mutex / execution claim / reservation | same |

### 10.2 The hook

```
ChipRunLaneState::abandon_plan(run)                      // lane side, new
  1. proof: !run->crossed_launch_fence AND the runner holds NO native_runs_ entry for the epoch
  2. runner->abort_run_plan(slot, epoch)
  3. (existing) release_device_spans(run, proven_done = true)

DeviceRunnerBase::abort_run_plan(slot, epoch)            // runner side, new
  a. re-assert the proof; on violation return non-zero — never clean up silently
  b. rollback_staged_claims()                            // defensive; empty while parked
  c. if the prepare state records a retained-temp reference for this epoch:
         workspace_.release_ref(retained_temp_addrs_[slot], epoch)
  d. if plan_stamped: release_run_bindings_impl, then invalidate the stamp
  e. release the token if this plan holds it
  f. reset SlotPrepareState to {None, 0}
```

- **No-device-submission proof.** Every non-zero prepare rc drops the native-run entry before throwing (`chip_worker.cpp:1060-1064`), so absence of an entry for the epoch is the checkable statement that no launch transaction could have named this run. Step (c) is safe only because of it.
- **Phase-sensitive.** A phase-S park has no stamp and may have no retained-temp reference; (c) and (d) are conditioned on the recorded state, which is why `SlotPrepareState` keys on `epoch` rather than on the stamp. That is cancellation safety before the plan exists.
- **Idempotent.** A second call with `epoch == 0` or a different epoch is a no-op returning success, so every path may call it unconditionally.
- **Failure propagates.** A non-zero return makes the lane poison the run instead of proceeding to `release_device_spans`: a plan whose ownership cannot be unwound is exactly the case where handing back the borrow could expose pages the device may name.
- **Host buffers are not freed** — they are per-slot and grow-only; invalidating the stamp is what stops the next occupant from resuming a stale plan. Their bytes return only through the trim or teardown.

### 10.3 Call sites — all of them

`abandon_unlaunched`'s QUEUED branch (`chip_run_lane.cpp:137-142`); `ChipRun::abandon`'s else-branch (`:701-703`); `launch_front`'s poison QUEUED branch (`:292-298`); `drain_front`'s poison and not-activated QUEUED branches (`:564-577`); `prepare_successor_if_eligible`'s catch-all (`:499-505`); `ChipRunLane::submit`'s unwind (`:798-812`). Each already calls `release_device_spans`; `abandon_plan` runs before it. `ChipRun::wait_until` expiring does not terminate a run, so no deadline-specific path is invented — the caller's later `abandon` or `close` reaches the hook.

### 10.4 Unproven local terminal, and main's actual poison behaviour

When a run's lane-local `finalize_native_run` fails, `finish` records the error and poisons (`:255-277`). An unproven device stop means no byte statement on that endpoint is trustworthy, so **every parked plan on that endpoint is failed, not resumed** — the poison reaches them through the QUEUED branches, which route through `abandon_plan`. The ledger independently keeps or quarantines that epoch's references (`workspace_manager.h:934-956`), which also removes its blocks from `releasable` and clears `slot_proven_` for the affected slots; the failure of dependent parked plans is stated as a rule rather than left to emerge from the exclusions.

**Premise corrected: `#2591` is closed, not in flight.** It was closed without merging on 2026-10-15 (`mergedAt: null`, `mergeCommit: null`), so the control-service-survives-poison behaviour it proposed is **not in main** and this design must not assume it. Main's actual behaviour is the one the child loop implements today: a lane poison or a failed progress step sets `stop_progress` / `shutdown_message` and **breaks the serve loop**, after which the `finally` runs `_close_chip_run_lane` (`worker.py:4086-4102`), i.e. `ChipRunLane::close` → the `drain_front` loop → for a parked plan, `drain_front`'s QUEUED branch. So a parked plan is reached by `abandon_plan` on that path too, and §10.3's call-site list already covers it. The design neither depends on nor duplicates the withdrawn fix; if an equivalent change lands later, the only effect here is that the parked plan would be reached by `stop_admission`'s `abandon_unlaunched` instead of by the close drain — both of which are in the list.

---

## 11. The four backend paths, and how each reaches concurrency

`packed_temp_bytes` counts only `H2D` tensors with non-zero bytes (`temp_buffer_plan.h:43-53`), so **phase S does nothing at all for an all-DEVICE run on any backend**. That is why §8.1's proof is a completed commit rather than provisioned staging.

### 11.1 What "no allocation" means, and what is still fallible

Every zero-allocation statement in this design is scoped to **ledger-managed bytes only** — the three arena regions and the per-slot retained staging, which are the four region families `WorkspaceManager` accounts (`coverage_is_partial` is always 1). A **new set** still costs resources outside that accounting, and each is separately fallible:

| Per-set resource | Created | Failure | Policy |
|---|---|---|---|
| `RunCompletionFence`'s two events | at **launch**, in `arm` — *"creating its events can fail, and here that failure costs the run nothing"* (a2a3 `device_runner.cpp:752-759`, a5 `:561`; `run_completion_fence.h:165-181`) | `ops_.create` rc | **launch failure**, rolled back by the existing launch transaction before any execution-visible submission. Not parkable: there is no `releasable` for an event |
| `RunCallerOrder`'s three events | at launch, in `arm` (`run_caller_order.h:38-52`), only for a caller-stream run | same | same |
| `runtime_bufs_[slot]` host storage, `get_runtime_size()` | first use of the slot (§3) | `std::bad_alloc` | **fatal prepare failure** (`INTERNAL`), before phase S |
| SM mirror, host arena, graph-definition staging, run-image staging, `#2566`'s `OrchestrationScratch` | during the bind, via `acquire_sm_mirror` / `acquire_graph_staging` / `acquire_run_image_staging` | non-zero rc or throw | **fatal prepare failure**; not ledger-backed, so no trim and no park |

Two consequences the design must carry rather than gloss:

- **`slot_proven_` proves ledger bytes, not launchability.** It is set when phase C completes, and the set's events are created later, at launch. If that creation fails on a new set, the launch transaction rolls the run back and `slot_proven_` for that slot is **cleared**, so capacity stops offering a slot that could not actually carry a run.
- **The four-region accounting is not a proof about device or host capacity.** It bounds nothing outside itself; a context can still fail on an event, a host buffer, or any allocation this ledger does not own, and those failures are fatal rather than parkable, by design — a wait needs a named releaser, and these resources have none.

So the honest form of the TMR claim is: *after the first commit, a further set costs **no ledger bytes** — not that it costs nothing.*

### 11.2 The matrix

| Backend | Arena declaration | Phase S demand | Phase C demand | How a new slot becomes proven |
|---|---|---|---|---|
| a2a3 HBG | `GM_HEAP` + `RUNTIME_IMAGE` = `HOST_PER_RUN`; `GmSm` sized 0 | `packed_temp_bytes`; **0 for an all-DEVICE run** | its **own** bank; `heap_bytes` + `device_arena_bytes` known only after the callback | by a real arena growth: the new bank is uncommitted ⇒ `Replace` ⇒ token ⇒ allocation ⇒ commit. Each set genuinely costs arena bytes |
| a5 HBG | same | same | same | same |
| a2a3 TMR | all three `DEVICE_SCRATCH` ⇒ **bank 0 for every slot** | same | sizes from `derive_arena_static_sizes`, known **before** any build (`runtime_maker.cpp:645-647`) | with **no ledger allocation**: bank 0 is already committed at unchanged sizes ⇒ every region `Keep`; an all-DEVICE run needs no staging ⇒ phase C completes. The set's events and host buffers are still created and can still fail (§11.1) |
| a5 TMR | same | same | same | same |

Park relevance follows: HBG can park on staging **or** arena; TMR can park only on staging, because another run's arena blocks are excluded by the shared-bank rule (§9) and a growing successor is declined pre-bind by the compatibility probe (`c_api_shared.cpp:1534-1548`) and prepares at the front after the predecessor's fence. An all-DEVICE TMR chain therefore never parks and takes **no further ledger bytes** after the first run; it admits more runs as slots prove, which is what the sustained-DEVICE case needs.

---

## 12. Traces

Illustrative arithmetic. `free` is device memory available beyond what this context holds.

### 12.1 E1 — cold start is 1, on every backend

No run has committed anywhere, so `slot_proven_[s] == false` for every `s`, `proven_prefix = 0`, the token is free, and `affordable_sets = clamp(0 + 1, 1, D) = **1**`. The predicate produces 1 because nothing has been *demonstrated*, not because nothing is *held* — a never-prepared slot is false under §8.1 regardless of what bytes exist. The first run is admitted on slot 0; when its phase C completes, `slot_proven_[0]` becomes true and the next whole-run fold reports `clamp(1 + 1, 1, D) = 2`.

### 12.2 E2 — trim to empty, and a quarantine in the middle

Slots 0–2 proven. A growth trims slots 2 then 1 descending; each release clears that slot's `slot_proven_`. `proven_prefix` falls 3 → 2 → 1, and a trim of slot 0 takes it to 0 with capacity back to 1. A **quarantine** on slot 1 instead clears only slot 1, so `proven_prefix` is capped at **1** while `slot_proven_[2]` stays true — correct, because `try_acquire` scans a prefix and would otherwise hand out the quarantined slot 1.

### 12.2a E2a — all-DEVICE TMR: serial reuse, then concurrent ratchet

a2a3 TMR, every input a DEVICE tensor, so `packed_temp_bytes == 0` on every run and no staging is ever provisioned. Two mechanics govern the trace and r19's version violated both: **capacity changes only at the whole-run fold**, which is also where that run's lease is released (`orchestrator.cpp:227-255`), and `try_acquire` returns the **first free** slot (`run_resource_arena.h:95-111`).

**Part 1 — steady serial reuse.** Capacity does not climb on a serial chain, and should not.

| step | event | state |
|---|---|---|
| 0 | cold start | `slot_proven_ = {F,…}`, `proven_prefix = 0`, `admission_depth_ = clamp(0+1,1,D) = **1**` |
| 1 | **R1** admitted; `try_acquire(1)` can only return **slot 0**. Phase S: `required == 0` ⇒ no growth, no token. Phase C: bank 0 uncommitted ⇒ `Replace` ⇒ token ⇒ `ensure_static_arenas` commits the three regions ⇒ commit completes ⇒ token released at U | `slot_proven_[0] = true` — **but the parent still reports 1**: the sample travels with R1's completion and is folded at its retirement, not at phase C |
| 2 | R1's tasks all complete ⇒ `active_tasks == 0` ⇒ `retire_terminal_run`: the fold publishes `proven_prefix(1) + token(1) = **2**` **and** releases R1's lease on slot 0 in the same critical section | capacity 2, slot 0 free |
| 3 | **R2** admitted. `try_acquire(2)` ⇒ first free is **slot 0**, not slot 1. Phase S nothing; phase C every region `Keep` ⇒ no token, **no ledger allocation** ⇒ commit | `slot_proven_[0]` already true ⇒ the fold at R2's retirement reports **2** again |
| 4 | R3, R4, … serially | each reuses slot 0; capacity stays at **2** — one working slot plus one slot of exploratory headroom. **Capacity does not grow on every serial run**, and nothing needs it to |

**Part 2 — sustained concurrent admission.** Growth happens only when demand actually puts a run on a new slot.

| step | event | state |
|---|---|---|
| 5 | with capacity 2, **R5** is admitted on slot 0 and is still outstanding when **R6** is submitted. `run_fifo_.size() (1) < 2` ⇒ R6 admitted; `try_acquire(2)` ⇒ slot 0 busy ⇒ **slot 1** | two runs concurrent |
| 6 | R6's phase C on slot 1: bank 0 already committed at the same sizes ⇒ every region `Keep`; staging demand 0 ⇒ **no token, no ledger allocation**. Its per-set *events* are created later, at launch (§11.1) | `slot_proven_[1] = true`, still invisible to the parent |
| 7 | R5 retires ⇒ its fold publishes `proven_prefix(2) + 1 = **3**` and frees slot 0 | capacity 3 |
| 8 | three outstanding runs ⇒ slots 0, 1, 2 ⇒ slot 2 proves ⇒ the next retirement publishes **4** … up to `D` | the ratchet is driven by *concurrency demand*, one new slot per sustained wave, and each step's growth becomes visible only at the following whole-run retirement |
| — | invariant | **no ledger allocation was manufactured.** The only ledger bytes ever taken are bank 0's one-time commit, which extra sets do not multiply. Under r18's staging-driven rule this workload reported 1 forever |

### 12.2b E2b — HOST progression on HBG

a2a3 HBG, HOST inputs present. Same two mechanics; the difference is that each new slot costs real arena bytes.

| step | event | state |
|---|---|---|
| 1 | **R1** on slot 0: phase S grows slot 0's staging (token acquired), phase C commits bank 0 under the **same** token, commit completes | `slot_proven_[0] = true`; the parent learns 2 at R1's retirement, which also frees slot 0 |
| 2 | a serial successor reuses **slot 0** and allocates nothing; capacity stays 2 | steady state |
| 3 | two outstanding runs ⇒ the second takes **slot 1** ⇒ phase S grows slot 1's staging and phase C finds bank 1 **uncommitted** ⇒ `Replace` ⇒ token ⇒ real allocations ⇒ commit | `slot_proven_[1] = true` ⇒ the next retirement publishes 3 |
| 4 | an allocation refusal at either phase | the two-attempt / trim / park / typed-fatal path (§4.2–4.3); `slot_proven_` is **not** set, so capacity does not rise on a failed probe |

### 12.2c E2c — mixed input change: a proven slot still crosses every check

Slot 1 was proven by an all-DEVICE run (zero private bytes). The next candidate on slot 1 has HOST inputs needing `wanted = 40`.

| step | event |
|---|---|
| S0 | `get_retained_temp_buffer` ⇒ size 0; `40 > 0` ⇒ `needs_staging_growth` **true** — being proven bought admission, not bytes |
| S1 | the token is acquired (or the candidate parks on it) exactly as for any growth |
| S2 | one attempt, then trim + reclaim + one re-attempt, then park on a named launched holder or the typed fatal |
| — | `slot_proven_[1]` stays true throughout; a failure does not clear it, because the resources the earlier commit established are untouched by a refused growth (`retained_temp_bump.h:82-87`: a refusal leaves the slot naming the block it already had) |

### 12.3 E3 — HOST staging refusal, named holder, resume (the gap-A case)

State: slot 0's staging 20, **referenced by R7 launched on `e`**; slot 1's staging 10, `ref 0`; slot 2 fully idle with staging 30. `free = 0`. R8 is admitted on slot 1; its args need `wanted = 40`.

| step | event |
|---|---|
| S0 | `get_retained_temp_buffer` ⇒ size 10; `wanted 40 > 10` ⇒ `needs_staging_growth` — **decided with no allocation** |
| S1 | token free ⇒ R8 acquires it; `SlotPrepareState{Staging, epoch_R8, holds_token = true}` |
| S2a | `acquire_retained_temp(40)` ⇒ ledger finds no same-key block ≥ 40 ⇒ `publish_new_block(40)` ⇒ `backend_.acquire` with `free = 0` ⇒ **nullptr**. `begin` would have returned false and today's code would return INTERNAL; instead the claim record is rolled back (nothing was published, so it is empty) |
| S2b | trim: slot 2 is fully idle ⇒ released ⇒ **30 returned**, `free = 30`; `slot_proven_[2] = false`; `reclaim` runs; one re-attempt for 40 ⇒ **fails** (30 < 40) |
| S2c | `holders(e) = {R7}` — R7 is launched and holds slot 0's staging and, on HBG, its arena regions; the amount is **not** compared against `wanted` (§9.1) ⇒ **PARK ON BYTES at phase S**, token retained, `pending_holders = {R7}`. The slot still names its old 10-byte block, untouched |
| resume | the same `run_staged_frame_loop` round drops the lane mutex and drives R7's `chip_run.done()` ⇒ `finish(R7)` ⇒ `finalize_native_run` ⇒ `drop_run_refs` — endpoint-local; R7's blocks are now `ref 0` but still `current`, so **no bytes yet**. The loop clears R8's park |
| S2d | R8 re-enters **S2** (not S0, and never P): `assert holds_token` ⇒ it does not consult `token_free`. One attempt ⇒ still 30 free ⇒ fails ⇒ trim: slot 0 is now idle ⇒ released ⇒ **40 returned**, `free = 70` ⇒ one re-attempt for 40 ⇒ **succeeds** |
| S3 | HOST inputs copied H2D into the new 40-byte block. `slot_proven_[1]` already true; the slot is re-proven at the new size when this phase C completes |
| P, C | the callback runs **once**; the arena commit follows with the token **still held**; at U the token is released |

### 12.4 E4 — close while waiting at phase S

R8 is parked at S (no plan stamp, retained-temp reference **not yet** taken because the growth never succeeded, token held).

`close()` → `stop_admission` → `abandon_unlaunched` → QUEUED branch → `abandon_plan`: the proof holds (no launch fence, no native entry) → claim record empty → step (c) skipped, the prepare state records no retained-temp reference → step (d) skipped, `plan_stamped == false` → **token released** → state reset → `release_device_spans(R8, true)` → TERMINAL, erased. The slot keeps its original 10-byte staging block, now unreferenced and trim-eligible. The whole-run path then releases the lease. A second call (close after abandon) is a no-op.

### 12.5 E5 — region A allocates, region B fails, twice, with a failed release

Bank 1 has GmHeap (`A`) and RuntimePool (`B`); `free = 30`; plan asks `A = 30`, `B = 20`, both `Replace`.

1. C2 attempt 1 stages `A` ⇒ a new 30-byte block is published, ref = candidate, `free = 0`; claim recorded. Staging `B` ⇒ `backend_.acquire(20)` ⇒ nullptr ⇒ `failed_region = 1`.
2. `staged_guard` ⇒ `StageAborted(A30)` ⇒ `note_unpublished` ⇒ `current = false`; `abort_replacement` ⇒ `free_` ⇒ **no-op** for a managed block ⇒ **A30 obsolete but `ref 1`** — the leak.
3. `rollback_staged_claims()` ⇒ `release_ref(A30)` ⇒ `ref 0`; `!was_region_backing` ⇒ `note_unpublished` ⇒ **reclaimable**.
4. C3 ⇒ no idle slot ⇒ `reclaim_workspace_obsolete()` frees A30 ⇒ `free = 30` again. One re-attempt repeats the same shape and is rolled back identically — **one** obsolete generation at any instant, never two.
5. C4 ⇒ `holders(e)` empty ⇒ `RESOURCE_EXHAUSTED` ⇒ `cleanup_failed_prepare` retires the epoch, dropping the retained-temp reference too, which is correct for a fatal.
5′. **Failed-release variant**: step 4's release returns `StillMapped` ⇒ A30 quarantined, `degrade_locked` latched, charge kept ⇒ the re-attempt returns `Degraded` ⇒ `INTERNAL` fatal; `slot_proven_` clears for that slot and `proven_prefix` drops.

### 12.6 E6 — two larger candidates, token serialization

Slots 0 and 1 proven at staging 10 and arena 20. `X` (slot 0) and `Y` (slot 1) both need 40 of staging. `X` takes the token at S1. `Y` reaches S1, finds it held, and **parks on the token** naming `X` — `Y` has no claim, no plan and no token. `X` proceeds S→P→C holding the token; if it bytes-parks at C4 it keeps it; when `X` reaches U the token is released and `Y`'s park clears. `Y` then re-enters S1, acquires the token, and evaluates against the state `X` left. At no instant were two growth attempts in flight or two peaks superposed.

---

## 13. Acceptance

Deterministic cases use a counting backend, a stubbed allocator with a settable `free`, a counting orchestration callback, a ledger inspector and a token inspector. cpput links no CANN translation unit. **Nothing has been run; no result is claimed.**

| # | Case | Asserts |
|---|---|---|
| E1 | **cold start predicate** | with no commit ever completed, `proven_prefix == 0` and the **reported** `affordable_sets == 1`; the assertion reads the predicate's output, not a constant. A never-prepared slot is false **regardless of what bytes exist**, so no cold slot is vacuously proven |
| E2 | **trim to empty, and a middle quarantine** | each trimmed slot clears `slot_proven_`; the prefix falls monotonically and returns to 0; a quarantined middle slot caps the prefix at its index while higher slots keep `slot_proven_` true |
| E2a | **all-DEVICE TMR progression (finding 1)** | with every input a DEVICE tensor, `packed_temp_bytes == 0` on every run and **no staging block is ever provisioned**; nevertheless slot 0 proves on its first commit and each later slot proves with **zero** allocator calls, so the reported capacity climbs 1 → 2 → 3 → … → `D` and at least two runs are concurrent. Negative control: under r18's staging-driven rule the same workload reports 1 forever |
| E2b | **HOST progression on HBG** | each slot proves only after a real arena growth; a refused growth leaves `slot_proven_` unset so capacity does not rise on a failed probe |
| E2c | **mixed input change** | a slot proven by an all-DEVICE run, then given a HOST-input candidate, still reports `needs_staging_growth`, still takes the token, and still runs the two-attempt / trim / park / typed-fatal path; a refused growth does **not** clear `slot_proven_`, and the slot keeps naming its previous block |
| E3 | zero private bytes vs never prepared | a commit that completed with a zero private demand sets `slot_proven_`; an untouched slot with the same absence of bytes does not. Both halves asserted in one case so neither can be satisfied vacuously |
| E4 | shared-bank prerequisite | under a shared declaration, `proven(s)` requires `slot_proven_[s]` **and** bank 0 committed-and-usable; a bank-0 **growth** preserves it, a bank-0 **release** clears it for every slot at once without touching their private state |
| E5 | **§4.2 pre-staging detection** | `needs_staging_growth` is computed from `get_retained_temp_buffer` with **zero** allocator calls |
| E6 | **§12.3 E3** | the staging refusal parks instead of returning INTERNAL; the slot keeps its previous block; retirement alone returns no bytes; the trim after retirement does; the resume re-enters S2 and **never** P |
| E7 | **token continuity** | one token acquisition across S→P→C for the same candidate; C0 does not re-acquire; a bytes-parked plan never consults `token_free`; `holds_token` asserted on resume |
| E8 | **§12.6 E6** | the second candidate parks on the token, holds no claim or plan, and resumes on release; never two growth attempts in flight |
| E9 | **§12.4 E4** | close while parked at S: no stamp, no retained-temp reference, token released, state reset, previous staging left unreferenced; idempotent on a second call |
| E10 | **§6.1 negative control** | with the rollback disabled, a failed staging leaves `current == false` with `ref_count == 1`, neither reclaimable nor reusable |
| E11 | **§12.5 E5** | after rollback the block is `ref 0` / `current false` and is freed within the same phase; across two attempts and two resumes at most one unreferenced obsolete generation per key at any instant; the allocator's live-block count returns to its entry value |
| E12 | `was_region_backing` | the rollback drops the ref and does **not** clear `current` when the allocator returned the region's own live backing; a rolled-back reuse has `current` restored |
| E13 | never `drop_run_refs` on a park | after any park-path rollback the plan's retained-temp reference is still held |
| E14 | §7.1 cleanup split | `cleanup_failed_prepare` retires the epoch and drops its refs; `park_prepare` notes no RunFact, keeps the bindings, the plan and the token, and releases only the reservation and claim |
| E15 | **§7.3 taxonomy** | each of `RESOURCE_PARKED`, `RESOURCE_EXHAUSTED`, `PREPARED_INCOMPATIBLE` and `INTERNAL` routes to its stated cleanup, exception and lane action; all four leave `native_runs_` empty for the epoch |
| E16 | §10.2 proof | with a native-run entry present the hook returns non-zero and the lane poisons; the borrow is **not** released |
| E17 | §10.3 coverage | all six QUEUED call sites route a parked plan through `abandon_plan` |
| E18 | **§10.4** | an unproven local terminal fails every parked plan on that endpoint; none re-enters S2 or C2; the unproven epoch's blocks leave `releasable` and its slots leave materialization |
| E19 | §8.2 prefix | with proven = {0, 2}, capacity is 1 + token, not 2; `try_acquire` never offers an unproven slot |
| E20 | §8.3 fold | no fold until `active_tasks == 0`; `min` across endpoints; an endpoint with no sample contributes nothing; a decrease grants no lease above the new depth and drops no record or output |
| E21 | §8.3 launch | `launched_prefix ≤ min(launch_depth_, fifo.size())` always; `≤ admission_depth_` once every pre-decrease run retires; an explicit `launch_depth=n` caps at `min(n, …)` |
| E22 | §9 locality and front | a lane's `finish` drops refs while the lease is held and `active_tasks` is non-zero; a front park has an empty holder set and takes the typed fatal |
| E23 | callback once | exactly one orchestration-callback invocation per submission across any number of token waits, byte parks and resumes |
| E24 | §2 mode | `_init_level2` reaches native init with the route value unset ⇒ no phase-S token, no park, no trim, no derived capacity; nested, remote/MPI, explicit depth, explicit launch depth, explicit budget and borrowed contexts likewise; a missing staging symbol fails init with the actionable message |
| E25 | no quota, and explicit budgets untouched | the default path installs no `set_limit`; an **explicit-budget** context receives no route value, so it gets no phase-S token, no park, no trim and no derived capacity, and its `OverQuota` refusal fails the prepare immediately — today's behaviour, asserted as unchanged |
| E26 | **sim excluded** | a simulated platform never has the route value set; its admission, enqueue order and observed concurrency are byte-for-byte today's, and no token, park or trim path is reachable |
| E27 | **serial reuse does not grow capacity** | §12.2a part 1: a serial chain reuses slot 0 indefinitely, the reported capacity stays at 2, and `try_acquire` returns slot 0 — not slot 1 — after each retirement |
| E28 | **concurrent ratchet** | §12.2a part 2: a second outstanding run takes slot 1 and proves it; the **next** whole-run retirement publishes 3; capacity never changes at phase C, only at a fold |
| E29 | **zero-allocation scope** | on a new set with an all-DEVICE candidate, the ledger's allocator-call count is **0** while the fence's two events, the caller-order events (caller-stream runs), `runtime_bufs_[slot]` and the per-slot host scratch are each still created — counted separately, and each failure-injected independently |
| E30 | **event failure clears the proof** | an injected `ops_.create` failure at the first launch on a new set rolls the launch back with no execution-visible submission and **clears** `slot_proven_` for that slot, so the next fold lowers capacity |
| E31 | **non-ledger failures are fatal, not parked** | a host `bad_alloc` for `runtime_bufs_[slot]` or a failed `acquire_sm_mirror` produces a fatal prepare with no park, no trim and no holder set consulted |
| E32 | **§9.1 the 40/30/20 case** | with shortfall 40, free 30 and one launched holder of 20, the plan **parks** (it does not fatal); after that holder retires and the trim converts its 20, the single re-attempt succeeds. Negative control: the old `20 >= 40` rule fatals the same input |
| E33 | **§9.1 snapshot state machine** | the snapshot is captured **once** per phase-park-episode and is never recomputed or appended to; a retirement inside it moves the identity into `consumed` and clears the park; parks per phase never exceed `|holder_snapshot[phase]|`; with `pending` empty the plan takes the typed fatal naming shortfall, consumed identities and bytes released |
| E34 | **§9.1 two holders retiring separately** | snapshot `{H1,H2}`: H1's retirement consumes and re-activates; a still-refused attempt parks again on `{H2}`; H2's retirement consumes and re-activates. Exactly two parks, two retirements |
| E35 | **§9.1 unrelated retirement** | an endpoint-local retirement outside the snapshot neither clears the park nor consumes anything |
| E36 | **§9.1 shrink while parked** | a fold lowering `admission_depth_` leaves the snapshot and its bound untouched; a grandfathered launched run stays eligible; the bound is `D − 1`, never `admission_depth_ − 1` |
| E37 | **§9.1 exhaustion, no append** | a run launched *after* the snapshot is not added to it; once `pending` empties, the plan fatals rather than waiting on the newcomer |
| E38 | **§9.3 attempt bound** | ≤ 2 transaction attempts per activation; ≤ `1 + |snapshot|` activations per phase; a `Keep`-satisfied region makes **zero** backend calls; the counted backend calls never exceed the §9.3 totals |
| E39 | **§9.2 eligibility** | a launched, running holder **is** eligible; quarantined, release-unconfirmed, retained-mapping and unproven-stop epochs are each excluded independently — asserted as five separate cases so "unproven" cannot be read as excluding every launched run |

**Device scenarios.** S1 §12.3 on a2a3 HBG — a staging refusal, a named holder, a resume, with the ledger's block count and charge reported at each step and a callback counter. S2 §12.5 on a2a3 HBG, including the failed-release variant. S3 §12.6 on a2a3 HBG. S4 §12.4 close-while-parked on a2a3 HBG and a5 TMR. S5 §12.1 and §12.2 on a2a3 HBG. S6 the shared-arena path on a2a3 TMR and a5 TMR: §12.2a's all-DEVICE progression with the allocator call count reported, plus a staging park and the pre-bind arena decline. S7 the Qwen consumer on the **external owner's** driver (`#2592`, `#2436`/`#2429`), re-qualified when this lands, not claimed as the same completion.

Gates: one callback per submission; no unreferenced obsolete generation surviving a reclaim; at most two transaction attempts per **activation** and at most `1 + |holder_snapshot[phase]|` activations per phase (§9.3); at most one growth attempt in flight per endpoint; no half-installed region; no borrow released while device reachability is unproven; `releasable` excludes every §9 category; no trim of a live, referenced or shared block; reported admission equals the §8.3 predicate. Coverage per item as `reached | partial | inconclusive`. **Wall time is never evidence of a wait** — the observables are the prepare phase, the ledger's block count and charge, the allocator call count, the token holder and the callback counter.

---

## 14. Implementation-decision summary

One page, for the decision. Nothing below is new; it is the contract the rest of the document specifies.

**Normal submit, nothing configured (local L3, onboard).** `submit` admits while `run_fifo_.size() < pending_run_limit_`, parking **before a run id exists** otherwise. The child prepares in four phases — pre-staging **S**, plan **P** (the user's orchestration callback, exactly once), commit **C**, bind/publish **B/U**. Capacity starts at 1 and is `clamp(proven_prefix + token_free, 1, D)`, folded once per whole run at `retire_terminal_run`.

| Path | Behaviour |
|---|---|
| **reuse** | every applicable region `Keep` and staging fits ⇒ no token, no transaction, **no backend call**. The common case, and the one a serial workload stays in forever |
| **growth** | the first phase that needs it takes the endpoint's exclusive growth token and holds it through S→P→C; one transaction attempt, then trim + reclaim, then one more |
| **wait** | after two refused attempts, if an eligible launched holder exists the plan **parks** — holding its token, its lease, its borrow and its durable plan, but no new staged allocation — and resumes when an identity from its immutable snapshot retires endpoint-locally. Bounded by the snapshot: ≤ `D − 1` parks per phase, ≤ `2(D − 1)` per plan |
| **error** | typed and separated: `RESOURCE_PARKED` (wait) vs `RESOURCE_EXHAUSTED` (no eligible holder, or snapshot exhausted) vs `PREPARED_INCOMPATIBLE` (existing shared-bank fallback) vs `INTERNAL` (`Degraded`, ledger/record inconsistency, host allocation). Non-ledger per-set resources — the fence's and caller-order's lazily armed events, `runtime_bufs_`, host scratch — are **never parked**: they are fatal prepares or rolled-back launches, and a launch-event failure withdraws that slot's proof |
| **close** | `stop_admission` → a parked plan is QUEUED, so every terminal path routes it through `abandon_plan`, which releases the token, the retained-temp reference and the stamp, then the existing `release_device_spans`. Idempotent, and it refuses to clean up if a native-run entry exists |

**Applicable routes.** Local L3 automatic mode, onboard only. Excluded and byte-for-byte unchanged: owned L2, nested L3 under an L4 parent, remote- and MPI-hosted inner Workers, explicit `pipeline_depth`, explicit `launch_depth`, explicit `workspace_budget_bytes`, kernel/borrowed contexts, and **every simulated platform**.

**Resource costs.** Per-run host CPU: two predicates, the `affordable_sets` prefix scan and ledger walk, the entry write, the parent-side capture and the fold — all listed in §14.1, none on a device path, **no performance benefit claimed**. Host state: `SlotPrepareState` including the ≤ `D − 1`-identity holder snapshot, one token per endpoint, one `host_arena` per used slot, one `uint32_t` wire field. Device bytes: **no quota**; the allocator's refusal is the only capacity fact used, and the four-region ledger accounting proves nothing about resources it does not own.

**Acceptance.** E1–E39 deterministic (pyut/cpput with counting backend, stubbed allocator, counting callback, ledger and token inspectors) plus S1–S7 on device. Gates in §13. **Nothing has been run and no result is claimed.**

**Shape.** One complete feature in one eventual PR — W1–W10 in the work table in §14.1 is its internal sequencing, not separate deliveries.

---

### 14.1 Work process and costs

Concrete scope for the decision, in the order the pieces depend on each other. No new architecture: every item is an addition to a named existing seam. Counts are of distinct edit sites, not lines.

| # | Work item | Seams touched | Depends on |
|---|---|---|---|
| W1 | `StagedClaim` record in `g_workspace_plan`; append in `acquire_arena_backing`; `rollback_staged_claims()`; commit-clears-without-releasing on `outcome.rc == 0` | `device_runner_base.cpp` (plan identity, arena backing, `reference_bank_arenas` site) | — |
| W2 | `park_prepare(state)` beside `cleanup_failed_prepare`; two new internal rc values; `PreparedRunResourceParked` / `PreparedRunResourceExhausted` | `c_api_shared.cpp`, `runtime_c_api.h`, `chip_worker.{h,cpp}` | W1 |
| W3 | `SlotPrepareState` per slot; the growth token per endpoint; acquire at first growth need in S or C; retain across S→P→C; resume self-recognition | `device_runner_base.{h,cpp}` | W2 |
| W4 | Phase S: `needs_staging_growth` from `get_retained_temp_buffer`; route `RetainedTempBump::begin`'s refusal into the attempt/trim/park/fatal sequence instead of an immediate `INTERNAL` | the four `runtime_maker.cpp` files (one call site each), `retained_temp_bump.h` | W3 |
| W5 | Plan durability: `acquire_host_arena(slot, …)` as a fourth retained per-slot host buffer beside the three that exist; plan stamp; resume entry at the commit step | `device_runner_base.{h,cpp}`, HBG `runtime_maker.cpp` ×2 | W3 |
| W6 | `abandon_plan` / `abort_run_plan` on all six QUEUED call sites; idempotence on the stamp; failure poisons | `chip_run_lane.cpp`, `device_runner_base.{h,cpp}` | W2, W5 |
| W7 | Capacity: `slot_proven_` set at phase C, cleared on trim/quarantine/launch-event failure; `proven_prefix`; `affordable_sets` in the run-state entry; the fold in `retire_terminal_run` | `device_runner_base.cpp`, `worker_manager.{h,cpp}`, `worker.py` (child publish), `scheduler.cpp` (capture), `orchestrator.{h,cpp}` (fold) | W3 |
| W8 | `park_admissible` / `holders(e)` / `waited_for`; the refusal message's arithmetic | `device_runner_base.cpp` | W3, W7 |
| W9 | Mode routing: `automatic_admission` keyword, the staging entry, `Install::ManageWithAutomaticAdmission`, fail-closed on a missing symbol, onboard-only gate | `task_interface.py`, `worker.py`, `chip_worker.cpp`, `c_api_shared.cpp`, `workspace_staging.h` | — |
| W10 | Acceptance E1–E39 and the device scenarios | `tests/ut/py`, `tests/ut/cpp` | all |

**Costs, stated plainly. No performance benefit is claimed anywhere in this design.**

Added CPU work on the **steady-state** path — the earlier "none that is new" was wrong, because capacity reporting and claim bookkeeping run on every run, not only on growth:

| Added work | Where it runs | Frequency |
|---|---|---|
| `needs_staging_growth` (a read of the slot's recorded size) and `needs_growth_arena` (`arena_region_action` over the bank's regions, already computed by the transaction today) | child, prepare | twice per run |
| `StagedClaim` append per `acquire` | child, inside an existing allocation callback | ≤ 4 per growing run, **0** for a `Keep`-only run |
| `affordable_sets` computation — a `proven_prefix` scan over at most `D` slots plus the ledger walk that decides `proven(s)` | child, at terminal publication | **once per run**, on every run |
| writing the figure into the run-state entry | child, beside the rc it already writes | once per run |
| reading it in `terminal_completion` and carrying it on `WorkerCompletion` | parent endpoint | once per task completion |
| capture into `RunState::endpoint_samples` | parent, `Scheduler::worker_done`'s existing first statement | once per task completion |
| the fold: `min` over endpoints with a sample, clamp, assign `admission_depth_` / `pending_run_limit_` | parent, inside `retire_terminal_run` under `runs_mu_` it already holds | once per run |

All of it is small and host-side, and none of it is on a device path — but it is real per-run work and it is listed rather than waved away. Under growth, add up to `2 × D` transaction attempts and one trim pass per phase (§9.3), both demand-driven.

Added **host state**:

| State | Owner | Size |
|---|---|---|
| `SlotPrepareState`, **including `holder_snapshot` and `consumed`** | the runner, per slot | phase, epoch, flags, plus ≤ `D − 1` epoch identities at 8 B each |
| growth token | the runner, per endpoint | one flag plus the owning epoch |
| `host_arena` retained buffer (W5) | the runner, per **used** slot | `layout.arena_size` — the price of not re-running the user's callback across a park |
| `affordable_sets` | an internal wire field in `MailboxRunEntry` between a parent and its own forked child | one `uint32_t` |
| sample carrier | one field on `WorkerCompletion`, one small per-endpoint map on `RunState`, and `latest_sample` / `sample_stamp` per endpoint on the Orchestrator | bounded by the endpoint count |
| `slot_proven_` | the runner, per slot | one bool |

- **Serialization**: all growth on an endpoint is serialized by the token. `Keep`-only workloads are unaffected; a workload where every run grows commits one at a time.
- **Review surface**: W1, W2 and W6 are the ownership-critical ones — a mistake there strands bytes or releases a borrow while device reachability is unproven. W4 touches four runtime files identically and is the widest but shallowest change.
- **What is not costed**: any device-side effect of a refused allocation (driver logging, fragmentation reporting) is unmeasured, and nothing in this document has been executed.

**Sequencing.** W1→W2→W3 is the ownership spine and is worth landing as one reviewable unit with E10–E15; W4+W5 complete the prepare path with E5–E9; W7+W8 are capacity and can be judged against E1–E4 and E27–E33; W9 is independent and small; W6 must land with or before anything that can park. The complete-function rule still applies — this ships as one feature, not as a sequence of partial behaviours.

---

## 15. Tradeoffs, and external ownership

1. **The allocator attempt is a probe** — bounded to two per growth phase, fully rolled back, and the only way to learn real capacity without modelling memory this ledger does not own. A failed device allocation may cost driver-side logging or fragmentation reporting; unmeasured.
2. **The token serializes all growth on an endpoint.** `Keep`-only workloads are unaffected; a workload where every run grows commits one at a time.
3. **A phase-C parked plan holds its retained-temp device block and its recorded bindings**; a phase-S parked plan holds neither. Both hold the token if they needed growth.
4. **`host_arena` retention is a new per-slot host cost** — the price of not re-running the callback.
5. **A front park cannot wait** — intrinsic; the typed fatal is the stated alternative.
6. **On TMR the park is staging-only**; arena growth still serializes behind the predecessor's fence.
7. **A release failure escalates to a context fault**, reachable from the rollback path as well as the trim.
8. **Capacity folds only at whole-run retirement**, so the prefix adapts across runs while the token and the parks cover the within-run case.
9. **Automatic admission as the unconfigured default is still a proposal**, and nothing here is measured.

No new depth or grant knob, no mandatory user budget, no hidden fraction, no new parent/child handshake, no implicit cross-run synchronization, no sleep and no timer; every re-activation is paid for by a distinct consumed retirement from a finite immutable snapshot (§9.1, §9.3). **No workload driver is added and no external task is taken over.**

All **33** open PRs rechecked at `cc6c4d51f`: `#2592` `93b7b4c` (Crane-Liu, sustained multi-request Qwen DEVICE decode driver — **remains its author's task**, configures both `pipeline_depth` and `launch_depth` explicitly, so §2 leaves it on today's path unchanged; S7 runs on theirs and is not claimed as the same completion), `#2593` `25be88ec` (communicator control state in the chip child — touches the serve loop the park returns to, scoped to comm-domain handles; no overlap, a seam to re-check before implementation), `#2588` `d5911fa0` (DFX, sole writer, active — consumed in the launch gate, never edited), and 29 others in kernel-mode prefetch, HBG replay, AICore retirement and unrelated areas with no intersection. `#2566` (merged, `e9e04fe3b`) contributes the per-set `OrchestrationScratch` counted in §3.
