> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# PMU old/current matched pair — completed

## Result

Old `17129ba496c6d7d710b0e1320b3d1c25aeb9d1e1` reproduced the all-zero symptom on epoch 3. Current `4ec3ef36a832d33e68234d8529aa782e7d3dbbd6` passed all four runs on the same physical pair. Numerical results and independent swimlane task attribution passed in all eight runs. This disproves that #2588's new fences/terminal banks/joined diagnostic launch are necessary for this symptom. It does **not** establish a driver cause, a fix in current, or unchanged failure frequency. Same-card old failure followed by current success also does not support permanent individual-card failure.

| Arm | Epoch | Seed | Swimlane | PMU mode | Rows | Positive | Zero | Raw minimum | Raw maximum |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| old | 1 | 5 | 1 | 2 | 513 | 513 | 0 | 2269 | 1633343192 |
| old | 2 | 11 | 1 | 2 | 513 | 513 | 0 | 2236 | 1633342867 |
| old | 3 | 5 | 1 | 2 | 513 | 0 | 513 | 0 | 0 |
| old | 4 | 11 | 2 | 2 | 513 | 513 | 0 | 2239 | 1633342453 |
| current | 1 | 5 | 1 | 2 | 513 | 513 | 0 | 2270 | 1633344312 |
| current | 2 | 11 | 1 | 2 | 513 | 513 | 0 | 2299 | 1633344286 |
| current | 3 | 5 | 1 | 2 | 513 | 513 | 0 | 2304 | 1633344290 |
| current | 4 | 11 | 2 | 2 | 513 | 513 | 0 | 2288 | 1633344736 |

Raw cycle counts are not converted into latency. Manager independently recomputed the eight raw CSV counts/minima/maxima, epochs and 513-element swimlane task multisets; this delivery also rechecked them offline. Func IDs are exactly one 0 and 512 instances of 1. Old aggregate: 1539 positive / 513 zero; current: 2052 positive / 0 zero.

## Execution, cleanup and ownership

