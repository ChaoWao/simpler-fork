> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# #2596 post-merge source review

## Scope and conclusion

Reviewed merged `a2b0ad2ade8a3cecb1517f2ee937626773f7aa85`, published head `77460ea26466cb1d17a23f8a51758034a95c4a43`, and old base `17129ba496c6d7d710b0e1320b3d1c25aeb9d1e1`. Main and published head have the identical tree `60cbe4bbcba089cf21948e923ceaf717840cfc5a`; the original product checkout remains on 77460 and clean. Source anchors below refer to that merged tree unless explicitly marked otherwise. The manager's bound gate is `post2596-review.review.json` and `.preflight-live.json`, checked 2026-10-17 01:27:51 UTC. This is a bounded read-only review, not fresh runtime acceptance.

**Main conclusions:** the alleged arena check/detach race is not reachable through the current supported native prepare path; terminal capacity folding has the required lock ordering and an existing decrease regression. A same-thread, different-context native reentry can destroy the outer workspace TLS identity: this is a concrete conditional API-boundary risk, not evidence that ordinary Python submissions mix contexts. Cold admission does deliberately remove first-pair preparation overlap, and this behavior is already documented. The automatic directory is finite: **168 resource-set identities**, not unlimited requests.

## 1. Arena trim: check/detach separation is real; the alleged production interleaving is blocked

`src/common/platform/include/host/run_resource_trim.h:121` checks each arena backing through `idle_backing`, then the arena branch at 169–183 detaches through the zero-size replacement transaction. Staging at 157–166 instead uses `detach_idle_backing` atomically. There is no ledger lock spanning all arena checks and detachment. Consequently the helper is not independently safe against an arbitrary concurrent new reference. Its caller contract at 73–76 requires that the owner has already excluded live/saved-plan consumers.

The production owner establishes that exclusion:

1. `src/common/platform/onboard/host/c_api_shared.cpp:1564` reserves the native run before `WorkspacePlanScope` at 1580 and bind at 1691. The reservation remains held throughout both growth points and any trim inside them.
2. `device_runner_base.cpp:4834` / 4844–4868 serializes reservation admission with `native_run_mu_`. Every existing reservation must already have an execution claim before a successor may reserve. The preparing trimmer has no such claim: `launch_prepared_run` acquires it only later at `c_api_shared.cpp:1971`.
3. Therefore a second thread doing a **non-growing**, warm prepare still fails reservation before bind. Avoiding the growth token does not avoid this gate. The proposed interleaving “T1 checks bank idle; T2 warm prepare references it; T1 detaches it” cannot pass step 2.
4. The growth view snapshots existing reservations under that same mutex (`device_runner_base.cpp:655–680`, 4882 onward). `run_resource_growth.cpp:89–92` excludes their slots and banks, independently of their current ledger ref counts. A predecessor completing after the snapshot only leaves a conservative exclusion. No new prepare can reserve while the trimmer is merely prepared.
5. A live saved plan without a reservation disables all arena trim (`run_resource_growth.cpp:74–77`), while live/reserved/requesting slots exclude staging trim too. This covers the parked-plan case where the view cannot recover the bank from a reservation.

Call-site audit: onboard `bind_callable_to_runtime` has one production caller, the reserved prepare above; `simpler_run` composes prepare at `c_api_shared.cpp:2399`. Arena setup is an internal HostApi callback (`c_api_shared.cpp:448,600`), not a standalone exported C API in `runtime_c_api.h`. Runtime makers use it during bind/resume. Production trim is entered through `grow_with_one_retry`, from retained staging at `device_runner_base.cpp:1052` or arena setup at 1499. The convenience C++ `trim_idle_run_resources` at 755 has no production caller. Private workspace reference additions at 846 and 1004 are the arena/staging prepare paths; callers' imported allocations are not an alternate API for referencing these private banks. Sim has a separate runner and does not participate in default automatic admission.

**Disposition:** reject a current production use-after-free finding based only on the split checks. This proof depends on preserving the reservation invariant and supported entry paths; calling the internal helper/HostApi concurrently outside that ownership contract would not be covered.

**One worthwhile regression, proposed only:** run two real native prepares on one fake-RTS runner. Suspend T1 after trim eligibility and before detachment while its reservation is held. Have T2 attempt a warm, non-growing prepare on the candidate bank from a second thread. Assert rejection occurs before T2 bind/reference, allow T1 to finish, release/finalize T1's unlaunched reservation, then verify T2 retry uses current storage. Assert both backing lifetime and reference epochs. Use deterministic barriers, not sleeps. A helper-only test injecting a ledger reference while bypassing reservations would test a different contract. Existing trim tests do not exercise this native contention seam.

