> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# Ordinary async: removing the fixed run-slot model

**Manager consolidated contract — based on Revision 7.** Supersedes r1–r6. Only the §2.3.5 finalization tail (and
the §2.2 tag rule, §6 rows and §13.1 fixture plan that follow from it) is corrected; every section the
r6 review accepted is unchanged. Response table: §16.

**Manager corrections are normative:** F2 preserves a nonzero wait result; F5 quarantines inconsistent non-destroying success; F6 follows the explicit error precedence. The original R7 is archived separately.

**Design only.** No product edit, no test authored or run, no build/install/device command, no
commit, no publication, no CI action, no external message. **New implementation remains
unapproved.** Original P4 session, worktree `pipeline-a-p4-submission`.

**Baseline.** Main re-verified at the start and end of this revision:
**`a89c1170c87ae6c776a977bce2dcabea5fd74b7a`**, unchanged. #2563 is now at **`b697`**, its original
author **idle awaiting CI**, no shared writer; the dependency is **file integration only**, not a
merge (§15).

**Integration addendum (2026-10-14):** main `5f4f9525d` includes #2561. Preserve the post-discharge poison recheck before retiring resources; a cleanup operation can change safety and require quarantine. This is an existing failure-boundary preservation requirement, not new implementation authorization. [Review](main-2561-integration-review.md). #2563 is Ready at `b697`, awaiting user merge. The baseline above records the original design audit.

**Labels.** `[source]` current-main fact with a path · `[design]` proposal · `[open]` undecided ·
`[untested]` asserted, never executed.

**Scope, unchanged:** all four ordinary program backends (a2a3/a5 × HBG/TMR), sustained N > 3 in FIFO
order, local L3, single and multiple endpoints, onboard and sim. **Out:** cross-device addressing,
communication (HOLD), capture, kernel mode, remote/MPI. No new `PTO_`/`PTO2` identifier, no version
field, no new diagnostic capability, no widened poisoning, no new ABI mechanism, **no new public
recovery or retry surface**.

---

## 1. Why all four backends stay in the target

### 1.1 Declarations

`[source]`

| Runtime | declared `pipeline_depth` | GM_HEAP / RUNTIME_IMAGE / GM_SM | TASK_ARGS |
|---|---|---|---|
| a2a3 HBG `runtime_maker.cpp:118-127` | `PTO_PIPELINE_MAX_DEPTH` | **HOST_PER_RUN** (GM_SM absent) | — |
| a5 HBG `:142-163` | `PTO_PIPELINE_MAX_DEPTH` | HOST_PER_RUN | — |
| a2a3 TMR `runtime_maker.cpp:94-106` | literal **2** | **DEVICE_SCRATCH** | HOST_PER_RUN |
| a5 TMR | literal **2** | DEVICE_SCRATCH | HOST_PER_RUN |

### 1.2 One shared scratch is ordered by a FIFO, and a FIFO is transitive

`[source]` `a2a3 .../tensormap_and_ringbuffer/host/runtime_maker.cpp:112-145`:

> *"The successor's AICPU op is ordered by the **queue**: a stream is a FIFO, and the predecessor's
> AICPU payload, its own-AICore-boundary wait, its whole-operator marker and its boundary record all
> sit ahead of it. So the arena reset cannot be dispatched until the predecessor's own AICore kernel
> has ended, **which is what the single shared instance needs**."*

`[design]` That argument counts no runs. Run 3's AICPU op queues behind run 2's whole-operator
boundary record, which queues behind run 1's. **The bind side is also N-independent** `[source]`:
`prepared_run_config_compatible_impl` answers yes only on a prebuilt-arena-cache hit, *"where the
bind writes no device byte"*, and the build/upload path runs only on a miss, *"which the platform
refuses while a predecessor holds the bank."*

### 1.3 Per-run vs shared, traced (A3/A5 TMR)

`[source]`

| Object | Per run or shared | Isolated / ordered by |
|---|---|---|
| GM_HEAP, GM_SM, RUNTIME_IMAGE | **shared, one instance** | AICPU stream FIFO (§1.2); content is a function of callable sizing, uploaded only on a cache miss |
| the arena **reset** | shared target | queued behind the predecessor's whole-operator boundary record |
| TASK_ARGS | per run (`HOST_PER_RUN`, declared) | its own copy |
| published execution-result record | per run | epoch-stamped; `device_run_result_published` requires a non-zero epoch match |
| completion fence, boundary marks, queued waits | per run | identity-bound |
| host staging (retained temp, run image, graph staging) | per run, grow-only | per-run slot today |
| shared-memory header | shared | **not read** since #2560 |

`[design]` No conflicting write or lifetime dependency makes a third compatible serially-queued TMR
run impossible. The incompatible-config gate is retained unchanged (§4.3).

### 1.4 The grant is a concurrency authorization, not a memory reservation

`[source]` A workspace budget is **optional**: `chip_worker.cpp:494-503` installs a limit only when
`workspace_budget_bytes != 0`, and zero means *no limit installed*; a simulated backend **refuses** a
budget outright (`:508`). Per-run region sizes are decided by each bind's host orchestration.

`[design]`

| Question | Answered where | By what |
|---|---|---|
| **representable** | wire/ABI | `slot_id`, `pipeline_slot`, `pipeline_depth` are already `uint32_t` `[source]` |
| **authorized concurrency** (the *grant*) | `ChipWorker::init` | `granted = min(requested ?: kDefaultRunResourceSets, declared_structural)` — **no budget consulted, none required** |
| **actual allocation** | each bind, on demand | today's grow-only commit, checks and rollback |

Each maker declares `UINT32_MAX` for `declared_structural`. **Shortage policy:** a region that cannot
be allocated at bind is a **pre-launch failure of that run** and is **not** a lane poison (§6).
**Default:** request `2`, granted 2 on every backend; an explicit request above the grant **fails
startup** — `_granted_chip_pipeline_depth` semantics, preserved.

---

## 2. Identity, the operation lease, and finalization

### 2.1 Borrow identity

`[source]` Two sites independently compute `slot_id + 1` and must agree: `chip_run_lane.cpp:77`
`borrow_identity` (registers at enqueue) and `host_api.h:472` `run_identity()` (declares writes and
queries readability during bind). `CallerDeviceBuffers` already keys `borrows_`/`writes_` by an
opaque `uint64_t` (`caller_device_buffers.h:142-200`) and names the hazard: *"the run is gone and its
slot — and so its identity — is handed to the next run"* (`:74`).

`[design]` Mint `run_epoch` at **lane admission** and have `prepare` adopt it instead of minting
(`chip_worker.cpp:1024`). Then `borrow_identity → run.run_epoch` and `HostApi::run_identity() →
run_epoch_`, an **existing member** already used by `run_result(...)` (`host_api.h:428,478`). No new
field.

### 2.2 What must be protected, who destroys, and what a magic word proves

`[source]` `NativeRunContext` is a **placement object** in caller-owned storage
(`native_run_context.h:27-92`) with non-trivial members. `destroy_native_run_context` (`:97-101`) ends
its lifetime and zeroes the first 8 bytes of the storage.

`[source]` **Destruction already happens inside the C finalize entry, on both platforms** — onboard
`c_api_shared.cpp:2230` and sim `sim/c_api_shared.cpp:1200`, both **before** the return-precedence
block, so **a nonzero return still means the object was destroyed**. `[source]` **Both have an early
return that does not destroy:** onboard on `state == nullptr` and on `context_rc != 0` from
`validate_current_context()`, with the comment *"A refused environment leaves the run owned for a
retry on its context"* (`:2039-2043`); sim on `state == nullptr` (`:1116-1120`).
`[source]` `ChipWorker::finalize_native_run` already clears owner state **after** the C call
(`chip_worker.cpp:1296-1298`).

`[design]` Destruction stays solely at the existing C boundary; the owner only reserves, drains,
observes, and then marks/removes/recycles. **The owner's observable is a `memcpy` of 8 bytes from
storage it owns** — no object dereference — with **three** cases, not two:

| magic after the call | meaning | owner action |
|---|---|---|
| `0` | the placement object was destroyed | erase the entry; **recycle the host storage block only** |
| `kMagic` exactly | the object was **not** destroyed | returned nonzero known context refusal: retain post-wait state; returned zero is inconsistent and quarantined; see F4/F5 |
| **anything else** | **noncanonical** — a partially torn-down or corrupted tag | `destruction_unknown`: **quarantine** the storage forever, count it against the grant, never recycle, never retry |

`[design]` **The tag is only consulted when the C finalize *returned*.** A throw from inside it proves
nothing about how far teardown got — resources may already have been released and execution state
mutated while the tag still reads `kMagic` — so **every** in-C throw is treated as unknown mutation
and quarantined, whatever the tag says (§2.3.5 rows F7–F9).

