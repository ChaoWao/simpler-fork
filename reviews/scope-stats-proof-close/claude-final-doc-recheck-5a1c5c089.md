# Actual Claude final documentation recheck

Session `5a72d87d-1265-4f3d-b815-476b402df029`; final head `5a1c5c089`.

## Final documentation recheck — PR #2684 @ `5a1c5c089`

Read-only. No edits, builds, hardware, GitHub writes, or agents. Scoped to the delta plus the sibling-consistency question; no new general review performed.

### Delta verified

`git merge-base 71077b04a 5a1c5c089` → `71077b04a`: base unchanged, still one commit ahead.

`git diff 77aba9a8a 5a1c5c089` is **one file, +1/−1**:

```diff
 tests/st/a5/tensormap_and_ringbuffer/dfx/scope_stats/test_scope_stats.py:20
-Output (``scope_stats.jsonl``): line 1 is run metadata
+Successful output (``scope_stats.jsonl``): line 1 is run metadata
```

Nothing under `src/`, `python/`, `simpler_setup/`, `docs/`, `tests/ut/`, or any `CMakeLists.txt` appears in `--name-only`. I confirmed that mechanically rather than by inspection — git tree hashes are identical between the two heads for every reviewed directory:

| Path | tree hash at `77aba9a8a` and `5a1c5c089` |
| :-- | :-- |
| `src` | `d04c8c4a72aa6a4060b684dceeda8fd6eb209374` |
| `simpler_setup` | `c8eb6887fbbca73a3d2cc5c405749fe8ff83b643` |
| `docs` | `0318842c1798e2661fed74b7524b768a3611fc4b` |
| `python` | `9ddde49b7085777882687c9d6b7532b2e1fb9789` |
| `tests/ut` | `aab51729505b1cec403621116131dfce0c2b7091` |

`pto_isa.pin` is untouched and still `b1d460ee8c001585ede36b68ba3678dc5a226bbf` — the Appendix B advisory from the prior review carries over verbatim.

### Sibling consistency

The a2a3 and a5 scene-test module docstrings are now character-for-character identical. Whole-file comparison at the final head leaves exactly three classes of difference:

1. `KERNELS_BASE` path (`examples/a5/…` vs `examples/a2a3/…`) — arch-correct.
2. `platforms` lists (`["a5sim", "a5"]` vs `["a2a3sim", "a2a3"]`) — arch-correct.
3. A `"run_epoch"` required-field entry and a nine-line single-non-zero-epoch assertion present only in the a2a3 file.

Item 3 is **pre-existing on base**: `git show 71077b04a:…a2a3/…` already contains that block, and the PR's full diff against base for that file is the one-word docstring line and nothing else. It is not this PR's asymmetry, it is not touched by this PR, and I am deliberately not reopening it — recorded only so the equivalence claim below is not read as broader than it is.

### Does this close the final Should-fix?

**Yes, closed.** The finding was precisely that this PR's own a2a3 docstring edit created a one-word divergence from a previously byte-identical a5 sibling, contrary to `codestyle.md` §10 and `doc-consistency.md` §1. The divergence the PR introduced is gone, fixed in the same commit, with no collateral change.

### Scope of the code-equivalence claim

Stated precisely, so it is not over-read:

- **Exact and mechanical:** `src/`, `python/`, `simpler_setup/`, `docs/`, and `tests/ut/` are bit-identical between `77aba9a8a` and `5a1c5c089` by tree hash. No production code, no unit test, no build configuration, no runtime source, and no documentation changed. The 20 unchanged source/runtime hashes you report are consistent with this and independently corroborated by the five tree hashes above.
- **The single changed byte-range** is a module-level docstring in one scene test. It alters `__doc__` only — no statement, assertion, fixture, `CASES` entry, platform list, or collection behavior. Scene-test selection and execution are unaffected.
- **What this does not claim:** it is not a statement that the final head is green. It is a statement that the final head is *behaviorally indistinguishable from the head I reviewed and approved*. That makes the `77aba9a8a` qualification results (C++ 394/394, Python 3549/192, A3sim 65 / A5sim 61 plus both L2 groups, A3 `task-submit` public 4 / scope-scene / 154 sweep sessions plus both L2 groups) transferable as evidence for everything except the one docstring line — because nothing those suites exercise has changed. The a5 scene test's *collected* behavior is identical; only its `__doc__` differs.
- **Still outstanding:** final-head CI has not run to completion in anything I can see, and I treat no pending check as green. I built and ran nothing; all of the above is derived from git object identity and file reads.

### Final verdict

**Approve.**

Problem real → method sound → implementation matches claims → the sole Should-fix is closed → residual is nil. The chain from my `77aba9a8a` review holds unchanged, and the optional lock tightening stays optional exactly as analyzed — no reachable interleaving exists, so it is not required for merge.

Remaining gate before merge is final-head CI, which is yours to confirm separately.
