> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# Memory candidate review18 — fail-closed guard and reservation lock lifetime

Design only, baseline44178c9f7. Accepted review17 corrections: explicit default-off, pre-reserving both ownership records, honest unknown reset reclamation and best-effort logging after state progress. Remaining concrete issues are in the newly specified mechanisms, not reopening the overall direction.

F1 required guard currently fails open. workspace_report returns false for runtime rc errors/invalid schema/empty ctx, not only absent optional symbol. Python `rep is None` returns and allows Buffer teardown even when budget was successfully enabled. Track enabled/required capability independently; resolve BOTH setter and report capability before enabling; after enabled, absent/invalid/query-failed report must block protected teardown, not imply disabled. `startup_abort` currently continues past gate error too: specify safe partial-init exemption from actual ownership facts, otherwise preserve protection on that path. Give enabled+queryerror and enabled+startup-abort traces. Observation-only teardown report precedent is not sufficient for a mandatory safety gate.

F2 reserve_tracking_slot() and alloc_with() are separate calls each locking mu_, but proof asserts no other writer between reserve and insertion. Existing non-Module MemoryAllocator::alloc remains unchanged and can fill capacity between them; multiple outstanding TrackingSlots also reserve size()+1 without accounting others. Explicitly carry the same lock via move-only reservation until commit/cancel (take before device call), or reserve all outstanding capacity under a concrete invariant and ensure later writes preserve it. State lock ordering/latency consequence honestly; don't just mark node insertion noexcept. No API split that silently releases the needed lock.

F3 finalization result storage: step1 records then clear then step3 writes BackingBlock. Specify where result survives clear without allocating; map currently maps ptr→size, no result fields, and non-Module allocations can be present. Prefer recording Module results directly into existing blocks BEFORE clearing plus bounded aggregate for other allocations; no invented per-map-entry buffer built during cleanup. Keep clear guaranteed and logs afterwards.

Revise current artifact in place, concise. No implementation/build/tests/PR, no broad research. Return exact corrections then idle.
