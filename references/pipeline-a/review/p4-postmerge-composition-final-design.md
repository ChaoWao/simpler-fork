> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# Qwen dynamic-grant composition — final design

> **已被后续用户讨论替代，非当前实施合同。** 旧grant/depth CLI、4/2验收、固定N+1及零runtime改动方案撤回。当前方向是默认按资源自动准入与背压，见[现行设计要求](p4-qwen-grant-composition-decision.md)。以下保留历史源码分析与证据限制。

Read-only design. No product or test edit, no build, no test, no repro, no benchmark, no device call, no git mutation, no publication, no comment. Every source fact is `git show upstream/main:<path>` at the head in §0. This document replaces its own earlier revisions; §1.3 records the claim withdrawn in this one.

## 0. Baseline

| | |
|---|---|
| `upstream/main`, rechecked this round | **`653a24c4a61fde1ef8c28838b67a4a6dd37a916b`** — unchanged |
| #2571 in main | `78365f79ccd263e2e37cef827b69a0c4fa5f2eb5` (squash) |
| Open PRs, rechecked | 37, unchanged set (§7) |

**(S) Source fact** — read out of `653a24c4a`; a source read is not a test pass, and a formula in a driver is not a measured count.
**(H) Historical execution** — the accepted #2571 pre-merge evidence is **`a002acc47`** (`pr2571-a002acc47-ready-evidence.md`). Evidence about that head only; nothing about `653a24c4a` and nothing about any Qwen run.
**(X) New execution** — none, in any revision of this document.

## 1. Settled foundations

### 1.1 Three retirement boundaries (accepted)

| Boundary | Trigger | Code | Frees |
|---|---|---|---|
| Native resource reuse | the run's own terminal fence; no Python call | `orchestrator.cpp:200-222` → `:224-256` | FIFO entry + lease, under `runs_mu_`, then `runs_cv_.notify_all()`, successor leasing, `activate_fifo_head()` |
| HOST output validation / reuse | `RunHandle.wait()/result()` | `worker.py:5216` → `:5189` → `:13185` → `orchestrator.cpp:590-617` | retained `RunState`; emits `node.post_fence_retirement` |
| DEVICE last consumer | driver retention rule | `device_feedback_worker_submit.py:120-126`, `:371-376` | a sampled-token buffer, after its consumer run completed *and* the final audit read it |

Withholding `result()` does not hold the FIFO full: capacity returns at each native terminal, without the host.

### 1.2 The N+1 frame algorithm (accepted, unchanged)

`FRAME_COUNT = N + 1`, one submitter thread, validation cursor one step behind the admitted window:

```
pending = deque()                              # submitted, not yet host-validated
for i in range(steps):
    f = i % (N + 1)
    while pending and pending[0] <= i - (N + 1):        # release frame f's last owner
        j = pending.popleft(); handles[j].result(timeout=T); finish(j)
    bound = prepare(i, frame=f)                          # rewrite metadata[f]; NaN-fill out[f]
    handles[i] = submit(i, bound)                        # may block in begin_run
    pending.append(i)
for j in list(pending):                                  # final drain, same order
    handles[j].result(timeout=T); finish(j)
```

Why it is correct, and the limit of that claim:

- **No overwrite before admission.** Frame `f`'s previous owner is step `i-(N+1)`, validated by the loop above before `prepare` touches it. The `out[f]` NaN-fill follows that run's logit read; the metadata rewrite follows its bind by ≥ `N+1` submissions.
- **On the successful serial path**, step `i-(N+1)` lies one beyond the FIFO's capacity `N`, so it is already natively terminal when its `result()` is called; that call then performs host finalization only. **This is not a promise that `result()` never blocks** — it still takes locks, runs the finalization cursor and may transfer. The claim is only that it is not waiting on device completion.
- **Failure path keeps the conservative reading:** if a run fails or times out, the drain order is unchanged but no assumption is made about which runs are terminal; every held handle is waited on in submission order and nothing is reused.
- **`N` is a parameter.** `N = 4, L = 2` is the acceptance instance. `pipeline_depth` accepts `[1, UINT32_MAX]` (`worker.py:631-654`); the mailbox sizing cap is the separate `[1, MAILBOX_MAX_GRANTED_SETS]` policy (`worker_manager.h:204`, `:215`). No new ceiling.

