> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# Default automatic admission — acceptance audit

## Result

**The complete-feature acceptance gap is real.** The current registered four-backend early-enqueue scenes do not exercise default automatic admission. Both scene Worker factories explicitly supply `launch_depth`, including when a case omits that key. The product deliberately treats any explicit launch-depth setting as opting out of automatic admission.

The PR has substantial deterministic component coverage and real explicit-depth device coverage. Those are useful, but their combination does not test the newly approved default `Worker.submit` behavior through parent, forked child, real runner, allocator, feedback and retirement. Minimum cohesive additions belong in **this same PR**, described below. This is a test-definition/acceptance finding, not a newly demonstrated product algorithm defect.

## Scope and evidence identity

- Reviewed pinned source: `1c9c112aa69ac4e60894ca84dd2275b76caa62d3`, still HEAD at the last substantive source read. The final post-report check observed the sole author's publication at `b571e7323188c5f866ee1c7c305509de430e2e77`; its entire tree delta is the single Optional-narrowing assertion described below. All capability/coverage conclusions therefore apply unchanged.
- Reviewed the 53-file feature inventory against main `289b3016463f60c159fb1255a581868daf04a104`, the approved decision/design, original five feedback seams, and `codex-p4-ad481-full-review.md` plus author result. The retained-plan implementation was independently source-reviewed in that preceding report; this audit evaluates acceptance coverage.
- Manager gate: `codex-p4-auto-acceptance-audit-overlap-review.json`, live gate supplied at 11:33:15, main 289b/all 36. New external #2605 is the fixed-shape borrowed-L2 vLLM adapter and is outside this local-L3 feature. External #2600/#2604/#2598 ownership is preserved.
- The sole concurrent edit was `tests/ut/py/test_worker/test_host_worker.py`: `assert worker._worker is not None` before the existing chipless assertions. The final 1c9-to-b571 tree diff confirms exactly that one insertion; the worktree then has no tracked diff. It is not treated as a capability change.
- No tests, collection, build, hardware, installation, product edits, Git mutations, publication or external comments were performed. This report makes no test-pass claim.

## 1. Why the existing scene route cannot establish automatic behavior

| Entry point | Source fact | Consequence |
| --- | --- | --- |
| Pytest `st_worker`, L3 branch | `conftest.py:1948` maps an omitted case launch depth to 1; `1984` always passes `launch_depth=launch_depth` to Worker. Pipeline depth is conditionally omitted, but launch depth is not. | Every L3 SceneTestCase using this fixture takes the explicit-mode predicate branch. |
| Standalone scene dispatcher | `simpler_setup/scene_test.py:3010` computes the same default and `3051` always passes it. | Standalone execution of the same scenes also opts out. |
| Product scope | `python/simpler/worker.py::_automatic_admission_requested` rejects any config containing `launch_depth`, `pending_run_depth`, or `workspace_budget_bytes`, and rejects explicit pipeline depth. | Explicit `launch_depth=1` is semantically different from omitting the key. |
| Standalone sharing | `_standalone_worker_groups`, `simpler_setup/scene_test.py:264`, keys runtime/level/pipeline depth/diagnostic retention, not admission-mode provenance. | A new true-default class must not share a Worker with an old explicit control. |

The existing `DepthOneControl` classes' descriptions of an unset default are therefore misleading for the public default: the harness supplies an explicit value. They still serve as valid explicit-serial controls. Do not silently change their route when adding automatic coverage; several FIFO tests deliberately require a successor callback to enter while the first logical run is held open, which is incompatible with cold automatic admission one.

## 2. Registered CI paths and what they actually cover

### Hardware/public API matrix

All four `early_enqueue/test_early_enqueue.py` files below are registered SceneTestCase modules. `ci.yml:109,153` invokes the two architecture scene workflows; `_st-npu-a2a3.yml:116` and `_st-npu-a5.yml:104` run `pytest examples tests/st --platform <arch> --exclude-level 4`. Per-PR manual mode defaults to `exclude`; automatic acceptance additions must not be manual-only.

