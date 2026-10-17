# Preparation refusal: source lifetime retirement

Baseline: main `aa91a9d60e0e7ce22cdae3cf70533ece46875546` (#2610), branch `fix/prepare-refusal-source-retirement`. [PR #2612](https://github.com/hw-native-sys/simpler/pull/2612), original head `e18ff1064deaa976267b5c75004517fc73ae4da4`; ready for review, not merged. The original-head local validation and CI pass. Current rebase validation is recorded separately below.

## Exact problem and intended behavior

A budget refusal can release every native input binding and enqueue no operator kernel, yet public Worker retains its source reservation forever because the native exception loses that fact. Successful Worker.close does not release the failed submit's source. Buffer.close consequently refuses even when no other invocation exists.

A clean preparation refusal must release only that invocation's reservation, preserve the original failure, and permit another valid call when the environment remains usable. Other users of the same Buffer still hold their own references. Unknown completion, partial enqueue, failed rollback and stale state are not release permission. Host orchestration effects and completed synchronous copies are not undone by releasing bindings.

## Feedback loops and red controls

- Exclusive A3 task `task_20261016_230622_186794510894` was the baseline refusal repro. Both TMR/HBG fail at Buffer.close after successful Worker.close. Command: the common stateful suite with `-k refused --platform a2a3 --device 8`, under task-submit and isolated ASCEND logs.
- The compatibility ChipWorker.run regression independently fails: the native preparation refusal leaves its imported SharedMemory mapping open.
- Removing the Python consumers after implementation makes all three targeted tests fail (immediate submit, deferred wait, synchronous compatibility entry); exact original code is restored afterwards.

## One production contract across the boundary

1. Onboard and simulator cleanup check native bindings, abandoned preparation and saved resource-plan cleanup. Only full success publishes a host-only two-word release marker with this attempt's existing run_epoch into the destroyed carrier's storage. Original return codes are preserved. Zero alone still means destruction, not input release. Resource-wait remains live control flow and supplies no terminal release marker.
2. ChipWorker reads only within its allocated slot size and consumes only the expected epoch. A stale marker, short storage or plain zero supplies no proof. The next raw prepare can reuse an unconsumed marker after clearing it; no device descriptor grows and no ABI-version/capability query is introduced.
3. The native lane releases only this run's caller borrow on a verified refusal. Missing/stale cleanup proof keeps the borrow and poisons further lane admission. A predecessor's borrow is unchanged. A release failure after a resource wait also prevents a release verdict.
4. Public Worker handles immediate submit and deferred RunHandle.wait through the same RunPreparationRefused RuntimeError subclass. The original error remains terminal and cached for repeated wait; only this run_id's source count is decremented. Compatibility ChipWorker.run consumes the same fact for its host import registry. Other exceptions keep their conservative retention behavior.

The exception certifies no enqueued operator kernel and released native inputs, not tensor-content atomicity, allocation ownership or general environment recoverability. Local no-publication preflight refusals use the same lifetime result. The error remains observable even after references are released.

## Discriminating verification

- Baseline TMR/HBG refusal cases are red; both pass after implementation.
- 184 focused Python tests pass; the compatibility/input suites pass 62 cases after their extension.
- Both real lane C++ targets pass. Tests cover clean vs missing/stale release state, once-only epoch consumption, short storage, predecessor-borrow preservation and keep-on-unknown.
- Actual A3 C ABI and public retry task `task_20261016_232001_196605820340`: six cases pass, 36.11 s. This includes both runtimes' ordinary prepare/finalize, forced failed binding-release results and public same-Worker/same-Buffer retry followed by Buffer.close and more stateful calls.
- The raw negative producer arms inject a nonzero release result after calling the real binding release. They intentionally retain the raw context through subprocess exit rather than claiming release permission from implementation knowledge. HBG uses a synchronous publication failure; TMR uses its unsupported HOST/NONE binding error. An initial TMR arm wrongly assumed a warm preparation necessarily performs a memcpy; it failed to trigger and is not counted as product regression evidence.
- Public retry uses an over-budget ring heap for TMR and one failed synchronous metadata publication for HBG. The valid retry uses the same Worker and Buffer before that Buffer is closed; its physical caller allocation remains alive for the subsequent stateful checks.

Full Linux Python UT passes: 3415 passed / 115 skipped. Full C++ UT passes: 327/327 targets. Four native-admission fixture targets initially assumed every failed prepare leaves zero; the clean terminal paths now consume and assert the exact epoch release receipt before checking empty storage. Parked/waiting paths retain their zero assertions. This changes the assertion to match the new producer contract without accepting unknown storage as released.

All changed-file hooks pass, including clang-tidy, formatting and pyright. Final head `e18ff106` was rebuilt and frozen through the sweeps:

- Full a2a3sim Resource dispatch: 61/61 cases pass; L2 HBG 14 passed / 7 skipped; L2 TMR 31 passed / 1 skipped. Command exits 0 in 179.37 s.
- Full a5sim Resource dispatch: 57/57 cases pass; L2 HBG 16 passed; L2 TMR 30 passed. Command exits 0 in 171.83 s.
- Exclusive A3 task `task_20261016_233418_222780629259` holds devices 8–11 with an isolated ASCEND log root. Full C ABI hardware suite: 38 passed / 21 deselected in 89.47 s. The ordinary non-SDMA/non-fault scene sweep passes: 151/151 Resource cases, L2 HBG 43 passed / 1 skipped, L2 TMR 60 passed, plus one parent test. Exit 0 in 594.81 s. The task releases all four locks. No AIV or other scene failure occurred in this run.

CI has been triggered on the same head. At the post-local-sweep query, pre-commit, network1, DeepSeek, A3 hardware UT, packaging and A5 onboard pass; remaining jobs are pending, with no failed checks yet. A5 onboard is not available locally. Completed CI A5 scene job [117332139023](https://github.com/hw-native-sys/simpler/actions/runs/39010295132/job/117332139023) was read through its logs endpoint: both `refused-host_build_graph` and `refused-tensormap_and_ringbuffer` arms pass, including the new Buffer.close behavior, alongside the ordinary scene corpus. The dedicated public-retry/failed-release C ABI arms run in hardware UT, whose A5 result is still pending; the scene job is not substituted for that coverage. Original logs remain local. No AIV fix or capture support is claimed.


## Review follow-up

CodeRabbit's only review nit says the retry arm is not enabled by the scene script's `_check`. That is the wrong consumer: `test_borrowed_prepare_refusal_retries_through_public_worker` in `test_kernel_mode_c_api.py` starts the `public_prepare_retry` subprocess entry, which imports `_run` and explicitly passes `refusal_retry=True` with a 128 MiB budget and the required test shim. This case passed on both runtimes in the six-case focused task and again in the final 38-case C ABI suite. The scene-only 1024-byte budget case deliberately remains refusal-only: a valid retry cannot fit that budget, and it does not preload the publication-failure shim. No unreachable production/test arm was found; the PR validation names both consumers explicitly.


## Rebase onto #2611 — 2026-10-17

PR #2612 now targets main `0eda81c931f982a4f8149b1cac4cda272ec3d8fb`, head `de70972847000d3bcc19894c87d6c6557f0248b7`. The runtime_c_api.h comment conflict preserves both the module/thread non-reentrant-prepare contract and the per-attempt release contract. Range-diff shows no changed product behavior beyond taking main; the two added test assertions consume the exact release epoch for the new callback-exception and invalid-config cleanup tests. Nested/pre-reservation refusals still assert untouched/empty storage.

The original head completed CI with 19 successful checks and 1 skipped check, including A5 hardware UT. Those results and the original full sweeps above remain attributed to e18ff106, not to the rebased head. On the rebased head, the six native-admission/lane C++ targets and all changed-file hooks pass. Rebuilt full C++ UT passes 327/327 targets. Full Linux Python UT passes 3415 / 115 skipped in 138.25 s. Exclusive A3 task `task_20261017_011016_38869462780` passes all 12 stateful/budget Resource cases plus 38 C ABI tests / 21 architecture deselections in 149.06 s; exit 0 and lock released. A session-timeout target was observed with PPID 1 during the full UT run; it was absent at the post-suite cleanup check, so no termination was needed this time. The source head remains frozen throughout. New CI is triggered by the lease-protected update of the original PR branch.