### 1.3 Withdrawn: the caller-span / terminal inequality

The previous revision claimed a submission was *proved held* iff
`start(attempt[i]) < start(post_fence_retirement[i-N]) ≤ end(attempt[i])`.
**That claim is withdrawn in full, and nothing replaces it.** Two independent reasons (S):

1. **The interval can be satisfied with no wait at all.** The caller span opens at `t0`; the thread is then descheduled, or blocks acquiring `_submit_mu` (`worker.py:12635` — `submit` holds it exclusively across `_begin_run`) or `runs_mu_`; at `t1` the predecessor goes terminal and retires; only at `t2` does the thread evaluate the predicate, finds it **true on first evaluation**, and returns at `t3`. All three premises of the old argument hold and no condition-variable wait ever happened.
2. **The timestamp is not the capacity-release instant.** `trace_terminal_ns` is stamped inside `finish_run_if_ready` (`orchestrator.cpp:209`) **before** `retire_terminal_run` (`:224`) erases the FIFO entry under `runs_mu_`. The span's start therefore precedes the release it was being used to date.

What the driver may report instead is the bare fact, with no inference attached: *submission `i`'s interval overlapped the predecessor's terminal record; this alone cannot establish that admission blocked.* No timing threshold, no poll count, no retry, and no runtime instrumentation is proposed in its place. The capacity contract is carried entirely by the deterministic host layer (§4.1).

## 2. The feature

> **Sustained Qwen decode under a caller-chosen run-resource grant on one A3 HBG endpoint.**

Public capability: a caller chooses how many decode runs are concurrently admitted, runs the real model at that depth with the DEVICE feedback dependency intact, and gets a report that separates what was *configured*, what was *observed*, and what was *proved*.

**No product change.** Everything used already exists in `653a24c4a`: `pipeline_depth` / `launch_depth`, arena sizing and reuse, automatic native retirement, joined launch, and the existing host spans. No new API, no new span, no new counter, no gate.

### 2.1 Public API flow

```python
worker = Worker(level=3, platform="a2a3", runtime="host_build_graph",
                device_ids=[dev], num_sub_workers=0,
                pipeline_depth=N,          # existing key; default 2
                launch_depth=L)            # existing key; L <= N
worker.register(chip); worker.init()
handle = worker.submit(dispatch, args=None, config=cfg)   # blocks in begin_run when N are admitted
handle.result(timeout=T)                                   # HOST validation boundary only
worker.close()                                             # retirement basis
```

`pending_run_depth` stays unset, so `pending_run_limit_ == pipeline_depth` (`orchestrator.cpp:150`). Instance: **N = 4, L = 2, steps = 127**, one A3 HBG endpoint, diagnostics off. `L < N` is deliberate — equal budgets cannot separate a launch bound from a resource bound (`test_early_enqueue.py:2401-2407`).

### 2.2 Files

| Path | Status |
|---|---|
| `examples/a2a3/host_build_graph/qwen3_14b_serving_effective/grant_depth_worker_submit.py` | **new**, the only new production-tree file |
| same dir: `clone_worker_submit.py`, `device_feedback_worker_submit.py`, `reference_worker_submit.py`, `real_worker_submit.py`, `callable_bridge.py`, `reference_fixture.py` | **imported unchanged**, exactly as `device_feedback_worker_submit.py:20-37` already imports them |
| same dir: `README.md`, `callable_bridge.py` | **not edited** — #2436 has pending edits to both (§7) |
| `tests/ut/py/test_qwen_grant_depth.py` | **new**, driver-unit (stubbed `Worker`), mirroring `test_qwen_device_feedback.py`'s fixture style |
| `tests/ut/py/test_worker/test_host_worker.py` | **one case parameterized** to a caller-chosen `pipeline_depth` (§4.1) |

New driver docstring states the scope inline; no doc file is added or edited.

### 2.3 CLI

Identical in shape to the merged drivers (`device_feedback_worker_submit.py:48-54`) plus two depth keys:

