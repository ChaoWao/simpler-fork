> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# PR2571 931d current-head hardware acceptance evidence

Head931d59d408eda0babacc5bde8f593d695cac5ce6, test merge e0179cf16955fffddec1171c17c9dc64ca571496. GitHub commit parents independently verified as main620807d2d and head931d. Both complete logs checked, no local tests or retries.

- A3 onboard115905104831: 320570-byte full log. HBG and TMR five-launched/accepted cases and 8-held/4-launched/64-submission cases each actually PASS, not collection-only or skipped test_run. New TestA2a3TmrWideOutstandingClose::test_close_drains_a_wide_unwaited_prefix PASS8.4s.
- A5 onboard115905105190: 202224-byte full log. HBG and TMR five-launched/accepted cases and 8-held/4-launched/64-submission cases each actually PASS. DFX smokes completed successfully.
- Evidence files: ../claude-managed/pr2571-931d-a3-onboard.log, pr2571-931d-a5-onboard.log, pr2571-931d-testmerge.json, current jobs snapshot.

This supports current-head capacity/turnover and the specific wide-close case. It does not complete the missing DEVICE/failure/multi-endpoint/scheduler recovery coverage or diagnose the earlier missed A5 window. Docs strict build still fails at missing nav; original author is fixing it in current WIP. CPU CI remains underway. Not Ready.
