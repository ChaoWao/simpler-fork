> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# PR2419 published rebase review — 4b2c797ee

Exact published head 4b2c797eedfd1ec095d3e3debffe2ea861bf41a9, base98c7c176457e0396f21cfaafc8cad256487cee3b. GitHub OPEN/MERGEABLE. Final tree 1a011e669cd44f1b677f6f2db8940fc2c8a43f72 equals reviewed local ee1114350 exactly; final amendment restores commit-message headings only. The migration and production review in p4-2419-ee1114350-rebase-review.md applies unchanged.

Static PASS, all prior manager blocking feedback remains addressed. Fresh feedback retrieval: four resolved inline threads, nine review summaries, one bot conversation; no new actionable request, complete pagination including nested thread comments. Initial CI3success/1skip/1running, so NOT READY; old-head hardware evidence does not replace current-head CI.

Original80765 delivered and reaped exit0. Author reports configure, compile/link, registration listing and static checks only, no test/probe execution or manual CI retry. Native exports and all three relocated cases retain coverage. Manager did not build or run tests. Old-head exact A5 UT retry authorization becomes superseded and remains unconsumed; not transferred to this head.

## Current-head hardware evidence — 2026-09-22 continuation

Read-only download of A3 job106584427506 verifies head4b2c797eedfd1ec095d3e3debffe2ea861bf41a9, completed SUCCESS. Raw log in claude-managed/p4-2419-4b2c797ee-a3-job.log and job metadata JSON. Actual nodeids show:

- TestEarlyEnqueueDepthTwo::test_two_runs_are_launched_and_accepted_at_once PASS 5.8s.
- TestEarlyEnqueueDepthTwo::test_sustained_refill_overlaps_successive_run_pairs PASS 5.3s.
- TestEarlyEnqueueDepthOneControl::test_no_overlap_without_the_opt_in PASS 15.9s.

Generic test_run rows show pytest skip despite wrapper PASS and are not cited as acceptance. Strict dedicated real-submit assertions are the acceptance evidence. Current total18success/1skip/1macOS-sim queued; not yet Ready. No tests rerun or retry POST.

## READY — 2026-09-22T03:12:40Z

Current head unchanged 4b2c797eedfd1ec095d3e3debffe2ea861bf41a9; all current checks completed19SUCCESS/1SKIPPED. Fresh three-surface feedback fully paginated:4resolvedthreads/9reviews/1conversation, no pending blocker. Exact-head manager staticPASS and dedicated current-head A3 early-submit/sustained-refill/default-depth evidence above. READY awaiting user merge; original80765 idle, no new task or automatic merge. [Current CI](https://github.com/hw-native-sys/simpler/actions/runs/35676412309).
