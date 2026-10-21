> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# memory lane — closeout audit after merged #2487 / #2473

Read-only. Exact main `01bf24bfa8a997c89210258c5954c4105843d783` via `git show`; worktree left at its
old head, nothing reset, built, run or published. `#2487` = `ef38a0ed8`, `#2491` = `67bce6291`,
`#2490` = `01bf24bfa` — all merged, no fix loops resumed.

## 1. First: the old A3 cause audit does not survive as proof

**Unresolved historical evidence.** What survives on `01bf24bfa` is the *mechanism*, not the cause:

- `tests/st/a2a3/host_build_graph/{early_enqueue,worker_async_fifo}` still set
  `_DEVICE_SPIN_ITERS = 200_000_000` (lines 140 / 60), and
  `worker_async_fifo/kernels/aiv/delayed_add.cpp:29-30` still spins that count on a `volatile int32_t`.
- `src/a2a3/platform/include/common/platform_config.h:90` still sets
  `PLATFORM_SCHEDULER_TIMEOUT_MS = 20000`, below `PLATFORM_OP_EXECUTE_TIMEOUT_US = 45000000` (line 77).

So the observed `-100` is still `SIMPLER_ERROR_SCHEDULER_TIMEOUT`
(`src/common/host_build_graph/runtime_status.h:42`, negated by `runtime_status_from_error_code`), and the
one instance that carried locators was `sub_class=S1:running-stalled completed=1/513 running=1
stuck_core=28` — a kernel still executing, on a `tensormap_and_ringbuffer` run
(`runtime_maker.cpp:1038` is TMR's `LOG_RUNTIME_FAILURE`, not HBG's).

What is **not** proven, then or now: *why* that die exceeded 20 s. The first failing case's own
device-side log was never captured (its child's stderr was swallowed by pytest; no
`ASCEND_PROCESS_LOG_PATH` artifact exists), and no AICore clock/thermal telemetry was collected. The
correlation evidence is still retrievable — jobs `109713748663` (this lane) and `109721325620` (#2096,
a disjoint file set) both report `Runner name: 'DEVICE_HOST'` and the same
`-100` / `507018` / `comm_init` / `S1` signature family, and both ended on the 1200 s pytest session
timeout (`exit 124`) — but correlation on a shared runner is not attribution. **Green CI on later heads
is not root-cause proof and is not offered as one.** I am closing this as unresolved historical
evidence rather than carrying it forward; re-opening it needs a device-side log that does not exist.

## 2. Delivered capability

| | |
| --- | --- |
| `#2473` | Four managed regions on every onboard program context: three pooled arena regions (`Domain::ExecScratch`) plus the per-slot retained staging buffer (`Domain::HostStaging`) — `workspace_manager.h:122-129`. Reference / quarantine / obsolete-generation discipline, optional budget (off by default), and `workspace_report` out to Python. |
| `#2487` | The run's Graph Definition section travels in the AICPU launch arguments. No device Definition block, no independent alloc/upload/free, no manual fallback; per-reader byte-safe views; fanin CSR copied by value into execution storage. |

Spot-checked as actually wired on `01bf24bfa`: `DeviceRunnerBase::acquire_arena_backing`
(`device_runner_base.cpp:488-504`) routes arena commits through `workspace_.acquire`;
`acquire_retained_temp` (`:600-652`) takes a managed block and `note_published`s it, keeping the previous
generation addressable until its last consumer retires; `release_arena_backing` (`:519-525`) refuses to
free a ledger-owned block. Arena banks are `std::array<…, PTO_PIPELINE_MAX_DEPTH>`
(`device_runner_base.h:2592`, depth 3 — `runtime_c_api.h:133`) and commit lazily, so depth-1 programs
commit one bank. That matches "L3 on-demand/reuse, no mandatory budget"; nothing here imposes one.

## 3. Candidates examined and rejected (so they are not re-proposed)

- **Per-slot / per-context blocks outside the ledger** — scheduler-state storage
  (`acquire_scheduler_state_storage`, which calls `mem_alloc_.begin_reservation()` directly),
  `device_wall_dev_ptrs_`, `device_run_result_dev_ptrs_`, AICore register tables
  (`device_runner_base.cpp:2498-2552`), `SlotPersistentArgs::runtime_args`. **Not a gap**: this exclusion
  is deliberate and already declared in two places —
  `runtime_c_api.h:406-409` and `workspace_manager.h:72-76` both enumerate "external tensors, run-result
  and diagnostics regions, registered code and device ELF, RTS argument blocks and provider memory".
  Pulling them under the budget would also contradict the standing no-mandatory-budget direction.
