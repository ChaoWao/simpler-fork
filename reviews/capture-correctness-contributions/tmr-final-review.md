## Reviewed scope

- **Head (frozen):** `28a9de54af839e1bc5319641379948a9ba608724`; `git rev-parse HEAD` confirms it, `git status --porcelain` is **empty**, and `28a9de54a^` = `71f5b4fc6b8ac6ac2419086539ccf0c9af6efb57` (published #2625 head). Single commit on top of the published parent.
- **Diffs read:** `git diff 71f5...28a9` (3 files, **+17/−2**, all `tests/`) and the two-dot `git diff 121f8b9a4 28a9de54a` (one hunk, `transport.cpp` only).

| Bucket | Files | +/− |
| --- | --- | --- |
| Test/Ex | 3 | +17 / −2 |
| Core / Build / Docs / Uncat | 0 | — |

**Limitations:** read-only. No edits, checkouts, builds, test runs, hardware, GH comments, or external CLIs. I inspected the frozen source, the already-written build artifacts, and `objdump` output from objects that were already on disk. Per your instruction I did not treat any artifact as final-`28a9` on assertion alone — each is tied to the head below by mtime-after-source **and** by content that can only come from this commit, plus recorded SHA-256. No macOS, libc++, current-main (`62565feb3`), or A5 hardware/timeout conclusion is drawn; the A5 timeout remains an explicit merge blocker for the feature PR and is outside this correction.

**Carried forward from the previous round, unchanged:** my original claim that an explicit call to `::operator new` cannot be elided was **wrong and stays retracted** — g++ 10.3.1 at `-O2 -DNDEBUG` removed the direct `new`/`delete` pair and the handler at `121f8`, which is what made four native-admission targets red at witness 0 vs 1. Nothing below rests on standards reasoning; every claim about optimized behaviour below is read off machine code.

**Withdrawn:** my prior reporting-only Consider about the `121f8`-era green logs. You have shown those were genuinely re-measured and hashed in the unoptimized/default configuration — a configuration distinction, not carried-forward `2990` results. The attribution was unsupported and I withdraw it; it is not repeated here and should not carry into the record.

---

## The change, and whether the Must closes

The delta from `121f8` is one hunk in `tests/ut/cpp/support/native_admission/transport.cpp:118-130`:

```cpp
*c.capture_refuse_after_core = true;
// The volatile allocator target preserves the faulting call under
// optimization. Only bad_alloc observed at this site satisfies the witness.
void *(*volatile allocate)(std::size_t) = ::operator new;
try {
    void *storage = allocate(1);
    ::operator delete(storage);
} catch (const std::bad_alloc &) {
    ++c.capture_record_allocation_failures;
    throw;
}
```

Source-level checks: the target type `void *(*)(std::size_t)` selects the single-argument throwing overload — i.e. exactly the one replaced at `test_native_admission.cpp:140-144` — out of the `::operator new` overload set; `std::size_t` and `std::bad_alloc` are both supplied by the already-present `<new>` (`transport.cpp:23`); the handler rethrows with bare `throw;`, so the production path is unchanged; the comment states a present-tense invariant (why the indirection exists, what the witness counts) rather than narrating the edit. No macro, no `#pragma`, no per-file optimization flag — nothing that would make this configuration-specific.

**The Must is closed, and this is read off the Release objects, not inferred.** `transport.cpp.o` for both TMR targets was compiled at 23:25:37 / 23:25:39 from the 23:25:34 source, with `CXX_FLAGS = -O2 -DNDEBUG -std=c++17` (recorded verbatim in the qualification report). Both disassemble identically:

```text
0000000000000750 <aclrtRecordEvent>:
 764: ldr  x0, [x19, #72]    ; capture_refuse_after_core
 768: cbz  x0, 798
 76c: ldr  x1, [x19, #96]    ; capture_core_calls
 770: cbz  x1, 798
 774: adrp x1, _Znwm         ; materialize the replaced operator new
 778: add  x1, x1, #0x0
 77c: str  x1, [sp, #40]     ;   into the volatile slot
 784: strb w1, [x0]          ; *capture_refuse_after_core = true   (armed BEFORE the call)
 788: mov  x0, #0x1
 78c: ldr  x1, [sp, #40]     ; volatile reload — target now opaque to GCC
 790: blr  x1                ; INDIRECT allocation call, survives -O2
 794: bl   _ZdlPv            ; matching delete, non-throwing path
 ...
 7a8: cmp  x1, #0x1          ; landing pad
 7b0: bl   __cxa_begin_catch
 7b4: ldr  x0, [x19, #80]    ; capture_record_allocation_failures
 7b8: add  x0, x0, #0x1      ; ++
 7bc: str  x0, [x19, #80]
 7c0: bl   __cxa_rethrow
```

Four things this establishes that the `121f8` object did not:

1. **The allocation call exists at `-O2`.** `blr x1` through the volatile slot; GCC cannot identify the callee as a replaceable allocation function, so the `new`/`delete` pair-DCE that erased the `121f8` body cannot apply. Compare the saved `record-event-disassembly-121f8b9a4.txt` body: ten instructions, no branch, no `_Znwm`.
2. **The handler exists**, with `__cxa_begin_catch` → increment → `__cxa_rethrow`, and a `__cxa_end_catch`/`_Unwind_Resume` cleanup edge.
3. **The incremented field is the asserted one.** Offset 80 in `Control` is `capture_record_allocation_failures`, given the layout in `transport.h` (72 = `capture_refuse_after_core`, 96 = `capture_core_calls`, both confirmed by the loads at 0x764/0x76c).
4. **The arming store precedes the call** (`strb` at 0x784 before `blr` at 0x790), so the refusal is live when the replacement runs — the fault is raised inside this callback, which is what the 1/0 witness asserts.

**Independent Release qualification at this head is green and tied to these exact binaries.** `.docs/review/qualification-28a9de54a-release/report.json` records `head: 28a9de54af839e1bc5319641379948a9ba608724`, **8/8 targets rc=0, 169 passed, 3 skipped**, finished 23:27:43–44, with `-O2 -DNDEBUG -std=c++17` captured in the report. I re-hashed the two TMR binaries on disk: `36fcdf42…` and `2441963b…` — **identical to the `binary_sha256` values in the report**, and those are the binaries linked from the objects disassembled above. So the green run and the machine-code evidence are the same artifact, not two separate claims.

Net effect on the original defect: the refusal is now consumed inside `aclrtRecordEvent` at `-O2`, so the fault no longer reaches (or depends on) the `std::string("simpler_aicpu_exec")` temporary at `transport.cpp:175`. The commit subject — "independent of string storage" — is satisfied in the optimized configuration where it previously was not.

---

## Findings

### Must fix

**None.** The single Must from the `121f8` round is closed, verified at the machine-code level on both arch Release objects and corroborated by a hash-matched 8/8 green.

### Should fix

**None.**

### Consider — 1 (optional, no scope expansion)

The volatile indirection is load-bearing and non-obvious: a future "simplification" back to a direct `::operator new(1)` would silently return the fixture to the `121f8` state — green on libstdc++, blind to the libc++ defect — with no compile error. The comment already states the invariant correctly, and the owner-reported mutant (remove only the allocate-call/delete pair, keep pointer setup/flag/catch) failing witness 0 vs 1 in both configurations is the right regression barrier. Nothing to change; worth keeping that mutant recipe in the R3 report so the next person can re-run it rather than re-derive it. (I did not re-run the mutant here — that remains owner-reported for `28a9`.)

---

## Verdict

**Approve the exact correction at `28a9de54a`**, with the pending-validation limitations stated below.

Chain: the defect is real and unchanged (libc++ SSO absorbs the 18-char name, so the pre-correction arm never faulted — `TMR_CAPTURE_REPORT.md:17`) → the method now actually executes in the shipped optimized configuration, proven by the indirect `blr` and the catch/rethrow in both arch `-O2` objects, not by reasoning → the witness semantics match the claim (only a `bad_alloc` observed at this site increments the asserted field, armed before the call) → residual is zero Must, zero Should.

Scope is unchanged and correct: three test/support files, no production code, no ABI, no env/macro gate, no mode split, no optimization-disabling flag — appropriate to fold into the existing meaningful #2625 rather than open a separate PR.

**Truthful pending state, as of this review:**

- **Release (this worktree, `tests/ut/cpp/build`): complete and green** — 8/8, 169/3, hash-matched to the disassembled binaries.
- **Debug (`.docs/cpp-debug-28a9de54a`): still building.** Targets were being linked at 23:26–23:27 and **no Debug ctest/qualification output exists yet**; I make no claim about it. The Debug configuration is the weaker test of this particular fix (the elision only appears under optimization), but it is the configuration in which the earlier green was measured, so its completion still matters for the record.
- Owner-reported and **not independently re-verified here**: the `28a9` mutant results in both configurations, and the Debug 169/3.
- Out of scope and not claimed: macOS/libc++ execution, current-main (`62565feb3`) qualification of #2625, complete-capture acceptance, and A5 silicon — with the A5 timeout remaining an explicit existing-feature merge blocker, independent of this correction.

Final qualification receipts should be manager-reviewed before release, as planned; my approval covers the correction itself and the Release evidence above, not the release decision.