`[design]` **What `magic == 0` proves, stated narrowly** (r5 overreached): it proves the *context
object* was destroyed. It does **not** prove the device drain succeeded, that bindings were released,
or that any device resource may be reused. Recycling the **host storage block and the entry** is
authorized by it; **releasing the resource set is not** — that stays gated on §5.2's two predicates,
which are separate facts reported elsewhere.

### 2.3 The protocol

#### 2.3.1 Owner state — reservation, execution fact and latched error are three things

`[design]` r5 overloaded one `phase` as both reservation and execution fact. They are split:

```cpp
enum class ExecState { PREPARED, LAUNCHED, COMPLETED };   // COMPLETED == today's REAPED

struct RunEntry {                        // ChipWorker-owned; outlives the placement object
    void*               storage;         // owner's allocation; magic word at offset 0
    uint64_t            run_epoch;
    uint64_t            lease_generation;
    ExecState           exec_state;      // the EXECUTION fact; written by whichever op
                                         // establishes it, reservation or not
    unsigned            ops_in_flight;   // 0 or 1 — exclusive operation lease
    bool                finalize_reserved;   // the RESERVATION; never `exec_state`
    bool                destruction_unknown;
    int                 pending_rc;      // latched error to report at finalize; first writer wins
    bool                pending_rc_set;
};
std::unordered_map<uint64_t, std::unique_ptr<RunEntry>> runs_;      // guarded by native_run_mu_
std::condition_variable                                  ops_done_;
```

**Invariants.**

1. No iterator, reference or `RunEntry*` is held across a mutex release; every re-entry re-looks-up
   by `run_epoch`.
2. `ops_in_flight ∈ {0, 1}` — at most one operation inside the runtime for one run.
3. `finalize_reserved` is set `false → true` by exactly one thread under the lock and **is not
   `exec_state`**.
4. **`exec_state` and `pending_rc` are execution facts and are written unconditionally by the
   operation that establishes them, reservation or not.** `pending_rc` is **first-writer-wins**, so no
   later path overwrites an earlier error.
5. `destroy_native_run_context` is called **only** by the C finalize entry.
6. The host storage block is recycled only when `magic == 0` **and** `ops_in_flight == 0`. Resource-set
   release is a separate decision (§5.2).

#### 2.3.2 Typed operation outcomes, mapped from the real codes

`[source]` `runtime_c_api.h:118-124`:

```c
SIMPLER_NATIVE_RUN_POLL_ERROR     = PTO_RUNTIME_ERR_INTERNAL   /* a distinct negative code */
SIMPLER_NATIVE_RUN_POLL_NOT_READY = 0
SIMPLER_NATIVE_RUN_POLL_COMPLETE  = 1
```

`[source]` `chip_worker.cpp:1221-1246` returns early `true` when already REAPED, returns `false`
**only** for `NOT_READY`, marks REAPED for `COMPLETE`, and **throws for everything else**.
`wait_native_run` (`:1248-1266`) returns early when REAPED, else calls `wait_run_fn_` and
**unconditionally** sets `wait_rc = rc` and `phase = REAPED` — it does **not** throw, and a nonzero
`wait_rc` is an error the finalize reports. `[source]` `launch_native_run_joined` (`:1180-1217`)
returns `false` on `PTO_RUNTIME_ERR_UNSUPPORTED` (a decline, no state change); on any other nonzero
rc it **probes with a poll** and sets `phase = REAPED, wait_rc = rc` when that poll says `COMPLETE`,
else `phase = PREPARED, wait_rc = 0`, and then throws.

`[design]` **r5's `return rc == COMPLETE` is withdrawn** — it silently turned every error into
not-ready — and so is `is_wait_like(Poll)`, which could have put `COMPLETE == 1` into a field where
nonzero means error. Typed outcomes, with an explicit mapping and no raw poll code reaching
`pending_rc`:

```cpp
enum class PollOutcome  { NotReady, Complete, Error };
enum class WaitOutcome  { Completed };          // wait always establishes completion
enum class LaunchOutcome{ Launched, Declined, Failed };

PollOutcome classify_poll(int rc) {
    if (rc == SIMPLER_NATIVE_RUN_POLL_NOT_READY) return PollOutcome::NotReady;
    if (rc == SIMPLER_NATIVE_RUN_POLL_COMPLETE)  return PollOutcome::Complete;
    return PollOutcome::Error;    // includes SIMPLER_NATIVE_RUN_POLL_ERROR and any other value
}
```

| Operation | C result | typed outcome | `exec_state` | `pending_rc` | returns / throws |
|---|---|---|---|---|---|
| poll | already `COMPLETED` | — (fast path, **no lease taken**) | unchanged | unchanged | returns `true` |
| poll | `0` | NotReady | unchanged | unchanged | returns `false` |
| poll | `1` | Complete | → `COMPLETED` | **untouched** (1 is success) | returns `true` |
| poll | anything else | Error | unchanged | **untouched** (today writes none) | **throws**, today's message |
| wait | already `COMPLETED` | — (fast path, **no lease taken**) | unchanged | unchanged | returns |
| wait | any rc | Completed | → `COMPLETED` | `rc` if nonzero (first-writer-wins) | returns; never throws |
| launch | `0` | Launched | → `LAUNCHED` | unchanged | returns `true` |
| launch (joined) | `UNSUPPORTED` | Declined | **unchanged** | unchanged | returns `false` |
| launch | other nonzero, probe poll `COMPLETE` | Failed | → `COMPLETED` | `rc` | **throws** |
| launch | other nonzero, probe poll otherwise | Failed | → `PREPARED` | unchanged | **throws** |

Every row is today's behaviour, expressed as a typed outcome rather than shorthand. The two
**completed fast paths are preserved and are decided inside `acquire_op` before the lease is taken**,
so an already-completed run costs one locked lookup.

#### 2.3.3 Acquiring the operation lease

```
enum class Acquire { Ok, AlreadyComplete, Absent, Stale, Reserved, Busy };

Acquire acquire_op(uint64_t epoch, uint64_t lease_gen, OpKind kind,
                   void** raw_out, ExecState* prior_exec_out)
{
    std::unique_lock<std::mutex> lk(native_run_mu_);
    for (;;) {
        // (1) FULL re-lookup and FULL revalidation. Initially and after EVERY wake.
        auto it = runs_.find(epoch);
        if (it == runs_.end())                        return Acquire::Absent;
        RunEntry& e = *it->second;                    // valid only until the next unlock
        if (e.destruction_unknown)                    return Acquire::Stale;
        if (e.run_epoch != epoch ||
            e.lease_generation != lease_gen)          return Acquire::Stale;

        // (2) Completed fast paths, BEFORE any lease or wait (today's REAPED early return).
        if ((kind == OpKind::Poll || kind == OpKind::Wait) &&
            e.exec_state == ExecState::COMPLETED)     return Acquire::AlreadyComplete;

        if (!phase_admits(kind, e.exec_state))        return Acquire::Stale;
        if (e.finalize_reserved)                      return Acquire::Reserved;

        // (3) Only now consider waiting.
        if (e.ops_in_flight != 0) {
            if (kind == OpKind::Poll)                 return Acquire::Busy;    // not-ready, no block
            ops_done_.wait(lk);                       // releases the mutex
            continue;                                 // → back to (1)
        }

        // (4) Take the lease. `exec_state` is NOT changed here: it is an execution fact and
        //     only an actual outcome may move it (invariant 4).
        e.ops_in_flight = 1;
        *prior_exec_out = e.exec_state;
        *raw_out        = e.storage;
        return Acquire::Ok;
    }
}
```

`Absent`/`Stale` map onto today's *"native-run token is stale or used in the wrong phase"*; `Reserved`
onto a new *"another finalization is in progress"*; `Busy` is poll's not-ready.

#### 2.3.4 An ordinary operation, closed

