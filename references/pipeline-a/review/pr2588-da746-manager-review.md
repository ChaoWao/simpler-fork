> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# PR #2588 da7460e15 incremental review

## Disposition

Not Ready. Reviewed 20 DFX files against b2a7; the other 11 changed files match the #2595/#2597 base advancement and are not new DFX implementation. New base e994e106d, one commit. Current CI workflow 38869839635 is in progress, not a qualification result.

Accepted statically: independent producer_note_close at offset 52 in the same 64-byte terminal; reset/claim and both arch readers/writers updated; unchanged PMU register writes and strict nonzero-cycle assertions. Extension publication now requires the matching Admitting bucket, rejects null and charges before copying caller bytes. No root-cause repair is established. Author compile/link/lint results are reported evidence, not manager-run tests.

## Evidence corrections required

1. The author's design/result says every one of 72 cores read exactly 0x2. b2a7 lost=72 is a predicate count combining init state and close enable bits; only the first payload and task-producing cores have printed raw words. It does not establish all 72 raw values or prior enable. Remove that overclaim.
2. Same init/end enable bits do not prove enable held throughout the run. A disable/re-enable interval remains possible. docs/dfx/pmu-profiling.md currently says carried across the whole run; fix at the next substantive publish.
3. The three proposed outcomes are neither exhaustive nor uniquely causal: mixed states, missing close notes and mappings remain possible. Exactly three losses matching active cores supports the per-task-gate hypothesis but does not prove register aliasing. Do not select a repair from counts alone.
4. Correct current design's stale no-CI and base row. Full b2a7 final results include macOS20min and A5UT30min timeout annotations; successful Python/CTest subsets do not close their tail coverage. See temporal-b2a7-macos-pending.md.

Preserve strict acceptance and serial device execution. Await the new head's A3 evidence rather than guessing register operations. No local tests/retry/merge performed.

## A3 CI 增量：初始化与结束状态不同

当前 da746 的 A3 onboard job 116872397143 最终 checkout 为 2b41e84d（da746 与 e994 的测试合并）。HBG device 2 第二次运行 epoch 2 的 513 条 PMU 记录全零；同 job TMR device 3 通过。epoch 1 的初始化/结束均报告 72 核使能；epoch 2 初始化 72 核使能、结束 72 核未满足使能谓词。首核及三个有任务核打印 0x7→0x2；没有输出的核不能推定原始值也为 0x2。

源码核查：HBG scheduler 的 shutdown 在 retire_cores 前调用 close_run_cores 和 finalize；PMU finalize 对每个逻辑核先读取结束状态，再用 init 保存值恢复寄存器。core ownership 注释声称分区；仍需核调用次数、物理窗口是否唯一及跨线程退出次序，不能单靠注释排除别名或重复恢复。AICore per-task CTRL gate 与 AICPU MMIO PMU_CTRL_0 是不同代码接口，不能未经硬件证据就视为同一寄存器。现有证据没有定位使能变化来源，保留严格数值断言，不重试掩盖。

日志：claude-managed/pr2588-da746-a3-onboard.log。当前反馈保存给唯一原作者；未重复启动或运行本地测试。
