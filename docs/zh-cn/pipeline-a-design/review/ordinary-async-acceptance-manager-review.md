# Ordinary async acceptance/cost map — manager review

设计归档快照：状态与阅读顺序见[索引](../README.md)。设计约束、历史合同和当前实现须分别阅读。

Baseline main600540064. Source-only review; no test/build/device execution. Original artifact copied from dynamic worktree after successful result, unchanged tracked tree.

Accepted: GraphDefinition independent device allocation/upload was removed by #2487; current host packing differs from that obsolete proposal. Remaining host arena rebuild is a candidate requiring correctness/cost evidence, not an automatically justified optimization. A3 TMR shared-header fallback belongs in P4 complete early-enqueue delivery. Do not make isolated acceptance/constant PRs.

## Required factual corrections

R1 — Partial is neither a sole-producer nor entirely untested condition. `src/common/worker/native_run_execution.h:228-250` produces Partial for marked first-submit failure, exceptions, and second-submit failure. `tests/ut/cpp/common/worker/test_native_run_execution.cpp:94-196` covers AicoreFailureAfterMarkedSubmissionPoisons, AicpuFailureAfterMarkedSubmissionStaysPartial, AicoreExceptionPoisonsWithoutTryingAicpu, AicpuFailureAfterAicorePoisonsWithoutReceipt and AicpuExceptionAfterAicorePoisonsWithoutReceipt. Distinguish transaction grading coverage from any truly missing lane resource-retention/close integration. Enum is NotStarted/Partial/Complete, not Launched. Nonzero rc alone (even Complete) cannot prove quiescence or authorize reference discharge; read actual drain/teardown ownership before proposing a control. Remove unsafe hypothetical oracle.

R2 — Matrix row6 cites opt-out on A3 HBG and DEVICE-control rejection on A5 HBG, then calls growth/incompatibility covered on all routes. These prove different claims. Split actual growth, incompatible config, opt-out and host-address restriction; evidence names must prove their cell. Distinguish generic same-device DEVICE result chaining (A3 test_a_device_result_feeds_the_next_two_runs at early_enqueue:1395, A5 counterpart) from production same-request autoregressive control/KV feedback owned by #2511. Do not globally label already-covered data chaining missing.

R3 — H2D is not every non-output caller tensor. HBG maker bind skips DEVICE-space tensors; qualify retained-temp slicing/H2D as the HOST copy path and record source address/transfer rules. Also pipeline_depth alone does not select early launch: launch_depth defaults1, actual scenes explicitly require launch_depth2/3. Rewrite measurement plan with separate resource and launch-depth axes, equal workload, actual receipts and warm-up, and endpoint resource occupancy. No execution authorized; no causal/performance claim from source.

R4 — Update #2511 evidence to current259481f3 (direct e047→259 only timeout60→30 reverted, workload unchanged) and new #2557 stream binding (26d8, original owner). Do not stale-head gate unrelated work or duplicate other author. Final report must distinguish source presence, named-case actual execution and prior-head CI aggregate. I1/I2 are acceptance obligations of complete affected features, not two standalone micro-PRs.

## Revision 2 disposition — 2026-10-13T03:34:19.987293+00:00

R1–R4 substantive corrections accepted after source review. Manager corrected remaining report-only defects in the collected copy: Complete success is not a fifth Partial producer; the proposed resource/launch comparison now uses four arms (2/1, 2/2, 3/2, 3/3), so adjacent comparisons change one axis; finite test searches do not prove global absence. Existing per-run TMR result storage is retained, not newly proposed. No product change or measurement is justified by this report. A3 TMR attribution/real-API obligations stay inside the approved P4 feature. The stale Worker.submit A5 HBG docstring is recorded for that feature's final documentation review. Original dynamic author is idle; no new PR or testing dispatched.
