以下保留独立 validator 交接时的记录；后续 manager 接受和 fork 发布状态以[当前贡献报告](REPORT.md)为准。原始日志仍只在本地。

---

# Independent COMMON-C2 receipt: dbfbaf877

## Candidate and scope

- Frozen correction: `dbfbaf87757818d37550bd95c763d0bccd23c4ac`, exact parent published #2626 `f701832b091290d3db9f20eabe678700a4e7a043`.
- Whole correction: three approved files, +136/-6. Endpoint comparison against reviewed sibling e0d6: one header only, +4/-2 comment lines. Runtime/test blobs are byte-identical.
- Sole owner implemented; independent validator used its own detached worktree `c2-empty-reuse-validation-20261023`, no source edits. Source hashes match `LOCAL_PATH`.

## Review and required closures

Actual Claude CLI same-session exact-local review `572d4e76-1228-4d4f-804c-8eca4f08d4b6`:

| Finding from e0 review | Final disposition |
|---|---|
| freeze comment misattributes capacity enforcement | Closed: first successful prepare fixes capacity; freeze enables launch binding |
| refusal atomicity comment is insufficiently scoped | Closed: explicitly says repeated-request refusal |

**Final actual review: approve, 0 Must /0 Should**, `claude-recheck-dbfb.md`; raw prompt/result/stderr alongside it. The prior whole-mechanism review and e0 return are preserved in `claude-review.md` and `C2_VALIDATION_E0_RECEIPT.md`.

The review's explanatory claim that first-call cleanup always latches `closing_` is not used as evidence: successful resources cleanup resets that flag, while the wrapper separately controls its phase. The required repeated-request wording and both closures follow directly from the actual code, without this overgeneralization. Optional phrasing and initially-empty test suggestions were explicitly outside the required fix scope.

## Fresh independent final-head qualification

Normal (`-std=c++17`) and Release (`-O2 -DNDEBUG -std=c++17`) were rebuilt at exact dbfb, then all three targets freshly executed with per-target XML:

| Mode | Execution state | Binder | Invocation validation | Result |
|---|---:|---:|---:|---|
| Normal |57|6|5|68 passed /0 skipped /0 failed|
| Release |57|6|5|68 passed /0 skipped /0 failed|

Raw `qualification-dbfb/report.json`, target XML/logs; build logs `LOCAL_PATH}-build.log`. All three source and six binary hashes recorded. All six resulting binaries match their independently qualified e0 counterparts, consistent with the comment-only delta.

## Historical source-equivalent negative proof

At **e0d6 only**, this validator compiled/relinked eight real production-TU mutants: original offset guard, skip empty kind identity, discard first capacity, publish empty view early, each in normal/Release. All fail on their corresponding real lifecycle/atomicity tests. Parent-source arm leaves55 old controls passing and exactly2 new tests red. Raw independent driver/log/counts are `independent-negative-e0d6*`; qualified source/six binary hashes remained unchanged after mutants. These were **not rerun at dbfb**, whose runtime/test sources are identical.

## Conclusion and limits

Independent correction verdict: approve for contribution into existing meaningful #2626, pending manager acceptance and delegated publication. No original PR/author branch/GitHub state was mutated by validation. No hardware, real RTS scheduling, macOS, current-main integration, whole common foundation or full capture signoff is claimed. Provider allocation counters do not imply host-vector allocation freedom. Parent scope and exact original-head dependency remain explicit.
