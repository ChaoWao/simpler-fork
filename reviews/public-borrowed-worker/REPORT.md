# Public Worker borrowed eager

2026-10-16. Base main `e994e106d8020c049dc419a67498db37bc6fcd3d` (#2597 merged). Current PR #2599; head `b91c04b505daabf3499cef56634d73a2f194f0cc`.

## Contract

The existing public `Worker` gains `init(borrowed_context_generation=...)` and per-invocation `submit/run(..., caller_stream=...)`. The same registration, Buffer.wrap/Tensor/TaskArgs, native owner/lane and RunHandle retirement are used. No new executor, public parameter type, device ABI or PreparedCall cache.

Borrowed initialization is direct onboard L2 only, with a strict nonzero uint64 caller-managed generation. It omits the builder's default Program SDMA warmup ELF and rejects explicit SDMA or Program workspace budgets. This generation is a logical identity, not reset detection or external allocation proof. Caller stream is a strict nonzero uint64, required for borrowed execution, optional for owned L2. No process-local handle crosses L3/remote. Caller validation invokes the existing native predicate before Python publishes imports, source reservations or run IDs; native admission and launch retain their own checks. Borrowed tensor H2D is refused; explicit HOST/NONE controls retain existing permissions and lifetime obligations.

## Qualification

The committed hardware test uses the actual public path, with no native-run/Python-handle bridge. Caller allocations are wrapped through the public source API. Four invocations alternate two caller streams, each with independent inputs/output. No RunHandle query or wait precedes caller synchronization and byte-value checking. Source close refuses before retirement. Wrong-current-context submit/wait/result/close refuse and recover on restore. Borrowed close leaves both caller streams and exact original RTS context usable. Caller allocations are freed only after all handles retire.

A3 task `task_20261016_004903_6623679278`, exclusive device 2 via task-submit: **8 passed / 8 A5 cases deselected**, 105.77 s. TMR/HBG × owned/borrowed × existing SDK `TASK_QUEUE_ENABLE=0/1`. Logs remain local at `/tmp/public-borrowed-board/task.log`; isolated device log root `/tmp/public-borrowed-board/ascend`. This checks direct RTS calls under SDK environment variants, **not** an actual framework background-emission-queue adapter.

Focused host tests after collaborator migration: **219 passed**. Full Linux UT: **3356 passed / 107 skipped / one existing PyTorch warning**. Owned simulations: **10 A2/A3 + 6 A5 passed**. Changed-file hooks all pass. Final committed head rebuilt successfully; the focused set passes **219 tests** again. New tests use compiled runtime DSOs for missing/invalid stream, caller refusal, H2D refusal, initialization options, close failure/retry and absence of prematurely published logical state. Existing private borrowed tests now supply required streams; private owned test doubles expose the native environment property.

## Remaining acceptance

K2c is not fully complete: A5 hardware is committed but not run locally; actual framework emission-queue ordering, broad partial enqueue/fault/DFX/business-state qualification remain separate explicit acceptance work. Capture/replay remains rejected and is subsequent production work. S3 module/code lifetime remains required before C2 graph ownership.

## Local evidence

[Full Linux UT](../../LOCAL-EVIDENCE.md#artifact-a7e737161e76), [focused tests](../../LOCAL-EVIDENCE.md#artifact-d3e1670b627b), [final-head focused tests](../../LOCAL-EVIDENCE.md#artifact-53a9e7b33ebe), [final build](../../LOCAL-EVIDENCE.md#artifact-2e416c6df573), [hooks](../../LOCAL-EVIDENCE.md#artifact-f646aedf2e50), [A2/A3 sim](../../LOCAL-EVIDENCE.md#artifact-6ec23ffc3a04), [A5 sim](../../LOCAL-EVIDENCE.md#artifact-fc72937e7a43), [exclusive hardware task](../../LOCAL-EVIDENCE.md#artifact-13fc48564a57). Raw logs remain local and are excluded from the design backup branch.

The first full local UT run exposed old borrowed private-test call shapes and missing native-environment fields in owned doubles; both were migrated, with no production validation fallback. A source-inspection test failed while the loaded module's file was being formatted; rerunning with stable product files passed both focused and full suites. No files were edited during the successful full-suite run. Final-head rebuild/focused checks are subsequent checks of the same product content, not an additional board run.
