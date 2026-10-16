# Joined-launch DFX: implementation design and decision

Manager checkpoint (2026-10-16 08:22 UTC): da746 CI ended with 16 successes, 1 skip, A3 HBG failure and macOS timeout cancellation. This PR is not Ready. The author has resumed causal investigation. The latest macOS CTest completed 358 cases, followed by about four minutes of silent checks before timeout; earlier timing breakdowns below are historical. Absent close notes do not prove close never ran; matching physical IDs in one run pair do not prove why an older pair differs. Build optimization recommendations remain proposals, with no implementation or measured benefit. External PR #2600 owns PMU host cleanup and requires later integration; it is not a demonstrated repair of this live-run PMU failure.

设计归档快照：状态与阅读顺序见[索引](../README.md)。设计约束、历史合同和当前实现须分别阅读。

> **How to read this file.** §0 below is the **current status**; everything from §1 onward is the
> **historical design baseline**, kept as written so the reasoning that produced the shipped code
> stays readable. Where the two disagree, §0 governs, and each superseded claim is labelled at the
> place it was made. The design was written before implementation; it is no longer "design only",
> and three of its claims are now wrong.

## 0. Current status — supersedes the header and §1/§8 below

**This design is implemented and published.** PR
[#2588](https://github.com/hw-native-sys/simpler/pull/2588) is OPEN with **one** commit:

| Field | Value |
| ----- | ----- |
| head | `da7460e15` |
| base | `e994e106d8020c049dc419a67498db37bc6fcd3d` (main, re-fetched; **#2595** forked-child worker and **#2597** argument materialization landed, plus #2592 earlier) |
| shape | 1 commit, 141 files vs main, `+21417 / −537`, 0 file deletions |
| author | `Chao Wang` |
| open PRs | 33, re-enumerated this session. **#2596 is P4's own admission PR** — it is fixing the confirmed admission gaps and is P4's alone; it shares one file with this PR (`onboard/host/device_runner_base.h`) and touches nothing this PR touches in it. #2597 merged; external #2583 / #2453 / #2436 / #1967 / #1987 remain theirs. |

Previous heads: `b2a7feb41` on base `1a8a3ee22`, and `c04408217` on base
`3351950f4`. **No CI run exists for the current head.** Nothing below may be
read as current-head qualification; §0.3 and §0.4 are what CI has actually
said so far.

### 0.1 Superseded claims, named

| Where | Claim as written | Status now |
| ----- | ---------------- | ---------- |
| header | "**Design only.** No product edit, build, test, hardware run, CI action, commit or publication." | **Superseded.** Product code, tests and docs are edited and published under the head above. Still true in this file's own terms: no hardware run and no CI action were taken *by this artifact*, and nothing here is a manager file. |
| header | "P4 (`PID1403923`) is the sole implementation writer … Approval is not requested." | **Superseded.** P4's automatic resource admission is **approved**, and the original P4 writer is actively implementing it on `codex/auto-resource-admission` (per the current contract; that branch is on P4's own remote, not observed from here). It is **not** pending approval, and nothing in this PR waits on it. P4's normative §3/§12 remain adopted verbatim. |
| header | "Baseline main `accab1cc5…`" | **Historical.** The shipped base is `1a8a3ee22` above. |
| §1 | "four onboard backends × **six output owners**" | **Superseded for one cell.** Six is the owner *set*, not a per-backend count. a5 `host_build_graph` has no ArgsDump producer; see §0.2. The remaining applicable cells remain in the delivery scope; this is not a claim that their current-head acceptance passed. |
| §8 | ArgsDump row: `✓ ✓ ✓ ✓` | **Superseded.** The a5 HBG cell is **unsupported and refused**; see the matrix in §0.2. |

### 0.2 Current support matrix

User direction (recorded in the current contract): *a5 `host_build_graph` is to be treated as not
having the ArgsDump capability; adding it is separate, later work.* The task-boundary capture
protocol was **not** approved and is **not** implemented.

| Owner | a2a3 HBG | a5 HBG | a2a3 TMR | a5 TMR | sim |
| ----- | -------- | ------ | -------- | ------ | --- |
| swimlane L1–L2 | ✓ | ✓ | ✓ | ✓ | inapplicable |
| swimlane L3 | ✓ | ✓ | ✓ | ✓ | inapplicable |
| swimlane L4 | ✓ when initialized at L4 | ✓ same | ✓ same | ✓ same | inapplicable |
| **ArgsDump** | ✓ | **✗ unsupported — request refused** | ✓ | ✓ | inapplicable |
| PMU | ✓ | ✓ | ✓ | ✓ | inapplicable |
| ScopeStats | ✓ | ✓ | ✓ | ✓ | inapplicable |
| DepGen | n/a (TMR channel) | n/a | ✓ | ✓ | inapplicable |
| HBG host graph | ✓ host-side | ✓ | n/a | n/a | n/a |

**Why that one cell.** a5 HBG's route is the AICore Scheduler: the on-core schedulers both dispatch
tasks and reap their completions, so no AICPU thread holds a task's own boundaries, and the only
instants an AICPU thread owns are the run's. A snapshot taken there is not that task's data — a
consumer's input would carry the content from before its producer ran, and a buffer several tasks
write in turn would report the last write as every one of their outputs.

**How the refusal is expressed.** `args_dump_capture_supported_impl` joins the per-runtime
`*_supported_impl` predicates the platform's `c_api_shared.cpp` weak-defaults and each
`runtime_maker.cpp` answers — 1 on a2a3 HBG and on both TMR runtimes, 0 on a5 HBG, verified in all
eight built `libhost_runtime.so`. `simpler_prepare_run` refuses an `enable_dump_args` request that
answer cannot serve with `PTO_RUNTIME_ERR_UNSUPPORTED` (-1001), and `simpler_run` delegates to that
entry, so both public entrypoints carrying a per-run `CallConfig` are covered. Only that owner is a
term: a request not naming it reaches the device unchanged, and one naming the swimlane and PMU
beside it is refused for *it* while those two stay available.

**Where in the sequence the refusal lands — stated exactly.** The check is at the top of the C
preparation entry, before the runtime carrier is constructed, before any diagnostic reservation or
bank, before the bind, and before any device launch. It is **not** before all caller-side state:
`ChipWorker::prepare_native_run` has already inserted a `PREPARING` `NativeRunEntry` and the lane may
already have borrowed this run's device spans by the time it calls the C entry. Those are unwound on
the error — the entry is dropped, the borrows released — and `chip_run_lane.cpp::launch_front` fails
only that run, popping it from the FIFO without poisoning the lane. "Nothing was prepared" is a claim
about the runtime carrier and the device, not about the caller's bookkeeping.

**Deferred, not pending.** `docs/investigations/2026-10-a5-resident-per-task-args-capture.md` is the
bounded design that would close the gap. It is deferred to its own change at the user's direction;
nothing in this PR or in P4's work waits on it, and its four-row contract delta is what such a change
*would* take rather than a decision owed to anyone.

### 0.4a Current head `da7460e15`: the split fields answered, and what they ruled out

A3 onboard job `116872397143`, test merge `2b41e84d` (`da7460e15` + main `e994`).
a2a3 **TMR passes** on device 3. a2a3 **HBG fails** on device 2 at scene index 1
— the *second* run, epoch 2 — with 513 rows, correct identities, per-core
167/179/167 matching that epoch's own terminal, all cycles zero. Index 0
(epoch 1) passed. The separated fields report, verbatim:

```text
run 1 ... at init enabled=72 not-enabled=0 no-window=0; at close held=72 lost=0;
          first reporting producer: physical core 0, PMU_CTRL_0 0x7 at init, 0x7 at close
run 2 ... at init enabled=72 not-enabled=0 no-window=0; at close held=0  lost=72;
          first reporting producer: physical core 0, PMU_CTRL_0 0x7 at init, 0x2 at close
```

Per-producer, for the three cores that executed tasks — the same logical→physical
mapping in **both** runs, so the run-to-run physical-id difference seen at `b2a7`
was two different core allocations and nothing more:

| run | logical → physical | init | close |
| --- | ------------------ | ---- | ----- |
| 1 (passed) | 24→25, 26→27, 28→29 | `0x7` (code 2) | **`0x7` (code 4, held)** |
| 2 (failed) | 24→25, 26→27, 28→29 | `0x7` (code 2) | **`0x2` (code 5, lost)** |

**What this rules out.**

- *"The init write never took."* **Ruled out for both runs.** `at init
  enabled=72` is a per-core predicate satisfied by every one of the 72 reporting
  producers, in the run that passed and the run that failed.
- *"The AICore's per-task `GLB_PMU_EN` clear is what the AICPU observes."*
  **Disfavoured by a control inside the same log.** Run 1's three task-executing
  cores ran the identical per-task `pmu_aicore_begin` / `pmu_aicore_end`
  sequence and closed at `0x7`. If that clear were visible in the AICPU's
  `PMU_CTRL_0`, run 1's task cores would have closed at `0x2` as well. They did
  not. And in run 2 the 69 cores that executed *no* task are also counted
  `lost`, so whatever changed the word reached cores the per-task gate never
  touched. The two are separate code interfaces — an AICore SPR and an AICPU
  MMIO word — and the shared field *name* in two comments is **not** evidence
  that they are one register.

**What is now correlated, and still not explained.** Across these two runs the
close-time window state and the symptom move together: `held` with non-zero
cycles and a passing index, `lost` with all-zero cycles and a failing index, on
the same device, the same cores and the same code. `0x2` is also exactly what
`pmu_stop` would write if the saved pre-run word were `0x2`, so "the restore
preceded the observing read" remains open — but it has to explain why the
*order* differs between two serial runs rather than why the *value* differs.
Nothing here names the writer. The 69 non-printing cores' words are still not
observed, only their predicate; a causal claim needs the per-core notes, and the
next step is the manager's to assign.

### 0.4 What CI has found so far, head by head

**`b2a7feb41` — workflow for that head completed. A5 passes; A3 does not.**
Job `116853656768` (A5 onboard) verifies the strict HBG scene (7.0 s) and the
strict TMR scene (7.1 s) on device 2, plus the DFX smokes: **the host-extension
run-ownership repair below is confirmed on hardware.** Job `116853656790` (A3
onboard) still fails the HBG agreeing pair on device 6, and its TMR strict scene
passes on device 4 (7.6 s). The tested merge is `029d66bad72d038e80388da2b92d67711f595b94`
= `b2a7feb41` + main `8c3b0e174` (so #2595 was included; the A3 failure is not a
missing-#2595 artefact). A5 HBG ArgsDump stays unsupported and deferred.

The A3 failure moved and the new discriminator is why:

| Field | run 1 (epoch 1, scene index 0) | run 2 (epoch 2, scene index 1) |
| ----- | ------------------------------ | ------------------------------ |
| rows | 513, correct identities, cores 24/26/28 = 181/162/170 | not reached — index 0 failed first |
| cycles | **all zero** | — |
| close read-back | **held=0, lost=72** | **held=72, lost=0** |
| first producer | physical 0, `PMU_CTRL_0` **0x2** | physical 24, `0x7` |

Device boundaries from the same log put the two ops strictly serial —
`aic_start` 1914103843035 for run 2 against `wo_end` 1914103843033 for run 1,
and disjoint `device_wall` windows — so **no concurrency between the two runs'
AICPU ops**, and the shared AICPU-SO globals are not being written by two live
runs. The per-producer physical ids (59/61/63 for run 1's logical 24/26/28,
57/59/61 for run 2's) are all inside the AIV range of the flat register table
and are two different, internally consistent core allocations; they are not
evidence of a corrupted mapping. Physical 0 as the *first reporting* producer is
the real mapping of logical core 0 — the `c044` log reports the same id for the
same position, with `0x7` at init.

**What is actually observed, stated at its real width.** Four raw words were
printed for run 1, not 72: the *first reporting* producer (physical 0) and the
three task-producing cores (logical 24/26/28), each `PMU_CTRL_0 = 0x2`. The
figure `lost=72` is a **predicate count, not 72 raw values** — and at `b2a7`
that predicate was `(close word carries the enable) AND (init code was
ENABLED)`, so a core counted `lost` may have failed either term. `lost=72`
therefore does **not** establish that 72 cores read `0x2`, nor that 72 cores
had been enabled at init, nor which of the two terms failed on any core beyond
the four printed. The init distribution for that run is not recoverable from
this log at all, because the close note overwrote it.

`platform_config.h` names the three fields in that word —
`GLB_PMU_EN | USER_PMU_MODE_EN << 1 | SAMPLE_PMU_MODE_EN << 2` — so the four
printed `0x2` words are `USER_PMU_MODE_EN` alone, with `GLB_PMU_EN` **and**
`SAMPLE_PMU_MODE_EN` clear. In this repo the only writer of a non-`0x7` value
is `pmu_stop`, which writes the word that run's own init saved.

**Two endpoint reads do not describe the interval between them.** Even for run
2, where init and close both carry the enable, a disable and re-enable *within*
the run is fully consistent with both observations: the pair bounds the
endpoints and says nothing about continuity. No claim here may be read as "the
enable held across the run".

**Candidate explanations, none adopted and none excluded.**

- *`GLB_PMU_EN` has two writers.* `pmu_collector_aicore.h` names the AICore
  `CTRL` SPR bit 0 `GLB_PMU_EN` — the same named field the AICPU reads as
  `PMU_CTRL_0` bit 0 — and `pmu_aicore_end()` clears it with an unsynchronized
  read-modify-write after every task. This would account for a cleared bit 0 and
  a clobbered bit 2 on the cores that executed tasks. It is a hypothesis about
  aliasing; the field names are repo text, **the aliasing is not established**,
  and the per-task gate reaches only task-executing cores.
- *`pmu_stop` had already run* for the observed cores when the read was taken,
  which needs a second `pmu_aicpu_finalize` for the same core. The only caller,
  `SchedulerContext::shutdown`, runs once per AICPU thread over a disjoint core
  slice; `emergency_shutdown` touches no PMU and none appears in this log. That
  makes a second finalize unlikely on the paths read, **not impossible**.
- *The init write never took on some cores* — indistinguishable from the above
  in `b2a7`'s combined predicate, and the reason the predicate was split.
- *Mixed states across cores*, *cores whose close note is simply absent*, and
  *a register window that is writable but not this core's* all remain open; the
  printed sample of four cannot separate them.

No register write was added, removed or reordered, and no mapping was guessed.

**The precise remaining evidence gap, and why the next run closes it.** The
first revision of this discriminator wrote its close observation *over* the init
one and folded the init code into the close verdict, so `lost=72` could not be
told from "init never enabled on 72 cores" and the init distribution was gone
(`at init only enabled=0 not-enabled=0 no-window=0` in the log above). That is a
defect in the evidence channel, and it is fixed at this head: the close
observation has its own field, its code is decided by the close word alone, and
the host prints both distributions and both words.

That separates the two terms the old predicate conflated. It does **not** turn
the next run into a verdict. Readings that would *narrow* the search, none of
them exhaustive and none of them uniquely causal:

| at init | at close | narrows toward, without proving |
| ------- | -------- | ------------------------------- |
| `enabled=N` | `lost=N` over cores that executed nothing | a writer that is neither the per-task gate nor our own restore — but says nothing about which, and a wrong-but-writable window would read the same |
| `enabled=N` | `lost` ≈ the count of task-executing cores | the per-task `GLB_PMU_EN` clear — *supports* the aliasing hypothesis, and a coincidence of counts is not aliasing |
| `not-enabled=N` | either | the init write, rather than the window — leaving open whether the address, the value or the field is at fault |
| mixed, or close notes absent | — | nothing yet; the sample has to be read per core, not per count |

The counts are evidence about where to look next. **A repair must not be
selected from counts alone**, and the per-core notes — not the aggregates —
are what a causal claim has to rest on.

### 0.5 What the `c044` head's CI found (history)

Three concrete failures, all in the joined-launch acceptance suites, from the
two saved onboard logs (final checkout `be10986` = `c044` + `335` in both):

| Job | Suite | Failure |
| --- | ----- | ------- |
| `116828803984` A5 onboard | a5 HBG, agreeing pair | scene index 1 — **the second run, epoch 2** — published no AICore task rows; the drain logged `A5 HBG: failed to publish AICore task records` and `Runtime chip-swimlane extension publication failed` |
| `116828803930` A3 onboard | a2a3 HBG, agreeing pair | scene index 1 (epoch 2): 513 PMU rows, correct task identities, **all zero cycles**; per-core 155/180/178 |
| `116828803930` A3 onboard | a2a3 TMR, *disagreeing* pair | scene index 1 (epoch 4): 64 PMU rows, correct identities, all zero; per-core 23/23/18 |

Scene run labels are **zero-based** (`enumerate(prefixes)`), so `run 1` is the
second run of its pair. The per-core counts match that run's own terminal
exactly, so the records are correctly attributed — **cross-run misattribution is
excluded, not suspected.**

**Failure 1 — the host extension's run ownership. Fixed at this head.** The
runtime publishes `aicore_tasks` / `scheduler_tasks` through
`HostApi::publish_chip_swimlane_extension`; the collector kept one *resident*
slot per section for every run it served. The log's own ordering is the proof:
the `chip.run` span for `run_epoch=1` closes at `mono_ns=…155530225`, the two
warnings land at `…147214184`, and `run_epoch=2`'s span closes at `…149729408`
— so the refusal is the **second** drain's. The sequence is `run_begin(1)` →
`run_begin(2)` *(clears the slot while run 1 is still executing)* → drain 1
publishes → `run_close(1)` copies the slot **without clearing it** → drain 2 is
refused as a double write → `run_close(2)` copies the same slot, so epoch 2's
artifact carries epoch 1's rows, whose `run_epoch` column matches nothing epoch
2 executed. The scene's epoch filter rejected them, which is why the symptom was
"no rows" rather than wrong rows.

The publication now carries the publishing run's epoch, taken from the caller's
own `HostApi` rather than from the runner, and a retaining collector stores the
section in that epoch's bucket. A section naming a run the collector does not
hold open is refused rather than stored anywhere; a second publication of the
same section by the same run is still refused and the first copy is kept; the
per-run budget is charged at the publication against that run's bucket, before
the copy exists; and a refusal marks that run's metadata incomplete so an absent
section is never read as a run that produced none. A new unit case,
`EachOpenRunPublishesItsOwnExtension`, drives two open runs and asserts each
artifact carries its own section and neither carries the other's.

**Failures 2 and 3 — the a2a3 zero-cycle PMU result. Not fixed, and not claimed
to be.** It is the **same unresolved issue** carried through five rounds, not a
regression from `c044`: that delta touched the ArgsDump capability predicate,
four runtime makers, the a5 lifecycle comment and the a5 scene, and **no a2a3
PMU code at all**. The earlier head `1c811` passed on different devices, which
is not a root-fix proof. New this round: both runtimes show it; the affected run
is not the first of its pair; and the TMR case is the **disagreeing** pair —
which does not join — so joining is not a term.

What this head adds is the one piece of evidence that separates the candidates,
and nothing else: each core's close now reads its control word back **before**
the restore and replaces the per-producer note with `PMU_NOTE_HELD` or
`PMU_NOTE_LOST`, so the collector's line reports an init-time figure and a
task-time figure apart. `HELD` with zero cycles narrows the question to the
AICore's per-task gate or to the measurement; `LOST` narrows it to whatever
cleared the window mid-run; a note still reading `ENABLED` is a core whose close
never observed it. Neither code is proof a counter counted — both are read
through the address the AICPU programmed, and the AICore `CTRL` gate is an
AICore SPR with no AICPU read path. **No assertion was relaxed:** the non-zero-cycle,
row/identity, record-completeness and execution-match checks are byte-identical
on all four backends, and the a5 HBG ArgsDump owner remains unsupported and
refused.

### 0.3 Evidence boundaries

- **CI exists, and it is mixed.** At `b2a7feb41` (workflow `38863843732`) the final rollup is
  **15 success, 1 skipped, 1 failure, 2 cancelled**: **A5 onboard passes** the strict HBG and TMR
  scenes and the DFX smokes, **A3 onboard fails** the HBG agreeing pair (§0.4), and the two
  cancellations are **job-timeout annotations, not test failures** — see §0.6, including which
  checks therefore have *no* completion evidence. At the earlier `c04408217`: workflow `38855904192`
  finished **16 success, 1 skipped, 2 failure** on the repository's own checks, with the external
  status counted separately (§0.5).
- **A CI run for the current head `da7460e15` exists and was still in progress when this was
  written** — workflow `38869839635`, 8 success / 1 skipped / 11 in progress at inspection, with
  `ut-a2a3` and `ut-a5` already successful. **No current-head verdict is claimed.** No retry was
  submitted. The matrix in §0.2 describes the intended supported scope, never passed current-head
  qualification, and the feature is **not Ready**.
- The A3 strict HBG/TMR scene passes were observed on the **earlier** head `1c811a039`, on different
  devices. They are neither a result for any later head nor a unique root-cause proof for the a2a3
  zero-cycle PMU question, which **remains unresolved** (§0.4).
- **No test of any kind was executed locally for this head** — no scene test, no unit test, no
  cpput, no simulation, no device run, and no pyut or pytest. An earlier revision of this branch ran the cpput suite
  locally (359/359; the author's own result, never a manager run). That **violated the standing
  no-local-tests constraint**; CANN-free, host-only, short, or new cases do not authorize overriding
  it, and a skill guideline does not outrank it. It is recorded, not repeated, and is not
  authorization for anything later.
- No CI POST, no retry, no merge, and no external comment, reply or thread resolution at any point.
  The eight remote review threads are unchanged and unanswered; the one review body and one
  conversation comment are the same bot items, with no new finding.
- Compile/lint evidence only: eight runtime builds (4 runtimes × onboard/sim) with 0 errors and 0
  warnings; the cpput tree configured, compiled and **linked** without being run; clang-format
  `--Werror`; clang-tidy over the sim compile databases; ruff check and format; markdownlint; and
  the repo lint set. `check_english_only.py` fails on 10 vendored
  `examples/**/kernels/vendor/` headers that are **pre-existing on `upstream/main`** and that this
  PR does not touch.
- `[src]` citations below were read at the historical baseline commit. Line numbers in §2–§15 are
  **not** re-verified against `b2a7feb41`.

### 0.6 The two `b2a7` cancellations: budgets, not failures

Both are GitHub job-timeout annotations. Neither is a hung tool, and neither
job's own clock was spent queueing for a runner (`created_at` 06:59:09 →
`started_at` 06:59:19 for the macOS job: 10 s).

**macOS `ut` job `116853656998`** — annotation: *"The job has exceeded the
maximum execution time of 20m0s"*. `_ut-no-hardware.yml` sets
`timeout-minutes: 20`. Step timings from the saved job record, against the
Ubuntu job in the same workflow (`116853656951`, **success in 15m02**):

| step | Ubuntu | macOS |
| ---- | ------ | ----- |
| set-up + GoogleTest + pip cache | 0m20 | 0m33 |
| Install dependencies | 2m55 | 3m49 |
| Python unit tests | 1m39 | 3m23 |
| cpput cmake configure | ~0m00 | 0m03 |
| **cpput build (`--parallel 4`)** | **7m52** | **11m00** |
| ctest | 1m04 (359) | 1m16 (358) |
| **axis + stub-linkage + export-parse roundtrip** | **1m09** | **never reached** |
| total | **15m02** | 20m04 → killed at 20m00 |

CTest finished at 07:19:23.7 and the cancellation landed at 07:19:33.9 — **10 s
after the last test, with zero margin for the three post-CTest checks.** Those
are `check_ut_cpp_axis.py --jobs 4`, `check_ut_cpp_stub_linkage.py` and
`tests/ut/py/test_swimlane_export_run_identity.py`; on Ubuntu they cost 1m09
together. **They have no completion evidence on macOS and the job must not be
read as passed.** The 20-minute overrun is also **not new**: the same annotation
appears at the `710` head, before this PR's newest unit case existed.

What the build is actually spending: **1546 compile actions over 279 distinct
sources — 1267 (81%) are recompilations of a source already compiled in the same
build.** 31 sources account for 996 of them (`profiling_copy.cpp` 112×,
`device_run_result_base_aicpu.cpp` 91×, `chip_swimlane_collector.cpp` 58×,
`runtime.cpp` 57×, `args_dump_collector.cpp` 48×). The cause is structural: each
`platform_case` / `hbg_case` / `tmr_case` target re-lists the same shared
platform/collector closure, so a 4-case × 2-arch × 2-runtime family compiles that
closure 16 times. This PR's own `test_args_dump_capability_refusal` contributes
**80 of the 1546 actions** across its four targets — ~5%, on the order of 30 s of
the macOS build. A real increment, not the cause.

**Bounded repair recommendation, in order, every check preserved:**

1. **Stop the duplication, don't raise the cap.** Give each (arch, runtime) its
   shared closure as a CMake `OBJECT` library that the case targets link instead
   of re-listing. Every target, test and check stays identical; only repeated
   compilation of the same translation unit goes away. **Carries one obligation
   that must not be skipped:** `check_ut_cpp_axis.py` proves each case's
   arch/runtime coverage by reading `compile_commands.json`, and an object
   library changes what appears there — so it must learn to resolve a target's
   axis through its linked object libraries in the same change, or that check
   silently weakens.
2. `--parallel 4` is hard-coded for every runner; letting each use its own core
   count is safe but bounded, and may be neutral on a 3-vCPU macOS box.
3. A ccache keyed on compiler + source hash helps repeat runs on an unchanged
   tree, which is not the shape of this PR's pushes.
4. **Raising `timeout-minutes` is the wrong first move.** The job was doing real
   work with no queue wait; a raise would hide an 81%-redundant build. If a
   stop-gap is wanted anyway, the honest figure is ~22–24 min, because the job
   needed ≈21.5.

**A5 `ut-a5` job `116853656885` — already resolved at the current head; no
change recommended.** Annotation: *"maximum execution time of 30m0s"*
(`_ut-npu-a5.yml` sets `timeout-minutes: 30`). The hardware Python sweep passed
(40 selected, 3365 deselected) and exited 0 at 07:19:24; the `cann-examples`
tail then submitted `task_20261016_151924_28897121107` and sat at
*waiting-for-execution* until the job limit killed it at 07:29:58. There is **no
lock-acquisition, build or smoke output for that tail**, so it is neither a
passing smoke run nor an observed tool hang — it is a shared-device-queue wait.
**At `da7460e15` the same job succeeded in 3m48 with the complete tail**
(`116872397324`, manager-verified), so the queue was simply free. One latent
mismatch is worth recording without acting on it: both `task-submit` calls in
that job use `--timeout 1800`, a 30-minute *queue-wait* budget equal to the whole
job budget, so a tail submitted ~10 minutes before the job's deadline can only
ever surface as an uninformative job timeout rather than as "the queue was
busy". Fixing that would be a diagnostic improvement, not a timeout increase,
and it is not needed while the job passes.

**Baseline (historical)** main `accab1cc54adff486c82f8367976ae8343c09ff0`, re-fetched when §1–§15
were written; 33 open PRs re-enumerated, unchanged set. #2566 / #2567 / #2568 are external authors, no
takeover. #2563/#2564 merged, #2565 paused.

`[src]` = read at that commit. **§1–§15 claim no execution evidence**; what execution evidence exists
is §0.3's and §0.4's, with their boundaries.

---

## Historical design baseline (§1–§15, as written)

**Design only, as of writing.** No product edit, build, test, hardware run, CI action, commit or
publication — see §0.1, which supersedes this. P4 (`PID1403923`) is the sole implementation writer —
dynamic resources and mailbox WIP — and none of its files are touched here. Its normative §3 and §12
are preserved as written. Approval is not requested. *(P4's admission work is since approved and in
implementation; §0.1.)*

---

## 1. Scope

Full capability: **four onboard backends × six output owners**, with each remaining restriction proven
and its user impact stated (§8). *(Superseded per backend: a5 HBG carries five, not six — §0.2.)* One
eventual complete PR. Out: kernel mode (`kKernelExecution`),
capture, output discovery. Simulation has no second launched run
(`sim/host/c_api_shared.cpp:1233`, `:1238`) — inapplicable; §9.4 states what it still pays.

---

## 2. Source facts this design rests on

| Key | Fact | Location |
| --- | ---- | -------- |
| S1 | per-run DFX settings are AICPU-SO **process globals**, pushed at kernel entry from that run's `KernelArgs`; the run epoch is one atomic for the whole SO | `a2a3/platform/onboard/aicpu/kernel.cpp:76-131`; `shared/aicpu/device_run_result_base_aicpu.cpp:39-40` |
| S2 | joined ordering is a **queued device wait**; a run's producers start *"after the previous run's producers are gone with their op"* | `a2a3/runtime/host_build_graph/host/runtime_maker.cpp:153-158`; `shared/aicpu/chip_swimlane_collector_aicpu.cpp:384-390` |
| S3 | the host close chain runs **at the fence**: `wait_run_fence` → `teardown_shared_collectors_after_run` → each `run_close` | `a2a3/platform/onboard/host/device_runner.cpp:934`, `:973`; `onboard/host/device_runner_base.cpp:4101-4151` |
| S4 | **`ChipRun::wait_until` / `done()` call `progress()` holding `lane_->mu`**; `progress` runs `launch_ready_prefix()` **before** `poll_native_run`/`finish`, then again after; `block_on_front()` blocks on the device **under the same mutex** | `worker/chip_run_lane.cpp:521-553`, `:602-616`, `:626-659` |
| S5 | the serial route reaches the lane **with the GIL held** (`run_materialized` has no `call_guard`); the staged route releases it | `python/bindings/task_interface.cpp` `run_materialized` / `_submit_chip_run_materialized` |
| S6 | **the host is the free-queue producer**: `try_push_to_free_queue` writes `buffer_ptrs[slot]` then `tail`, both narrow, slot before tail. **AICPU is the consumer and never the producer** | `include/host/profiler_base.h:913-943`; `shared/aicpu/dep_gen_collector_aicpu.cpp:172-203` |
| S7 | a held buffer is returned by the **producer reusing it in place**, re-stamped — *"Popping a replacement would strand it"* | `shared/aicpu/dep_gen_collector_aicpu.cpp:172-203` |
| S8 | ScopeStats producer flush: `count == 0` ⇒ returns with the pointer **still set**; failed enqueue ⇒ counts loss, zeroes count, **clears** the pointer; success ⇒ clears it | `shared/aicpu/scope_stats_collector_aicpu.cpp:279-304` |
| S9 | ScopeStats' **host** re-stamps a producer-held buffer | `shared/host/scope_stats_collector.cpp:196-253` |
| S10 | PMU zeroes per-core counters from the host at admission; a2a3/a5 layouts differ; `run_close` assumes the successor has not launched; `snapshot_run_records` sums across cores with no run filter and reads `current_buf_ptr` | `a2a3/platform/shared/host/pmu_collector.cpp:320-366`, `:744-756`; `include/host/pmu_runs.h:770-780` |
| S11 | ArgsDump takes admission totals and subtracts at close; same successor assumption; reads `current_buf_ptr` | `shared/host/args_dump_retained_runs.cpp:386-392`, `:645-679`, `:838-846` |
| S12 | **the arena ACK is the ownership edge**: unproved payloads are *"never counted as received or discarded, so `publish_arena_acks` will not acknowledge them and the producer's own barrier keeps the arena from being reused under them"*; cursors are per lane and **lifetime-monotonic** | `shared/host/args_dump_retained_runs.cpp:876-896`; `shared/host/args_dump_collector.cpp:792-815` |
| S13 | the terminal entry is **counters only** — `run_epoch`, `total`, `dropped`, `published_records`, `published_buffers`, `live_at_close`; epoch match is the whole validity test; one cache line per entry | `include/common/chip_swimlane_profiling.h:360-420` |
| S14 | the bank reaches the device **as an address** written into `KernelArgs::chip_swimlane_run_terminal_bank`; today the index is the pipeline slot | `include/common/kernel_args.h:112`; `chip_swimlane_collector.cpp:1556-1586`; `onboard/host/device_runner_base.cpp:2899` |
| S15 | all five `CallConfig::enable_*` are `int32_t` where **the value is the mode** (`enable_pmu`: *"0 = disabled; >0 = enabled, value selects event type"*) | `common/task_interface/call_config.h:116-125` |
| S16 | the gate requires equal `enable_chip_swimlane` and **zero** on the other four, on both sides | `worker/chip_run_lane.cpp:408-416` |
| S17 | swimlane admission **blocks** on a full table (`retained_cv_.wait`, no timeout); PMU/ArgsDump/DepGen **refuse** | `chip_swimlane_collector.cpp:3398-3422`; `pmu_runs.h:715-722`; `args_dump_retained_runs.cpp:344-351`; `dep_gen_retained_runs.cpp:153-160` |
| S18 | `simpler_flush_diagnostics_ctx` computes a **local** timeout at call time (`timeout_ms > 0 ? timeout_ms : 30000`); it is **not** a stored run budget | `onboard/host/c_api_shared.cpp:2428-2440` |
| S19 | `ensure_device_orch_pool` returns 0 when already seeded (`tail != 0`) **before** the retaining guard; `initialize()` builds the pool at level ≥ `ORCH_PHASES` | `chip_swimlane_collector.cpp:528-556`, `:464` |
| S20 | level ≥ 3 phase pools are re-primed per run **by the producer** | `shared/aicpu/chip_swimlane_collector_aicpu.cpp:1073-1115` |
| S21 | both TMR arches re-handshake every AICore per launch | `a2a3/runtime/tensormap_and_ringbuffer/runtime/scheduler/scheduler_cold_path.cpp:777-790`, `:910-935`; a5 sibling |
| S22 | DepGen's receive path is single-epoch (`retained_epoch_`/`retained_epoch_open_`) while `slots_[kMaxOpenEpochs]` holds two export slots; `quarantine_held_` refuses outright | `shared/host/dep_gen_retained_runs.cpp:139-146`, `:176-180` |
| S23 | PMU has **no host byte budget**; `retains_runs()` ORs 3 of 6 owners while 6 are armed elsewhere | `include/host/pmu_runs.h:640`; `onboard/host/device_runner_base.h:377-390` |
| S24 | policy constants live together and are documented as *"a minimum viable default, not a tuned value"* — `kCutAckBudgetMs` = `kControlAckBudgetMs` = 2000 | `include/host/chip_swimlane_runs.h:33-48` |

**Normative (P4 §12, preserved)**: banks are collector-owned, keyed by `run_epoch`, independent of the
run-resource index, mapped **by address** through the existing `KernelArgs` field;
`PLATFORM_RUN_TERMINAL_BANKS = 3` stays; **release → reuse safe only when
`report_run_terminal_snapshot` returns**; a fourth diagnostic run **declines for this round**, the
progress source being a retirement; P4 widens no diagnostic semantics itself.

---

## 3. The ordering truth

S1 + S2: device producers never co-execute, so a buffer's `run_epoch` stamp is sound. S3: the host
close runs at the fence. Therefore:

> **A successor's device init is ordered after the predecessor's device op, and is *not* ordered after
> the predecessor's host close.**

Nothing may depend on the host having recovered anything before a successor's producer init runs.

---

## 4. Per-run evidence: counters only

The terminal entry (S13) is 64 bytes of counters written by the producer at its own last flush. It is
**not** a record store and **not** a payload. Publication order, per producer:

1. final enqueue attempt of the active buffer (today's path, S8);
2. loss accounting for whatever the enqueue could not place;
3. write `total`, `dropped`, `published_records`, `published_buffers`, `live_at_close`;
4. `wmb()`; 5. write `run_epoch`; 6. `cache_flush_range` over the whole line.

Step 1 before 3 makes `published_*` final; 5 after 3 makes a readable epoch imply readable counters; 6
flushes whole lines, hence one entry per line. The host copies the snapshot into collector storage
(`read_run_terminal_snapshot`, `chip_swimlane_collector.cpp:1589-1600`) **before** the bank can be
reused, and the bank is released only at `report_run_terminal_snapshot` (§7).

**Saturation**: `saturating_add` reserves `UINT32_MAX` as "at or past the countable limit"
(`scope_stats_collector_aicpu.cpp:98-105`); a field carrying it is reported **counts-unknown** for that
producer — never clamped, never balanced.

Coverage: swimlane (AICPU task, AICore task, sched phase, orch phase), PMU (per core), ArgsDump (per
dump lane, **metadata records only** — payload is §5.3), ScopeStats (instance 0), DepGen (instance 0),
HBG host graph (none — host-side, never reaches a device collector). Units are records and buffers,
`uint32_t`.

---

## 5. Transport ownership

### 5.1 The successful path — host drains, host recycles

S6 is the rule and it runs **host-side**: the device pops a buffer from the free queue, fills it, and
enqueues it on the ready queue; the host's drain reads the ready queue and copies the buffer out; the
host then **returns it by publishing it back onto the free queue** — `try_push_to_free_queue` writes
`buffer_ptrs[slot]`, then `tail`, each as a narrow write, slot before tail so the device never sees a
tail without its pointer. **The host is the free-queue producer; the AICPU is its consumer and never
its producer.** Nothing in this design changes that path.

### 5.2 The unsuccessful paths — producer-held reuse

A buffer never enqueued was never handed over, so the host cannot return it. S7: the owner is the
producer, and the return is **reuse in place at the next run's producer init**, re-stamped.

| State at a run's end | Owner after | Settled by | Loss |
| -------------------- | ----------- | ---------- | ---- |
| enqueued | host, via the ready queue | §5.1 drain + recycle, then §5.3 ACK for payload | none — in `published_*` |
| non-empty, enqueue failed | **producer**, pointer left set | next run's producer init, reuse in place | `dropped += count` at the failure; records are gone and **marked**, never reconstructed |
| empty (`count == 0`) | **producer**, pointer left set | next run's producer init, reuse in place | none |

Two device-side changes, both in the ScopeStats producer:

- **stop clearing the pointer on a failed enqueue** (S8) — clearing it strands the buffer, since §5.1's
  return needs an enqueue that did not happen;
- **reuse in place at producer init** (S7's sequence: `count = 0`, `run_epoch`, `local_seq = 0`,
  `wmb()`, `current_buf_seq = 0`; pop only if the pointer is zero). The host's `adopt_current_buffer`
  (S9) is **deleted**.

All host recovery of a buffer a successor may reuse is removed: PMU stops reading `current_buf_ptr`
(S10), ArgsDump stops deciding leftovers from the live pointer (S11). What those reads were for is
`live_at_close` (S13). An empty or failed buffer is **never** a source of host recovery.

Exactly-once holds because `current_buf_ptr` is device-written, the producer is single per instance,
and the reuse runs before this run's handshake releases its producer threads (S2).

### 5.3 Payload is settled by ACK, on a different clock

Only ArgsDump has a payload arena. S12 is its existing, correct mechanism and is **untouched**: an
unacknowledged payload keeps the producer's arena barrier closed.

| Cursor | Lifetime | Reset per run? | Drives |
| ------ | -------- | -------------- | ------ |
| `published_payload_count` (device); `received_` / `written_` / `discarded_payload_counts_` (host), per lane | life of the collector, monotonic | **never** | arena reuse — the ACK |
| terminal `total` / `dropped` / `published_*` | per run, device-published | written fresh each run | loss accounting and completeness |

Conflating them would release an arena nobody had read. ArgsDump keeps `publish_arena_acks` and its
"nothing unproved is read" branch verbatim, and drops only `admit_published`/`admit_dropped` and the
delta subtraction (S11).

### 5.4 Per-run counter initialization

At each run's producer init, after the predecessor's terminal commit, **the producer zeroes its own
pool-head record counters** — what the swimlane already does, for the reason written in place: *"The
host cannot do this once a successor may arm while its predecessor is still producing… Here it is
ordered by the device itself"* (`chip_swimlane_collector_aicpu.cpp:380-400`). Scope: `total_record_count`,
`dropped_record_count` and their per-class equivalents **only** — the §5.3 payload cursors are not
pool-head counters and are not reset.

Host-side counter resets at admission are removed from PMU, ScopeStats and DepGen.
`publish_run_config` keeps only its narrow mode field, which is idempotent under §6's predicate.

---

## 6. Admission: the predicate, and non-blocking in the lane

### 6.1 The equality predicate, stated exactly

All five `CallConfig::enable_*` are `int32_t` in which **the value is the mode** (S15). So one rule
covers enabled set and mode together:

> **A joined pair requires `enable_chip_swimlane`, `enable_dump_args`, `enable_pmu`, `enable_dep_gen`
> and `enable_scope_stats` to be pairwise equal.**

Equal values ⇒ equal enabled set, equal swimlane level, equal dump level, equal PMU event type. This
replaces S16's "equal swimlane, zero on the other four". A pair that differs in any one value — a
different level, a different event type, one channel on and the other off — **declines the join**; both
runs then launch ordinarily, in queue order, each with its own complete diagnostic. That is the
mixed-mode fallback, and it is the only one.

The mode field written at admission (`chip_swimlane_level`, `dump_args_level`, PMU `event_type`) is
safe precisely because the predicate makes the write idempotent for the pair. `collector_shape_is_stale`
stays, extended to every enabled owner.

### 6.2 Reservation tokens

```text
reserve(enabled_set, run_epoch):
    tokens = []
    for owner in enabled_set in FIXED_ORDER:   # swimlane, dump, pmu, scope, depgen
        t = owner.try_reserve(run_epoch)       # takes ONLY owner's own lock, briefly, non-blocking
        if t.fatal: rollback(tokens); return Declined{fatal, owner}
        if t.full:  rollback(tokens); return Declined{full, owner}
        tokens.push(t)
    return Granted{tokens}
rollback(tokens): for t in reverse(tokens): t.owner.return_reservation(t)
```

**Lock order: one collector lock at a time, never two, and never the credit mutex (§7.2) while holding
a collector lock.** There is no lock pair to order. A token names
`(owner, run_epoch, bucket_slot, bank_index)`, is owned by the attempt, and is **consumed once** by
that owner's `run_begin` or **returned once** by rollback — the attempt's scope guard returns anything
still held, including on an exception, so no reservation leaks after a fallback. `try_reserve` is the
only mutating call and always yields a token the caller must dispose of; there is no mutating
capability query.

With a granted reservation `run_begin` neither waits nor refuses for capacity, which removes S17's
divergence from the admission path.

### 6.3 Lane admission: no unbounded wait, no capacity wait — proven, not asserted

S4 is the call graph, and it forecloses a front wait inside the lane for three independent reasons:

1. `progress()` runs **under `lane_->mu`**, which `done()` and every other waiter also need. Blocking
   there blocks all of them.
2. `launch_ready_prefix()` — where admission happens — runs **before** `poll_native_run`/`finish` in
   the same `progress()` call, on the same thread. That thread is therefore a candidate for retiring
   runs, and blocking it inside `launch_ready_prefix` can stop the very retirement another run needs.
3. On the serial route the lane mutex is taken **with the GIL held** (S5). Blocking there blocks the
   interpreter.

**Therefore lane admission takes no unbounded wait and no capacity wait**, for the front launch as much
as for a join. `launch_front()` and `launch_ready_prefix()` attempt `reserve()`; on `full` the run stays
`PREPARED` and `progress()` returns false; on `fatal` the run fails with the owner named.

**Scope of "non-blocking", stated honestly.** A capacity reservation removes the *capacity* wait, not
every wait inside `run_begin`. Two bounded costs remain in the admission path and are **retained and
documented**, not promised away:

| Remaining cost | Bound | Where |
| -------------- | ----- | ----- |
| shard-table acknowledgement — *"admitting a run waits for every shard to acknowledge the new run table"* | **explicit protocol wait**: `kControlAckBudgetMs` = 2000 ms per enabled owner; a timeout sets that owner fatal | `device_runner_base.cpp:3793-3796`; `chip_swimlane_collector.cpp` / `pmu_runs.h:759` |
| first-run collector thread start (`collector.start(thread_factory)`) | one-off per owner, first admitted run only — **no wall-clock bound is claimed** | `device_runner_base.cpp:3792` |

So **every explicit protocol wait in admission carries a stated budget**, and `progress()` / `done()` are
not wait-free. The bound applies to the protocol wait only: thread creation is a scheduler event, and
this design makes **no scheduler guarantee** about how long `start(thread_factory)` takes — it is one-off
per owner and already the behaviour of every collecting run today, which is the whole of the claim.
Taking the ACK out of the lane would need a new two-phase admission state machine; this design does not
propose one, because its budget is explicit and charged once per admission.

This still **removes a pre-existing defect**: today the swimlane's `run_begin` blocks on
`retained_cv_.wait` with **no timeout** (S17) inside that same call path, i.e. it already blocks
`lane_->mu` and, on the serial route, the GIL. The reservation makes that unreachable, leaving only the
2000 ms bound above.

**No completion thread is invented.** There is none; the retirement work is done by whichever caller
runs `progress()`.

### 6.4 Credit is owed by publishers, not by native runs

Both predecessors can be natively retired while their **file writers** still hold both buckets. That is
a healthy, finite case and must not be rejected. So:

- credit ownership is tracked **per owner, per credit**, independent of native run lifetime — a bucket
  is held by its writer until publication, a bank by the collector until
  `report_run_terminal_snapshot` returns;
- there is **no** "a launched-or-closing run must owe a release" assertion. It would reject exactly
  this case;
- the waiter's predicate is a **generation change**, and the only thing that bounds it is the deadline
  of §7.3.

---

## 7. Waiting for credit: where, how, and for how long

### 7.1 Where the wait happens

Not in `progress()`, and not under `lane_->mu`. `wait_until` already releases the lane mutex between
iterations (S4), and that gap is the only correct place:

```text
bool ChipRun::wait_until(Deadline deadline):
    while true:
        credit_wait = {}
        { lock(lane_->mu)
          if lane_->progress(run_): rethrow; return true
          if unbounded && lane_->block_on_front(): continue
          credit_wait = lane_->credit_block_of(run_)     # {generation, owner} or empty
        }                                                # lane mutex released here
        if credit_wait: wait_for_credit(credit_wait, deadline)   # no lane mutex, no collector lock
        if Clock::now() >= deadline: return false
```

`credit_block_of` is a plain read of what the last `reserve()` attempt recorded on the lane. The credit
wait holds **no lane mutex, no collector lock and no token**; it does cross the C boundary, because the
signal lives in the runtime module and the waiter does not — §15 is that bridge. A bounded waiter keeps
its existing deadline; an unbounded one now has something to block on instead of the busy loop it falls
into today when the front is `PREPARED` rather than `LAUNCHED` (S4's `block_on_front` returns false in
that case).

**This path is `wait_until` only.** The local L3 chip child does not use it — §15.2.

### 7.2 The signal and its wake sources

One `std::mutex`, one `std::condition_variable` and a monotonic `uint64_t credit_generation`, owned by
`DeviceRunnerBase`. Every owner bumps the generation and notifies on **any** of:

| Wake | Raised by |
| ---- | --------- |
| output bucket released | that collector's **writer**, after publication |
| terminal bank released | `report_run_terminal_snapshot` returning |
| owner became fatal | whichever thread set it |
| cancellation | a **counted** cancellation call (§15.3) — from the teardown owner, from `stop_admission`, or from a poisoning thread under its own existing lease |
| shutdown | runner teardown |

A release path takes the credit mutex **after** releasing its own collector lock, so the notify never
runs under a collector lock — consistent with §6.2's order.

**No lost wakeup.** The generation is read under the credit mutex *before* the reservation is attempted
(the attempt takes only collector locks, §6.2), and the waiter sleeps only if the generation is still
what it read. A release in the window bumps it, the predicate fails, and the waiter rechecks.

**Lifetime, concretely.** Broadcast alone does not prove a waiter exited — and a waiter count kept
*inside the runtime* does not prove it either, because it cannot see a caller that has not yet entered
and cannot see one that has already decremented but not yet returned through the runtime's epilogue. The
barrier is therefore **owned by the extension**, not by the runner: an **active-call gate** in
`ChipRunLaneState` with three states (`Open` / `Closing` / `Detached`), entered before any `dlsym` target
is read and left only after control is back in extension code (§15.3). Every call is counted, **including
cancellation**; exactly one thread claims teardown and the rest join it; the targets are cleared at
`Detached`, before anything is destroyed. `cancel` itself is **non-blocking** and only makes every
waiter's predicate true; the *drain* belongs to the teardown owner and runs with no lane lock, no
collector lock and no gate lock held. `finish_retained_runs()` then joins every collector writer; only
then are the collectors and the signal destroyed, and only then may the extension `dlclose` the runtime.
No callback is registered; the signal is a plain member of the runner, reached across the ABI by value
(§15.1), never as a subscription.

### 7.3 The deadline — honest

**There is no existing per-run diagnostic deadline.** `simpler_flush_diagnostics_ctx` computes a
*local* argument at call time (S18); it is not stored, not shared, and has no remaining share to take.
Claiming otherwise would be false.

**Proposed policy**, with no new user knob: one policy constant beside the existing acknowledgement
budgets in `chip_swimlane_runs.h` (S24), which that file already describes as minimum viable defaults
rather than tuned values.

| Field | Value |
| ----- | ----- |
| constant and **value** | `kCreditWaitBudgetMs = 30000` — an explicit new policy figure, chosen to match the only comparable existing default (`simpler_flush_diagnostics_ctx`'s 30000 ms fallback) while being a **separate** constant, because S18 proves that one is not a run deadline |
| start point | the first `full` decline for **this run at the front**; a joined decline starts nothing |
| storage | on that run's `ChipRunState`, lane-local |
| propagation | none — not a user value, not in `CallConfig`, not in `KernelArgs`. The *budget* never crosses the ABI; only §15.1's `timeout_ms` argument does, per wait |
| on expiry | the run **fails**, naming the owner and the elapsed time |
| enforced in | **`progress()`**, so it applies equally to `done()` and to `wait_until` (§15.2) |

**Two timeouts, different meanings — never conflated:**

| Field | caller wait timeout | admission expiry |
| ----- | ------------------- | ---------------- |
| source | `wait_until(deadline)`, the caller's own | `kCreditWaitBudgetMs` from first front-full |
| on expiry | **returns false; the run stays live** and may be waited on again | **the run fails**, owner named |
| applies to | the waiting thread | the run |
| already-launched runs | unaffected — each keeps its own outcome and its own drain | unaffected, for the same reason |

A caller deadline sooner than the budget ends the *wait*, not the run. The budget sooner than the
caller deadline ends the *run*, and the caller's next `progress()` observes the failure.

**This is changed behaviour and belongs in the decision (§10):** a run whose diagnostic credit does not
return inside that budget fails, where today it would either block forever (swimlane, S17) or fail
immediately (the other three, S17). It is bounded in both directions, which neither of today's
behaviours is.

---

## 8. Capability

> **Superseded by §0.2 for one cell.** The ArgsDump row below reads `✓` for a5 HBG; that runtime has
> no task-boundary producer and now refuses the request. Every other cell holds. The table is kept as
> written because the per-owner reasoning under it is what the implementation followed.

| Owner | a2a3 HBG | a5 HBG | a2a3 TMR | a5 TMR | sim |
| ----- | -------- | ------ | -------- | ------ | --- |
| swimlane L1–L2 | ✓ today | ✓ | ✓ | ✓ | inapplicable |
| swimlane L3 | ✓ | ✓ | ✓ | ✓ | inapplicable |
| swimlane L4 | ✓ **when initialized at L4** | ✓ same | ✓ same | ✓ same | inapplicable |
| ArgsDump | ✓ | ✓ *(superseded: ✗, §0.2)* | ✓ | ✓ | inapplicable |
| PMU | ✓ | ✓ (second host implementation, S10) | ✓ | ✓ | inapplicable |
| ScopeStats | ✓ | ✓ | ✓ | ✓ | inapplicable |
| DepGen | n/a (TMR channel) | n/a | ✓ after multi-epoch receive | ✓ same | inapplicable |
| HBG host graph | ✓ host-side | ✓ | n/a | n/a | n/a |

**Level 4** is supported when the collector was initialized at level 4: S19 shows
`ensure_device_orch_pool` returns 0 on an already-seeded pool *before* the retaining guard, and
`initialize()` builds it at that level. The only refusal is the low-level→L4 **upgrade** on a retaining
collector, which §6.1's equality predicate makes unreachable for a pair anyway. The user-visible rule is
the one the existing message states: profile at that level from the Worker's first collected run.
**Level 3**'s pools are built by `initialize()` and re-primed per run by the producer (S20) —
device-ordered, hence safe under S2 — and their terminal bases already exist (S13).

**DepGen**: S22's single receive epoch becomes an epoch→slot map under `records_mutex_`, the shape
`ShardEpochView` already has in the swimlane and ArgsDump, making its two export slots reachable.
`quarantine_held_` stays a **fatal** decline. Overlapping its graph write and host close with the next
serial operator is the benefit; no "no benefit" rationale is offered.

**TMR** serializes producers by re-handshaking every AICore per launch (S21).

---

## 9. Credits, capacity and default behaviour

### 9.1 Independent credits, by address

Banks stay collector-owned, epoch-keyed, 3, address-plumbed (normative §12). Each added owner gets its
**own** bank array, its own `bank_epoch[]`, and its own `uint64_t` selector in `KernelArgs`
(`pmu_run_terminal_bank`, `dump_run_terminal_bank`, `scope_stats_run_terminal_bank`,
`dep_gen_run_terminal_bank`), each with moved offset assertions and the existing sim `set_platform_*`
hook. No shared pool, so one owner's slow report cannot starve another. **No run-resource index is ever
a selector and no macro is raised.** Each owner's bank and bucket are reserved together (§6.2) and
released at different points — bank at report, bucket at publication — which is the whole of §7.

### 9.2 Capacity, stated accurately

Depth 8, swimlane (3 buckets granted) + PMU (2 buckets), banks 3 each:

- **eight run-resource sets** can be in flight — that is a *storage* figure;
- **one** operator executes on the device at a time (S2);
- **two** runs can be *admitted with diagnostics at once*, because `min(3,2)` over the enabled set is 2
  — §6.1 requires the whole enabled set, so PMU's credit bounds the pair.

So: eight storage sets, **two admitted diagnostic runs**, one device operator. Runs 3–8 execute
ordinarily and take diagnostic credit at the front as it returns. **No macro changes and the two
capacities stay independent.**

### 9.3 Default and non-retained mode — not unchanged

The §4 terminal write and the §5.4 producer-side counter reset are **in the producer** and therefore
run on **every** launch, including `collect_across_runs = false`. The earlier claim that nothing in this
design is reachable by default is **retracted**. What is preserved, explicitly:

| Default-path behaviour | Status |
| ---------------------- | ------ |
| `begin_run` remains the admission path; its host-side mode-field write stays | unchanged |
| PMU / ScopeStats / DepGen host counter resets on the **default** path | **removed** — the producer now zeroes at init (§5.4), so the default path reconciles against producer-zeroed counters instead of host-zeroed ones. Same value, different writer, device-ordered |
| ScopeStats `adopt_current_buffer` | **removed on both paths**; the producer reuses in place |
| each owner's verdicts and artifacts | unchanged |
| terminal entries | written on every run; read only where a collector asks for them |

The default path is therefore **behaviour-preserving but not code-unchanged**, and its regression
coverage is part of the acceptance set (§11.12).

### 9.4 Simulation and budgets

Sim's producers write their terminal entries and its hosts never read them: one store per producer per
run. PMU has **no host byte budget** (S23); terminal entries are device memory and outside every host
budget — no uniform 256 MiB allowance is claimed. `retains_runs()` is fixed to all six owners (S23),
required because §6.2 asks all six.

---

## 10. Incomplete output, per owner — preserved

No uniform rule; the design adds a cause, not a policy.

| Owner | Policy, unchanged |
| ----- | ----------------- |
| swimlane | `Published`/`PartialSafe`/`CutUnknown` publish a file (`CutUnknown` with `processing_complete = false`); `WriteFailed`/`Quarantined`/`CounterExhausted` publish none. An unreadable terminal maps to the existing Unknown verdict |
| ArgsDump | an unproved close publishes an incomplete result and fails the flush; nothing unproved is read or acknowledged (S12) |
| PMU | proved-empty writes **no CSV**; a frozen-config mismatch refuses the run at admission |
| **ScopeStats** | **complete-only** — no partial artifact. `live_at_close != 0` or a saturated counter ⇒ counts-unknown, nothing published |
| **DepGen** | **complete-only** — no graph is fabricated from missing records |
| HBG host graph | sealed in `prepare`, unaffected |

**There is loss, and it is never silent.** A failed enqueue charges `dropped` (§5.2) and the run is
reported incomplete by its own owner's policy. The claim is **no silent loss and no silent level
downgrade** — not "nothing is dropped".

---

## 11. Acceptance — source-only, nothing executed

> **As written, and still true of this section.** What execution evidence exists for the shipped head
> is §0.3's, with its boundaries — a local cpput run that should not have been taken, and CI in
> progress. Nothing below was executed here.

1. Per-run attribution, depth 3, swimlane + PMU, N > credits: each run's `total − dropped − published`
   balances from **its own** terminal entry.
2. **Contamination — controlled protocol fixture, labelled as such.** Today's gate refuses a joined
   PMU/ArgsDump pair (S16), so this is **not** a public repro and is not claimed as one.
3. Reservation rollback: a later owner declines; no owner admitted, no device header written, no token
   retained, run launches ordinarily with a complete artifact.
4. **Both predecessors retired, both writers holding buckets**: the front run waits, is woken by a
   writer release, and publishes. The healthy slow-writer case must not be rejected.
5. Front woken by a **retirement**: all banks assigned, release comes from
   `report_run_terminal_snapshot`, not a writer.
6. `kCreditWaitBudgetMs` expiry: the run fails naming the owner and the elapsed time; no artifact
   claims completeness.
7. Fatal, cancellation and shutdown each wake every waiter promptly. `cancel` **returns without
   waiting** (so a thread inside a counted call cannot block on its own lease); the teardown owner
   claims `Closing`, issues its **counted** cancellation, drains `calls_in_flight` — ordinary calls and
   cancellations alike — to zero, and only then detaches the targets, before `finish_retained_runs`
   joins the writers, before the context is destroyed and before `dlclose`. A waiter parked in
   `simpler_wait_diagnostic_credit_ctx` across a concurrent `close()` returns into extension code before
   the unload; a caller arriving after the claim is refused and takes a local result without reading a
   target.
7a. **Concurrent close** (two threads through `_close_chip_run_lane`, or one of those against another
   thread's `finalize`): exactly one owner, exactly one cancellation, exactly one drain and one detach;
   the follower blocks until `Detached` and then returns the same outcome. The owner paused between its
   claim and its cancellation is counted throughout, so the follower can never observe zero and proceed
   to destruction.
7b. **Repeated close** — main's own `close()`-then-`reset()`-destructor pair (`chip_worker.cpp:721-727`):
   the second close returns `Already` without reading `ctx` or any function pointer; a `ChipRun` that
   outlives the lane gets local results from all three entries; nothing reopens.
7c. **Rejected close**: `finalize_device_fn_` fails, `finalize` throws and keeps the context and runtime
   loaded; the gate is already `Detached`, the retried `finalize()` takes the `run_lane_ == nullptr`
   path, and no credit caller can reach the retained context. A later successful `init()` builds a new
   gate with freshly resolved pins.
7d. **Poison inside a counted call**: it cancels under its own existing lease, claims nothing and waits
   on neither CV; the later teardown owner's drain is what observes its return.
8. **Lane blocking is bounded where the protocol waits, and not elsewhere**: with every credit
   exhausted, a second thread's `done()` acquires `lane_->mu` and returns after at most the shard-ACK
   budget (§6.3) plus the one-off first-run thread start, for which no bound is asserted; never an
   unbounded credit wait. On the serial route the GIL is released across every wait (§15.2b).
8a. **L3 staged loop**: with credit exhausted, `_run_chip_main_loop` keeps publishing mailbox state,
   keeps its parent-liveness countdown and still honours shutdown, retrying admission on its own next
   pass — no credit wait is taken on that thread.
9. ScopeStats held buffer, empty **and** failed-enqueue: reused in place; free-queue depth after N runs
   equals its depth after one; the failed-enqueue loss is reported, not reconstructed.
10. ArgsDump ACK independence: unproved payloads leave the arena unacknowledged while the next run's
    terminal still balances its own records; `received_payload_counts_` monotonic across runs.
11. Complete-only owners with records missing: incomplete reported, **no artifact, no fabricated graph**.
12. **Default-path regression** (`collect_across_runs = false`), every owner, every backend: artifacts
    byte-equivalent to today's, with the producer-zeroed counters replacing the host-zeroed ones.
13. Level 4 retained when initialized at L4; the low-level→L4 upgrade still refused with the existing
    message.
14. DepGen: two runs admit on the multi-epoch receive path; `quarantine_held_` declines as fatal.
15. Depth 8 / unequal credits: §9.2's figures reproduced — eight storage sets, two admitted diagnostic
    runs, one device operator, no macro raised.
16. Predicate: any single `enable_*` inequality declines the join; both runs publish complete artifacts.
17. Close with an unwaited prefix: every admitted run reaches a terminal verdict; every reservation
    returned.

---

## 12. Integration

> **Superseded on the P4 rows by §0.1.** P4's automatic resource admission is approved and in
> implementation on `codex/auto-resource-admission`; the sequencing below is no longer an open
> agreement, and the predicate it names is already strong on all four backends in the shipped head.

| Field | Value |
| ----- | ----- |
| **P4** | sole implementation writer; no file of its is touched. Normative §3/§12 adopted verbatim and extended to four more owners with independent credits. Its dynamic arena and mailbox WIP are independent of §9.1 — §9.2 shows differing per-owner credits at depth 8 with no macro change |
| `simpler_set_retained_epoch_limit_ctx` gated on `joined_diagnostic_launch_supported_impl` | making that symbol strong on three more backends opens a capacity path P4 owns — **sequencing to be agreed, not assumed** *(resolved: shipped strong on all four; P4's admission work is approved and separately in progress)* |
| **#2567** `f4ed8068c` | external caller-stream owner. Changes where the completion event's wait reference is consumed; it can delay the fence, hence the close and both releases, but moves neither release point. §7.3's budget is what keeps a deferred fence from becoming an unbounded wait. No takeover |
| **#2566** `eed677b397` | external. Touches `device_runner_base.{h,cpp}` and both `c_api_shared.cpp` — real file overlap with §6/§7. Whoever lands second rebases. No takeover |
| **#2568**, #2493, #2453 | examples and kernel mode; no overlap |

---

## 13. Costs

Device memory `producers × 3 × 64 B` per added owner — PMU largest (`PLATFORM_MAX_CORES × 3 × 64 B`);
one store plus one line flush per producer per run, inside the op. Four `uint64_t` fields in
`KernelArgs` with moved offset assertions and four sim hooks. Two ScopeStats producer changes. PMU
implemented twice (a2a3 and a5 host layouts differ). DepGen's receive path made multi-epoch. One policy
constant (`kCreditWaitBudgetMs = 30000`). One mutex + CV + generation + `cancelled` flag on the runner,
and one active-call gate on `ChipRunLaneState` — mutex, two CVs, a three-valued state, an owner id, one
counter and four pinned pointers.
`retains_runs()` widened to six. Sim pays one producer store per run for entries it never reads.

**Newly required interfaces** (all proposed; none exists today):

| Interface | Shape | Why |
| --------- | ----- | --- |
| `simpler_diagnostic_credit_generation_ctx` | `uint64_t(DeviceContextHandle)` | §15.1 — read a snapshot without blocking |
| `simpler_wait_diagnostic_credit_ctx` | `int(DeviceContextHandle, uint64_t seen, int timeout_ms)` | §15.1 — the only blocking entry; no C++ type crosses |
| `simpler_cancel_diagnostic_credit_waits_ctx` | `void(DeviceContextHandle)` | §15.1 — **non-blocking** cancel; the drain is the extension gate's, not this entry's |
| `ChipWorker` dlsym slots + forwarding methods for the three | as every other runtime entry | the waiter lives in the extension, the signal in the runtime |
| `CreditCallGate` in `ChipRunLaneState` (Open/Closing/Detached) + the gate sequence at the top of `ChipRunLane::close()`, and one counted cancellation in `stop_admission()` | extension-only; no ABI surface | §15.3 — the only thing that makes `dlclose` provable |
| `run_materialized` releases the GIL | `nb::gil_scoped_release` after the POD copy | §15.2b — today it holds the GIL across `wait_until` |
| four `KernelArgs` bank selectors | `uint64_t` each | §9.1 |

---

## 14. Decision — standalone

Readable on its own; §§1–13 and 15 are the evidence behind each line.

**1 — Scope.** All **six output owners** (swimlane L1–L2, L3, L4; ArgsDump; PMU; ScopeStats; DepGen —
six owners across those levels) on all **four onboard backends** (a2a3 and a5 × `host_build_graph` and
`tensormap_and_ringbuffer`). Any combination of them can be ordered behind a still-running predecessor
with ordinary early-enqueued `Worker.submit` runs, up to the configured execution depth and each owner's
own diagnostic credit. Simulation is inapplicable, not unsupported: it has no device producers to
publish evidence. L4 requires the Worker to profile at that level from its first collected run; the
low-level→L4 upgrade stays refused with today's message.

**2 — Operating sequence**, per joined run:

1. the pair's five `enable_*` values are compared pairwise; any inequality declines the join and both
   runs launch ordinarily with complete diagnostics;
2. admission reserves one terminal-bank credit and one output-bucket credit **per enabled owner**, all
   or nothing, rolling back on the first decline;
3. a declined run stays `PREPARED` and is re-asked on the next pass — the lane never takes a capacity
   wait and never takes an unbounded wait;
4. device producers initialize their own per-run counters at kernel entry and publish one 64 B
   epoch-stamped terminal record after their final enqueue, with the epoch written last;
5. the host copies the terminal, releasing the bank; the file writer publishes, releasing the bucket;
6. either release wakes the front run, which re-attempts admission.

**3 — Capacity, stated exactly.** The existing `PLATFORM_RUN_TERMINAL_BANKS = 3` is unchanged and is not
a mirror of execution depth: at depth 8 the user gets eight storage sets, as many admitted diagnostic
runs as the scarcest enabled owner's credit allows (two, when PMU is enabled), and one device operator —
with no macro raised. Execution depth and retained diagnostic count are separate numbers and this design
never equates them.

**4 — The 30 s policy** (`kCreditWaitBudgetMs = 30000`, a new constant, no user knob). It starts at a
run's first credit decline **at the front** and, on expiry, **fails that run** naming the owner and the
elapsed time. It is enforced inside `progress()`, so it covers the L3 `done()` loop as well as
`wait_until`. It is deliberately **not** the same thing as a caller's own wait timeout, which returns
false and leaves the run live. Already-launched runs keep their own outcomes under both. Today the
swimlane blocks such a run forever and the other owners fail it instantly; this is bounded in both
directions.

**5 — Failures and close.** No silent loss and no silent level downgrade: a charged enqueue failure is
reported by its owner's own policy, and the complete-only owners (DepGen, ScopeStats) publish nothing
rather than something partial. Teardown is an explicit three-state gate owned by the extension
(§15.3): one thread claims it, concurrent closers join that one attempt, every call including
cancellation is counted through its return, the targets are detached before anything is destroyed, and
repeated close — which main already performs, `close()` then the `reset()` destructor's second
`close()` — returns a local result without reading a target. A rejected device finalize keeps today's
retained context and retry semantics untouched. This rests on the enclosing serialization the public API
already enforces (`finalize` holds the GIL for its whole binding; a live native tree must be closed on
its init-owner thread; one claimed teardown attempt that others join) — the gate covers the two
GIL-releasing lane entries, and claims nothing beyond them.

**6 — Changed behaviour the user will notice.**

1. A front run whose diagnostic credit does not return within the 30 s budget **fails**, naming the
   owner (item 4).
2. `run_materialized` releases the GIL for the duration of the submission. Today it holds it across an
   unbounded `wait_until`, so any Python thread sharing that interpreter is currently blocked for the
   whole run and will stop being.
3. The default (`collect_across_runs = false`) path keeps its artifacts but not its code: per-run
   counter zeroing moves from host to producer.

**7 — What it costs — four kinds, none avoidable within this functionality.**

| Kind | Item |
| ---- | ---- |
| **resource** | device memory `producers × 3 × 64 B` per added owner, PMU largest (`PLATFORM_MAX_CORES × 3 × 64 B`) — **the largest single item**; one store + one line flush per producer per run, inside the op; sim pays the store for entries it never reads |
| **ABI** | three new runtime C entries (integers and the existing opaque handle only) and their `ChipWorker` dlsym slots; null slots degrade to today's behaviour, so no version negotiation and no new constant |
| **layout** | four `uint64_t` bank selectors in `KernelArgs`, with moved offset assertions and four sim hooks |
| **host-side** | one policy constant, one signal on the runner, one active-call gate on `ChipRunLaneState`, PMU implemented twice (a2a3 and a5 host layouts differ), DepGen's receive path made multi-epoch, two ScopeStats producer changes |

**8 — P4 dependency.** *(Superseded by §0.1: P4's admission work is approved and in implementation;
read "to be agreed" below as "since resolved".)* P4 (`PID1403923`) remains the sole product writer; none of its files is touched
here and its normative §3/§12 are adopted rather than amended — banks stay collector-owned, epoch-keyed,
independent of the run-resource index and mapped by address. The one real coupling is sequencing:
making `joined_diagnostic_launch_supported_impl` strong on three more backends opens a capacity path P4
owns, so that order is **to be agreed, not assumed**. P4's per-run operation lease and this gate are
complementary, on different locks and different keys (§15.3).

**9 — What is not claimed.** No execution evidence *(for §1–§15; §0.3 states what exists and its
limits)*, no performance number, no completion thread, no
shared run deadline, no scheduler guarantee for thread creation, no proof that a runtime-local waiter
count makes `dlclose` safe, no claim that the gate makes arbitrary concurrent `finalize` safe, and no
proof borrowed from the a2a3 HBG swimlane declaration for another owner or backend. Acceptance scenario
2 is a controlled fixture and is labelled one.

**10 — The alternative, if the device cost is unacceptable**: declare the join unsupported for every
owner but the swimlane and close the question. Coherent and free; leaves five of six owners on the
cliff and leaves today's unbounded swimlane block, the missing budget and the three-of-six
inconsistency in place. The honest fallback, not a recommendation.

---

## 15. Cross-layer protocol (the credit bridge)

The credit signal lives in `DeviceRunnerBase`, inside the **runtime module**. `ChipRun::wait_until`,
`ChipRunState` and the lane live in the **extension** (`_task_interface`), which reaches the runtime
only through `dlsym`-resolved C entries (`chip_worker.cpp:231`, `:267-309`). So the credit wait **does**
cross the C boundary; §7.3's "nothing crosses" was wrong and is corrected above. No C++ condition
variable, no `std::mutex` and no collector type is exposed; only integers and the existing opaque
handle.

### 15.1 The three proposed C entries

```c
/* A snapshot of credit progress. Monotonic, wraps never in practice (uint64).
   Non-blocking; safe to call under the lane mutex. */
uint64_t simpler_diagnostic_credit_generation_ctx(DeviceContextHandle ctx);

/* Block until the generation differs from `seen`, or `timeout_ms` elapses, or
   waits are cancelled. Must NOT be called holding the lane mutex or a collector
   lock. Returns:
      1  generation advanced
      0  timeout_ms elapsed
     <0  cancelled / shutting down / invalid handle           */
int simpler_wait_diagnostic_credit_ctx(DeviceContextHandle ctx, uint64_t seen, int timeout_ms);

/* Cancel every present and future wait: set `cancelled`, broadcast, return.
   NON-BLOCKING and idempotent — it waits for no waiter, so a thread that is
   itself inside a guarded call may invoke it (§15.3). It does NOT prove any
   waiter left; the extension's gate drain does that.                       */
void simpler_cancel_diagnostic_credit_waits_ctx(DeviceContextHandle ctx);
```

`ChipWorker` gains three `dlsym` slots and three forwarding methods beside the existing ones; a runtime
that does not export them leaves the slots null, and a null `wait` slot degrades to "no credit wait" —
the caller falls back to today's behaviour, which keeps the entries optional for any out-of-tree
runtime and adds no version negotiation.

**Ownership and pins.** The CV, mutex, generation and `cancelled` flag live in the runner and are only
ever touched through these three entries, which carry nothing but integers and the opaque handle. What
the extension owns is the *right to be inside them*: the handle and the three resolved pointers are
**copied once into the gate** (§15.3) and every call reads them from there, under the gate lock, with a
lease already held. The runtime-side waiter count proposed in R6 is **withdrawn, not supplemented** — it
was the thing that could not be proved, and the gate replaces it. There is no callback, no registration
and no runtime object whose lifetime the extension must track.

**No polling sleep** anywhere on this path: `wait` is a single `wait_for` on the runner's CV with the
budget as its bound.

### 15.2 Which caller uses which mechanism

Two call paths reach `progress()`, and they get credit progress differently. Both are existing paths;
neither gains a thread.

**(a) The local L3 chip child — `done()`, not `wait_until`.** `_run_chip_main_loop` progresses staged
runs with `staged.chip_run.done()` (`worker.py:3802`) inside a loop that also republishes mailbox
state, decrements `liveness_countdown`, checks `os.getppid()` and handles shutdown. **That thread takes
no credit wait at all.** It is already a progress loop, so an admission declined for credit is retried
on its own next pass — which is exactly the "re-asked every round" rule the lane already states
(`chip_run_lane.cpp:401-406`). Control, mailbox publication and shutdown therefore keep running while
credit is exhausted, which is the property this loop must not lose.

Consequence: **the admission-expiry budget must be enforced inside `progress()`**, not only around a
wait, or this path would never fail a credit-starved run. It is a timestamp comparison on
`ChipRunState`, needing no blocking and no signal (§7.3).

**(b) The synchronous route — `wait_until`, and the GIL.** `run_with_lease` ends in
`wait_until(Deadline::max())`, reached from `run_materialized`, which today has **no** `call_guard` and
therefore holds the GIL for the whole run. Two consequences, both addressed:

- a credit wait on that thread would hold the GIL — unacceptable;
- so would the device wait it already takes in `block_on_front`.

`run_materialized` must release the GIL for the duration of the submission, **after** copying its
Python-owned inputs — the args are already a bound C++ `ChipStorageTaskArgs` and the provenance is
already copied into the POD by `make_run_provenance` before any native call, so the copy boundary
already exists. The change is one `nb::gil_scoped_release` placed exactly as
`_submit_chip_run_materialized` already places it. §15.1's `wait` is then called with the GIL released
and the lane mutex dropped.

### 15.3 Teardown: an extension-owned active-call gate

**Why a runtime-local waiter count cannot prove unload safety.** Two interleavings defeat it, and both
are visible in today's code:

1. **Pre-entry.** A forwarding method has loaded the function pointer but has not yet entered the
   runtime or taken `credit_mu`. A runtime-local count reads zero, `close` proceeds, and that caller
   then calls into freed code with a destroyed context. The window is real today: `ChipWorker::finalize`
   `dlclose`s at `chip_worker.cpp:768` and nulls `*_fn_` **afterwards** (`:770-812`), so a reader can
   hold a valid-looking pointer across the unload.
2. **Post-decrement.** A waiter decrements the count inside runtime code, notifies and unlocks, but has
   not executed the runtime's own return epilogue. `close` observes zero and unloads the frame the
   thread is still returning through. This applies equally to the **non-blocking** generation read — a
   count that only wraps `wait` never sees that caller at all.

Neither is fixable from inside the runtime: the state that must be consulted has to live, and stay
valid, **on the side that survives the unload**.

**Where the gate lives.** In `ChipRunLaneState`, which `ChipRun` holds by `shared_ptr`
(`chip_run_lane.h:51`) and which therefore outlives both `ChipRunLane` and — for a `ChipRun` a caller
still holds — the worker itself. That choice is load-bearing: `ChipRunLaneState` keeps the worker as a
**raw** `ChipWorker *` (`chip_run_lane.cpp:84`), so a parked waiter must never reach the entries through
`worker->`. It reaches them through the gate, whose storage is in the extension image the interpreter
never unloads (`chip_worker.cpp:88`).

```cpp
enum class GateState : int { Open, Closing, Detached };

struct CreditCallGate {                     // member of ChipRunLaneState
    std::mutex              mu;
    std::condition_variable idle;           // calls_in_flight reached 0
    std::condition_variable settled;        // state reached Detached (followers join on this)
    GateState               state = GateState::Open;
    unsigned                calls_in_flight = 0;   // EVERY counted call — ordinary AND cancellation
    std::thread::id         owner{};               // meaningful only while state == Closing

    DeviceContextHandle     ctx = nullptr;  // the targets: captured once at lane construction,
    CreditGenerationFn      gen_fn = nullptr;   // while the worker is demonstrably alive,
    CreditWaitFn            wait_fn = nullptr;   // and cleared on entry to Detached
    CreditCancelFn          cancel_fn = nullptr;
};
```

| state | ordinary call | cancellation | targets |
| ----- | ------------- | ------------ | ------- |
| **Open** | admitted, counted | admitted, counted | valid |
| **Closing** | **refused** — local result | **admitted and counted**: the teardown owner's own cancel, and a poisoning thread's | valid; only the owner clears them, and only at Closing → Detached |
| **Detached** | refused — local result | refused — no-op | **null**; nothing reads them again |

**An admitted call.** The lease is taken **before** the pointer is readable, because the pointer is
handed out *by* the acquisition, under the gate lock — there is no "pointer in hand, lease not yet
taken" state to lose, and **cancellation is not exempt from counting**; it is exempt only from the
`Closing` refusal:

```text
enum class Admit { Ordinary, Cancel };

class CreditCall {                 // RAII, compiled into the EXTENSION
    CreditCall(CreditCallGate& g, Admit kind)
        // ctor:  lock(g.mu);
        //        if (g.state == Detached)                        -> empty
        //        if (g.state == Closing && kind == Ordinary)     -> empty
        //        ++g.calls_in_flight; copy ctx + the needed fn out
    ~CreditCall()                  // dtor:  lock(g.mu); if (--g.calls_in_flight == 0) g.idle.notify_all();
    explicit operator bool() const // empty == not admitted: the caller takes the local result below
};

int wait_for_credit(gate, seen, timeout_ms):
    CreditCall call(gate, Admit::Ordinary);    # lease held from here
    if (!call) return 0                        # not admitted: no wait, caller re-asks next pass
    return call.wait_fn(call.ctx, seen, timeout_ms)
                                               # lease released by ~CreditCall, in extension code,
                                               # strictly after the runtime frame has returned
```

The destructor is extension code, so the decrement cannot precede the runtime's return — which is
exactly what interleaving 2 needed. **All three entries are guarded**, the non-blocking generation read
included; `ctx` is a copy taken under the gate lock, so no call dereferences `worker` or re-reads a
`*_fn_` member.

**Local results, so a refused caller never reads a target.** Refusal is not an error anywhere:

| entry | result when not admitted | effect on the caller |
| ----- | ------------------------ | -------------------- |
| generation read | `0` | "no progress information"; the run is re-asked next pass |
| `wait_for_credit` | `0`, as a timeout | no wait taken — the L3 retry rule of §15.2a, unchanged |
| cancel | no-op | the thing it would have woken is already going away |
| `close_credit_gate` | `Already` | repeated close returns without touching targets |

**Lock order, one direction only.** `lane_->mu` → gate `mu` → (release gate `mu`) → the runtime call.
The generation read may therefore be taken under the lane mutex, as §15.1 says, because it holds the
gate lock only for the handful of instructions that copy the pins. Nothing ever takes the lane mutex
while holding the gate lock — step 3's drain is the case that would deadlock if it did, and it holds
only the gate lock, which the wait itself releases.

**One teardown owner; everyone else joins.** Latching an entry shut does not make the latcher the owner
of teardown, and R7's "owner-only cancel" was *uncounted* — which left exactly the race the review
names: close A captures the pins and pauses before calling, close B also closes the entry, cancels,
sees a public count of zero, destroys and unloads, and A then calls through a stale pointer. The fix is
that the privileged cancel is privileged **only in being admitted during `Closing`** — it is counted
like any other call, and only one thread ever performs it:

```text
enum class CloseOutcome { Owner, Already };

CloseOutcome close_credit_gate(gate):                      # top of ChipRunLane::close()
    unique_lock lk(gate.mu);
    if (gate.state == Detached) return Already;            # (A) repeated close: no target read
    if (gate.state == Closing) {                           # (B) follower: join, never a second owner
        gate.settled.wait(lk, [&]{ return gate.state == Detached; });
        return Already;
    }
    gate.state = Closing;                                  # (C) exclusive claim, under the mutex
    gate.owner = std::this_thread::get_id();
    auto ctx = gate.ctx; auto cancel = gate.cancel_fn;      #     pins copied under the same lock
    ++gate.calls_in_flight;                                #     the owner's cancel IS counted
    lk.unlock();
    if (cancel) cancel(ctx);                               # (D) privileged: admitted in Closing
    lk.lock();
    --gate.calls_in_flight;                                #     released after return, extension-side
    gate.idle.wait(lk, [&]{ return gate.calls_in_flight == 0; });   # (E) drain: ordinary AND cancels
    gate.ctx = nullptr; gate.gen_fn = gate.wait_fn = gate.cancel_fn = nullptr;   # (F) DETACH targets
    gate.state = Detached; gate.owner = {};
    gate.settled.notify_all();                             # (G) followers resolve
    return Owner;
```

Why each position is forced:

| Step | Why here |
| ---- | -------- |
| (C) before (D) | the claim is what makes a second thread a *follower*. Without it two threads both reach (D) and (E), and either one's (E) can observe zero while the other is paused at (D) |
| (D) counted | it is inside `calls_in_flight`, so no other thread's (E) can read zero while it is pending or in flight. This is the whole of the R7 correction |
| (E) after (D) | (D) has already made every waiter's predicate true, so the drain terminates on the waiters' return path. Reversing them would make close wait a full `kCreditWaitBudgetMs` |
| (E) holds the gate lock only | a returning call needs that lock to decrement; no lane mutex, no collector lock, no `native_run_mu_` is held across it |
| (F) before any destroy | after detach nothing can read a stale `ctx` or a stale pointer — the later `destroy_device_context_fn_` and `dlclose` have no reader left to race |
| (G) last | a follower returns only once (F) is a fact, so "close returned" means "detached", for every caller |

Then teardown continues in main's existing order: today's `close()` body (`lock(state_->mu)`,
`drain_front()` per run, `closed = true`, `chip_run_lane.cpp:885-895`), then
`finish_retained_runs()` joining every collector writer, then the collectors destroyed, then
`finalize_device_fn_`, `destroy_device_context_fn_` and `dlclose`.

**Detach clears the credit pins only.** The lane body that runs after it still finalizes and waits
native runs through the worker — those are **run-keyed** entries governed by P4's per-run lease, not by
this gate, and their `*_fn_` members are untouched by (F). Detaching early therefore costs the drain
nothing and breaks nothing.

**Where this sits in main's real order.** `ChipWorker::finalize` calls `run_lane_->close()` and then
`run_lane_.reset()`, **whose destructor calls `close()` a second time** (`chip_worker.cpp:721-727`,
`chip_run_lane.cpp:723-727`) — and both of those happen **before** `destroy_device_context_fn_` and
`dlclose` (`chip_worker.cpp:764`, `:768`). So the double close is already main's behaviour, not a
hypothetical: branch (A) is the one the destructor's close takes, and it is why repeated close must
return a local result rather than re-running anything. No claim is made that an ordinary destructor path
runs after the unload — it does not.

Because the sequence sits at the **top of `ChipRunLane::close()`**, ahead of `state_->mu`, it also runs
on the paths that throw: `close()` has two `rethrow_exception` exits and `~ChipRunLane` swallows them
(`chip_run_lane.cpp:723-727`), so detaching before the lock is what makes a *throwing* close still leave
the gate Detached.

**`stop_admission()` is not teardown and does not touch the state.** It issues one **counted**
cancellation in `Open` — waking waiters whose credit will never arrive — and returns; it claims nothing,
drains nothing and detaches nothing. R7 had it latch the entry shut, which would have parked the gate in
`Closing` with no owner that ever finishes and left a later follower waiting on `settled` forever. The
cancel is issued outside `state_->mu` to keep the lane → gate order, and it cannot block, which is what
`noexcept` requires of it.

**No self-drain, by poison or by anyone.** A thread that poisons from *inside* a counted call already
holds a lease. It issues its cancellation **under that same lease** — no second acquisition, no
release-then-call — which is admitted because cancellation is admitted in `Closing`, and which is
counted because its own lease already is. It must not claim ownership and must not wait on `idle` or
`settled`; the drain is owed by whichever thread later runs `close()`. Stated as an invariant — *a
counted call never drains and never claims* — and backed by a thread-local depth checked in an
assertion.

That invariant is the same one the public API already enforces one layer up: `Worker.close()` refuses to
run *"from within a run() / submit() / … or other leased Worker operation on this thread"* and from
within run finalization (`worker.py:13178-13184`).

**The enclosing serialization, named — this gate alone does not prove arbitrary concurrent close
safety.** Two layers, and the split is visible in the bindings:

| Entry | GIL | Consequence |
| ----- | --- | ----------- |
| `finalize` — `task_interface.cpp:3633` | `.def("finalize", &ChipWorker::finalize)` — **no `call_guard`**, so the GIL is held for its whole body | two Python threads cannot run `finalize` concurrently at all; destroy/`dlclose` are serialized by the interpreter itself |
| `_close_chip_run_lane`, `_stop_chip_run_lane_admission` — `:3716-3717`, `:3720-3722` | `nb::call_guard<nb::gil_scoped_release>()` | these **can** overlap each other and another thread's `finalize`. They are the concurrency the gate protocol above exists for |

Above that, `Worker.close()` supplies the policy the C++ layer mirrors: init/finalize *"still run on the
owning thread (enforced by `Worker.close()`)"* (`task_interface.cpp:3624-3626`); a worker with a live
native tree must be closed on its init-owner thread (`worker.py:13235-13240`); one teardown attempt is
claimed and published while concurrent callers **join that attempt** rather than start their own
(`worker.py:13205-13228`). The gate is the same protocol one layer down, for the two entries that leave
the GIL behind; it is not a claim that any thread may call `finalize` at any time.

**A rejected close keeps its retry.** Main's `finalize` throws when `finalize_device_fn_` fails, keeping
*"the context and host runtime loaded"* so an explicit retry can finish the job (`chip_worker.cpp:756-761`).
The gate does not interfere: it reaches `Detached` inside `close()`, which is **before** the first
fallible teardown step, so a device-finalize failure finds the lane already gone (`run_lane_.reset()`
has run) and the gate already terminal. There is nothing to reopen and no credit caller that can reach
the retained context; a retried `finalize()` takes the `run_lane_ == nullptr` path and never consults the
gate. **`Detached` is terminal and is never reopened** — a later successful `init()` builds a new lane
with a new gate whose pins are resolved against the newly `dlopen`ed module, which is the only correct
source for them.

**What P4's primitives actually cover.** P4 normative §2.3 introduces an **extension-owned** exclusive
operation lease — `ops_in_flight ∈ {0,1}` per `RunEntry`, guarded by `native_run_mu_`, with `ops_done_`
and a finalize-time drain (acquisition in its §2.3.3, the drain in its §2.3.5) — and §2.4 states plainly
that the magic/runner checks are *"validation, not lifetime protection"* and that **no ABI mechanism is
added**.

| Field | P4's operation lease | this gate |
| ----- | -------------------- | --------- |
| owner | extension (`ChipWorker`) | extension (`ChipRunLaneState`) |
| key | one `run_epoch` | the context, for calls that belong to no run |
| covers | `poll` / `wait` / `launch` / `finalize` on that run | the three credit entries |
| excludes | anything not keyed by a run — including all three credit entries | every run-keyed entry; P4 keeps those |
| proves | at most one operation in the runtime *for that run* | no thread in the runtime *via a credit entry* |

So the shape is **borrowed** — same owner side, same lock-then-copy-then-call discipline, same
drain-before-teardown — and the coverage is **not**: P4's lease is per-run and is not a module-wide
lease, so it neither admits nor drains a context-keyed call, and nothing in it bears on `dlclose`. The
two coexist on different locks and are never nested: a credit call holds no `native_run_mu_`, and a run
operation takes no gate lease. P4's §2.4 caller-serialization rule continues to govern the pre-existing
run-keyed entries; this design does not claim to have replaced it and changes none of its files.

**What this gate does not fix.** The pre-existing pointer-versus-`dlclose` window of interleaving 1
remains open for *every other* runtime entry; closing it there is P4's §2.4 territory and is out of
scope here. The gate is scoped to the entries this design adds — which are also the only ones a thread
is expected to sit inside for seconds rather than microseconds, which is what makes them the ones that
must be gated.

### 15.4 What this appendix does **not** claim

- No completion thread, in either path. The retirement work is done by whichever caller runs
  `progress()`.
- No wake-based wait exists on the L3 staged path, and none is added there.
- `done()` is **not wait-free**. Every *explicit protocol wait* left inside admission carries a stated
  budget — the shard-table ACK, ≤ `kControlAckBudgetMs` per enabled owner. The one-off first-run
  collector thread start is **not** given a wall-clock bound, and no scheduler guarantee is claimed for
  it (§6.3).
- No proof that a runtime-local waiter count makes `dlclose` safe. §15.3 withdraws that count; the
  barrier is the extension's gate, and the gate is scoped to the three credit entries — it does not
  claim to close the pre-existing window on any other entry.
- **No claim that the gate alone makes arbitrary concurrent `close`/`finalize` safe.** It makes the two
  GIL-releasing lane entries safe against each other and against `finalize`; `finalize` itself stays
  serialized by its own GIL-holding binding and by `Worker.close()`'s owning-thread and single-attempt
  rules, which §15.3 names rather than replaces.
- No new version constant, no session concept, no `PTO_` identifier, and no user-facing knob.
