> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# #2446 A5 CI disposition at 46848ba18

Job108026299250 / workflow36120681665 is the only failed check; 18success/1skip. Head unchanged and static review passes. Original P4 completed finite read-only audit; no product changes, tests or CI requests.

Accepted evidence: the measurement uses direct native probe calls, not ChipRunLane admission; empty tensor arguments do not exercise new caller-buffer borrowing. The measured result read/fence functions are unchanged. Sibling #2455 same-base fails the same case, and an earlier #2446 head with the production feature passed. These observations support **no direct defect found in this PR**, not a causal exclusion or proof of flakiness. Every predecessor result was valid; one attempt had no initial overlap and four completed across the read.

The P4 audit's stronger 'attribution excluded' conclusion is not adopted. TMR's longer workload and passing control do not distinguish causes of an HBG completed-across-read observation. Device wall duration is not remaining work at the moment host begins reading. Proposed no-successor timing comparisons would add measurements but still would not establish causal waiting by themselves. No assertion/workload/attempt change is approved here, nor any new test run.

User question submitted for exactly one retry of this job; authorization PENDING, no POST. Ledger: ../claude-managed/ci-retry-2446-108026299250.json. Original P4 idle awaiting that decision. Root cause unknown; Ready remains false. The old retry ledgers do not authorize this request.
