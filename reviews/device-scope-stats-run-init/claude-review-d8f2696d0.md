# Claude 独立审查原文（历史 head d8f2696d0）

2026-10-22，通过 Simpler 仓的 `review-pr 2663` 工作流进行只读审查，参照同目录设计原则与路线。以下保留原始审查结论，不作为修订后代码的规范。S1/S2 的处理和 CI 实际日志核对见 [REPORT](REPORT.md)。审查时未能下载的硬件日志，作者随后通过 Actions job logs API 确认四个新增组合均实际通过。

注意原文 §1 的 before-PR 例子范围偏宽：held-buffer shadow 复制失败的精确回归是在移除 host adoption 后的中间实现上复现；base 的 host adoption 会先清/重盖 shadow header，不能将该中间实现的原样故障称作已验证的 base 故障。原文的 mutation 和源码判断保留各自范围。

---

# Review: PR #2663, "Update: initialize scope stats at each device invocation"

Head `d8f2696d0`, merge-base `fdcbdcc95` (upstream main), one commit. This was a read-only review: I ran no NPU work, posted nothing to GitHub and changed nothing in the repo. All scratch builds went to `LOCAL_PATH`.

## 0. Summary

Before this PR, ordinary scope-stats admission did synchronous device reads and writes from the host: it zeroed two producer counters with `publish_field`, then read `current_buf_ptr`/`current_buf_seq` back and uploaded a re-stamped buffer header. Retained mapped runs instead initialized on the device, from inside `run_orchestration`.

The PR makes every mode use one device initializer, run by the AICPU init leader before `pre_handshake_init`. The device now publishes the run's epoch into existing padding. The host, on the ordinary and unmapped paths, refuses to recover or export unless that epoch matches this run and its reads of the shared region and held buffer succeed.

The mechanism is correct as far as I can trace it, and its key tests are mutation-sensitive. I found no reproducible defect. What remains is comment/doc drift, one conditional behaviour change on scope-stats-OFF runs, and some test-strength gaps.

**Verdict: approve with follow-ups.** It is not merge-ready yet only because CI is still partly pending and there is no A3 `task-submit` result.

## 1. Background and problem

The scope-stats producer (the AICPU orchestrator thread) keeps four counters in `ScopeStatsBufferState`. At run end it may still hold a buffer in `current_buf_ptr`. A successor that reuses that pointer never pops a buffer, so nothing re-stamps the buffer's `count`, `run_epoch` or `local_seq`.

At base `fdcbdcc95`, two modes handled this differently:
- **Ordinary/unmapped:** `ScopeStatsCollector::begin_run()` (host) cleared `dropped`/`total` and pushed them with `publish_field`. `adopt_current_buffer()` then did two narrow D2H reads and one header H2D. On the device, `scope_stats_aicpu_begin_run()` returned early when `s_run_terminal_bank == 0`.
- **Mapped retained:** the device reset everything, but only once `run_orchestration` started. An init failure before that point left the predecessor's counters behind.
- **Reconcile:** `reconcile_counters()` copied the shared region and the held buffer back to the host without checking either copy. A failed copy on a detached shadow left the previous run's bytes in the host shadow, and those were then exported.

The stated goal (PR body, ROADMAP C1, REPORT) is to move all producer-state initialization out of host admission onto the device-entry boundary. That fits C1's "no synchronous metadata memcpy in admission" target and principle §10/§145 ("initialize after the old user ends, before the first new read or write"). The user's framing and the PR body agree. I found no goal downgrade.

**Concrete example (ordinary mode, before the PR):** run 11 leaves a held buffer with 1 record, which run 11's boundary recovers. Run 22 is admitted and the host re-stamps the buffer. If run 22's device entry never runs, or the held-buffer D2H fails on a detached shadow, `reconcile_counters` re-reads stale shadow bytes and `write_jsonl` publishes run 11's accounting as run 22's. `AnUnstartedRunCannotRecoverOrExport…` and `HeldBufferReadFailureCannotRecover…` model exactly this case.

## 2. Solution and rationale