| Backend | Existing real scene examples | What is established by their definitions | Default-auto gap |
| --- | --- | --- | --- |
| A2/A3 HBG | `tests/st/a2a3/host_build_graph/early_enqueue/test_early_enqueue.py`: `TestDeviceResultChain.test_a_device_result_feeds_the_next_two_runs` (1463), `TestThreeRunCapacity.test_sixteen_runs_keep_refilling_the_sets_they_retire` (1752), `TestWorkedCapacityExample.test_sixty_four_runs_retire_through_eight_sets` (2453). Also `worker_async_fifo` and `worker_async_endpoint`. | Public submit, real device ordering/results, explicit windows, DEVICE chaining, FIFO/protocol ownership. Wide cases explicitly set pipeline and launch depth; other classes receive explicit launch depth from the fixture. | No startup-one/terminal-growth/default sustained-enqueue assertion. |
| A2/A3 TMR | `tests/st/a2a3/tensormap_and_ringbuffer/early_enqueue/test_early_enqueue.py`: depth-two refill (983), per-endpoint enqueue (1110), successor error attribution (1177), caller lifetime (1515), outstanding close (1675), wide refill (2170). | Real shared-scratch execution, configured concurrency, caller lifetime and close/error behavior. | Same explicit-mode exclusion; no all-DEVICE automatic zero-private-byte growth proof. |
| A5 HBG | `tests/st/a5/host_build_graph/early_enqueue/test_early_enqueue.py`: depth-two refill (933), third-set refill (1333), DEVICE result chain (1533), wide DEVICE chain (2296), wide refill (2475). | Real HBG device execution and configured resource sets. | No default-auto startup/growth or retained-plan pressure route. |
| A5 TMR | `tests/st/a5/tensormap_and_ringbuffer/early_enqueue/test_early_enqueue.py`: depth-two refill (946), multi-endpoint enqueue (1074), successor error attribution (1222), wide refill (2027). | Real shared-scratch execution at explicit depths. | No default-auto startup/growth or default failure/close acceptance. |

Exceptions were checked rather than assuming every raw Worker is a scene fixture. The TMR `dynamic_register/test_dynamic_register.py` modules directly create unconfigured L3 Workers, but their platform markers select `a2a3sim`/`a5sim`; automatic mode is excluded there. `tests/st/worker/comm_region/recursive_single_owner/test_l3_single_hop.py:31` directly creates a default TMR L3 Worker and is enabled onboard on both architectures. Its helper executes two sequential `worker.run` lifecycles (`_helpers.py:248`); this is incidental default-route compatibility for communication controls, not ordinary sustained automatic enqueue, resource-pressure recovery, or an HBG/four-backend qualification. Communication-domain fault variants are sim-only.

### Deterministic/no-hardware matrix

`_ut-no-hardware.yml:80` selects non-hardware Python UT; `:108` executes CTest excluding the hardware label. The following definitions are registered there, not merely unreferenced source files.