## 2. Terminal FIFO fold and decreased reports

`orchestrator.cpp:160` records nonzero endpoint reports under `runs_mu_`; this alone does not change admission. `retire_terminal_run` at 280 holds that mutex continuously across FIFO removal (287), lease release (299), minimum fold/apply (304–305), and successor lease/early-launch assignment (306–307). It unlocks before `runs_cv_.notify_all()` at 310. `begin_run` uses the same mutex and FIFO-size predicate at 69–83. There is no window where a freed FIFO position grants a new lease at the pre-decrease bound.

`finish_run_if_ready` notifies the separate completion CV at 271 before retirement. That notification is not the admission CV and does not bypass `runs_mu_`; it is not the alleged stale-capacity wakeup. Existing leases are deliberately kept valid even above a newly reduced prefix (`acquire_lease_locked`, 211 onward).

`tests/ut/cpp/common/hierarchical/test_early_launch_admission.cpp:280–326`, `AutomaticStartupWaitsForProofThenEnqueuesACompatibleSuccessor`, already checks startup one, no expansion from a sample alone, expansion on retirement, then a 2→1 decrease while a fourth submitter waits. It checks that the third run retains its slot/generation and that the fourth remains blocked until that run retires. This is actual async waiter coverage, though its timeout checks are not a deterministic lock-window hook. No test was run in this review.

## 3. BASE-5: planned diagnostic reservation, not an available hole

Merged `src/common/worker/runtime_c_api.h:117` says BASE-5 is held by diagnostic collector capacity refusal. Full tracked-source searches for the enum, numeric `-1005`/standalone 1005, `ERR_BASE - 5`, and literal arithmetic found no implementing symbol in merged main. Collector implementations examined return their existing INTERNAL/-1 statuses. Thus the merged comment describes a consumer that is not yet in this tree.

However the external review's “no source for this reservation” conclusion is wrong: the manager identified in-flight #2588, and read-only inspection of its temporal worktree confirms `runtime_c_api.h:133` defines `SIMPLER_RUNTIME_ERR_DIAGNOSTIC_CAPACITY_FULL = PTO_RUNTIME_ERR_BASE - 5`, with its launch-retry contract immediately above it. This is separate in-flight work, not a merged-main implementation or permission to modify that branch.

**Recommendation:** retain the reserved numeric value. Clarify the merged comment as a planned reservation for diagnostic launch capacity / #2588 if a follow-up is made. Do not renumber RESOURCE_WAIT/UNAVAILABLE or occupy -1005.

## 4. Cold 2→1: exact visibility boundary and cost

Old base `python/simpler/worker.py:582` requested two native sets by default; omitted pending depth returned zero (derive from grant), while omitted launch depth returned one. Thus two logical runs could be admitted/prepared, with early device enqueue still separately restricted. All four configuration knobs were optional already.

Merged automatic mode explicitly installs `initial_admission_depth=1` before accepting runs (`worker.py:9398–9405`). The proof/report chain is:

1. Successful native prepare marks its slot proven only after all preparation requests complete (`c_api_shared.cpp:1746–1753`). A warm/zero-private-byte run qualifies too.
2. The child publishes `cw.admissible_run_resource_sets` after run completion/finalization at `worker.py:3997–4013`, before its terminal mailbox store.
3. `LocalMailboxEndpoint::terminal_completion` reads that terminal-owned entry (`worker_manager.cpp:1178–1191`), then `Scheduler::worker_done` records its endpoint capacity before completion aggregation (`scheduler.cpp:148–154`).
4. Logical task accounting reaches `Orchestrator::finish_run_if_ready` (`orchestrator.cpp:765–774`); the submission must be closed and **all** active tasks finished (256–263), including SUB work or other endpoints.
5. Whole-run retirement performs the minimum fold under the FIFO lock (280–310). Only then can the second `begin_run` pass and enter its Python graph callback.

So the second cold logical run cannot overlap the first with graph construction, prepare, or enqueue. An early chip completion does not suffice if other work keeps that logical run alive. The first commit's proof is not immediately visible as new parent admission. Once retirement offers a larger prefix, later runs can overlap subject to compatibility and resources; this says nothing quantitative about latency/throughput.

This is the approved conservative startup cost, not grounds here to change the resource-proof boundary. The claim that it is undocumented is too broad: `worker.py:12706–12710` explicitly says a second submission blocks before its graph callback until the first whole run retires, and `docs/task-flow.md:319–326` explains report/fold timing. A useful documentation improvement is the direct old-default-two comparison and an explicit statement that first-pair preparation overlap is sacrificed. No benchmark inference is made.

## 5. TLS identity: conditional cross-context reentry defect

