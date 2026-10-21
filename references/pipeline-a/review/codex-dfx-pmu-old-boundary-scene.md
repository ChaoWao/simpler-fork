> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# Old17129 boundary scene — failure reproduced and localized

Exactly **one scene / four serial runs** completed. Task `task_20261016_232214_19866573504` exit **1**; original scene **1**, offline evidence **0**, health **0**. Failure is only the final strict `PMU_ZERO_RUNS` assertion. All numerical checks and 513-task PMU/swimlane attribution checks passed. Manager independently recomputed the raw CSV and CTRL distributions.

| Epoch | Seed | Swimlane | PMU mode | Rows | Positive | Zero | Raw min | Raw max |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 5 | 1 | 2 | 513 | 513 | 0 | 2298 | 1633344953 |
| 2 | 11 | 1 | 2 | 513 | 0 | 513 | 0 | 0 |
| 3 | 5 | 1 | 2 | 513 | 513 | 0 | 2284 | 1633345382 |
| 4 | 11 | 2 | 2 | 513 | 513 | 0 | 2248 | 1633345400 |

Counts are raw PMU values, not derived latency. Aggregate 1539 positive / 513 zero. This failure is epoch2; the uninstrumented old pair failed epoch3. Do not align them by run number or treat the old input seed as the cause.

## What the boundary data proves

Every epoch: 72 saved values=0; 72 immediate post-init reads=7; 72 reads in the separate **after-all-init** sweep=7; every original restore operand=0 and every added post-restore read=0. All72 physical IDs and cached virtual windows are unique within each epoch. Owners are exactly 0/1/2; each owns24 cores and has one sampled task core.

| Epoch | First completion pre/post CTRL (3 task cores) | Before own restore (72 cores) | After own restore |
|---|---|---|---|
| 1 | 7 / 7 | 7 ×72 | 0 ×72 |
| **2** | **2 / 2** | **2 ×72** | 0 ×72 |
| 3 | 7 / 7 | 7 ×72 | 0 ×72 |
| 4 | 7 / 7 | 7 ×72 | 0 ×72 |

For epoch2 task cores the loss boundary is **all-core init sweep7 → first-completion pre-read2**. It is not simply an enable write whose immediate/full-init observation never succeeded. Other69 cores only have sweep7 → their own restore-pre2, a wider interval. No evidence shows all72 transitioning simultaneously, nor do the remaining510 zero rows continuously observe CTRL2.

| Bad epoch logical core | Owner | Physical core | Cached virtual window | First cycles | A successful neighboring observation of same physical core |
|---:|---:|---:|---:|---:|---|
| 24 | 0 | 65 | 17516119195648 | 0 | epoch3 logical26: 4992 cycles, CTRL7/7 |
| 26 | 1 | 67 | 17516127584256 | 0 | epoch3 logical28: 2398 cycles, CTRL7/7 |
| 28 | 2 | 69 | 17516135972864 | 0 | epoch1 logical26: 4055 cycles, CTRL7/7 |

Physical assignment varies per run (task-core triplets: 67/69/71 → 65/67/69 → 63/65/67 → 61/63/65). Same physical cores have both failed and successful observations, limiting a permanent bad-core explanation. Unique virtual windows do not rule out physical aliases or unknown mapping semantics.

## Restore order: source proof rather than cross-core timestamps

All nonempty scheduler owners are0/1/2, and **each** records its own first bad task. The actual frozen source calls PMU record in [scheduler_completion.cpp](../ARCHIVE-INDEX.md#ref-82cedc57aacb03b3), inside its scheduling work; [aicpu_executor.cpp](../ARCHIVE-INDEX.md#ref-2e8bec9b4d8fcd48) calls that owner's shutdown afterwards. [scheduler_cold_path.cpp](../ARCHIVE-INDEX.md#ref-f601e4d46de2ad61) is the HBG caller of PMU finalize, before core retirement. Emergency retirement does not call PMU finalize or perform a second PMU restore.

Take the earliest actual restore among these owners: that owner's first bad observation already occurred before its own shutdown. Thus **at least one observed loss precedes every own-run restore**. The first observed loss cannot originate from this run's original PMU restore path. This uses source order and complete owner coverage, not timestamp ordering. It does not prove all72 losses precede every restore, nor exclude an unknown external writer/alias side effect. The fully serial workload also completed previous wait/flush before next submit, excluding ordinary successor-launch overlap.

CNTVCT extrema are retained in [summary JSON](../ARCHIVE-INDEX.md#ref-cb8a6ddb861570ef) only as raw envelopes; no cross-thread happens-before claim is based on them. The remaining narrow interval is after successful PMU init/sweep and before first task completion: shared AICore startup/task SPR gates, asynchronous runtime effects and mapping semantics need source-level discrimination. AICore SPR CTRL bit0 and AICPU PMU_CTRL_0 are distinct interfaces; their name similarity does not prove aliasing. No driver cause or repair is claimed.

## Identity, cleanup and preservation

Source stays old `17129ba496c6d7d710b0e1320b3d1c25aeb9d1e1` plus reviewed patch `ba8a93f52b31b09532910f89ce0efcd54186b22ffa40808255a908d55561b520`; all frozen inputs and sealed build outputs rehashed unchanged. Child PID1988246's actual maps/hash match native `5aa6f72f…` and host `ecbe111e…`. Actual device debug line172 records AICPU `simpler_inner_607f316ff3c48761_2.so`,3722384 bytes; device run lines46/47 resolve Init/Exec from it. Full-byte FNV64 of sealed AICPU equals that basename; SHA256 is `1c9955610912cc0b87bde677882fb1dfc4cb5dbb3e2dfab46ca7a507c1d3596e`. Full paths/hashes/lines are in [identity](../ARCHIVE-INDEX.md#ref-acb40b2c91ab908f). This proves software upload/load identity, not physical MMIO identity.

Native host spool remains unbound, accurately recorded by the harness. The new boundary exports are present directly beside all four raw PMU CSVs and do not depend on that spool. Full failed pytest capture includes host bootstrap; original ASCEND device logs remain available. No original error was suppressed.

Architecture precheck passed before submission. [Before health](../ARCHIVE-INDEX.md#ref-76556543a8bd5297), per-arm health files, [after health](../ARCHIVE-INDEX.md#ref-90c24765fff86796) and [queue](../ARCHIVE-INDEX.md#ref-9121655d5762a245) retained. [Task output](../ARCHIVE-INDEX.md#ref-47423eff9b6fb0fd) confirms locks2/3 released, and [ledger](../ARCHIVE-INDEX.md#ref-c97248701ab7e1ad) consistently records one actual completed scene/four runs. All102 prior pair-evidence/binary entries remain unchanged. [Delivery hashes](../ARCHIVE-INDEX.md#ref-68298449b7d54747) seal raw boundary/PMU/swimlane, logs, maps, statuses and identities.

No rebuild, source edit, repeat scene, reset, CI action or publication occurred. Next action is a bounded **read-only** audit of the newly evidenced init-to-first-completion gap; this report does not extend the experiment allowance.