| Requirement | Exact existing definition/registration | Boundary still missing |
| --- | --- | --- |
| Scope and startup arguments | `test_worker/test_automatic_admission_scope.py::test_an_unconfigured_local_l3_worker_sizes_itself`; `test_host_worker.py::test_start_hierarchical_passes_each_chip_its_published_resource_grant`. | Scope test does not init devices; startup test fakes parent/child initialization. No real parent/child automatic acceptance. |
| Startup one, whole-run fold, shrink-before-wake, lease identity | `test_early_launch_admission.cpp::AutomaticStartupWaitsForProofThenEnqueuesACompatibleSuccessor`; production hierarchical object library in its CMakeLists. `test_auto_admission.cpp` covers cold offer, sparse prefix, zero-byte proof and token serialization. | Real orchestrator/state, but manually supplied endpoint samples and synthetic task completion. No forked endpoint feedback path. |
| Allocator refusal, idle trim, 40/30/20 wait, exhausted/unrelated holders, rollback | `test_run_resource_trim.cpp::ARefusedPlanWaitsForALaunchedHolderAndThenFits` (496), staging equivalent (567), exclusions/rollback; `test_auto_admission.cpp` wait-set cases. Platform CMakeLists:65 links production `run_resource_growth.cpp`. | Real coordinator/ledger/arena transaction, but custom Endpoint copies runner adapter responsibilities and BoundedBackend supplies allocations. Does not execute onboard DeviceRunnerBase or C API park/reservation cleanup. |
| Four maker status bridges | HBG `test_hbg_bind_ledger.cpp::ManagedStagingStatusReachesThePrepareCallerUnchanged`; TMR `test_trb_runtime_temp_buffer.cpp::ManagedResourceStatusesSurviveBothAdapterBridges`. HBG CMakeLists:97 and TMR CMakeLists:113 compile each architecture's actual runtime_maker. | Injected HostApi statuses; no real allocator refusal feeding the actual runner and native prepare caller. |
| HBG callback once, stable HOST storage, plan and diagnostics across attempts | `test_hbg_bind_ledger.cpp::AResourceWaitRetainsTheGraphAcrossRuntimeDestructionAndPublishesOnce` (2239), `AResumedUnpublishedImageHasOneOwnerEvenWhenPublicationFails` (2310), abort/reuse (2348), unsupported retention (2371). | Actual makers and cross-thread Runtime replacement, but fake retained owner/phase pool/services. Abort resets a shared_ptr, not native reservation/ledger cleanup. |
| DEVICE borrow safety and failed abort | `test_chip_run_lane_joined_launch.cpp::ARefusedPlanAbortKeepsTheWaitingRunsBorrow` (1057), related last-consumer cases. | Real lane with stubbed native prepare/abort/finalize. Does not establish a real device last-consumer boundary on the default route. |
| Early retry failure/close | `test_auto_admission.cpp::AReservedRetryOwnsItsPlanBeforeAnyFalliblePreparation` (426), lane abandonment cases. | State adoption/end tested separately; actual C API attach/reclaim/provision/bind/publication failure ordering remains source-traced only. |
| Diagnostic continuation | `test_host_phase_records.cpp::APrepareRetryAppendsAndKeepsTheDroppedLedger`, HBG retained graph cases above. | Real store/maker components; no integrated saved-plan failure/close artifact test with real runner ownership. |

## 3. Minimum cohesive additions in this PR

### A. Give test infrastructure a true-default route

Add a **test-only class/decorator declaration** for “construct this L3 Worker with omitted admission controls,” defaulting off. It must affect both `conftest.py::st_worker` and `_create_standalone_worker` through one shared kwargs policy helper. The opted-in route must omit all four controls: `pipeline_depth`, `launch_depth`, `pending_run_depth`, `workspace_budget_bytes`. Do not pass None or 0 as substitutes for absence. Reject contradictory explicit controls on an automatic class. Preserve existing classes' explicit depth behavior.

Include this declaration in `_standalone_worker_groups` so automatic and explicit classes cannot share a Worker. Extend `tests/ut/py/test_scene_test_capacity_grouping.py` and config/factory tests to assert exact kwargs for both factories, group separation, contradiction rejection, and unchanged old controls. This is test-harness metadata, **not a new mandatory Worker/user knob**.

### B. Register one small automatic scene family for all four backends

Use `tests/st/<arch>/<runtime>/automatic_admission/` thin classes with a shared assertion helper, or equivalently new isolated classes alongside each existing early-enqueue module. Reuse the real small add/spin/fatal kernels and variable-directory entry sampler; avoid old helpers that assume a two-entry grant. Four backend registrations must be visible in the normal onboard workflows above, without a new manual-only workflow.

Minimum scenarios, with finite timeouts and identities attached to observations:

1. **Cold admission and sustained growth:** assert omitted controls and `_automatic_admission_requested`; hold the first logical run nonterminal with an existing host gate, start the next submit on another host thread, and prove its Python graph callback has not entered. Release the gate, prove the next callback proceeds only after whole-run retirement. Then continuously refill a bounded set of requests for multiple rounds, checking coherent LAUNCHED+accepted entries and joined-launch records before predecessor terminal, every golden result, distinct run/slot/generation identities, FIFO retirement and repeated slot reuse. Observe growth beyond the initial probe (at least a third concurrently used set where the backend/resources support the small workload), not merely a single two-run overlap. Sequential-only controls must reuse slot zero instead of ratcheting capacity by call count. Use endpoint completion capacity samples already in MailboxRunEntry; do not equate the 168-entry directory with readiness.
2. **HOST and DEVICE ownership:** mutate/release caller HOST storage immediately after successful submit and verify the run consumes its admitted copy; distinguish the Python graph callback from the HBG native orchestration callback. Run a persistent DEVICE output chain while withholding waits; a caller free must remain refused until its final consumer is proven complete. On both TMR backends include an all-DEVICE path that creates no input staging yet still proves new sets and grows admission. Follow with a HOST input on a reused slot to check the mixed-demand path rather than assuming its earlier zero-byte proof covers the new bytes.
3. **Error and close:** use the existing bounded reported-fatal workload under automatic mode; verify error attribution, no successful result for unproven work, predecessor/successor disposition, caller storage protection, and close with outstanding accepted runs. Keep physical stop-unknown/device-fault injection in deterministic boundary coverage unless an existing safe hardware fixture supplies it. Do not manufacture a device hang to exercise this feature.