```
grant_depth_worker_submit.py
  --fixture PATH        (required)   frozen reference KV/golden fixture root
  --artifact PATH       (required)   verified HBG artifact directory
  --model PATH          (required)   Qwen3-14B weights root
  --kv-reference PATH   (required)   reference decode KV
  --output PATH         (required)   new directory, created with exist_ok=False
  --device INT          (required)   A3 device id (held by task-submit)
  --steps INT           (default 127, must satisfy 1 <= steps <= fixture.steps)
  --grant INT           (default 4)   -> Worker(pipeline_depth=...)
  --launch-depth INT    (default 2)   -> Worker(launch_depth=...), must satisfy 1 <= L <= N
```

Refusals before any allocation or submission: `L > N`; `steps` outside the fixture; `--output` exists; `grant < 1`.

### 2.4 Resources and byte formula

Per frame, from the merged driver's shapes: `out` = `BATCH × PADDED_VOCAB` fp32 **host**; `next_hidden` = `BATCH × HIDDEN` bf16 **device**; metadata = `seq_lens` (`BATCH` i32) + `slot_mapping` (`BATCH` i32) + `block_table` (`BATCH*32` i32), **host**. Frames go `2 → N+1`.

```
device_extra  = (N + 1 - 2) * nbytes(next_hidden)
host_extra    = (N + 1 - 2) * (nbytes(out) + nbytes(seq_lens) + nbytes(slot_mapping) + nbytes(block_table))
device_total  = (steps) * nbytes(sampled_ids) + (N + 1 - 1) * nbytes(next_hidden)     # pre-submission check
```

`device_total` is the existing check generalized from `FRAME_COUNT` to `N+1` (`device_feedback_worker_submit.py:123-126`): compared against `worker.device_memory_info().free_bytes`, and on failure raises `MemoryError` → `status="capacity_blocked"` with **zero submissions**. Weights, RoPE and KV are unchanged and resident. A grant is a count and never a promise these bytes exist (`G3`).

### 2.5 Scope

**In:** one A3 HBG chip child (`worker=0`, `num_sub_workers=0`), diagnostics off (`enable_dep_gen=False`, `enable_chip_swimlane=0`), frozen 127-step reference, `N=4/L=2`, DEVICE sampled-token feedback, failure/close/last-consumer acceptance.