One accepted task `task_20261016_225907_180296928198`, one submission, locks 2/3 held across both arms; lock process 1803043. Exactly two scenes and eight enabled runs consumed. Old scene exit 1 is solely the final `PMU_ZERO_RUNS` assertion after all four measurements; current exit 0. Both per-arm health checks exit 0; original task exit 1 preserves the old failure. The complete [task output](../ARCHIVE-INDEX.md#ref-5fe2e50f549ab27a) records release of locks 2/3. [Final queue](../ARCHIVE-INDEX.md#ref-8011d42463496bd1) shows 0/16 running; [final health](../ARCHIVE-INDEX.md#ref-79e119c744317494) and [initial health](../ARCHIVE-INDEX.md#ref-1d90ef97fb015e07) show the selected physical pair healthy and idle. The precheck resolved `a2a3|Ascend910_9392|Ascend910_93` before submission. Another device was occupied before this task; there is no claim of machine-wide exclusivity. No reset, extra scene, product edit, new build or CI action was performed in this phase. [Ledger](../ARCHIVE-INDEX.md#ref-a29d3bcfc3de995f) now consistently records completed, two actual scenes and released locks.

The common workload uses retained HBG, launch depth 1, and strict `submit().wait()` → numerical check → flush → CSV/identity validation before submitting the successor. Positive-cycle validation remains strict and is aggregated only to preserve all four bounded observations. Old historical PMU smoke checked file/schema/nonempty/epoch, not positive cycles, so old green CI never supplied this missing assertion. Prior successful serial/provenance runs were PR-side, not this baseline.

## Actual identities and evidence boundaries

Both isolated sources matched their exact native stamps and the manager-accepted matching builds (same compiler/PTO, DFX=1). Actual child PIDs: old 1805955, current 1812345. Post-test process maps and loaded hashes match the built native and HBG host libraries. Both loaded the same CANN runtime hash `74ee59b9059d2b8a57692787205de34be80956c74d5c12c0faad78f1e26272da` and HAL hash `0a69928ba6771ee057ee82a02094ac14e7ec85548ef9ac1d2f69dd5b074ab741`.

Actual device debug line 172 records old AICPU upload `simpler_inner_229951325029042a_2.so`, 3716592 bytes; current records `simpler_inner_9f064e2373dbf868_2.so`, 3789640 bytes. Device run logs lines 46/47 resolve Init/Exec successfully from the corresponding basename. Offline ELF inspection found no GNU Build-ID; full-byte FNV1a64 independently equals those loader fingerprints. Full SHA256 and exact paths, raw CSV/swimlane files, child maps, device logs and extracted line numbers are in [result JSON](../ARCHIVE-INDEX.md#ref-9008572d94480f42) and [90-file hash manifest](../ARCHIVE-INDEX.md#ref-6f7d59a72c0fca1f). This is software upload/load provenance, not a physical MMIO mapping proof.

Both harness events report native host spool unavailable because this common scene did not bind a host log directory. Old failing pytest output preserves captured setup; current successful pytest does not emit captured host setup stdout. We preserve that limitation: complete task/pytest/device files are available, but host-spool coverage is not complete. No rerun was made to fill it.

## Narrow shared-path causal question

The failure is an entire epoch, after a normal epoch 2 and before a normal epoch 4. Epochs 1 and 3 have the same seed 5 / swimlane 1 / PMU mode 2; epochs 2 and 3 both use swimlane 1. Existing device logs show each run reinitializes discovery with 24 AIC + 48 AIV and three argument-dump scheduler threads. Thus neither seed 5 nor switching to swimlane 2 explains the failure by itself. The remaining question is which **per-run rearm/restore or core-assignment state** differs in the third entry of a shared older path, not which PR-only join path to revert.

Two concrete source bounds, using the old source (also shared in shape by current):

1. **Cross-run AICPU restore overwriting the next run's init is excluded by this serial workload's completed wait/flush before successor submission**, assuming the successful completion contract. Old `scheduler_cold_path.cpp:593` calls `pmu_aicpu_finalize` before retiring owned cores. `pmu_collector_aicpu.cpp:398–410` restores each owned core's cached saved CTRL value. The next submit does not happen until the prior whole run returned and diagnostic flush finished. No successor launch API can account for an earlier failure in this pair. Within a run, scheduler ownership partitions logical cores; that excludes duplicate logical-owner restores, **not** unknown physical aliasing between resolved PMU windows.
2. **A simple software ordering inversion between per-task SPR start and AICPU PMU init is not evident.** Host `device_runner.cpp:835–855` launches AICore before AICPU; AICPU then handshakes/discovers cores and calls `pmu_aicpu_init` at `scheduler_cold_path.cpp:1029` before scheduler dispatch. Init programs event selectors, reads counters, programs cycle bounds, saves CTRL and writes enable (`pmu_collector_aicpu.cpp:73–93,197–232`). AICore only calls `pmu_aicore_begin/end` around dispatched work (`aicore_executor.cpp:269–280`). These change **AICore SPR CTRL bit 0**, not directly the AICPU **PMU_CTRL_0** MMIO address. The names do not prove aliasing. Host launch-call return also does not prove all asynchronous RTS register effects have finished. Old binaries lack CTRL readback observations, so this pair cannot tell whether old epoch 3 was never armed, later disabled, or armed against a different effective window. Prior PR-side sweep7→first2 evidence must not be attributed to old epoch 3.

The smallest useful next check is to distinguish those specific boundaries on the **old path**: did all actual PMU windows read enabled after full init, and was the first bad task observed before any owner restored a saved value? Reusing the already reviewed bounded provenance design on old would discriminate failed rearm from later loss, with per-core physical ID/window and saved value needed to distinguish owner restore/alias candidates. A single order-isolation intervention should only follow a reproduced boundary (e.g. move restore behind all-core quiescence if loss coincides with an earlier owner's restore); otherwise it would be a speculative fix. This is a proposed next decision, not an executed or authorized additional scene. Merely varying seed or adding repeats is not proposed.

The current evidence excludes the easy cross-run successor explanation and shows an existing-path failure, but does not identify a concrete violating write. Continue source narrowing at the shared init→dispatch→per-owner restore chain; do not stop at “preexisting” or label the driver responsible.