```
bool poll_native_run(run):
    void* raw; ExecState prior;
    switch (acquire_op(run.run_epoch, run.generation, OpKind::Poll, &raw, &prior)) {
        case AlreadyComplete: return true;                    // fast path, no runtime call
        case Busy:            return false;
        case Absent: case Stale: case Reserved: throw stale_or_reserved_error(run);
        case Ok:              break;
    }

    int rc = 0; std::exception_ptr pending;
    try { rc = poll_run_fn_(device_ctx_, raw); }
    catch (...) { pending = std::current_exception(); }        // (5) captured, not a flag

    {
        std::lock_guard lk(native_run_mu_);
        auto it = runs_.find(run.run_epoch);                  // re-lookup; no carried reference
        if (it != runs_.end() && it->second->run_epoch == run.run_epoch) {
            RunEntry& e = *it->second;
            if (!pending) {
                switch (classify_poll(rc)) {
                    case PollOutcome::Complete:
                        e.exec_state = ExecState::COMPLETED;  // (6) unconditional: execution fact
                        break;                                //     no rc into pending_rc
                    case PollOutcome::NotReady: break;        // no state change
                    case PollOutcome::Error:
                        pending = make_poll_error(rc, run);   // today's throw, as an exception_ptr
                        break;                                // no state change, as today
                }
            }
            e.ops_in_flight = 0;
        }
        ops_done_.notify_all();                               // under the lock: no lost wakeup
    }
    if (pending) std::rethrow_exception(pending);             // (7) never a bare `throw;`
    return classify_poll(rc) == PollOutcome::Complete;

void wait_native_run(run):
    void* raw; ExecState prior;
    switch (acquire_op(..., OpKind::Wait, &raw, &prior)) {
        case AlreadyComplete: return;                         // fast path
        case Absent: case Stale: case Reserved: throw stale_or_reserved_error(run);
        case Ok:              break;                          // Busy is unreachable: Wait blocks
    }
    int rc = 0; std::exception_ptr pending;
    try { rc = wait_run_fn_(device_ctx_, raw); }
    catch (...) { pending = std::current_exception(); }
    {
        std::lock_guard lk(native_run_mu_);
        auto it = runs_.find(run.run_epoch);
        if (it != runs_.end() && it->second->run_epoch == run.run_epoch) {
            RunEntry& e = *it->second;
            if (!pending) {
                e.exec_state = ExecState::COMPLETED;          // today: REAPED unconditionally
                if (rc != 0 && !e.pending_rc_set) { e.pending_rc = rc; e.pending_rc_set = true; }
            }                                                 // a throw leaves exec_state alone
            e.ops_in_flight = 0;
        }
        ops_done_.notify_all();
    }
    if (pending) std::rethrow_exception(pending);
    // no throw on nonzero rc: today's wait latches it for finalize to report
```

Steps (5)–(7) fix r5's `catch (...) { threw = true; } … if (threw) throw;`, which had **no active
exception** at the rethrow and would have called `std::terminate`.

#### 2.3.5 Finalization — one finalizer, one destruction, persisted wait, explicit precedence

`[source]` **The precedence in main is `finalize_rc` first, otherwise `wait_rc`**
(`chip_worker.cpp:1300-1305`: `int rc = finalize_rc != 0 ? finalize_rc : wait_rc;`). `[design]`
r6's prose "first error wins" and its intermediate `wait_rc` reassignments contradicted that and are
**withdrawn**. There is exactly one precedence rule and `wait_rc` has exactly one source:

| Precedence | Value | When |
|---|---|---|
| 1 | `finalize_rc` | nonzero — always outranks everything below |
| 2 | `wait_rc` | otherwise |

**`wait_rc` is single-sourced, and the two sources are mutually exclusive by construction:**

| Post-drain `exec_state` | `wait_rc` comes from | the finalizer's own wait |
|---|---|---|
| `COMPLETED` | the persisted `pending_rc` (a prior wait, or a failed-launch probe) | **skipped** |
| `LAUNCHED` | the finalizer's **own** wait, which then persists it (step 11) | performed once |

A run cannot be `COMPLETED` without an operation establishing completion (a successful poll keeps the initialized zero error), and cannot be
`LAUNCHED` with a completed wait outstanding — so no value is ever written twice and nothing is
overwritten.

```
void finalize_native_run(run):
    void* raw;
    {
        std::unique_lock lk(native_run_mu_);
        auto it = runs_.find(run.run_epoch);
        if (it == runs_.end() || it->second->destruction_unknown ||
            it->second->run_epoch != run.run_epoch ||
            it->second->lease_generation != run.generation ||
            !phase_admits_finalize(it->second->exec_state))   throw stale_error(run);
        // (8) EXACTLY ONE finalizer, decided independently of exec_state.
        if (it->second->finalize_reserved)
            throw std::runtime_error("another finalization is in progress " + identity(run));
        it->second->finalize_reserved = true;

        // (9) Drain. Re-lookup after every wake; no carried reference (rehash-safe).
        for (;;) {
            auto cur = runs_.find(run.run_epoch);
            if (cur == runs_.end()) return;                   // defensive; cannot happen
            if (cur->second->ops_in_flight == 0) { raw = cur->second->storage; break; }
            ops_done_.wait(lk);
        }
    }

    // (10) Read the POST-DRAIN execution fact and persisted error.
    ExecState exec; int persisted_rc; bool persisted;
    {
        std::lock_guard lk(native_run_mu_);
        auto it = runs_.find(run.run_epoch);
        exec         = it->second->exec_state;    // may have advanced to COMPLETED during the drain
        persisted_rc = it->second->pending_rc;
        persisted    = it->second->pending_rc_set;
    }

    std::exception_ptr pending;
    bool entered_c_finalize = false;
    int  finalize_rc = 0;

    // (11) The finalizer's own wait PERSISTS its outcome into the entry, exactly as the
    //      ordinary wait path does, BEFORE the C finalize can be entered. Skipped entirely
    //      when completion is already established, so a wait is never repeated.
    if (exec == ExecState::LAUNCHED) {
        int rc = 0;
        try { rc = wait_run_fn_(device_ctx_, raw); }
        catch (...) { pending = std::current_exception(); }
        if (!pending) {
            std::lock_guard lk(native_run_mu_);
            auto it = runs_.find(run.run_epoch);
            if (it != runs_.end() && it->second->run_epoch == run.run_epoch) {
                RunEntry& e = *it->second;
                e.exec_state = ExecState::COMPLETED;                 // mandatory execution fact
                if (rc != 0 && !e.pending_rc_set) { e.pending_rc = rc; e.pending_rc_set = true; }
                exec = e.exec_state; persisted_rc = e.pending_rc; persisted = e.pending_rc_set;
            }
        }
        // A throw here leaves exec_state alone — today's rollback baseline (:1284-1289).
    }

    // (12) Only now can destruction happen at all.
    if (!pending) {
        entered_c_finalize = true;
        try { finalize_rc = finalize_run_fn_(device_ctx_, raw); }    // destroys, per §2.2
        catch (...) { pending = std::current_exception(); }
    }

    // (13) Observe the tag only when the C finalize was entered.
    MagicState tag = entered_c_finalize ? read_magic(raw) : MagicState::Canonical;
    std::exception_ptr synthesized;    // used only when nothing was captured

    {
        std::lock_guard lk(native_run_mu_);
        auto it = runs_.find(run.run_epoch);
        if (it != runs_.end()) {
            RunEntry& e = *it->second;
            if (!entered_c_finalize) {
                // pre-C wait throw: certainly not destroyed; restore the POST-WAIT fact.
                e.finalize_reserved = false;
                e.exec_state        = exec;
            } else if (pending) {
                // (14) A throw INSIDE the C finalize proves nothing about how far teardown got:
                //      it may have released resources or mutated execution before throwing while
                //      the tag still reads kMagic. Retain and quarantine; never authorize retry.
                e.destruction_unknown = true;          // no further device call for this run
                e.finalize_reserved   = false;
            } else switch (tag) {
                case MagicState::Zero:                 // destroyed, returned normally
                    { void* s = e.storage; runs_.erase(it); recycle_host_storage(s); }
                    break;                             // host storage only; set release is §5.2's
                case MagicState::Canonical:            // the non-destroying context refusal
                    e.finalize_reserved = false;
                    e.exec_state        = exec;        // POST-WAIT fact, so a retry never re-waits
                    if (finalize_rc == 0) {            // inconsistent non-destroying success: never retry
                        e.destruction_unknown = true;
                        synthesized = make_cleanup_error(
                            "finalize returned success without destroying the run context", run);
                    }
                    break;
                case MagicState::Noncanonical:         // partial teardown / corrupt tag
                    e.destruction_unknown = true;
                    e.finalize_reserved   = false;
                    if (finalize_rc == 0 && (!persisted || persisted_rc == 0))
                        synthesized = make_cleanup_error(   // (16) never a silent success
                            "finalize left the run context in an unknown state", run);
                    break;
            }
        }
        ops_done_.notify_all();
    }

    if (pending)     std::rethrow_exception(pending);        // the original exception, preserved
    if (synthesized) std::rethrow_exception(synthesized);    // only when none was captured
    int rc = finalize_rc != 0 ? finalize_rc : (persisted ? persisted_rc : 0);
    if (rc != 0) throw finalize_error(rc, run);              // today's throw, :1300-1305
```

`read_magic(raw)` is a `memcpy` of 8 bytes from the owner's own allocation, classified
`Zero` / `Canonical` (`== kMagic`) / `Noncanonical`.

**Closed outcome table** over *(how the call ended × tag × rc)*. Every row ends in an exception or a
clean success; there is no silent path.