**Device side** (`scope_stats_collector_aicpu.cpp:303-331`):
- `scope_stats_aicpu_begin_run()` no longer gates on the terminal bank.
- It zeroes all four counters and re-stamps the held buffer's header, leaving the pointer, the sequence number and the free-queue cursors untouched.
- It then issues `wmb()`, publishes `state->run_epoch = get_platform_run_result_epoch()`, issues `wmb()` again, and clears the terminal-bank entry.
- The call moves from `run_orchestration` into `AicpuExecutor::init`'s leader branch, right before `pre_handshake_init`, in both `a2a3/…/aicpu_executor.cpp:325` and `a5/…:327`.

**Why the placement is safe:**
- The only producer is the orchestrator thread. Whenever `nthreads > 1` it is a non-leader, and non-leaders spin on `hs_setup_done_` (acquire), which the leader sets (release) after `begin_run`.
- With `nthreads == 1` the leader is the orchestrator.
- AICore never touches scope-stats state.
- The predecessor's AICPU op has finished, because AICPU ops on the stream run in order.

So the reset happens after the old writer ends and before the first new write. That satisfies principle §145. Putting it in the "AICPU entry" is acceptable here only because no other device reader or writer exists.

**ABI:** `_pad[9]` becomes `_reserved` + `volatile uint64_t run_epoch` + `_pad[6]`. Both `static_assert`s (size, and offset == FreeQueue+32) hold. The host shm is zeroed at `init()`, so a run that never initialized reads epoch 0, which never matches.

**Host side:**
- `begin_run` is now host-only: records, receipts, `recovered_*`, `active_run_epoch_`.
- `reconcile_counters` goes through `read_terminal_checked()`, which checks the D2H and compares the epoch in `snapshot_unchecked()`. It checks the held-buffer D2H as well.
- `write_jsonl` requires `device_snapshot_valid_`.
- The mapped retained path never consults `snapshot_unchecked()` (`run_close_locked_path` reads banks and receipts only), so a stale `active_run_epoch_` there is harmless.

**Alternatives:** keeping the host adoption and just checking its copies would still leave a synchronous admission-time device write, which C1 forbids. Moving the call to `run_orchestration` for every mode would leave the init-failure window open. The chosen shape mirrors the already-merged #2653 (PMU) and #2660 (args dump). That rationale is implied by the ROADMAP but not written in the PR body.

## 3. Implementation vs. claims

| Stated goal | Design choice | Code location | Assessment |
|---|---|---|---|
| Host admission clears only the host window | `begin_run` host-only; `adopt_current_buffer` / `begin_run_host_state` deleted | `scope_stats_collector.cpp:176-189` | ✅ (UT memcmp of shm + header) |
| One device initializer for every mode | bank gate removed; call moved into init leader | `scope_stats_collector_aicpu.cpp:303`, `aicpu_executor.cpp:325/327` | ⚠️ [1] |
| Before handshake/config can fail | placed before `pre_handshake_init` | same | ✅ (not tested at executor level [4]) |
| Epoch published in padding, layout unchanged | `run_epoch` at +32, `static_assert`s | `scope_stats.h:155-170` | ✅ |
| No stale export on skipped entry or failed read | epoch check + checked shm/held D2H + `device_snapshot_valid_` | `scope_stats_collector.cpp:436-459, 563, 651`; `retained_runs.cpp:103-119` | ✅ (mutation-verified) / ⚠️ refusal is silent [3] |
| Mapped banks/receipts lifetime unchanged | mapped `run_close` untouched; `terminal::begin_run` moves earlier | `retained_runs.cpp:257-274` | ✅ |
| Comment contracts synced (REPORT) | — | `scope_stats.h:119-121`, `docs/dfx/scope-stats.md:220-222` | ⚠️ [2] |

**[1]** Removing `if (s_run_terminal_bank == 0) return;` also removes the only thing that kept this from running on runs with scope stats disabled. Wherever the shared base survives into an OFF run, an OFF invocation now rewrites shared collector state. See S2.

**[2]** The `ScopeStatsBuffer::run_epoch` comment says the held buffer "is re-stamped by the next run's **orchestrator** … after the preceding boundary has recovered any records". The code actually does it in the init leader (tidx 0, normally a scheduler thread). For joined mapped runs, the predecessor's host boundary may not have run yet; its held records are reported through `live_at_close` instead. The docs sentence "its boundary has accounted for the old records" has the same inaccuracy.