`AutoAdmissionState::Claim` (`auto_admission.h:81`) contains backing, region, epoch and ownership flag, but no context/ledger identity. Its vector is a single function-local thread-local object (`426–432`), and commit clears the entire vector. `WorkspacePlanIdentity` (`device_runner_base.cpp:578–592`) likewise has no owner. `begin_workspace_plan` overwrites it; `end_workspace_plan` at 601 resets it to zero. `WorkspacePlanScope` (`device_runner_base.h:788–795`) does not save/restore an outer scope.

This is safe against simple cross-thread overlap, and sequential different-context calls finish their scope before the next begins. It does **not** establish same-thread nesting safety. A minimal source-reachable sequence is:

1. Native context A reserves a run and opens its scope at `c_api_shared.cpp:1580`.
2. Its HBG bind calls the registered C++ host orchestration entry (`a2a3/.../runtime_maker.cpp:960`; A5 has the corresponding entry). This is inside that scope, before the final arena commit.
3. The entry invokes the exported `simpler_prepare_run` function for a separately initialized context B in the **same loaded runtime module and OS thread**. B's reservation mutex is independent. Use a separately registered orchestration SO, so there is no shared orchestration binding-global violation: `docs/task-flow.md:507–523` explicitly describes unique RTLD_LOCAL registrations. The callback can invoke a native function pointer; normal Python graph callbacks are not this nesting seam.
4. B returns and its scope destructor zeroes the TLS identity instead of restoring A.
5. A returns from its callback, parks its host plan and resumes at `runtime_maker.cpp:1610–1612`; `resume_host_plan` at 1110 calls `commit_host_plan` and arena setup at 727. Warm staging references at `device_runner_base.cpp:1004`, arena allocation at 882, and arena references at 846 now consume epoch zero. `WorkspaceManager::add_ref` at 1001–1005 treats zero as the context epoch and returns success without adding a run reference. Growth also reads the reset slot/epoch at 1469, potentially targeting the wrong plan/token.

A warm A bank with no input staging is a particularly narrow witness: it bypasses growth-token failure and reaches the missing-reference case. This proves lost run ownership attribution; it does not by itself prove immediate free/use-after-free because reservations still exclude that live bank from trim. Missing holder attribution can also distort refusal/wait decisions. The minimal callback sequence does not require claiming that an A arena allocation was already in the shared claim vector when B committed; that stronger cross-ledger-rollback assertion has not been demonstrated.

**Boundary/limitation:** the supported Python lane does not make this recursive native call. No documented native non-reentry rule or runtime guard was found, but absence of a prohibition is not a promise to support arbitrary callback recursion. Native HBG code is executable C++, and the public prepare entry can be called through a function pointer, so this is more than merely observing `thread_local`. No execution repro was run, and other tracing state also deserves consideration before claiming full nesting support.

**Narrow follow-up:** decide and enforce the native reentry boundary before modifying TLS. Reject nested prepare on the same runtime thread before it changes an outer identity if recursion is outside the intended contract. If recursion is intended, use owner/scoped claim transactions and restore all outer identity state; saving just slot/epoch does not make the shared claim vector context-safe. A regression should invoke real A prepare, enter B prepare from A's native callback with separate registrations, then assert either clean pre-mutation rejection or correct A epoch references and independent rollback. This task implements neither option.

## 6–8. Comments, prefixes, ABI and naming

- **“Never zero”:** `auto_admission.h:196` is overbroad. `affordable_sets(0)` explicitly returns zero at 201, and disabled mode returns its directory argument. The nonzero invariant applies to enabled, positive-directory contexts. Normal initialized automatic paths have a positive grant; no operational zero-capacity bug was found. Qualify the comment, not the zero-input behavior.
- **Prefixes:** keeping externally contracted PTO error names and naming new codes SIMPLER follows the no-new-PTO convention. Uniformly renaming legacy ABI names would be unrelated churn.
- **Version:** `PTO_PIPELINE_CONTRACT_ABI_VERSION` stays one (`runtime_c_api.h:137`); this feature does not alter that contract's layout. Automatic enable/query hooks are separately optional `dlsym` lookups (`chip_worker.cpp:334–338`); missing enable returns false and the nonautomatic query falls back to the grant (`953–963`). An old module therefore does not magically gain measured automatic behavior. This is optional capability discovery, not justification for a speculative version bump. No compatibility claim is made for mixing arbitrary partially implemented modules.
- **`g_workspace_plan`:** it is anonymous-namespace thread-local state, not a process-global identity. Renaming the prefix would not repair scope/owner semantics. Prefer a precise ownership comment and the targeted boundary fix above if authorized; no aesthetic-only refactor is recommended.