| # | Ended | tag | `finalize_rc` | destroyed | owner action | caller |
|---|---|---|---|---|---|---|
| F1 | wait threw, C finalize **never entered** | — | — | **certainly not** | restore post-wait `exec_state`, clear reservation | **rethrow original**; lane poisons |
| F2 | returned | `Zero` | `0` | yes | erase entry, recycle **host storage** | throw persisted wait/launch error if nonzero; success only when it is zero |
| F3 | returned | `Zero` | ≠ 0 | yes | erase entry, recycle host storage | `throw finalize_error(finalize_rc)` |
| F4 | returned | `Canonical` | ≠ 0 | no | keep owned, restore post-wait `exec_state`, clear reservation; **low-level retryable** | `throw finalize_error(finalize_rc)`; lane poisons |
| F5 | returned | `Canonical` | `0` | no | inconsistent: `destruction_unknown`, retain/quarantine, no retry | **synthesized cleanup failure** — not success (step 15) |
| F6 | returned | `Noncanonical` | any | **unknown** | `destruction_unknown`, quarantine forever, no further device call | `finalize_rc` if nonzero, otherwise persisted wait/launch error if nonzero, otherwise synthesized cleanup failure (step 16) |
| F7 | **threw inside** C finalize | `Zero` | — | yes | `destruction_unknown` — teardown progress unknown despite the tag; storage quarantined, never recycled | **rethrow original**; lane poisons |
| F8 | **threw inside** C finalize | `Canonical` | — | **unknown mutation** | `destruction_unknown`, quarantine, **no retry** (step 14) | **rethrow original**; lane poisons |
| F9 | **threw inside** C finalize | `Noncanonical` | — | **unknown** | `destruction_unknown`, quarantine | **rethrow original**; lane poisons |

`[design]` Rows **F7–F9** are the r6 correction: a throw from inside the C finalize is treated as
unknown mutation **whatever the tag says**, restoring r5's quarantine and withdrawing r6's
restore-and-retry. Only **F4**, where the call *returned* the context refusal, keeps the run
retryable — and only at the low level (§6). **F5 and F6 can no longer return success**, which r6
allowed.

**Worked example — the `wait(-7)` case the review named.** Entry starts `LAUNCHED`,
`pending_rc` unset. Step 11's own wait returns `-7`: the finalizer **persists**
`exec_state = COMPLETED, pending_rc = -7` before touching the C finalize. The C finalize then returns
the context refusal with `finalize_rc = -5` and a `Canonical` tag — row **F4**. The owner restores
`exec_state = COMPLETED` (not `LAUNCHED`) and keeps `pending_rc = -7`; the caller gets `-5` by
precedence rule 1; the lane poisons. A later low-level finalize on a valid context re-reserves, reads
`COMPLETED`, **does not wait again**, and if its own `finalize_rc` is `0` reports the preserved `-7`.
Under r6 the restore would have put it back to `LAUNCHED` and the second wait would have destroyed the
`-7`.

#### 2.3.6 Finite interleavings

**(a) waiter + active op + finalizer.** T1 `poll` holds the lease and is inside `poll_run_fn_`. T2
`wait` sees `ops_in_flight == 1` and sleeps. T3 `finalize` reserves and sleeps in the drain. T1
returns `COMPLETE`, re-looks-up, sets `exec_state = COMPLETED` **despite the reservation**
(invariant 4), clears `ops_in_flight`, notifies. T2 wakes, re-looks-up, hits the
**`AlreadyComplete` fast path** at step (2) — before the reservation check — and returns without a
runtime call. T3 wakes, reads the **post-drain** `exec_state == COMPLETED` at step (10), **skips the
wait entirely**, and calls `finalize_run_fn_` once.

**(b) two competing finalizers.** One wins the `false → true` transition; the other throws *"another
finalization is in progress"*, never drains, never reads `raw`, never destroys.

**(c) a completed wait's error survives.** T1 `wait` returns rc = −7: it sets
`exec_state = COMPLETED` and latches `pending_rc = -7`. T3's finalize reads `exec == COMPLETED`,
**does not wait again**, and reports −7 unless `finalize_rc` outranks it. r5 would have re-waited and
overwritten it.

**(d) a failed joined launch that nonetheless completed.** `launch_run_joined_fn_` returns a nonzero
rc; the probe poll says `COMPLETE`; the owner sets `exec_state = COMPLETED` and latches that rc, then
throws — today's behaviour, now expressed as a typed outcome. A later finalize skips the wait and
reports the latched rc.

**(e) waiter wakes after erasure** ⇒ `Absent` ⇒ today's stale throw; no dereference of recycled
storage.

**(f) other-run map growth.** A rehash while T2 sleeps is invisible: T2 re-looks-up by key.

**(g) the non-destroying early finalize return.** `validate_current_context()` refuses; nothing threw;
tag is `Canonical`; the owner clears the reservation and keeps the run owned. **The lane still
poisons** (§6) — low-level retryability is not lane availability.

**(h) noncanonical tag.** The owner quarantines, never retries, rethrows; the storage leaves the free
list permanently and still counts against the grant.

### 2.4 The truthful direct-C concurrency rule

`[source]` `native_run_context` reads a magic from the handle and compares `state->runner` against the
context (`c_api_shared.cpp:1085-1099`); `destroy_native_run_context` zeroes the magic afterwards.

`[design]` Those are **validation, not lifetime protection**. The supported rule, which main already
requires and this design does not change:

> The C run entries are **not** safe to call concurrently on the same `RuntimeHandle`. A direct caller
> must serialize its own calls per run, and must not call any entry concurrently with
> `simpler_finalize_run` on that handle. The magic and runner checks catch a *stale* handle presented
> after finalization completed; they do not make concurrent calls safe.

`ChipWorker` satisfies that rule through §2.3's exclusive lease. **No ABI mechanism is added.**

---

## 3. The parent/child protocol

### 3.1 Owners from the source

`[source]` `LocalMailboxEndpoint::try_publish_activation` (`worker_manager.cpp:922-931`) is
**parent-owned**: `frame_identity_matches` then
`mailbox_compare_exchange_state(frame, FRAME_STAGED, ACTIVATE)`; `frame_identity_matches`
(`:895-919`) compares the full tuple. `[source]` The frame tail already *is* the run state
(`worker.py:371-387`), and `accepted_state` is `frame_addr + _OFF_ACCEPTED` (`:3293`) — a pointer the
**device** writes.

### 3.2 The terminal store is the child's final access — no child ack

`[source]` `publish_completed_frame` (`worker.py:3655-3676`): `_write_error(frame_buf, code, msg)`
then `_mailbox_store_i32(frame_addr + _OFF_STATE, TASK_DONE | TASK_FAILED)` then
`staged_frames.pop(staged.index, None)`. `[source]` The teardown sweep writes `TASK_FAILED` only to
frames in a **live** state; `TASK_DONE`/`TASK_FAILED` are not in that set.

`[design]` The migrated loop must preserve exactly that order — rc and text first, terminal state
last, drop tracking immediately after — which is what lets the ack be removed: the parent's acquire
load of the terminal sees rc and text, and from that instant the parent is the entry's sole accessor.

### 3.3 Single-writer fields and two independent credits

`[design]`

| Field | Sole writer | Edge | Reader |
|---|---|---|---|
| `E.identity{…}` | parent, before publish | plain, released by `F.state` | child, identity check |
| `E.entry_generation` | parent, on allocate and free | release | child, to refuse a stale reference |
| `F.state` (`TASK_READY`) | parent | release | child acquire |
| `E.frame_consumed_generation` | **child**, once | release | parent acquire — a **level** |
| `E.state` `FRAME_STAGED → ACTIVATE` | **parent**, CAS, identity-checked | acq_rel | child acquire |
| `E.state` later, `E.run_epoch`, `E.rc`, `E.error_text` | **child** | rc/text plain, then `E.state` release — the child's **final** access | parent acquire |
| `E.acceptance word` | **device**, at the kernel-launch marker | `__ATOMIC_RELEASE` (`native_run_context.h:58-63`) `[source]` | parent/child acquire |

| Region | Count | Per entry | Credit |
|---|---|---|---|
| control frame | 1 | 64 KiB | unchanged |
| request frames `F` | `K`, default **2** | 64 KiB | concurrent **hand-off** |
| run-state entries `E` | `M = granted + K` | ~336 B | concurrent **in-flight runs** |

**No launch is accepted without both an `E` and a granted set.**

### 3.4 Sequence

