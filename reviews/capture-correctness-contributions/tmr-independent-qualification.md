以下保留独立 validator 交接时的记录；后续 manager 接受和 fork 发布状态以[当前贡献报告](REPORT.md)为准。原始日志仍只在本地。

---

# Current TMR correction validation receipt —28a9de54a

## Exact candidate and review

- Frozen candidate: `28a9de54af839e1bc5319641379948a9ba608724`.
- Exact parent: published #2625 `71f5b4fc6b8ac6ac2419086539ccf0c9af6efb57`.
- Three approved test/support files,+17/-2; no production code, public API, ABI version, mode split, optimization-disabling flag or hardware mutation.
- Actual Claude final recheck session `5c246488-41b5-4036-ba21-25ca1105a356`: **approve, Must0/Should0**, exact full71f5...28a9 and delta121f8..28a9 inspected. Raw JSON/text `tmr-local-recheck-28a9de54a-result.json`, `claude-recheck-28a9de54a.md`.
- Intended as a contribution to existing meaningful capture PR, not a standalone tiny TMR PR. No publication by validation; root acceptance/release assignment required.

## Fresh independent final-head qualification

| Configuration | Result | Exact evidence |
| --- | --- | --- |
| GCC10.3.1 Release,-O2 -DNDEBUG |8target builds;169passed/3skipped|`qualification-28a9de54a-release/report.json`, XML and per-target logs |
| GCC10.3.1 trueDebug,-g |8target builds;169passed/3skipped|`qualification-28a9de54a-debug/report.json`, XML and per-target logs |
| Exact missing-call mutant |A3/A5 TMR named case fails in BOTH modes, witness0 expected1|`exact-call-mutant-results-28a9de54a.json`, `independent-mutants-28a9de54a.log` and per-target logs |
| Actual optimized call |Both architecture Release binaries contain `blr`, bad_alloc catch and rethrow|`record-event-{a2a3,a5}-release-28a9de54a.txt` |
| Source / binary identity |All3source hashes match R3 manifest; original8binary hashes per mode unchanged after mutant links|per-mode report plus final validation rehash |

Mutant removes only actual `allocate(1)` / delete lines, preserving volatile pointer setup, armed refusal, catch and source plumbing. It is compiled as an alternative object into separate validation executables; original product source and tested binaries remain untouched. Later libstdc++ allocation still throws but cannot satisfy the callback-local failure witness. Fresh exact-code Debug and Release results were both completed independently before this receipt; Claude's earlier snapshot described Debug as still running and is not used to claim its completion.

Build logs: `LOCAL_PATH`, `LOCAL_PATH`, `LOCAL_PATH`. No package/runtime install was needed for this C++ UT-only patch. The worktree owns its own venv and builds, remains detached/clean, and uses no other contributor's mutable tree.

## Required finding closures and retained corrections

1. Original2990 callback-entry counter could pass with no actual explicit allocation. Independent object mutant reproduced that false-green. Replaced with catch-only `capture_record_allocation_failures` witness.
2. Revised121f8 direct new/delete was optimized away under actual GCC10.3.1,-O2. All four fixture targets failed site witness; disassembly and actual Claude RequestChanges are preserved. This closes no product path by itself: old169/3 is NOT transferred forward.
3. Final28a9 uses a volatile indirect call to the actual replaced allocator inside catch/rethrow. Optimized machine code retains the call and typed witness; both configurations and exact missing-call controls discriminate correctly.
4. Claude's original statement that an explicit allocation call cannot be elided is explicitly retracted in its121f8 and28a9 reports. Its speculative suggestion that the owner carried old green results forward is also withdrawn: the genuine distinction was unoptimized/default versus optimized configuration, and the owner did remeasure/hash its source. Neither false claim is retained as accepted evidence.

Final reviewer optional wording about a direct-call simplification being silently green is not used: the current catch-site witness already makes the real121f8 direct-call optimized code red. No additional product change is needed for that optional observation.

## Limits / next owner

Current main62565feb3, full capture acceptance and original #2625's A5 timeout remain separate. Actual original macOS CI is the platform reproduction; there is no local macOS/libc++ pass claim. No A5 silicon result or timeout fix, no hardware work, no GitHub comment/PR/push. Existing author owns feature integration and eventual exact-head CI; root must process this receipt into acceptance/return and a concrete release task or external blocker.
