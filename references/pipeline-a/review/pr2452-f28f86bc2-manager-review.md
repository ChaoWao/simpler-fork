> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# PR2452 f28f86bc2 — manager follow-up

Exact head f28f86bc2298d54ecb4b26e85b3225606b56f48e. Code static PASS against previously reviewed78091183b: only degraded-state explanation and migration of AnUnmappableBlockIsKeptRatherThanFreedUnderItsMapping. Test now expects Degraded with no allocations/releases and preserves owns/must_keep/quarantine/charged bytes. It does not relax the production guard. No tests rerun by manager. Current CI7success1skip12running; newhead completion required.

R1-R4 fixes retained. No full end-to-end device execution of default managed L2 is claimed. Init/attach/cleanup error propagation remains static evidence.

## Remaining PR description correction only

R5 now distinguishes release schedules, but the new table overclaims a reachable still-reading UAF without proving the existing slot/bank admission permits it. The table says arenas “any route” release at publication, contradicting its own managed-budget row. Replace with exact scope: unmanaged temp frees the slot's old block before replacement allocation (allocation failure loses it); unmanaged arenas retire superseded blocks at transactional publication; managed explicit-budget route tracks consumer retirement but only reclaims obsolete blocks under pressure. Existing slot/bank rules are not removed and this review has no reproduced stale-consumer defect. Avoid blanket statement none are last-consumer-aware when managed row is explicitly so.

Body-only revision suffices; source/test/commit/head changes are not requested. Preserve current head and CI. After remote body corrected and CI green, no remaining manager blocker.

## Follow-up 2026-09-25T04:07:20.806978+00:00

Current remote body verified: unmanaged temp/arena vs managed schedules correct, old slot/bank protections explicitly retained, no reproduced UAF asserted. R5 addressed, no manager blocking feedback remains. Head unchangedf28f86bc2. Static PASS,15success1skip4running, NOT READY until CI complete. Author idle. No local test or CI retry.

## Ready verification 2026-09-25T04:37:18.572083+00:00

Exact remote head unchanged f28f86bc2298d54ecb4b26e85b3225606b56f48e, OPEN/MERGEABLE. Current CI19success1skip, all completed. All three remote feedback surfaces checked, two threads resolved, no unresolved threads or new blockers. Manager code PASS and R1–R5 addressed. READY awaiting user merge; no further writer task. Scope/validation limits above remain. Manager performed no test rerun, CI POST or merge.


## User merge confirmed 2026-09-28T02:48:01.371462+00:00
GitHub reports MERGED at 2026-09-28T02:41:31Z, merge commit `a1ca296f8628842354d2b795e63cb94732fbb036`. Final reviewed head `f28f86bc2298d54ecb4b26e85b3225606b56f48e`; CI19success/1skip and feedback closure retained. Removed from Ready; original memory continues finite local-L3 design only.