- **Registered-code / orch-SO upload** — already content-hash deduped and refcounted per device context
  (`device_runner_base.cpp:1509-1523`, `record_device_orch_callable:1860-1871`). Uploaded once per
  distinct content; not re-uploaded per run.
- **Quarantined bytes stranded after a confirmed reset** — looked promising, is **vacuous**:
  `force_reset_device()` (`a2a3/platform/onboard/host/device_runner.cpp:1132`) has exactly one caller,
  inside fatal teardown (`:1353`), after which the context is finalized. No surviving context carries the
  stranded bytes, so there is nothing to reclaim.
- **HBG has no prebuilt-arena cache** (the cache exists only in TMR —
  `a2a3/runtime/tensormap_and_ringbuffer/host/runtime_maker.cpp:395-402, 706-726`). Real asymmetry, but
  the reuse route for a whole-graph-resident bind is capture/replay, which this contract excludes.

## 4. Remaining user-visible gap

**A user cannot size `workspace_budget_bytes` against their device, nor attribute an OOM to inside or
outside the budget, because the one number that would answer both is computed and never reported.**

- `SimplerWorkspaceReport` (`src/common/worker/runtime_c_api.h:419-432`) carries `limit_bytes`,
  `reserved_bytes`, `relinquished_bytes`, `quarantined_*` — all scoped to the two managed domains — plus
  `coverage_is_partial`, which `workspace_manager.h:682` hardcodes to `1` in every report.
- So the report states *that* coverage is partial and never *by how much*. `worker.py:8469` reads it only
  for the `live_blocked` teardown refusal.
- The missing datum already exists: `MemoryAllocator::committed_bytes()`
  (`src/common/platform/include/host/memory_allocator.h:260`) sums the requested size of every
  currently-tracked device allocation this context holds — managed and unmanaged alike — and its own
  doc-comment says it exists so downstream runtimes "account for device HBM this process has reserved
  (which may be invisible to `aclrtGetMemInfo`)". `git grep committed_bytes` over `src` and `python` on
  `01bf24bfa` finds it only inside the two `memory_allocator.cpp` files: it reaches no report, no C API,
  no binding, no Python surface.

This is a reporting gap, not an allocation defect. It touches no tensor lifetime, needs no budget to be
mandatory, and adds no flag.

**Not covered by open work.** All 30 open PRs were checked against the manager snapshot; the only two
whose titles touch these words are #2453 (Leaf-Salix — TMR report epochs, kernel clear removal) and #2059
(wxwnnzdyd — PTO-ISA *kernel async* workspace: `pto_isa.pin`, `comm_hccl.cpp`, SDMA; a different concept,
and its file list touches neither `workspace_manager` nor `runtime_c_api`). The excluded ownerships
(#2494 nalinaly, #2453 Leaf-Salix, #2120 georgebisbas, #2116 superxf, #2388 qweasdzxcht, plus #2028,
#2424, #2436, #2493, #2067, #2179, #2183) are untouched by this finding.

## 5. One next bounded task — needs new contract approval

**Report the context's total committed device bytes alongside the partial-coverage flag.** Add one
`uint64_t` to `SimplerWorkspaceReport` fed from `MemoryAllocator::committed_bytes()`, thread it through
`DeviceRunnerBase::workspace_report` (`device_runner_base.cpp:593`) and
`simpler_get_workspace_report_ctx` (`c_api_shared.cpp:2280`) to the existing binding
(`task_interface.cpp:3816-3847`), and say in the field's doc that it is a **stock** figure — what is held
now — making no claim about allocation counts or rates, for which no counter exists.

Why it needs approval rather than being in scope already:

1. It changes a versioned wire record — `WORKSPACE_REPORT_SCHEMA` (`runtime_c_api.h:394`) is `1` and
   `chip_worker.cpp:682` rejects any report whose schema does not match exactly, so the bump and both
   sides must land together. That is a public-contract change, not maintenance.
2. It is new product source in a lane whose implementation authorisation ended with #2487.

If the manager prefers no new task, this finding stands as documentation of a known, declared limit and
needs nothing: the exclusion is already written down in two headers, and nothing is incorrect today.

No performance claim is made anywhere above. No capture/replay, no new flag, no budget made mandatory.