```
parent  allocate E; bump E.entry_generation (odd ⇒ in use); write identity tuple
parent  claim free F; write callable/config/lease/args; write {E index, E.entry_generation,
        F.frame_generation} into F's header;      rel F.state = TASK_READY
child   acq F.state; verify F header against E; copy the args blob out;
        rel E.frame_consumed_generation = F.frame_generation;  rel E.state = FRAME_STAGED
parent  acq E.frame_consumed_generation == the F it published ⇒ reclaim F (idempotent: a level)
parent  identity-check E, then CAS E.state FRAME_STAGED → ACTIVATE          ← parent-owned
child   acq ACTIVATE ⇒ activate; adopt run_epoch; rel E.run_epoch;
        rel E.state = PREPARE_READY / TASK_LAUNCHED as today
device  writes E.acceptance word at the kernel-launch marker
child   terminal: write rc + inline text, then rel E.state = TASK_DONE/TASK_FAILED  ← LAST access
child   drop E from its own tracking (immediately after, §3.2)
parent  acq terminal ⇒ consume it into the caller's RunHandle (§5.3);
        bump E.entry_generation (even ⇒ free); return E to the free entry list
```

**Missed pulse is impossible** — both cross-side reclaim decisions are **levels**. **Reuse ABA is
impossible** — every cross-side reference carries `E.entry_generation`; a stale one is refused and
takes today's `poison_progress(...)` path.

**Death / timeout / close** `[source]` reuse `check_child_death()`, `shutdown_child()` and
`poisoned_progress_quiesced()` unchanged, walking occupied **entries** on the same rule. A child that
dies without publishing a terminal leaves `E` occupied; the parent reports `ENDPOINT_FAILURE` and
**`E` is reclaimed only at endpoint teardown**.

---

## 4. Resources

### 4.1 Negotiation, checks and rollback

`[source]` `ChipWorker::init` sets
`resolved_contract.pipeline_depth = min(requested ?: kDefaultRunResourceSets, declared)`
(`chip_worker.cpp:375-378`); the child publishes it; `worker.py:12499` range-checks;
`_granted_chip_pipeline_depth` **fails startup** on an under-granted explicit request.

`[design]` Preserved, with four edits: `pipeline_contract.h:45` drops the `> PTO_PIPELINE_MAX_DEPTH`
clamp and keeps every other check; the grant becomes §1.4's two-way `min`; the four makers declare
`UINT32_MAX`; `worker.py:12499`'s range becomes `1..` the published grant. **Checked sizing at the
actual allocation point:** `M × sizeof(entry)` and `K × MAILBOX_FRAME_SIZE` against the shm limit at
mailbox creation; per-set regions keep today's grow-only checks at bind. **Rollback** is the existing
`ChipWorker::init` `try` that destroys the context and rethrows (`:398-408`).

### 4.2 `RunResourceSet`, `RunResourceArena`, three counts

`[design]` A set groups what is already co-indexed by slot: arena bank, `SlotPersistentArgs`,
`RunCompletionFence`, boundary marks, retained temp bump, graph staging, SM mirror, run-image
staging, scheduler-state storage, device-run-result region, host-phase state.

```
acquire():  free non-empty                   -> pop (reuse, no construction)
            free+live+quarantined < granted  -> construct lazily (may fail, §7)
            else                             -> nullopt  (backpressure; never blocks)
release(set)    -> free        (only via §5.2's two predicates)
quarantine(set) -> quarantined (never reused; still counted against the grant)
```

A `destruction_unknown` entry's host storage is quarantined on the same terms (§2.2).

`[source]` `DeviceRunnerBase::DeviceRunnerBase()` (`device_runner_base.cpp:256-265`) eagerly
constructs **three** `ArenaBank` and **three** `RunCompletionFence` objects regardless of the grant;
the `DeviceArena`s inside a bank commit device memory on demand. `[design]` Those become per-set and
are constructed on first `acquire()` — a **host object count** statement. **No device-byte claim is
made in either direction.**

### 4.3 A free set never bypasses the compatibility gate

`[design]` `permits_native_successor` → `supports_concurrent_native_prepare` →
`prepared_run_config_compatible_impl` is asked **before and independently of** `acquire()`. On TMR a
config change that would rebuild the shared arena is refused exactly as today.

---

## 5. Lifetimes, retirement, and the durable outcome

### 5.1 `M` derived from the grant, queued requests outside it

`[design]` `M = granted + K`. Requests beyond `M` wait in the parent's logical FIFO, plain host
memory bounded by `pending_run_depth`, holding no transport entry.

### 5.2 Release at the last consumer — two registrations, separate from §2.2

`[source]` `workspace_manager.h`:

```cpp
static bool retired(const RunRecord &r) {
    const bool device_settled = r.drain_proved || r.no_submission;
    return device_settled && r.copyback_returned && r.bindings_released;
}
```

`[source]` `c_api_shared.cpp:2139-2150`: `copy_back_run_outputs_impl` runs **first**, then
`CopybackReturned` *"after its outputs have come back through them"*, then
`release_run_bindings_impl` — and `BindingsReleased` **only when its code is zero**, because *"a
failed release may leave a borrower behind."* `[source]` `chip_run_lane.cpp:250-270` discharges the
caller borrow **after** finalize and only with `proven_done`.

`[design]` A **resource set** returns to `free` only when both discharge:

| Registration | Owner | Discharged by |
|---|---|---|
| workspace block refs (`bindings_released`) | `WorkspaceManager` | the run's own finalize, zero-rc release only |
| caller borrow + write declaration | `CallerDeviceBuffers`, keyed by the epoch (§2.1) | `release_device_spans(run, proven_done)` |

**These are not the same decision as §2.2's.** `magic == 0` authorizes recycling the **host storage
block and the entry**; the resource set waits for the two predicates above. A run can have its
context destroyed and its set still held.

### 5.3 The durable late-read contract is the outcome, not tensor bytes

`[source]` **`RunHandle.result()` is an alias for `wait()` and a successful run returns `None`**
(`worker.py:4899-4901`). `_publish_terminal` (`:4754-4770`) latches `_terminal` and caches `_error`.
`[source]` Outputs go to the caller: the makers walk per-run `TensorLease`, HOST bytes land in the
**bound caller destination**, a null host pointer means device-only with no copyback, and DEVICE data
stays in the **caller-owned allocation**.

`[design]`

1. **What the handle durably owns is the outcome** — terminal reached, and the error object when there
   is one; `_publish_terminal`'s cache makes **repeated** `wait()`/`result()` return the same outcome
   after any number of entry and set reuses. **No RunHandle-owned copy of any output tensor, no new
   API.**
2. **The record consumed before its storage is reusable** is the run's own *published execution-result
   record* — the epoch-guarded region read through `HostApi::run_result(...)` /
   `device_run_result(slot, epoch)`, turned into a status by `read_published_run_status`, consumed into
   this run's terminal **during its own finalize**, before the set is released (§5.2).

**Caller output lifetime is unchanged and not extended.** Old values are not preserved after the
caller or a later legal run overwrites the same destination.

| Object | Born | Dies |
|---|---|---|
| request (FIFO entry) | `Worker.submit` admitted | terminal consumed into the handle, or `close()` |
| request frame `F` | parent `rel TASK_READY` | `E.frame_consumed_generation` observed |
| entry `E` | parent allocates | terminal consumed; generation bumped |
| run object | lane admission | destroyed **inside the C finalize entry** (§2.2) |
| host storage + `RunEntry` | lane admission | erased and recycled only on row **F2/F3** (returned, tag `Zero`); quarantined on **F5–F9** |
| resource set | first `acquire()` that cannot reuse | **§5.2's two predicates** → `free`; or `quarantined` |
| **outcome** | `_publish_terminal` | with the `RunHandle` |

---

## 6. Failure policy — existing lane policy preserved

`[source]` Four lane sites poison: finalize error (`chip_run_lane.cpp:250-263` — **every** finalize
exception), poll error (`:538-545`), wait error in drain (`:576-587`) and in `block_on_front`
(`:604-608`); and `finalize_native_run` throws whenever its folded rc is nonzero
(`chip_worker.cpp:1301-1305`). `[source]` A **task** error is distinguished from a **cleanup**
failure at the Worker level (`worker.py:12811+`), where cleanup ambiguity keeps ownership and blocks
admission.

`[design]` **r5's row saying a non-destroying finalize return "is not poisoned by itself" is
withdrawn.** Retaining a low-level handle for a retry on its valid context does **not** mean the lane
continues: the throw reaches `finish()` and the lane poisons, exactly as today. The two levels are
distinct and both are stated:

