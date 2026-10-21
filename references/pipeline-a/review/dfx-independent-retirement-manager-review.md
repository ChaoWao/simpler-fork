> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# DFX complete-function design review

Revision required. Accept the identified couplings: independent host output must retain bytes and flush watermark after bucket retirement. Do not implement yet.

R1 — User's new complete-feature contract supersedes the assignment's prior independent-PR assumption. Remove the invented requirement that2534 must merge first because files overlap or existing execution evidence would be superseded. Same author can revise the same PR; previous execution remains historical, new code requires relevant current-head validation. Review2534 against the complete selected user operation. Either prove current bounded backpressure already fully delivers it and the proposed change is an independent optimization, or include necessary changes in the same feature design. Do not make a merge prerequisite from file overlap. Also correct stale evidence:8300 has actual A3 four-case execution evidence, not only307.

R2 — The proposed single-thread depth1 publication queue gives only one extra admission while a write stalls; it does not allow B's seal to progress during A's disk write. Do not call that complete receive/output decoupling or use 'raising the constant later is easy' to justify another fragment. State the promised user operation and show A/B/C/D continuous flow and bounded backpressure. Choose minimum COMPLETE design; no requirement to add threads unless its behavior needs them.

R3 — Flush/close must count an actively writing entry as well as entries still in deque. Specify dequeue→write→terminal/error publication→destroy/credit ownership and predicates, no mu held over blocking disk. If entry remains logically in queue during write, specify concurrency/teardown exclusion. Include allocation/move/queue admission failure, cut_release timeout after sealing, Partial/CutUnknown/Quarantined, write failure and shutdown ownership. Queue publication and bucket release must be commit-safe. Do not claim byte-identical/public behavior unchanged while admission and loss timing may change; identify semantic and schema compatibility separately.

R4 — Fixed/resident and merge scratch charge must be stated consistently. 'total record bytes <= budget' conflicts with the acknowledged insert-copy transient; either account the necessary live peak in the chosen design or explicitly qualify the resource contract and its bound. Do not silently exempt a newly extended lifetime as pre-existing. No tests/build now; specify true public API acceptance for whichever new user-visible behavior is proposed, not just old unchanged scenes.


## R2 delivery review — current8300, 2026-10-13

Full r2 result/design/decision read. Accept withdrawal of mechanical merge-first dependency and one-thread one-entry pseudo-decoupling. Existing real 16-run trace/file evidence supports the bounded selected capability. Two-thread optional optimization needs its own measured justification, no implementation now.

Blocking factual correction before adopting the report: reserve_run_records in src/common/platform/include/host/chip_swimlane_collector.h:1889–1923 already explicitly charges twice each retained record allocation (`checked_bytes(want - dst.capacity(), 2 * sizeof(T), &bytes)`) before reserve; comment explicitly pays for merged copy. Therefore proposed limit()+B_max "correction" double-counts that copy. Withdraw the requested #2534 code/doc edit based on this premise. Trace call sites before making a replacement byte-bound assertion; distinguish application accounted payload/storage from RSS/allocator overhead. No new product change is justified by finding a merge copy alone.

Decision's every-writer-step-bounded / loop-always-completes phrasing also overstates the detailed design, which explicitly says filesystem write is unbounded. State eventual healthy writer progress as assumption, finite flush timeout/fatal as reporting boundary, and close/join limitation honestly; no new timeout architecture required. Public API snippet must actually attach cfg to the registered/submitted graph and enable collect_across_runs; otherwise mark schematic, not runnable. Four successful methods prove normal scope, not every injected failure. Final concise response only these corrections; preserve complete capability decision.


## R3 accepted 2026-10-13T03:26:33.670582+00:00

Full result and corrected contract read; twice-charge call sites confirmed, spurious #2534 amendment withdrawn, filesystem liveness/flush/close limitations and schematic API corrected. Optional optimization not requested for implementation. #2534 exact8300 complete-feature review accepted with disclosed blocking-I/O limitation; current checks19success1skip and three feedback surfaces clear. Ready, originaltemporal idle.