**[3]** On the ordinary path, `close_scope_stats_run_boundary` discards the results of `reconcile_counters()` and `write_jsonl()`. The new `PTO_RUNTIME_ERR_INVALID_STATE` therefore never reaches the caller.

**[4]** No test drives `AicpuExecutor::init`, so moving the call back to `run_orchestration` would not fail any UT.

Stated goal and real goal match: this is a behaviour-moving update, not a refactor.

**Tests:**
- **Mutation check.** I restored the bank gate and disabled the epoch check in a `/tmp` copy of HEAD. `DeviceEntryOwnsAccounting…WithoutATerminalBank`, `AnUnstartedRunCannotRecoverOrExport…` and `HeldBufferReadFailure…` all failed. The tests therefore genuinely guard the change.
- **Negative-control limit.** A plain run of the new tests against the base source does not compile: they reference the new `run_epoch` field. The "fails before the fix" evidence is therefore the mutation result above, not a base-source run.
- **Hardware Python test.** `test_worker_initializes_scope_stats_each_invocation` (ON→OFF→ON→ON) would probably pass at base too, so it is coverage, not a regression barrier.

## 4. Residual issues

### Must fix

None. I found no reproducible defect.

### Should fix

**S1. Comment and doc drift: these contract sentences don't match the code.**
- `src/common/platform/include/common/scope_stats.h:119-121` says "re-stamped by the next run's orchestrator … after the preceding boundary has recovered". It should name the AICPU init leader, before handshake. For mapped joined runs, the predecessor's held records are reported by its `live_at_close`, not recovered by a host boundary.
- `docs/dfx/scope-stats.md:220-222` says "its boundary has accounted for the old records". This is not true for joined mapped runs, where the host close can run after the successor's device init.
- REPORT.md §"两轴复核" says the comment contracts are already synced. That is inaccurate for this line. Per `doc-consistency.md`, fix it in the same commit.

**S2. Scope-stats-OFF invocations now write shared scope-stats state on sim (conditional, not a reproduced failure).**
- `scope_stats_aicpu_begin_run()` gates only on `s_scope_stats_state != nullptr`.
- On sim, `kernel_args_.scope_stats_data_base` is a runner member. It is set in `init_scope_stats` and only zeroed in finalize (`a2a3/platform/sim/host/device_runner.cpp:1248, 1282`; a5 equivalent at `:1057`).
- `set_platform_scope_stats_base_func_` re-publishes it on every launch (`:605`).
- Result: after one ON run, every OFF run's leader zeroes the counters, sets the held buffer's `count=0`, and overwrites `state->run_epoch`.
- Onboard, `PreparedExecution` is created fresh per run (`a2a3/platform/onboard/host/device_runner.cpp:377`) and `init_scope_stats` only runs when scope stats is enabled, so the base is 0 and the call is a no-op. Sim and onboard therefore now diverge. At base, the bank gate made it a no-op on both.
- It is harmless today because `chip_run_lane.cpp:648-688` refuses to overlap runs whose `enable_scope_stats` differ, so an OFF successor launches only once it reaches the front.
- If that lane rule is ever relaxed (for example, capture or replay mixing DFX selections), an OFF successor's init would make an open ON predecessor's unmapped close fail its epoch check, or read zeroed counters.
- Suggested fix: gate on `is_scope_stats_enabled()`, which both entries set before `aicpu_execute` (`kernel.cpp:91`, sim `:604`). Alternatively, zero the sim base for OFF runs. Add a sim ON→OFF assertion that an OFF run leaves `state->run_epoch` and the held header untouched.

### Consider

- **C1. Refusals are silent on the ordinary path.** `close_scope_stats_run_boundary` (`onboard/host/device_runner_base.cpp:4902-4906` and its sim twin) discards the new `INVALID_STATE`. An epoch mismatch or a held-buffer D2H failure on an otherwise successful run yields no file plus a `LOG_ERROR`, nothing else. The default path was already best-effort, but now that the PR adds a concrete refusal code, either surface it or say in the docs that only the log reports it.
- **C2. Test strength.**
  - The only end-to-end test is hardware-only (a2a3/a5). Parametrizing it for `a2a3sim`/`a5sim` would cover it on every PR, and sim is exactly where S2 lives.
  - `_check_scope_stats` checks that each file has a single non-zero epoch, but not that consecutive ON invocations have distinct epochs or that the epoch matches the run.
  - No test covers the executor-level ordering, for example a failure inside `pre_handshake_init` after `begin_run`.