| Outcome | Low-level owner state | **Lane** | Caller borrow / set |
|---|---|---|---|
| **pre-launch refusal** — bind/allocation failure, incompatible config, no set available | run never launched | **not poisoned** — today's recoverable refusal | borrow discharged or never taken |
| **NotStarted** (rollback, sink unmarked) | `exec_state = PREPARED` | not poisoned | released |
| **poll Error** | no state change, throws | **poisoned** (`:538-545`) | protected until finalize |
| **wait nonzero rc** | `COMPLETED`, rc latched, no throw | poisoned when finalize reports it | §5.2 |
| **Complete, execution rc ≠ 0** | latched | **poisoned**, as today | released once §5.2 holds |
| **Partial** (quiescence unknown) | latched | poisoned | **set quarantined** |
| **finalize returned the context refusal** (§2.3.5 **F4**, tag `Canonical`, rc ≠ 0) | **run still owned, low-level retryable**; post-wait `exec_state` restored so a retry never re-waits | **poisoned** — no implicit retry, no new public recovery | borrow protection retained |
| **finalize returned `Canonical` with rc 0** (**F5**, inconsistent) | `destruction_unknown`; synthesized cleanup failure; no retry | poisoned | borrow protection retained; set quarantined |
| **finalize threw before entering C finalize** (wait throw, **F1**) | post-wait `exec_state` restored, **certainly not destroyed** | poisoned | borrow protection retained |
| **finalize returned tag `Noncanonical`** (**F6**) | `destruction_unknown`, storage quarantined forever; **never a silent success** | poisoned | set quarantined |
| **finalize threw inside the C entry** (**F7–F9**, any tag) | `destruction_unknown` — **unknown mutation**, no further device call, **no retry** | poisoned | set quarantined |
| **recovery/reset failed** | abandoned host-side | poisoned; existing abandon branch | **no per-resource device call issued** |

**No allocation error becomes a lane poison**; the task-vs-cleanup distinction is untouched; there is
**no teardown-always-releases claim** and **no new user-visible recovery path** — `close()` remains
the only stop, and a poisoned lane stays poisoned.

---

## 7. Finite allocation failure

| Condition | Behaviour |
|---|---|
| grant reached, all sets live or quarantined | `acquire()` → `nullopt`; the request waits in the FIFO; **not an error** |
| under the grant, construction or a bind allocation fails | that run's **pre-launch failure**; grant not lowered; existing sets untouched; **lane not poisoned** |
| allocation fails during `ChipWorker::init` | existing rollback: context destroyed, rethrow, **startup fails** |
| FIFO at `pending_run_depth` | `Worker.submit` blocks — unchanged |

---

## 8. Worked examples

`[design]` **Default, unconfigured:** `granted = min(2, UINT32_MAX) = 2`, `K = 2`, **`M = 4`**
(≈ 1.3 KiB). One set is constructed on the first submission.

**Configured, N = 8:** `pipeline_depth=8, launch_depth=4, pending_run_depth=64` ⇒ `granted = 8`,
`K = 2`, **`M = 10`**:

```
submit 1..8    FIFO 1..8; entries E1..E8; sets S1..S8 constructed lazily (8 constructions, ever)
               1..4 LAUNCHED; 5..8 PREPARED holding S5..S8
submit 9..64   FIFO grows to 64; no entry and no set free ⇒ host-side; submit does not block
run1 drain proved → copyback returned → zero-rc binding release → borrow discharged
               ⇒ S1 → free;  terminal consumed into handle 1;  E1 freed (no ack, §3.2)
               ⇒ run 5 (already PREPARED, next in FIFO) LAUNCHES
               ⇒ run 9 takes E1 and S1 and PREPARES behind run 8
steady state   64 runs retire through 8 reused sets and 10 entries; no construction after the 8th
submit 65      FIFO at 64 ⇒ Worker.submit blocks until a terminal is consumed
```

**TMR runs this identically** (§1.2). An incompatible sizing at run 6 is refused early by §4.3 and
drains to the depth-one path — **not** a poison.

**Protocol interleavings** are §2.3.6 (a)–(h).

**Late read after several reuses.** Handle 1's outcome was cached at terminal; E1 and S1 then serve
runs 9, 17, 25…; `handle1.wait()`/`.result()` at any later point — including after `close()` —
returns the same outcome from the cache, touching no entry, no set and no result region.

---

## 9. Public API and defaults

`[design]` No new key, no new identifier, no version field, no new return value, **no new recovery
surface**.

| Key | Meaning | Default | Change |
|---|---|---|---|
| `pipeline_depth` | resource-set request | `2` | granted per §1.4; **under-grant still fails startup**; no budget required |
| `pending_run_depth` | host-side logical FIFO bound | `0` ⇒ derive | unchanged |
| `launch_depth` | how many may be on the device at once | `1` | bound becomes the grant |

`RunHandle.wait()` / `.result()` keep their signature and `None`-on-success contract. The only new
caller-visible message is `"another finalization is in progress"` (§2.3.5).

---

## 10. Multi-endpoint

`[design]` Each endpoint owns its arena, grant, lane and table (the lane is already per `ChipWorker`,
`chip_worker.cpp:675` `[source]`). `worker.py:8961`'s single `min(...)` across endpoints becomes a
**per-endpoint granted count**. DEVICE chaining is accepted **within** one endpoint only; a
cross-endpoint device address is refused with a clear error. The Worker-level result is the
aggregation of per-endpoint outcomes.

---

## 11. Admission: a prefix, not a head

`[design]` Replace `dispatchable_locked`'s `active_run_id_ == run_id` and the single latched
`early_launch_run_id_` with the prefix the lane already implements: the orchestrator leases a set to
every FIFO entry it can, oldest first, while `acquire()` succeeds; `launch_depth` still bounds how
many are on the device. Device order is unchanged.

---

## 12. DFX: epoch-keyed banks, host-side, with a named progress source

`[source]` The bank reaches the device as an **address**:
`KernelArgs::chip_swimlane_run_terminal_bank` (`kernel_args.h:112`) written by the host at
`a2a3/platform/onboard/host/device_runner.cpp:1592-1593`; the AICPU reads it at `aicpu/kernel.cpp:80`;
sim takes the same value through `set_platform_chip_swimlane_run_terminal_bank_func_`
(`sim/host/device_runner.cpp:549`). `PLATFORM_RUN_TERMINAL_BANKS = 3` is ccec-compiled
(`chip_swimlane_profiling.h:437`) and `arm_run_terminal_bank` rejects an index ≥ 3
(`chip_swimlane_collector.cpp:1555-1565`).

`[design]` Remapping is **host-only and adds no capability**.

| Site | Owner | When |
|---|---|---|
| selector field in this run's `KernelArgs` | host runner | arming, before launch — same call site, same field |
| bank assignment | collector's `bank_epoch[PLATFORM_RUN_TERMINAL_BANKS]`, keyed by `run_epoch` | at arm |
| arm / reset | collector, per bank | at arm, as today |
| read | collector, `read_run_terminal_snapshot(bank, run_epoch)` | after the run's drain |
| **release → reuse safe** | collector | **only when `report_run_terminal_snapshot` returns** |
| sim | identical host path and hook | unchanged |

**A fourth diagnostic run waits, with a named progress source.** When all three banks are assigned,
`arm` reports "no bank"; the run keeps its diagnostic, stays `PREPARED`, and the join is declined
**for this round only**. `[source]` `permits_joined_diagnostic_launch` is *"Re-asked every round and
never remembered … capacity comes back."* **The progress source is a retirement**: the round that
finalizes a diagnostic run calls the collector's report, which releases that bank, and the same round
re-asks. No thread blocks on a bank and nothing is dropped.

`[design]` The `static_assert` at `device_runner_base.cpp:2891` / sim `:1090` becomes an assertion
that `bank_epoch` has exactly `PLATFORM_RUN_TERMINAL_BANKS` entries. `c_api_shared.cpp:2413`'s
retention-limit refusal is unchanged; `joined_diagnostic_launch_supported_impl` keeps its single
strong definition `[source]`. **No diagnostic semantics are widened.**

---

## 13. Acceptance — one PR, tests included

`[design]` `[untested]` — authored, none executed.