**Out, with reason:** A5 / TMR (no qualified joined-launch evidence shape for this workload); multi-endpoint or sub-workers (the joined and serial-boundary proofs are per chip child); diagnostics on (changes the retained-bank and buffer-return contracts — DFX's own track, §7); dynamic/variable request admission, vLLM serving, capture/replay, and any performance claim (explicitly out in the merged README `:194-198`); KV drift (#2459) — reported if seen, never absorbed.

**Clone fits by import, not extension.** `clone_worker_submit.py`'s `_await_joined_evidence`, `_first_join_evidence`, `_ordered_device_ticks`, `_assert_disjoint_storage` are reused verbatim. Two independent requests are *not* raised to grant `N` here: that would vary isolation and depth at once and leave a failure unattributable. It remains a separate candidate.

## 3. Driver-only reporting

`--output/result.json`, written in `finally` on every exit path (as the merged driver does, `:425-433`). Finite schema — no new channel, no log parsing beyond the spans the merged drivers already read:

```json
{
  "status": "correctness_passed | coverage_partial | failed | capacity_blocked",
  "configured": {"grant": 4, "launch_depth": 2, "frame_count": 5, "steps": 127},
  "resources": {"device_total_bytes": 0, "host_extra_bytes": 0, "device_extra_bytes": 0,
                "free_bytes_at_check": 0},
  "correctness": {
    "tokens": "passed | failed", "logits": "passed | failed", "kv": "passed | failed",
    "frame_discipline": "passed | failed", "last_consumer": "passed | failed",
    "joined_pairs": {"adjacent": 126, "joined": 0, "serial_fallback": 0},
    "retirement_basis": "worker_close | none"
  },
  "coverage": {
    "observed_peak_occupancy":  {"value": 0, "state": "reached | partial | inconclusive"},
    "set_reuse":                {"sets_seen_reused": 0, "of": 4, "state": "..."},
    "launch_vs_admit_separation": {"max_admitted": 0, "max_launched": 0, "state": "..."},
    "refill_beyond_grant":      {"value": 0, "state": "..."},
    "admission_overlap_observations": {"count": 0, "note":
      "interval overlap with a predecessor terminal record; does not establish that admission blocked"}
  },
  "steps": [{"step": 0, "frame": 0, "run_id": 0, "slot_id": 0, "generation": 0, "sampled": []}],
  "run_ids": [], "accepted_count": 0, "completed_count": 0, "retired_count": 0,
  "worker_closed": false, "error": null, "cleanup_error": null
}
```

Sources for the coverage numbers, all existing: `node.frame_submit` (`run_id`, `dispatch_id`, `slot_id`, `generation`, `prepare_only`), `chip.run`, `chip.run.joined_launch`, `chip.run.runner_run.aicpu_launch`, `chip.run.runner_run.device_boundaries`, `node.post_fence_retirement` — parsed with `simpler_setup.tools.strace_timing.parse_spans`, exactly as `device_feedback_worker_submit.py:165-205` already does.

### 3.1 Correctness gates versus coverage dimensions

**Correctness gates — any failure means `failed`:**

1. every step's sampled tokens equal the frozen reference;
2. logits within the existing `relative_l2` / `cosine` gates;
3. appended KV passes `validate_kv`;
4. frame discipline: for every `i ≥ N+1`, `finish(i-(N+1))` precedes `prepare(i)`, and each frame's NaN sentinel is consumed by exactly one run's publication;
5. last consumers: every sampled-token buffer read by the final host audit; no host buffer closed before `Worker.close`;
6. every run id appears in both `node.frame_submit` and `chip.run` with matching `dispatch_id`; a missing trace fails rather than passing silently;
7. ≥1 joined edge among the 126 adjacent pairs, i.e. not an all-fallback chain.

**Coverage dimensions — each reported on its own, never folded into a pass:**

| Dimension | `reached` when | Why it is machine-dependent |
|---|---|---|
| `observed_peak_occupancy` | the trace shows `N` runs concurrently admitted and non-terminal | if the host prepares slower than the device decodes, a correct chain may never hold more than two |
| `set_reuse` | each of the `N` `slot_id`s carries more than one run id, never an earlier run after a later one | a chain that never reaches depth `N` legitimately uses only some sets |
| `launch_vs_admit_separation` | `max_admitted > L` while `max_launched == L` | same rate dependence |
| `refill_beyond_grant` | ≥1 joined edge after the first `N` submissions | same |
| `admission_overlap_observations` | *never a pass criterion* — a count, with the note above | see §1.3 |

`partial` = the dimension was partly observed (e.g. 2 of 4 sets reused); `inconclusive` = not observed at all. **Neither is a failure and neither may be reported as having reached `N`.**

### 3.2 Overall classification

- **`correctness_passed`** — all seven gates pass **and** every coverage dimension is `reached`.
- **`coverage_partial`** — all seven gates pass, at least one coverage dimension is `partial` or `inconclusive`. The report names each one. This is the honest outcome on a host that cannot outrun the device, and it is **not** a pass of all claims.
- **`failed`** — any correctness gate fails, or `launch_vs_admit_separation` shows `max_launched > L`, or an admission is observed past the bound.
- **`capacity_blocked`** — the §2.4 pre-check refused; zero submissions.

The capacity contract (that `N` is admitted and the `N+1`-th blocks until a retirement) is **not** claimed from the real run in any of these states; §4.1 carries it.

## 4. Acceptance layers (classified by what actually runs)

### 4.1 A1 — deterministic native admission, real `Worker`, no device

`tests/ut/py/test_worker/test_host_worker.py`, extending the existing case at `:3735-3800` (real `Worker(level=3, num_sub_workers=…)`, SUB tasks blocking on shared-memory flags, no chip child) to a parameterized `pipeline_depth = N`. Deterministic on any machine; proves:

1. exactly `N` submissions admitted; the `N+1`-th parks — `native_orch._begin_run_waiter_count_for_test() == 1`, the standard `test_three_run_capacity.cpp:691-693` states: *"A caller is counted only once the FIFO bound has stopped it, so being counted is having met backpressure."*
2. no run id admitted while held, and the parked caller's graph callback has not run;
3. releasing **one** blocked task retires one run and admits exactly one more;
4. close with a parked submitter behaves as §5.2 specifies.

Already in main and **not duplicated**: the same park at grants 3 and 2 in C++ (`test_three_run_capacity.cpp:668-745`, `parks_until_one_of_three_retires`, `parks_until_one_of_two_retires`), which also proves one retirement is the release.

### 4.2 A2 — driver-unit, stubbed `Worker`, no device

`tests/ut/py/test_qwen_grant_depth.py`, using the fixture style of `test_qwen_device_feedback.py:300-412` — which monkeypatches `feedback.Worker` with a fake class and fake `Buffer`s (`:334`, `:386`), so **every case in that file is a stub, including the sentinel-injection case at `:440`.** *Correction: the previous revision classified that case as real-device Layer B. It is not.* A2 covers:

1. the N+1 frame cursor: for `N ∈ {1,2,4}` and `steps ∈ {1,2,N,N+1,127}`, no frame is prepared before its previous owner was validated, and the submission order is dense;
2. the §2.4 byte formula and its refusal (`free_bytes` stub, as `:212-214`);
3. report classification: a forced `partial` coverage value yields `coverage_partial`, a forced token mismatch yields `failed`, and neither mislabels the other;
4. failure paths: mid-chain raise, `result()` timeout, `close()` raise — each producing the §5.1 ownership outcome;
5. CLI refusals of §2.3.

### 4.3 B — real Qwen on A3 HBG, one device run

Proves only what a device can prove: the seven correctness gates of §3.1 and the measured values of the coverage dimensions. It does **not** carry the capacity contract (§4.1 does) and makes no performance claim.

## 5. Failure and close ownership

### 5.1 Failure

| Event | Owner | Required outcome |
|---|---|---|
| token / logit / KV mismatch | driver | stop admitting; drain every already-submitted handle in submission order; keep accepted run ids; `status=failed` |
| `result()` timeout | driver | keep the error primary; retain live consumers' buffers; do **not** record `retired_count` |
| capacity refusal | driver | `capacity_blocked`, zero submissions, no Worker created past `init` |
| run error reported natively | runtime → handle | the native verdict is primary; a diagnostics or cleanup error only logs behind it |
| `close()` raises | driver | record `cleanup_error`, retain host buffers, keep the execution error primary with the cleanup error in its context |

Host buffers are closed **only** after a successful `Worker.close` (the merged driver's rule, `:397-404`); `docs/buffer-abi.md:160-162` requires the caller to retain host backing through completion, and `release_buffer` refuses while any in-flight or abandoned run names the identity (`worker.py:12500-12510`).

### 5.2 Close with a parked submitter — ordering supported by the current API

Facts (S): `submit` holds an operation lease for its whole duration (`worker.py:7388-7432`) and `_submit_mu.exclusive()` across `_begin_run` (`:12635`). `close()` publishes CLOSED first — the sole public admission fence, rejecting *new* submits at the lease gate (`:7406`) — then **drains in-flight leases** within `_ROLLBACK_GRACEFUL_TIMEOUT_S = 10.0 s` (`:702`, `:13542-13551`); if a lease outlives the budget, close returns a `TimeoutError`, **teardown stays un-attempted, the worker stays CLOSED, and close remains retryable**. There is no API that interrupts or kills a parked submitter, and none is proposed.

Consequences, as the design's rule:

- **In the driver (single submitter)** the submitter thread *is* the owner thread, so a park and a `close()` cannot coexist: `close()` runs in the driver's `finally` only after the loop returned or raised. Nothing special is required, and none of A2's failure cases involves a parked submitter.
- **In A1 (deliberate second thread)** the owner must, in this order: (1) release the blocking SUB task so one run reaches its native terminal and the park is released; (2) `submitter.join(timeout)`; (3) `handle.wait(timeout)` each accepted handle; (4) `close()` in `finally`. If (2) fails, the case records the live thread and still calls `close()`, accepting the documented deferred teardown rather than asserting a clean one. This is exactly the shape the existing case already uses (`:3790-3800`).
- **A submitter parked at the moment `close()` is called** is therefore a *bounded wait, then a retryable deferral* — never a forced kill and never silent data loss.

## 6. Commands — plans only, not executed

```bash
# A1 + A2 (no device, no card)
python -m pytest tests/ut/py/test_qwen_grant_depth.py \
                 tests/ut/py/test_worker/test_host_worker.py -q

# B (one A3 card, held through task-submit, arch precheck first)
.claude/skills/onboard-arch-precheck/check.sh a2a3 || exit 1
task-submit --device auto --device-num 1 --timeout 3600 --max-time 3600 --run "\
  python examples/a2a3/host_build_graph/qwen3_14b_serving_effective/grant_depth_worker_submit.py \
    --fixture <reference-kv> --kv-reference <reference-decode-kv> \
    --artifact <verified-hbg-artifact> --model <Qwen3-14B> \
    --device \$TASK_DEVICE --steps 127 --grant 4 --launch-depth 2 \
    --output <new-result-dir>"

# Delivery gate
python -m pytest tests/ut/py -q && cmake --build tests/ut/cpp/build --parallel 8
```

None of these was run. `--output` must not exist; the driver creates `host/` under it for the span log it parses.

## 7. Overlap, rechecked read-only

| Item | Relation | Action |
|---|---|---|
| #2587 `fix/context-bound-buffer-source` | edits `python/simpler/worker.py`, `chip_worker.*`, `python-api.md`, `run-completion-fence.md`, `test_l2_argument_binding.py` | sequencing risk only; this design edits none of them. Another session's PR — no takeover |
| #2436 (Crane-Liu, stale 2026-09-23) | pending edits to this example's `README.md`, `build_hbg_artifact.py`, `callable_bridge.py`, `validate_real_inputs.py` | avoided by construction: new file only, README and bridge untouched |
| #2471 / #2511 / #2568 (merged) | fixture, bridge, joined-evidence helpers, chain driver | reuse by import |
| #2459 (open issue) | HBG distinct-batch KV drift after decode step 2 | distinct defect; reported if seen, not absorbed |
| #2579, #2583, #2566, #2573, #2493, #2545 | HBG host path, prefetch, host bind, replay checksum, kernel-mode swimlane, AICore retirement ACK | none touches `worker_manager.*`, the admission FIFO or the Qwen drivers; sequencing risk only; all external |
| **DFX** | active separate writer on `653a24c4a` | **no edit, no integration, no takeover.** Diagnostics stay off in this feature and no file is shared |

No part of this design duplicates any of the above.

## 8. Summary

- Main `653a24c4a` and the 37-PR open set are unchanged; the #2571 pre-merge baseline is **`a002acc47`**; no execution occurred in any revision of this document.
- **Withdrawn outright:** the caller-span/terminal inequality. It is refuted twice over — a descheduled or lock-delayed submitter satisfies the interval with the predicate true on first evaluation, and `trace_terminal_ns` is stamped before `retire_terminal_run` erases the FIFO entry. The driver may record interval overlap as a bare observation that cannot establish blocking. No threshold, poll count, retry or instrumentation replaces it.
- **Capacity contract moves entirely to A1**, the deterministic native-admission layer with a real `Worker` and no device, extending an existing pyut case to a caller-chosen grant; C++ already proves the same park at grants 2 and 3. The real run never claims to have waited.
- **Coverage is reported per dimension** — observed peak occupancy, set reuse, launch-vs-admit separation, refill beyond the grant — each `reached | partial | inconclusive`, separate from the configured grant and from the seven correctness gates. The overall verdict is `correctness_passed | coverage_partial | failed | capacity_blocked`, so partial real-workload coverage can never read as all claims passed.
- **Layer classification corrected:** every case in `test_qwen_device_feedback.py` is a stubbed-`Worker` driver-unit test (A2), including the sentinel-injection case; only §4.3 runs on a device.
- **Close/parked-submitter ordering** is specified against the current API: CLOSED fences new admission, in-flight leases drain within 10 s, and an outliving lease defers teardown retryably — no interrupt, no kill. In the single-submitter driver a park and a close cannot coexist; in A1 the owner releases, joins, waits, then closes.
- Kept as settled: `T = 120 s`, metadata at `N+1`, the N+1 algorithm, zero product change, `N=4/L=2` as an instance rather than a limit.
- One new driver file, one new driver-unit test file, one parameterized pyut case; one PR, one commit under the single-commit rule. **This document is a contract for review, not an implementation start, and no code was written or run for it.**
