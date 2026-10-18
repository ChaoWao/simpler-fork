# Launch exceptions retain the execution owner

Baseline: main `8b466791f374e1286b0936f8eaebffb9bf32d44b` (#2614). Branch `fix/retain-launch-ownership-on-exception`. [PR #2615](https://github.com/hw-native-sys/simpler/pull/2615), initial head `1577929fdaf81ec6d2e91fdb02490aea443b536d`; revised head `0e48839063400da4467d08ea2fd0402bc4bb33fb`, ready for review after revised validation, not merged.

## The missing ownership boundary

The prior onboard and simulator launch entries take PreparedExecution by value, submit work, then allocate ActiveExecution. An allocation failure after enqueue unwinds the prepared owner before the C API can receive an active owner. The C API sees an absent active pointer, reports a complete failed launch and finalizes through the unlaunched path. #2614 correctly withholds its clean-refusal proof, but does not restore the lost execution owner or force a drain.

The candidate uses the existing launch_execution entry as a non-virtual noexcept wrapper on each common base. It owns PreparedExecution and allocates an empty ActiveExecution before entering the architecture backend. The protected backend borrows PreparedExecution by reference and returns only LaunchTransactionResult. NotStarted returns the prepared owner; Partial and an escaping backend exception return the preallocated active owner, without acceptance. Complete transfers the same owner and completed receipt. Post-submission hand-off has no allocating operation. Existing query/wait/finalize, workspace facts and source consumers handle that returned ownership; no second executor, public exception, capability or version protocol is added.

This is an eager ownership correction, not capture support or a general device-fault recovery claim. A drain still has to establish its actual device result; neither this wrapper nor a failed receipt makes device work disappear. No silicon AIV attribution follows from the host model.

## Red and negative controls

- Real C API over CPU transport: all four TMR/HBG × A3/A5 native-admission targets fail the post-submission exception regression on the baseline. The child exits 3 because no prepared owner remains in the carrier after a bad_alloc injected after the actual exact_launch_transaction has completed. Other existing cases pass. The final regression checks the returned active owner, not a requirement to retain forever: it pins runtime pointer identity, Partial progress, absent acceptance, retained allocation/reference counts, pending poll, one real consumer drain and release only after that drain.
- The separate before-transaction backend exception retains an active owner and never gets the no-submission receipt; a fake drain that proves completion allows finalization while preserving the original launch rc.
- Owner-allocation failure invokes the actual common wrapper with a one-shot host operator-new refusal. It must report NotStarted, return the identical prepared owner and enter no backend.
- After the model reports complete submission, the next host allocation is armed to fail. Actual production hand-off must leave that failure unconsumed while returning the same active owner and valid receipt.
- Negative control moves the actual common wrapper allocation after backend submission. The last test fails (exit 1); finally restores the exact source, rebuilds and restores eight passing native-admission / simulated-callable targets. Mutation scripts and raw logs stay local.

## Initial-head validation (1577929f)

Eight focused C++ targets pass in 0.44 s; restored negative-control run passes in 0.43 s. All four architecture/backend implementations use the same ownership entry. The final frozen commit is 1577929f. All changed-file hooks pass after the mechanical pointer-to-reference migration is corrected in both concrete simulator backends. Editable runtime binaries and extension rebuild successfully. Full C++ UT passes 327/327 targets in 86.56 s; full Linux Python UT passes 3415 / 127 skipped in 144.20 s. Both simulator Resource dispatch phases have zero failures (61 A3 / 57 A5); their completed L2 and A3 sweep results are recorded below.


The new wrapper retains the active record across a backend exception; existing drain and workspace/source failure-isolation rules still decide retirement. It does not turn a failed drain into device completion or grant no-submission on a partial/ambiguous launch. This is distinct from the clean NotStarted source retirement delivered by #2614.


## Initial-head simulator and board validation (1577929f)

On frozen 1577929f, full a2a3sim exits 0 in 208.46 s: all 61 Resource dispatch jobs succeed, L2 HBG 14 passed / 7 skipped and TMR 31 passed / 1 skipped. Full a5sim exits 0 in 194.13 s: all 57 Resource dispatch jobs succeed, L2 HBG 16 passed and TMR 30 passed. Parent 346/357 deselections are scheduler-delegated items; dispatch-job counts are not a claim that every child executed an assertion rather than a supported skip.

Exclusive A3 task `task_20261017_200604_208188021170` holds devices 8–11 and uses isolated ASCEND logs plus occupancy/queue snapshots. Its C ABI/caller-stream suite passes 46 / 29 architecture deselections in 133.74 s. The completed ordinary non-SDMA sweep is recorded in the next section.


## Initial full board completion and review correction

Frozen head 1577929f completes exclusive task task_20261017_200604_208188021170 with exit 0. After the 46-case C ABI suite, all 151 Resource dispatch jobs exit successfully; L2 HBG passes 43 / 1 skipped, TMR passes 60, and the remaining parent item passes (204 scheduler deselections). Ordinary non-SDMA scene command takes 601.71 s. All four device locks are released; no unexpected AIV fault is observed in this run. These results remain attributed to 1577929f.

Code review identifies a missing obligation in the new exception path: retaining an active owner must also close runner admission through the same policy as any returned Partial result. The first candidate leaves that action in concrete backends, so a throw can bypass it. The revision centralizes Partial handling after ownership is installed, guards a failing onboard recovery policy so it cannot discard the owner or original rc, and uses the simulator's existing poison flag. Onboard admission must close before any fallible recovery work. New C API/Sim regressions are run red before this correction; the PR stayed draft during revision. This is part of the launch-failure contract, not a new admission mode or capability.


## Revision: shared Partial policy

The additional admission/recovery tests fail all eight targets on the first candidate, then pass all eight (0.46 s) after correction. The common wrapper installs its active owner before applying the Partial policy for returned results and escaping exceptions alike. Onboard guards recovery exceptions; each actual arch sets device_unusable before fallible recovery. Sim uses the existing atomic launch-poison flag. The original transaction rc survives even when recovery throws. Per-arch duplicate post-transaction policy calls are removed.

Revision 0e488390 preserves one PR commit. The complete hooks run passes every check except the formatter's intentional auto-edit status; an immediate formatter rerun passes, with no further source change. The new SDK/runtime and extension build succeeds. Full C++ UT passes 327/327 targets in 87.87 s. The remaining revised-head full results follow below. Initial full results remain on 1577929f. No proof of quiescence is inferred from a failing policy or drain.


## Revised-head validation

Frozen 0e488390 passes full rebuilt C++ UT 327/327 targets in 87.87 s and full Linux Python UT 3415 / 127 skipped in 138.63 s. Both full simulator commands exit 0: a2a3sim has 61 successful Resource dispatch jobs, L2 HBG 14 passed / 7 skipped and TMR 31 passed / 1 skipped (210.58 s); a5sim has 57 successful Resource dispatch jobs, HBG 16 passed and TMR 30 passed (196.42 s). The parent 346/357 deselections are scheduler delegation, not independent pass counts.

New exclusive A3 task task_20261017_202457_30480283570 holds devices 8–11. C ABI/caller-stream suite passes 46 / 29 architecture deselections in 133.48 s. The completed ordinary non-SDMA sweep and lock release are recorded below.


## Revised full A3 completion

Task task_20261017_202457_30480283570 exits 0 on frozen 0e488390 and releases devices 8–11. After its 46-case C ABI suite, the ordinary non-SDMA command completes in 608.80 s: 151 successful Resource dispatch jobs, L2 HBG 43 passed / 1 skipped, L2 TMR 60 passed and one remaining parent pass (204 scheduler deselections). Counts include inherited SceneTestCase placeholders that can skip; they are not 151 independent assertion passes. No unexpected AIV fault is observed. The source and HEAD remain frozen through the sweep; isolated ASCEND logs, queue snapshots and before/after NPU occupancy stay local. No orphan timeout target remains in this worktree after the full host UT.

PR #2615 is restored Ready after this full local validation. Initial 1577929f evidence remains historical above. Revision 0e488390 retains a single commit and the same main parent 8b466791. Current CI status is recorded separately when the snapshot is published; no pending job is represented as passed.


## CI snapshot

At 2026-10-18 03:39:05 UTC, head 0e488390 has no red checks. Build/pre-commit, both packaging jobs, profiling smoke, network/DeepSeek, both hardware UT jobs, A5 onboard scenes and three simulator jobs are successful. A3 onboard scenes, both host UT jobs and A3 simulator on macOS remain running. These are job-level statuses; no unreported per-case count is inferred from them. PR is Ready and mergeable, not merged.