| # | Case | A3 HBG | A5 HBG | A3 TMR | A5 TMR |
|---|---|---|---|---|---|
| 1 | default: device behaviour unchanged; `M = 4`; one set for a single run | ✓ | ✓ | ✓ | ✓ |
| 2 | `pipeline_depth=8, launch_depth=4`: ≥ 4 distinct runs launched+accepted at one coherent instant | ✓ | ✓ | ✓ | ✓ |
| 3 | 64 sustained submissions through 8 reused sets, `M = 10`; construction stops at 8 | ✓ | ✓ | ✓ | ✓ |
| 4 | **TMR compatible N > 2**: 8 queued runs hit the prebuilt-arena cache; no bind writes a device byte; device order serial | — | — | ✓ | ✓ |
| 5 | TMR incompatible sizing drains to depth one with a free set, and **does not poison** | — | — | ✓ | ✓ |
| 6 | request above the grant **fails startup**; **no budget required**; sim still refuses one | ✓ | ✓ | ✓ | ✓ |
| 7 | sustained submit without waiting: `M` never exhausted; backpressure is `pending_run_depth` | ✓ | ✓ | ✓ | ✓ |
| 8 | FIFO launch order: a freed set goes to the next unprepared run | ✓ | ✓ | ✓ | ✓ |
| 9 | set withheld until copyback returned **and** zero-rc binding release **and** borrow discharged | ✓ | ✓ | ✓ | ✓ |
| **10** | **poll mapping**: `0` ⇒ false and no state change; `1` ⇒ `COMPLETED` and **nothing written to `pending_rc`**; any other code ⇒ **throws** with today's message and no state change | ✓ | ✓ | ✓ | ✓ |
| **11** | **completed fast paths**: a second `poll`/`wait` on a `COMPLETED` run returns without entering the runtime and without taking the lease | ✓ | ✓ | ✓ | ✓ |
| **12** | **wait never repeated**: a wait that returned rc = −7 latches it and sets `COMPLETED`; the later finalize **skips the wait** and reports −7 | ✓ | ✓ | ✓ | ✓ |
| **13** | **failed joined launch that completed**: probe poll `COMPLETE` ⇒ `COMPLETED` + latched launch rc, then throw; finalize skips the wait and reports the latched rc | ✓ | ✓ | ✓ | ✓ |
| **14** | **`UNSUPPORTED` decline** changes no state and is not an error | ✓ | ✓ | ✓ | ✓ |
| **15** | **execution facts survive reservation**: an op completing under reservation sets `COMPLETED`/latches its rc, and the finalizer reads the **post-drain** state | ✓ | ✓ | ✓ | ✓ |
| **16** | **exception plumbing**: every throwing path rethrows the captured `exception_ptr` after the tail; **no bare `throw;` outside a handler** (a terminate-free assertion) | ✓ | ✓ | ✓ | ✓ |
| **17** | **F1** wait throw before C finalize: object certainly not destroyed; **post-wait** `exec_state` restored; reservation cleared; rethrown; lane poisons | ✓ | ✓ | ✓ | ✓ |
| **17a** | **the `wait(-7)` → `Canonical` rc ≠ 0 case (§2.3.5 example)**: `-7` is persisted before the C finalize; the caller gets `finalize_rc` by precedence; the restored state is `COMPLETED`; a later low-level finalize **does not re-wait** and reports the preserved `-7` | ✓ | ✓ | ✓ | ✓ |
| **17b** | **precedence**: `finalize_rc` outranks `wait_rc`; `wait_rc` has exactly one source per the §2.3.5 table; no value is written twice | ✓ | ✓ | ✓ | ✓ |
| **18** | **F2/F3** tag `Zero` ⇒ entry erased and **host storage** recycled, while the **resource set is still withheld** until §5.2's predicates | ✓ | ✓ | ✓ | ✓ |
| **19** | **F4** returned `Canonical` with rc ≠ 0 ⇒ run owned and low-level retryable, borrow protection retained, **lane still poisons** | ✓ | ✓ | ✓ | ✓ |
| **19a** | **F5** returned `Canonical` with rc 0 ⇒ synthesized cleanup failure, quarantined ownership and no low-level retry | ✓ | ✓ | ✓ | ✓ |
| **20** | **F6** returned `Noncanonical` ⇒ `destruction_unknown`, quarantined forever, counted against the grant, and **a failure is surfaced even when every rc was 0** | ✓ | ✓ | ✓ | ✓ |
| **20a** | **F7–F9** threw inside the C finalize, **each tag** ⇒ `destruction_unknown`, quarantined, **no retry and no further device call**, original exception preserved | ✓ | ✓ | ✓ | ✓ |
| 21 | §2.3.6(a) waiter + active op + finalizer: waiter hits `AlreadyComplete`; finalizer skips the wait; one destruction | ✓ | ✓ | ✓ | ✓ |
| 22 | §2.3.6(b) two finalizers: exactly one enters `finalize_run_fn_`; the other throws | ✓ | ✓ | ✓ | ✓ |
| 23 | §2.3.6(e) waiter wakes after erasure ⇒ `Absent` ⇒ stale throw; no dereference of recycled storage | ✓ | ✓ | ✓ | ✓ |
| 24 | §2.3.6(f) another run's admission rehashes `runs_` while a waiter sleeps; the waiter is unaffected | ✓ | ✓ | ✓ | ✓ |
| 25 | two concurrent operations on one run are serialized — `poll` not-ready, `wait` blocks then proceeds | ✓ | ✓ | ✓ | ✓ |
| 26 | repeated late `wait()`/`result()` after several reuses and after `close()` return the **same outcome**; `None` on success | ✓ | ✓ | ✓ | ✓ |
| 27 | a failed run's late `wait()`/`result()` returns the same error object every time | ✓ | ✓ | ✓ | ✓ |
| 28 | entry/frame recycle: stale `entry_generation` refused and poisons progress; `F` reclaimed exactly once on a late poll; **no child write after the terminal** | ✓ | ✓ | ✓ | ✓ |
| 29 | execution rc ≠ 0 poisons the lane; each already-launched successor still reports its **own** outcome | ✓ | ✓ | ✓ | ✓ |
| 30 | Partial: set quarantined, lane poisoned, no further launch, close reports it, no per-resource free on abandon | ✓ | ✓ | ✓ | ✓ |
| 31 | a bind/allocation failure is a **pre-launch refusal, not a poison**; the next submission still succeeds | ✓ | ✓ | ✓ | ✓ |
| 32 | error-text pressure: every one of `M` concurrent failures keeps its own full text and attribution | ✓ | ✓ | ✓ | ✓ |
| 33 | caller DEVICE allocation refused in flight, accepted after retirement | ✓ | ✓ | ✓ | ✓ |
| 34 | close drains an unwaited prefix of > 3 runs | ✓ | ✓ | ✓ | ✓ |
| 35 | two endpoints: per-endpoint grants; each reaches its own overlap; outcomes aggregate | ✓ | ✓ | ✓ | ✓ |
| 36 | two endpoints: cross-endpoint DEVICE address refused with a clear error | ✓ | ✓ | ✓ | ✓ |
| 37 | diagnostics: with swimlane on and 8 sets, a 4th diagnostic run waits for a bank **and still collects**; released only on report; mapped by epoch | ✓ | — | — | — |
| 38 | child death holding an entry ⇒ `ENDPOINT_FAILURE`; entry reclaimed only at teardown | ✓ | ✓ | ✓ | ✓ |
| 39 | sim parity: same admission, retirement, refusals, destruction boundary and bank mapping; no joined launch | ✓ | ✓ | ✓ | ✓ |

### 13.1 Fixture plan, corrected

`[design]` r6 pointed rows 10–25 at `tests/ut/py/test_early_enqueue_evidence_reader.py`. **That is
wrong and is withdrawn:** that file is a *Python* test over the scene suites' log-parsing helpers; it
is not a C++ runtime fixture and cannot inject a `poll`/`wait`/`finalize` outcome. Three distinct
homes, each matching an existing pattern:

| Rows | Home | Why it fits |
|---|---|---|
| 10–25, 28 — the operation/finalization protocol: poll and wait outcome tables, the two completed fast paths, F1–F9, the precedence table, the `wait(-7)` case, the four interleavings, entry-generation refusal | **new native C++ fixture** under `tests/ut/cpp/common/worker/`, with **injectable** `poll_run_fn_` / `wait_run_fn_` / `finalize_run_fn_` stubs and a settable magic word | this is the seam `tests/ut/cpp/common/worker/test_chip_run_lane.cpp` already uses — `int launch_run(void*, void*)` stubs with per-case return codes `[source]` — extended with a stub that can also *throw*, return each poll code, and leave the tag `Zero`/`kMagic`/garbage. Two-thread cases drive a real `std::thread` against the owner's mutex and CV. |
| arena three-count accounting, grant arithmetic, `M = granted + K`, quarantine against the grant, frame reclaim as a level, borrow/declaration keyed on one epoch, `bank_epoch` assignment/report/release | **native C++ unit tests** under `tests/ut/cpp/common/{worker,platform}/` | pure host logic over the owner's own structures, no device and no runtime stub needed |
| 1–9, 26–27, 29–39 — public behaviour | **scene tests** under `tests/st/{a2a3,a5}/{host_build_graph,tensormap_and_ringbuffer}/early_enqueue/` | they are `Worker.submit` claims and need real hardware |

`[design]` The protocol rows are **fault-injection** cases, not new public recovery: the injectable
stub manufactures F1–F9 deterministically, with no sleep, no device and no timing oracle. Rows 17a,
19a, 20a in particular are only constructible with such a stub, which is why naming the fixture
matters.

`[open]` A stub that can leave a *garbage* magic word writes into storage the owner allocated for the
fixture, never into a live runtime's. The fixture therefore links the owner's structures, not a built
`libhost_runtime.so`; the `tests/ut/py/support/*_probe.cpp` pattern that links the real library stays
for the contract checks that need it, and is not where F1–F9 belong.

