# Review recheck — local HBG restore-failure gate, frozen at `7d1dae51`

Read-only. No checkout, edit, build, hardware run, GitHub action, or external agent. I verified the ancestry claim first: `git merge-base --is-ancestor 81e52b086 7d1dae515` reports **not an ancestor** — `7d1dae51` is a sibling amendment, so the two-dot `git diff 81e52b086 7d1dae515` is the right delta shape and the full correction is `84b4653b...7d1dae51`.

## 0. Summary

Both Should items from my previous review are closed, and the one additional approved change (the HBG-private contract comment) is accurate against the code it describes. The delta against `81e52b08` touches only `kernel_graph_execution.h`'s comment and the registration test; the runtime implementation is byte-identical to what I already approved. **No remaining Must or Should. Approve**, subject to the unchanged qualification limits below.

## 1. What changed since the reviewed head

`git diff 81e52b086 7d1dae515 --numstat` is exactly two files:

```
5   1   src/common/host_build_graph/kernel_graph_execution.h
33  1   tests/ut/cpp/common/host_build_graph/test_hbg_kernel_registration.cpp
```

No `device/kernel_graph_execution_entry.cpp` entry, no `kernel_graph_execution_entry.h` entry — the restore-status save, the retirement aggregation, and the direct poison are unchanged from the version I analyzed in detail. The full `84b4653b...7d1dae51` numstat sums to +131/−16 over six files, matching the stated patch.

## 2. Closure 1 — contract comment (prior Should #1)

Old text asserted "Native errors require FatalFailure and Host context poisoning", which no caller did any more. New text at `kernel_graph_execution.h:75-79`:

> `FatalFailure or a nonzero runtime/teardown status poisons this execution slot; the enclosing owner retains responsibility for Host context poisoning and resource lifetime. The kernel entry directly poisons a failed restoration, including failures without an admitted attempt, and delegates only successful retirement here. Neither failure path authorizes reset/free.`

Checked clause by clause against the code:

- "FatalFailure or a nonzero runtime/teardown status poisons this execution slot" — exactly the disjunction at `device/kernel_graph_execution.cpp:343-348`.
- "the enclosing owner retains responsibility for … resource lifetime" — consistent with `poison_graph_execution_slot` (`kernel_graph_slot_control.cpp:208-213`), which only stores the phase and flushes; it frees nothing.
- "The kernel entry directly poisons a failed restoration, including failures without an admitted attempt, and delegates only successful retirement here" — matches `retire_kernel_graph` (`kernel_graph_execution_entry.cpp:171-184`): the `status != 0` branch poisons and returns, and the only `retire_graph_execution` call passes `{Completed}`.
- "Neither failure path authorizes reset/free" — true of both.

The preceding line, "Only a known controlled fault with no runtime/teardown error permits retry", is still correct and still exercised (`test_hbg_bind_ledger.cpp:2816`, `ControlledFailure` → `Ok`). The comment makes no claim that the lower API supports only success, and it is a present-tense statement of the current contract rather than change narration, so it is clean under `comments.md`. **Closed.**

One non-blocking observation, offered only because it is the single place the wording outruns the code: the poison disjunction is reached only after the function's earlier guards (registry valid, slot not already poisoned, phase not `Preparing`, `control.attempt == attempt`, phase in `{Ready, Failed}`). A `FatalFailure` carrying a stale attempt returns `NotReady` and poisons nothing. The surrounding sentences establish that precondition well enough that I would not change anything here — and it is precisely the case the wrapper's direct poison covers.

## 3. Closure 2 — fatal-branch coverage (prior Should #2)

`HbgFatalRetirementTest` (`test_hbg_kernel_registration.cpp:66-95`) is a stronger closure than I asked for. What I verified by reading:

- **The precondition is a genuinely admitted execution, not a pre-poisoned shortcut.** The test fabricates `attempt = committed_generation = 1`, `runtime_address = registration.destinations[1].address`, `phase = Ready`, `status = Ok`, then *positively* asserts `acquire_graph_execution_slot(...) == Ok`, `registry->phase == Ready`, `execution.phase == Ready`, and — the decisive one — `acquire_graph_execution(registry, 1, admitted) == Ok`. That last call re-checks phase, both generations, status, `runtime_address >= destinations[1].address`, `graph_span_fits(0, sizeof(RuntimeContext), 4096)` and every `live_bytes[i] <= capacity` (`kernel_graph_execution.cpp:376-385`). `RuntimeContext` (`runtime_core.h:120-146`) is three pointers, a mode, two `int64_t` and a `RuntimeArenaLayout` of small offset fields — a few hundred bytes, comfortably inside the fixture's 4096-byte `destinations[1]`, so the assertion is reachable rather than vacuous.
- **The three arms isolate the three OR conditions.** Tracing each against `kernel_graph_execution.cpp:343-348` and the `completed`/`controlled` fallthrough at `:350-356`:

| Arm | If its OR condition were removed | Other arms |
| :-- | :-- | :-- |
| `{FatalFailure}` | falls through; `completed` false (outcome ≠ `Completed`), `controlled` false → returns `Rejected` ≠ `Poisoned` → **this arm fails** | unaffected |
| `{Completed, -71, 0}` | `completed` true (Ready, committed == attempt, status Ok) → CAS to `Idle` → returns `Ok` ≠ `Poisoned` → **this arm fails** | unaffected |
| `{Completed, 0, -72}` | same as above → **this arm fails** | unaffected |

  So the discrimination the owner reports is structurally supported by the code; each mutant can only redden its own arm. Both per-arch targets exist for this translation unit (`hbg_case(test_hbg_kernel_registration.cpp ORCH` → `simpler_ut_runtime_case`, and `test_a2a3_hbg_kernel_registration` / `test_a5_hbg_kernel_registration` are the resulting binaries), so "six mutants on both architecture targets" is structurally plausible. I did not build or run anything, so I am confirming the test's discriminating power by reading, not the owner's mutation run.
- **No enum or API was removed**, and each arm also asserts the post-state: `registry->phase == Poisoned` and a subsequent `acquire_graph_execution_slot` returning `Poisoned`.
- **Fixture hygiene holds.** `storage` is a per-test fixture member, so each instantiation installs into freshly zeroed memory; `TearDown`'s `detach_graph_slot_registry` is a CAS on `current_registry` and is unaffected by poisoning. The new suite never touches the entry's `restore_status` / `restore_attempt` globals, so it cannot perturb the `TEST_F`s in the same binary.

**Closed.** The `next{}` default-initialization nit (`:221`) is also addressed.

## 4. Remaining findings

**Must fix:** none.

**Should fix:** none.

**Consider** (both carried forward unchanged, both explicitly out of scope per your instruction — listed only so the record stays complete):

- `(void)poison_graph_execution_slot(registry)` at `kernel_graph_execution_entry.cpp:179` discards its status without a log, unlike every sibling rejection in that file. Not a correctness hole: the only way it no-ops is `valid_registry` failing, which blocks a successor identically.
- A sentence in `kernel_graph_execution_entry.h` could state the "restore always precedes retire within one invocation" pairing that `restore_status`'s lifetime rests on. Both executors honor it; the skip path is fail-safe in direction.

## 5. Merge recommendation

Problem real and reproduced by the in-diff negative control → mechanism unchanged from the version I independently approved → both required closures land, the contract comment is accurate and the fatal branch is now covered by three discriminating arms over a positively-admitted execution → residual is two optional, explicitly out-of-scope notes → **approve**.

### Qualification limits (preserved, not endorsed)

- Evidence remains CPU-side compilation of the production entry/packet/restore/slot/retirement functions under UT. Not RTS device scheduling, not silicon recovery, not hang recovery.
- The independent Release four-target run (139 pass / 9 skip) and neighbor validation are **running, not claimed**; I did not execute or inspect them. The older 133/9, 406/406 and Python 41/66 figures attach to `81e52b08`, not to `7d1dae51`.
- The owner's six-mutant result, the frozen report, `LOCAL_PATH` and `LOCAL_PATH` are the owner's artifacts; I verified none of them.
- Shared owner/binder/launch ABI and host-side refusal against an already-poisoned context remain outside scope and unassessed.

### Appendix A — change breakdown (`84b4653b...7d1dae51`)

| Bucket | Files | + | − | churn |
| :-- | --: | --: | --: | --: |
| Core | 3 | 37 | 13 | 50 |
| Test | 2 | 71 | 3 | 74 |
| Docs | 1 | 23 | 0 | 23 |
| **Total** | **6** | **131** | **16** | **147** |

Far below the oversized thresholds; no Uncategorized files.

### Appendix B — pto-isa pin check (advisory)

ℹ️ `pto_isa.pin` at `7d1dae51` is `b1d460ee8c001585ede36b68ba3678dc5a226bbf`, unchanged by this correction. The only include added anywhere in the patch is `host_build_graph/kernel_graph_execution.h` — no pto-isa header reference was added, moved, or removed, so no bump or onboard `host_runtime.so` rebuild is indicated.

### Appendix C — independent reviewer notes

No `codex` / `gemini` opt-in token, and external agents were explicitly forbidden. Step 7 skipped; nothing surfaced or dropped.
