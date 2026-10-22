独立 Claude CLI 最后复核；固定 PR #2663 head `97aaed615d2f42b06f645275cfa3749434c04851`，2026-10-22。下列 CI 状态是复核当时快照，最终结论见 [REPORT](REPORT.md)。

# Focused recheck: CodeRabbit HBG regression fix, head `97aaed615`

Network calls stopped. Everything below rests on the code I read, the mutants I built in `/tmp`, and the cached logs.

## The regression was real, and the fix is correct

**Contract.** `CapacityMetadata` (`scope_stats_capacity.h:31-36`) is `{bool required{true}; optional<Capacities> values;}`. The only `required=false` in the tree is HBG's accessor, `src/common/host_build_graph/runtime.h:573`, returning `{false, std::nullopt}` by value. TMR holds a member that default-constructs to `required=true`; `runtime_maker.cpp` only ever assigns `.values`, and its two reset sites (`a2a3:880, 923`, a5 equivalents) assign `= {}`, which is `required=true, nullopt` — the unknown case.

**At the prior head** `expected = run_epoch` unconditionally. HBG has no producer, so `state->run_epoch` stays 0, the epoch check in `snapshot_unchecked` always mismatched, `read_terminal_checked` returned false, and `write_jsonl` bailed with `INVALID_STATE`. The historical metadata-only artifact disappeared. I confirmed this by reading, and reproduced it as a mutant: replacing the new expression with `expected_producer_epoch_ = run_epoch;` fails exactly `ProducerlessRuntimeExportsMetadataWithoutADeviceEntry` on both a2a3 and a5, and nothing else.

**The fix** is one expression: `expected_producer_epoch_ = capacities == nullptr || capacities->required ? run_epoch : 0;`. Its direction is right — the lax branch needs an explicit `required=false`; `nullptr` and unknown both stay strict, so a future caller that forgets the argument gets the safe mode rather than a silent bypass.

**Strictness is still genuinely enforced, not softened to a no-op.** The always-lax mutant (`expected_producer_epoch_ = 0;`) fails *two* tests on both arches: `AnUnstartedRunCannotRecoverOrExportItsPredecessorsAccounting` and the second half of the new producerless test. So the new test covers both directions itself.

**Wiring.** All three `begin_run` call sites pass metadata: onboard `device_runner_base.cpp:4616`, sim `:1244`, and retained-unmapped `scope_stats_retained_runs.cpp:276`. All four runner `RunLocalDfxMetadata` construction sites (a2a3/a5 × onboard/sim) pass `&prepared.scope_stats_capacity`. That member is a by-value copy taken in the `PreparedExecution` constructor (`device_runner_base.h:1543`), which outlives the admission call, so HBG's by-value accessor temporary cannot dangle.

**Ownership and surface.** `begin_run` reads `capacities->required` and stores a `uint64_t`; it retains no pointer. The only collector-side `CapacityMetadata` by-value member (`scope_stats_collector.h:549`) is the pre-existing retained Slot field. No new boolean or mode enum — `active_run_epoch_` was renamed to `expected_producer_epoch_`, same type and size. No `extern "C"`, wire-struct, binding, or public-Python change in the delta. No ABI or API bloat.

## Evidence quality

Your public reproduction is a genuine one, not a unit stub: `LOCAL_PATH` drives the real `simpler.worker.Worker(level=2, runtime='host_build_graph')` with `enable_scope_stats=True`, asserts the computation (42.0) and exactly one metadata line with `total==0, dropped==0`. Red log: `computation_correct: true, metadata_artifact_exists: false`. Green on both a2a3sim and a5sim.

Cached final-head CI confirms all eight `test_worker_collects_scope_stats_each_invocation` combinations PASSED — a2a3 and a5 × HBG and TMR × owned and borrowed. `LOCAL_PATH`: 17 success, 1 in-progress, 1 skipped.

My own run at final head: all nine scope-stats and admission suites pass shuffled ×3 on both arches (collector 10, read_failure 1, retained 25, TMR collector 3, TMR admission 7).

The carried-over Should-fix from my last round is closed: the `buf_seq` doc row (`docs/dfx/scope-stats.md:280`) now says "persistent rotation counter", matching the corrected header.

## Remaining issues

**Must fix:** none.

**Should fix:** none.

I looked for a reason to keep the metadata-only export itself on a leash — HBG's artifact carries all-zero capacities read from a header no HBG runtime populates — but that is unchanged from the merge base, so it is not this PR's to answer.

## Meaningful-PR assessment

Still yes, and this round strengthens it. The delivery now covers both runtimes end to end: TMR gets device-entry producer initialization with an epoch-checked export, HBG keeps its producerless metadata contract, disabled invocations are provably inert, and the public hardware matrix asserts all three behaviors across both arches and both ownership modes. Finding and fixing a compatibility regression that the first two rounds missed is the opposite of a line-count exercise.

## Verdict

**Approve, contingent on final-head CI completing green** — one A5 scene job is still in progress, and `deploy` skipping is expected. Nothing in the code is blocking.

One note for the record: `ut-a2a3` and `ut-a5`, the jobs carrying the eight-case hardware matrix, have already passed at this head per the cached logs. The remaining in-progress job is a scene job, so the hardware evidence specific to this change is already in hand.
