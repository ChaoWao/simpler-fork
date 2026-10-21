> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# L2 default workspace management — implementation disposition

Reviewed rev2 against main `bef53236679a0ff56d38abbf38d5a738f40e530c`.
The user agreed to proceed with L2 preparation/reuse, L3 on-demand reuse, common lifetime management and optional quotas. Proceed with ONE L2 implementation PR in the original memory worktree; L3 remains the next separate delivery. This is not a capture implementation.

## Accepted implementation shape
- Internal L2 routing distinguishes supported onboard program from child/sim/kernel paths; no new public enable knob.
- Install management and any optional limit before existing prewarm. Preserve known-size preparation; do not invent a guessed preallocation size.
- Keep current generations warm. Reclaim obsolete generations independently of quota pressure, only after final consumers and successful thread attachment. Compact fully released ledger history while preserving cumulative reports.
- Record terminal facts before exposing a reusable slot; stale epoch facts must never replace a newer record.
- Failed allocation preserves prior publication. Failed release remains owned/accounted and prevents unbounded new growth; already held safe blocks may remain usable.
- Report layout/schema unchanged; management and finite-quota enforcement are different predicates. Existing L2 close branches stay intact.

## Concrete constraints to apply while implementing rev2
1. Rev2 C5 must NOT break existing explicit-budget C callers. Preserve `simpler_set_workspace_budget_ctx(ctx, positive_limit)` as a sufficient request for management; the additive no-quota staging entry is needed only for the new default route. Preserve validation/error semantics and document any necessary declaration. An unchanged signature alone is not compatibility.
2. Do not label the slot ordering a proven reachable production failure merely from the shared helper. Moving the final fact under the held lease is appropriate; the stale guard needs an actual monotonic identity proof and must preserve predecessor refs safely.
3. Rev2 E2 must cover every unbounded retained-failure path, including unresolved run references quarantined by ContextDestroyed, not only free/unmap failures. Refuse further *new blocks* once ownership becomes unprovable, without freeing quarantined memory or inventing a byte quota. Recheck the latch after pressure reclamation before allocating.
4. Backend release currently collapses unmap/free to one int. Implement a concrete non-reentrant disposition result or equivalent: do not call back into the ledger mutex from its backend release callback. Propagate failure once, never retry during ordinary reclaim; terminal sweep rules remain explicit.
5. The actual retained_temp and arena fast paths must check quarantine/release state as well as capacity. `WorkspaceManager::reference` currently rejects quarantine but not release_unconfirmed; prevent a presumed reuse from handing out failed-release storage. Do not claim the shared acquire loop alone proves all callers safe.
6. Return reclamation errors through existing cleanup precedence without replacing an earlier execution/validation error; no device free after failed attach. Compaction only removes proven fully released records, not anything still used by terminal sweep/current-region ownership.

## Verification
New meaningful cases may run once; no existing or already executed case reruns, no broad pytest/ctest. Compile/static checks permitted. Review production routing, init rollback, consumer order and failure-admission integration, not just the manager in isolation. No CI rerun, merge, or takeover of another worktree.