## 9. Why slots remain; actual finite bounds

A slot is a reusable **resource-set index**, not the admission policy and not a unique request identity. `PipelineSlotLease` (`runtime_c_api.h:227–237`) pairs the index with a generation; the pool increments that generation on reuse (`run_resource_arena.h:95–110`) and checks it on release. The native descriptor carries the selected slot, separate arena bank, and process-unique attempt epoch (`runtime_c_api.h:263–279`); run/dispatch IDs are further provenance. Stable indexed owners still hold retained staging, saved plan, runtime/argument state, diagnostic state and execution resources until their last consumers retire. Removing those indices would not remove the need for ownership and stale-completion protection.

Bank and slot are deliberately distinct: HBG declares private per-run heap/image resources (`a2a3 HBG runtime_maker.cpp:121–132`); TMR declares shared device scratch alongside per-run task arguments (`a2a3 TMR runtime_maker.cpp:101–113`). `ChipWorker::arena_bank_for_slot` at 879 computes the mapping. Calling every index merely “slot” in prose obscures this distinction, but the existing `pipeline_slot`/`arena_bank` fields are already explicit. Use “run-resource-set index”, “lease generation”, “attempt epoch”, and “arena-bank index” in explanations; a broad public rename is unnecessary.

The automatic directory calculation is `max(2, floor(65536 / 384) - 2) = 168` (`worker.py:590–600`). `MailboxRunEntry` has four 32-bit fields, eleven 64-bit fields, 256 error bytes and the capacity word; its aligned size/64-byte stride is 384 (`worker_manager.h:106,142–184`). Two request frames are transport handoff buffers (`118`), not two live-run slots. The run-entry table has 170 entries (168 + 2), using 65,280 bytes; control/request frames are additional mailbox storage.

Admission starts at one and can never exceed that granted directory; runtime declarations of UINT32_MAX do not create an infinite allocation or queue. With automatic pending depth derived from capacity, `begin_run` blocks while the nonterminal FIFO is full (`orchestrator.cpp:69–83`), including at 168. Lease allocation scans only the admitted prefix and returns no lease when all eligible sets are in use (`run_resource_arena.h:95–112`). Completed submissions can reuse identities, so 168 is a simultaneous identity ceiling, not a lifetime submission count. Backend/pair gates, workspace pressure, quarantined resources, serialized Python graph construction and whole-operator device ordering remain further restrictions.

Delivered removal: eligible local onboard users no longer need to choose depth/budget values to obtain automatic resource-based admission; the old default-two policy is replaced there by proof-based growth/shrink within a finite identity directory. It did not remove internal resource slots, promise arbitrary concurrency, or convert caller-side blocked submissions into an unlimited accepted queue.

## 10. Optional controls and review-policy claims

The old validators already defaulted pipeline depth to two, pending depth to zero/derive, and launch depth to one (`17129:worker.py:582–643`); absence of a workspace budget selected the preexisting path (`17129:worker.py:8820`). The four knobs were not mandatory for use. Accurate wording is “optional controls that previously selected static concurrency/budget policy; all omitted now selects automatic behavior for eligible topology.” Explicit settings and out-of-scope topologies retain their prior policy.

The user's single-commit complete-feature policy takes precedence over suggestions to split a feature into many commits. The PTO-ISA pin is unchanged in the merged delta; this review found no pin-specific incompatibility requiring a speculative bump. No follow-up implementation, PR response, or external branch modification is authorized by this report.

## Ranked follow-ups and validation boundary

1. **Highest: native same-thread reentry boundary.** Resolve the conditional identity-loss path in section 5 with a narrow guard or fully scoped ownership, plus one native callback regression. Treat this as an API-boundary correctness follow-up, not a claimed ordinary Python production failure.
2. **Ownership regression:** add the deterministic real-prepare contention witness in section 1 to preserve the reservation proof. No arena locking redesign is justified by the reviewed race claim alone.
3. **Small documentation corrections:** identify BASE-5 as planned #2588 reservation; scope “Never zero” to enabled positive-directory calls; optionally add the old-two/new-cold-one overlap comparison. Existing cold blocking documentation must not be described as absent.
4. **No action recommended:** gratuitous slot/TLS renames, error-prefix migration, pipeline ABI bump, PTO pin bump, unbounded-queue claim, or multi-commit rewrite.

Only source/tree/status/search inspection was performed. No tests, collection, build/configure/link/install, hardware, CI POST, commit, push, review comment or thread resolution. External #2588/#2600/#2605/#2608 ownership remains untouched. The original product checkout remains clean at 77460. This report neither adds runtime acceptance evidence nor claims CI readiness.
