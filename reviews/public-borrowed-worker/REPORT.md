# Public Worker borrowed eager

2026-10-16. Base main `e994e106d8020c049dc419a67498db37bc6fcd3d` (#2597 merged). Current PR #2599; head `12304f0a09413211a77d0fcdad43a6d1768740eb`. Initial product/local qualification used `b91c04b505daabf3499cef56634d73a2f194f0cc`; the test/doc-only follow-up and its independent CI evidence are recorded below.

## Contract

The existing public `Worker` gains `init(borrowed_context_generation=...)` and per-invocation `submit/run(..., caller_stream=...)`. The same registration, Buffer.wrap/Tensor/TaskArgs, native owner/lane and RunHandle retirement are used. No new executor, public parameter type, device ABI or PreparedCall cache.

Borrowed initialization is direct onboard L2 only, with a strict nonzero uint64 caller-managed generation. It omits the builder's default Program SDMA warmup ELF and rejects explicit SDMA or Program workspace budgets. This generation is a logical identity, not reset detection or external allocation proof. Caller stream is a strict nonzero uint64, required for borrowed execution, optional for owned L2. No process-local handle crosses L3/remote. Caller validation invokes the existing native predicate before Python publishes imports, source reservations or run IDs; native admission and launch retain their own checks. Borrowed tensor H2D is refused; explicit HOST/NONE controls retain existing permissions and lifetime obligations.

## Qualification

The committed hardware test uses the actual public path, with no native-run/Python-handle bridge. Caller allocations are wrapped through the public source API. Four invocations alternate two caller streams, each with independent inputs/output. No RunHandle query or wait precedes caller synchronization and byte-value checking. Source close refuses before retirement. Wrong-current-context submit/wait/result/close refuse and recover on restore. Borrowed close leaves both caller streams and exact original RTS context usable. Caller allocations are freed only after all handles retire.

A3 task `task_20261016_004903_6623679278`, exclusive device 2 via task-submit: **8 passed / 8 A5 cases deselected**, 105.77 s. TMR/HBG × owned/borrowed × existing SDK `TASK_QUEUE_ENABLE=0/1`. Logs remain local at `/tmp/public-borrowed-board/task.log`; isolated device log root `/tmp/public-borrowed-board/ascend`. This checks direct RTS calls under SDK environment variants, **not** an actual framework background-emission-queue adapter.

Focused host tests after collaborator migration: **219 passed**. Full Linux UT: **3356 passed / 107 skipped / one existing PyTorch warning**. Owned simulations: **10 A2/A3 + 6 A5 passed**. Changed-file hooks all pass. Final committed head rebuilt successfully; the focused set passes **219 tests** again. New tests use compiled runtime DSOs for missing/invalid stream, caller refusal, H2D refusal, initialization options, close failure/retry and absence of prematurely published logical state. Existing private borrowed tests now supply required streams; private owned test doubles expose the native environment property.

## Remaining acceptance

K2c is not fully complete: actual framework emission-queue ordering, broad partial enqueue/fault/DFX/business-state qualification remain separate explicit acceptance work. A5 hardware is unavailable locally; new-head CI below qualifies its eight public-path combinations. Capture/replay remains rejected and is subsequent production work. S3 module/code lifetime remains required before C2 graph ownership.

## Local evidence

[Full Linux UT](../../LOCAL-EVIDENCE.md#artifact-a7e737161e76), [focused tests](../../LOCAL-EVIDENCE.md#artifact-d3e1670b627b), [final-head focused tests](../../LOCAL-EVIDENCE.md#artifact-53a9e7b33ebe), [final build](../../LOCAL-EVIDENCE.md#artifact-2e416c6df573), [hooks](../../LOCAL-EVIDENCE.md#artifact-f646aedf2e50), [A2/A3 sim](../../LOCAL-EVIDENCE.md#artifact-6ec23ffc3a04), [A5 sim](../../LOCAL-EVIDENCE.md#artifact-fc72937e7a43), [exclusive hardware task](../../LOCAL-EVIDENCE.md#artifact-13fc48564a57). Raw logs remain local and are excluded from the design backup branch.

The first full local UT run exposed old borrowed private-test call shapes and missing native-environment fields in owned doubles; both were migrated, with no production validation fallback. A source-inspection test failed while the loaded module's file was being formatted; rerunning with stable product files passed both focused and full suites. No files were edited during the successful full-suite run. Final-head rebuild/focused checks are subsequent checks of the same product content, not an additional board run.

## CI follow-up: host fixture independence

Initial head `b91c04b5`, CI run [38870994440](https://github.com/hw-native-sys/simpler/actions/runs/38870994440): Ubuntu Python UT reports 10 setup errors at `RuntimeBuilder(platform)` → `RuntimeCompiler.get_instance` → `env_manager.ensure("ASCEND_HOME_PATH")`; 3333 tests pass. macOS UT is cancelled by matrix fail-fast, not independently diagnosed as a second defect. The fixture replaced only `get_binaries`, after the constructor had already selected a real onboard compiler. Local SDK configuration hid this dependency leak.

A fresh process with `ASCEND_HOME_PATH` removed reproduces the exact single-test setup error in 1.23 s. Replacing the test binary-provider factory (while keeping real Worker/native DSO execution) passes all **20 public borrowed host cases** in the same environment in 1.35 s. Fixture registration/init are now covered by its cleanup finally, and the borrowed-storage example explicitly passes a caller stream. No production runtime behavior or CI SDK installation/configuration is changed.

Initial-head hardware logs independently confirm **all eight new public cases passed on A3 and all eight passed on A5**. A3 hardware Python UT: 46 passed / 3401 deselected; A5: 48 passed / 3399 deselected. This is evidence for `b91c04b5`, not a claim that the follow-up head has already completed CI. Source logs: [Ubuntu failure](../../LOCAL-EVIDENCE.md#artifact-1f9237a30fe1), [A3](../../LOCAL-EVIDENCE.md#artifact-9f0772e3aec4), [A5](../../LOCAL-EVIDENCE.md#artifact-f8f047ae10b9), [no-SDK red](../../LOCAL-EVIDENCE.md#artifact-c9ba558fd175), [no-SDK green](../../LOCAL-EVIDENCE.md#artifact-a44c574ad773). New-head full checks are recorded once complete.

Follow-up head `12304f0a09413211a77d0fcdad43a6d1768740eb` remains one commit on main `e994e106d`. With `ASCEND_HOME_PATH` unset and the same non-hardware marker selection as CPU CI: **3348 passed / 7 skipped / 108 deselected**, 210.11 s. This is a local environment-variable removal on a machine that still has an SDK installation, not a claim that local hardware/toolchains are absent. Final head rebuilt successfully; **219 focused tests pass** under the same environment removal; changed-file hooks pass. Evidence: [full](../../LOCAL-EVIDENCE.md#artifact-504a662d1ee4), [final focused](../../LOCAL-EVIDENCE.md#artifact-4309d6791c9e), [hooks](../../LOCAL-EVIDENCE.md#artifact-0d401974217f), [build](../../LOCAL-EVIDENCE.md#artifact-6242ca3f1795). New-head CI is pending at this update.

New-head A5 hardware UT now passes: [job 116885148641](https://github.com/hw-native-sys/simpler/actions/runs/38874103541/job/116885148641), **48 passed / 3399 deselected**, including all eight public Worker cases. [Local log](../../LOCAL-EVIDENCE.md#artifact-603d4c875d40). Other new-head jobs remain in flight at this checkpoint.

New-head A3 hardware UT also passes: [job 116885148506](https://github.com/hw-native-sys/simpler/actions/runs/38874103541/job/116885148506), **46 passed / 3401 deselected**, including all eight public Worker cases. [Local log](../../LOCAL-EVIDENCE.md#artifact-34df2093135f). New-head Ubuntu and macOS Python UT steps both completed successfully; their jobs continue with C++ UT at this checkpoint. Both onboard scene jobs pass.

New-head Ubuntu UT is fully green: [job 116885149145](https://github.com/hw-native-sys/simpler/actions/runs/38874103541/job/116885149145), **3343 Python tests passed / 12 skipped / 108 hardware cases deselected**, **321 C++ tests passed**, and 12 exporter follow-up Python cases passed. [Local log](../../LOCAL-EVIDENCE.md#artifact-e8b06b53059b). The five-test local/CI difference is reflected in the skip counts; local SDK-variable removal was never claimed as an identical machine image.

New-head macOS UT is fully green: [job 116885148990](https://github.com/hw-native-sys/simpler/actions/runs/38874103541/job/116885148990), **3334 Python tests passed / 21 skipped / 108 hardware cases deselected**, **320 C++ tests passed**, and 12 exporter follow-up Python cases passed. [Local log](../../LOCAL-EVIDENCE.md#artifact-c5c0faba73d7). Both CPU UT legs are now verified independently; the original macOS cancellation is not counted as a pass.

## Final follow-up disposition

At 2026-10-16 08:55 UTC, head `12304f0a` has **19 successful checks / 1 skipped deploy**, no failures, cancellations or pending checks, and zero unresolved review threads. PR #2599 remains OPEN and MERGEABLE; it has not been merged. Both review threads were answered and resolved, and the CI cause/validation are posted on the PR. [CI run](https://github.com/hw-native-sys/simpler/actions/runs/38874103541), [final local status record](../../LOCAL-EVIDENCE.md#artifact-f12685450fb1), [review record](../../LOCAL-EVIDENCE.md#artifact-9629b78010ca). Historical checkpoint statements above retain their original time/scope.

## Merge disposition

PR #2599 merged as `905d336e41d324ba8c8dbf87fe138965d84e74e2`. The final CI and review evidence above remains attributed to head `12304f0a`; the earlier OPEN status is a historical checkpoint. Subsequent work qualifies empty calls and persistent INOUT state through the same public Worker path.