For the multi-endpoint contract, add one two-device automatic case in the existing two-endpoint style: delay one endpoint's whole-run contribution, observe that a faster endpoint does not independently admit the next logical run, and check conservative fold/identity preservation. It can share the same scene helper; it is not a separate feature or capacity PR.

### C. Connect deterministic pressure to the actual native owners

Keep the existing coordinator/model tests. Add a bounded native integration target that invokes the **production DeviceRunnerBase allocation adapters and C API prepare/park/abort path**, using a deterministic test backing provider at the existing WorkspaceManager backend seam and stubbed platform attachment/device operations as needed. A fixture that merely returns WAIT from HostApi, or copies the runner adapter into another Endpoint, does not close this gap.

Use four maker configurations, with a focused shared scenario table rather than multiplying every state-machine case already covered. Minimum cases: idle trim succeeds on the one retry; 40/30/20 with a named launched consumer parks and resumes only on its retirement; unrelated retirement causes no allocator polling; exhausted holders produce typed UNAVAILABLE; multi-region failure preserves old published blocks and drops only new claims; non-capacity failures never WAIT. HBG must run its real callback once across Runtime destruction and retry, preserve staging address/publication/diagnostics, and release the saved owner on abort. Assert reservations, epoch references, token, bank pointer and ledger charges at each boundary.

In the same target cover retry failure immediately after reservation adoption and before bind, failure after bind during publication, close while parked before/after HBG callback, and abort refusal/stop-unknown retaining caller borrows. This closes E6/E9/E14/E15/E18/E23 at the integration seam that current fixtures skip. These are deterministic allocator-pressure **boundary tests**, not a claim that physical device OOM was induced or recovered. No mandatory byte-budget setting, global allocator pressure, giant allocation loop, hidden memory fraction, or user-facing fault knob is required.

### D. Qwen composition is a separately labeled consumer acceptance

Merged #2592's `examples/a2a3/host_build_graph/qwen3_14b_serving_effective/continuous_worker_submit.py:238` explicitly passes both `pipeline_depth=args.pipeline_depth` and `launch_depth=args.launch_depth`; `main` also requires depth at least four. Its `tests/ut/py/test_qwen_continuous_worker_submit.py::_exercise` uses callback fakes and SimpleNamespace handles. No workflow invocation of the continuous driver was found; compiling Qwen standalone kernels in scene workflows is not running this driver.

After the small automatic scenes/boundary tests, qualify an omitted-controls construction of this existing consumer with its ordinary mixed request lengths, numerical reference, DEVICE feedback, resource reuse and close checks. Preserve its explicit mode and external ownership; do not label its old configured-depth evidence as automatic evidence. This can be a same-PR compatibility entry or clearly identified follow-up acceptance run on the merged driver, consistent with S7; it is not grounds to postpone A–C into another capacity implementation PR. #2605 borrowed-L2 vLLM is excluded entirely.

## 4. Completion criteria and limits

The next review should be able to enumerate registered test node IDs/CTest targets for A–C, inspect that automatic Workers truly omit the controls, and trace each pressure/ownership assertion to a production boundary. Execution evidence must identify exact source/build/backend and distinguish native deterministic tests from onboard scenarios. Four-backend default-auto sustained/ownership/failure-close results and actual pressure-boundary results cannot be inferred from old explicit-depth passes or a green aggregate CI number.

At this audit's end, A–C are missing test-definition/integration coverage, not executed failures. Existing model/maker definitions are useful and should be retained. The preceding static source review's “no concrete source blocker found” remains narrowly true; it does not complete the approved feature acceptance contract. No Ready claim is warranted from this audit.
