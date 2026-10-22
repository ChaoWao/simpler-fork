# Claude 修订复核（0de1e010b）

2026-10-22，只读复核 S1/S2 与作者处置。最终 `a579b791a` 相对此被审代码只有一行产品文档变化：修正下文指明的 buf_seq 表格行；src/tests 树相同。CI 的最终状态单独记录在 [REPORT](REPORT.md)，不从此处推定。

---

# Recheck: PR #2663 at final head `0de1e010b`

The rewrite is still one amended commit, and the production change since `d8f2696d0` is small: three enable guards in `scope_stats_collector_aicpu.cpp`, plus comment and doc edits. The rest of the delta is one new UT and a stronger Python assertion. None of it reopens the broader investigation. S1 is resolved except one doc line the correction missed; S2 is fully resolved.

## Correction to my original evidence

You're right: my "before the PR" example was overbroad. At base `fdcbdcc95`, ordinary admission zeroed the counters and the held header, including the host shadow, so a skipped device entry or a failed held-buffer read would not have exported the predecessor's records.

What base actually had:
- synchronous host reads and writes of producer state at admission;
- an unchecked shared-region read at reconcile;
- in mapped mode, device init only once orchestration started.

The stale-held-shadow export was only reproduced on an intermediate implementation with host adoption removed and no copy guard. The new copy and epoch checks are regression guards for this PR's own move. They do not fix a stale export that existed at base. This makes the base fix narrower than I first stated, but doesn't change the meaningful-PR assessment.

## Original findings

**S1 (stale comments and docs): resolved, except one doc line.**
- `scope_stats.h:119-123` now names the "AICPU init leader … before handshake and orchestration". It separates the mapped path (device terminal) from the ordinary/unmapped path (host boundary). That matches the code.
- `docs/dfx/scope-stats.md:224-241` makes the same distinction and adds the disabled-invocation rule.
- The `local_seq` correction is right. `current_buf_seq` is zeroed only at host init; the engine advances it on each pop and switch (`profiler_device_engine.h:180,263`), and device `begin_run` copies it without resetting.
- **New inconsistency:** `docs/dfx/scope-stats.md:277` (the `buf_seq` row) still says "Buffer generation within that run. **Restarts per run**". That now contradicts the corrected header, and per `doc-consistency.md` it belongs in this commit. It's a one-line fix.

**S2 (OFF runs writing shared scope-stats state): resolved.**
- `begin_run`, `flush_buffers` and `close_run` now return early when `!scope_stats_enabled`.
- The flag is set on every launch before `aicpu_execute`: onboard `a2a3/kernel.cpp:91` and `a5/kernel.cpp:109`, and in both sims at `device_runner.cpp:604`. That covers exactly the case where sim keeps the base across runs.
- Your point about flush is valid. Before this change, an OFF run on sim with a non-empty held buffer would have enqueued that buffer, even though the code's own comment said disabled flush was a no-op.
- The new `DisabledProducerLifecycleLeavesAResidentPoolUntouched` test holds the resident base, a non-empty held buffer and a valid bank, and checks that all their bytes are unchanged.
- I confirmed it independently. Building the final head in `LOCAL_PATH` with only the flush guard removed makes that test fail on a5.
- Your `LOCAL_PATH` reports all 6 single-guard mutants (3 guards × 2 arches) failing.

**C1 (silent refusal on the ordinary path): resolved.** The docs now say ordinary collection is best effort and log-only, and that retained-mode error contracts are unchanged.

**C2 (test strength): partially resolved, accepted.**
- The Python test now requires distinct epochs across the ON invocations.
- The producer OFF UT models the sim resident-base condition statically.
- I agree it does not prove the `pre_handshake_init` ordering at runtime. That ordering rests on code reading: the leader runs `begin_run` before it releases `hs_setup_done_`, and the orchestrator waits on that flag.
- A sim end-to-end test remains optional. It doesn't block.

**C3 and C4:** I accept both dispositions. The quarantine policy is unchanged, and the host-only admission fixture with a simulated epoch is a reasonable unit boundary now that real device init has its own tests.

## New concerns from the delta

None in the code.
- Gating `close_run` on the enable flag cannot drop a real publication: disabled runs always get `scope_stats_run_terminal_bank = 0`.
- The tests that call the producer all set the enable flag explicitly. My shuffled ×3 runs of all nine scope-stats and admission suites passed on both arches: collector 9, read_failure 1, retained 25, TMR collector 3, TMR admission 7.

## CI evidence

- **Prior head `d8f2696d0`:** I confirmed from `LOCAL_PATH` and `LOCAL_PATH` that `test_worker_initializes_scope_stats_each_invocation` passed for owned and borrowed on both a2a3 and a5. That result belongs to the old head only.
- **Final head:** CI has just started (build and detect-changes pass, pre-commit pending). No hardware result exists yet for this revision.

## Remaining actionable issues

- **Should fix:** the `docs/dfx/scope-stats.md:277` `buf_seq` row's "Restarts per run" claim, as above.
- **Gate:** final-head CI green, including `ut-a2a3`, `ut-a5` and `st-onboard-a5`. Once equivalent CI hardware validation passes, I'm not asking for an extra local `task-submit` run.

## Meaningful-PR assessment

Unchanged: yes. The PR delivers a complete behaviour change for one DFX owner:
- ordinary and retained modes share one device-entry initializer;
- synchronous producer reads and writes are gone from host admission;
- the mapped-mode window where an init failure inherited the predecessor's counters is closed;
- disabled invocations are now provably inert;
- stale exports are blocked by epoch and copy checks.

It has mutation-sensitive tests on both arches. It is not a placeholder seam.

## Revised verdict

**Approve** once the `buf_seq` doc row is fixed and final-head CI is green. Nothing blocks on correctness.