---

## 14. File change map

`[design]` One PR, tests and docs included. **I** = also edited by #2563 (`b697`) — **integrate the
files**, no merge dependency; its worktree is not touched.

| Area | Files |
|---|---|
| grant | `runtime_c_api.h` **I**, `pipeline_contract.h`, `pipeline_slot_pool.h` → `RunResourceArena`, the four `runtime_maker.cpp` declarations, `python/simpler/worker.py` **I** |
| identity + lease | `chip_worker.{h,cpp}` **I**, `chip_run_lane.{cpp,h}` **I**, `native_run_context.h` **I** (comment only — the destroy helper is unchanged), `host_api.h`, `c_api_shared.cpp` **I**, `sim/c_api_shared.cpp` |
| protocol | `worker_manager.{h,cpp}`, `orchestrator.{h,cpp}`, `python/bindings/worker_bind.h`, `worker.py` **I** |
| per-run resources | `device_runner_base.{h,cpp}` **I**, `sim/device_runner_base.{h,cpp}` **I**, `run_boundary_marks.h`, `run_progress_slots.h`, `run_stream_pair.h`, `queued_stream_waits.h`, `workspace_manager.h` |
| DFX banks | `chip_swimlane_collector.{h,cpp}`, the two `static_assert` sites, `{a2a3,a5}/platform/{onboard,sim}/host/device_runner.cpp` **I** |
| tests | `tests/st/{a2a3,a5}/{host_build_graph,tensormap_and_ringbuffer}/early_enqueue/`, `tests/ut/cpp/common/{worker,platform}/`, `tests/ut/py/` |
| docs | `docs/worker-manager.md`, `docs/tensormap-and-ringbuffer-a2a3-vs-a5.md`, `Worker.submit` docstring |

**Not touched:** every `src/{a2a3,a5}/runtime/**` device source except the four contract
declarations; and the C finalize entries' **destruction position**.

---

## 15. Open-PR overlap

`[source]` **#2563 at `b697`** — additive identity-by-value, no depth expansion; **Ready, CI 19
success / 1 skipped, original author idle**, no shared writer and no edit to its worktree. The 12
shared files are a **file-integration** concern only; this design does **not** depend on that PR
merging. **#2562** (dispatcher third phase) repairs the collect-but-never-run gap the native and
Python unit rows depend on. **#2561** (cleanup-time poison) touches §6's quarantine path — track, do
not duplicate. **#2028** last moved 2026-08-28; its stream half is superseded by merged
`run_stream_pair.h` and #2557 — not a prerequisite. #2533, #2538, #2540, #2545, #2546 are adjacent
graph/ACK correctness with no capacity coupling. #1888, #1967, #1987, #2026, #2116, #2120, #2217,
#2304 are out of scope (§17). #2436 is a consumer. #2453, #2493, #2494, #2564 do not touch §14.

---

## 16. Response to the r6 findings

| # | Finding | Response |
|---|---|---|
| **1** | the finalizer's own wait loses its completion/error on a non-destroying C return; apply the same mandatory persistence as the ordinary wait path; state the real `finalize_rc`-over-`wait_rc` precedence and remove the contradictory "first error wins" and intermediate rewrites; include the `wait(-7)` example | **Accepted.** §2.3.5 step 11 now **persists** the finalizer's own wait into the entry — `exec_state = COMPLETED` and `pending_rc = rc` when nonzero — **before** the C finalize can be entered, identical to the ordinary wait path. Step 13's `Canonical` branch restores that **post-wait** state, so a retained handle never re-waits and never loses the rc. The "first error wins" prose and every intermediate `wait_rc` reassignment are **deleted** and replaced by **one** precedence table citing `chip_worker.cpp:1300-1305` (`finalize_rc` first, otherwise `wait_rc`) plus a second table showing `wait_rc` is **single-sourced** and its two sources mutually exclusive. The `wait(-7)` → `Canonical` rc ≠ 0 walk-through is written out at the end of §2.3.5 and is acceptance rows 17a/17b. |
| **2** | `Noncanonical` can return success because no exception is synthesized; `Canonical` + rc 0 is an inconsistent non-destroying success, not a normal one | **Accepted.** §2.3.5 adds `synthesized` and two steps: (15) `Canonical` + `finalize_rc == 0` raises *"finalize returned success without destroying the run context"*; (16) `Noncanonical` with no nonzero rc anywhere raises *"finalize left the run context in an unknown state"*. The closed outcome table makes **F2 the only clean success row** — every other row ends in a rethrown original or a synthesized cleanup failure, so no silent path exists. A captured exception always takes priority over a synthesized one. Acceptance rows 19a, 20. |
| **3** | `Canonical` after a **thrown** C finalize proves no complete destruction, not safe retry; r5 quarantined it and r6 restored it; keep the exception-origin distinction | **Accepted; r6's restore-and-retry withdrawn.** §2.3.5 step 14 treats **any** throw from inside the C finalize as **unknown mutation whatever the tag reads** — rows **F7–F9** all set `destruction_unknown`, quarantine the storage, forbid any further device call, and rethrow the original. The tag is now **only consulted when the call returned**, which §2.2 states explicitly. The three origins stay distinct: **F1** pre-C wait throw ⇒ certainly not destroyed, today's rollback; **F4** returned context refusal ⇒ owned and low-level retryable; **F7–F9** in-C throw ⇒ retained/quarantined, no retry. `magic == 0` still authorizes recycling the **host storage only**, with §5.2's resource-set predicates separate. Lane poisoning is unchanged for all of them (§6). Acceptance rows 17, 19, 20a. |
| **4** | clarify the fixture plan: the Python evidence-reader test is not a C++ runtime fixture | **Accepted.** New §13.1 withdraws that pointer and names three homes: a **new injectable native C++ fixture** under `tests/ut/cpp/common/worker/` for rows 10–25 and 28 — extending the stub seam `test_chip_run_lane.cpp` already uses so a stub can return each poll code, throw, and leave the tag `Zero`/`kMagic`/garbage, with real threads against the owner's mutex and CV; **native unit tests** for the pure host arithmetic; and **scene tests** for the public rows. The protocol rows are deterministic fault injection with no sleep, device or timing oracle. |

Also refreshed: **#2563 at `b697` is Ready, CI 19 success / 1 skipped, author idle**; the dependency
stays **file integration only** (§14, §15).

### Carried from r5, still accepted

| r5 # | What it fixed | Where it lives now |
|---|---|---|
| 1 | poll numeric semantics and the completed fast paths | §2.3.2 outcome table; `acquire_op` step (2) |
| 2 | execution facts written under reservation; no repeated wait | §2.3.1 invariant 4; §2.3.5 steps 10–11 |
| 3 | `exception_ptr` capture and rethrow; the three magic states | §2.3.4; §2.2; §2.3.5 |
| 4 | lane poisons on every finalize exception | §6 two-level table |

Carried unchanged because accepted: all four backends and sustained N > 3 in one feature; wake-up
loops that re-find and revalidate; reservation independent of phase; the direct-C concurrency
disclaimer; one destruction at the existing C boundary; optional budget, lazy allocation, caller-owned
outputs; the `grant 2 / K 2 / M 4` arithmetic; no device-memory claim; outcome-not-tensor late reads;
terminal-as-last-access; independent diagnostic bank capacity and progress source.

---

## 17. Out of scope

Cross-device addressing; communication/collectives (HOLD — `permits_joined_launch` already ends with
`!holds_live_comm_resources()`, neither widened nor narrowed); capture (preserved property: a run's
published identity is independent of host ordinal **and** of any resource index); kernel mode;
remote/MPI; raising `PLATFORM_RUN_TERMINAL_BANKS`; a unified cross-Worker allocator.

---

## 18. Costs

`[design]` **Device memory** — per set, the runtime's own per-run `PipelineResource` regions plus the
host staging it owns, committed on demand at bind exactly as today. **No device-byte claim in either
direction.**
**Host memory** — `M × ~336 B` shm table plus `K × 64 KiB` frames per endpoint: at the default grant,
`M = 4` ≈ 1.3 KiB and 2 frames = 128 KiB, against today's 3 × 64 KiB = 192 KiB of task frames; plus
one map node and one `RunEntry` per live run. Per-set host objects become lazy, where today three of
each are constructed eagerly `[source]`.
**Locking** — the arena mutex is taken *inside* the existing `native_run_mu_` / lane-mutex regions and
never the reverse; **no lock is held across any C call**; the only waits are `ops_done_` (bounded by
the one in-flight operation) and the orchestrator's existing admission wait.
**Per run** — one free-list pop/push, one map insert/erase, one lease acquire/release per operation,
one 8-byte `memcpy` per finalize that entered the C boundary.

`[untested]` **No throughput measurement exists and none is claimed.**