- **C3. Mismatches quarantine on the retained-unmapped path.** There, an epoch mismatch goes through the `quarantine_locked(...)` branch (`retained_runs.cpp:344-348`) even when completion was proven, which blocks later scope-stats runs. This was already the behaviour for terminal-read failures, and the path is mostly a fixture state, so it is low impact.
- **C4. Test fixture writes the field by hand.** `test_diagnostic_admission.cpp:172-175` sets `state->run_epoch` directly to stand in for device init. That's fine, but it couples the fixture to the internal layout. A one-line call to `scope_stats_aicpu_begin_run()` would model the device more faithfully.

### Follow-ups and uncovered cases

- A3 `task-submit` result `task_20261022_000018_70153720238` is still pending.
- CI is pending for `st-onboard-a5`, `ut (ubuntu)` and `ut (macos)`.
- `ut-a2a3` and `ut-a5` passed. They run `-m requires_hardware`, so the new Python test most likely ran, but the logs weren't available while the run was in progress.
- Neither the PR nor the REPORT makes a performance claim (the sync copies are removed, but nothing is measured). That's fine.

## 5. Merge recommendation

- **The problem is real (§1):** admission did synchronous device I/O, and an unchecked shadow re-read could export stale accounting.
- **The method solves it (§2):** a single device initializer at the leader, ordered before the producer through `hs_setup_done_`, plus an epoch-checked, copy-checked export.
- **The implementation matches the claims (§3),** apart from the stale comment and doc lines.
- **The residual risk is acceptable (§4):** no Must; S2 is conditional on a future lane change.

**Approve once S1 is fixed and the pending CI (`st-onboard-a5`, `ut`) and the A3 `task-submit` run come back green.** S2 should land here or as an immediate follow-up.

**Is this a meaningful PR?** Yes. It is a complete, end-to-end behaviour change for one DFX owner:
- the producer state moves to the device;
- a gap is closed for both modes (init failure before orchestration);
- an unchecked-D2H stale export is closed;
- it comes with mutation-sensitive tests and a hardware test.

It removes more core code than it adds (−216/+73) and follows the same C1 pattern as #2653 and #2660. It is not a disconnected seam or a padding-only change.

## Validation I performed

- Read the full merge-base diff, the callers (`kernel.cpp`, sim `device_runner.cpp`, `chip_run_lane.cpp`, the retained close path, `ensure_device_run_result_region`) and the five design docs.
- Built the scope-stats and admission C++ UTs at HEAD in `LOCAL_PATH`. All passed on both architectures: `scope_stats_collector` 8/8, `read_failure` 1/1, `retained_runs` 25/25, TMR `scope_stats_collector` 3/3, TMR `diagnostic_admission` 7/7.
- Mutation run (bank gate restored, epoch check off): the 3 new tests fail.
- Not done: no NPU runs, no Python UT run, and no executor-level or sim end-to-end run of my own.

**Uncertainties:**
- That `ut-a2a3`/`ut-a5` actually executed the new test is inferred from the workflow selector.
- S2's reachability relies on my reading that a non-joined successor launches only after its predecessor retires.

## Appendix A: Change breakdown

| Bucket | Files | + | − | = |
|---|---|---|---|---|
| Core | 10 | 73 | 216 | 289 |
| Test (incl. test CMake) | 6 | 310 | 6 | 316 |
| Docs | 1 | 14 | 9 | 23 |
| **Total** | **17** | **397** | **231** | **628** |

There is no oversized-PR warning and nothing uncategorized.

## Appendix B: pto-isa pin check (advisory)

ℹ️ `pto_isa.pin` is pinned to `b1d460ee8c001585ede36b68ba3678dc5a226bbf`. No pto-isa header references changed in this PR, so no bump or `host_runtime.so` rebuild is needed.

## Appendix C: Independent reviewer notes

No external CLI was requested, so none was run. The author's REPORT.md was used only as evidence. Its claim that the comment contracts were synced is contradicted by S1.
