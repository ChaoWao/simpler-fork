> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# PR #2455 — 53a271a17 manager review

2026-09-25 UTC。精确 head `53a271a17cf95b27bdb7a185cfff4b9bb1a38a93`，base `c605b03ca`。结论：READY；当前 head 静态审查通过、反馈处理完毕、CI全部完成。

## 本轮修正

相对已审 d02117281，检查全部17文件增量，并沿此前累计评审核对 ArgsDump 合同。`DrainOutcome` 显式分离 device_rc 与 diagnostics_rc：onboard wait/finalize 仅以 device_rc 记录 DrainProvedComplete，以 combined() 向调用方报告错误，设备错误优先。默认结果为内部设备错误，attach 失败/异常/无 active_execution 不会误记完成。

四个 onboard/sim override、两个 onboard C API、两个 sim C API、retention probe 和 sim test double 已全部迁移。返回类型变化阻止旧调用点默默丢弃诊断错误；sim 与 probe 使用 combined()。a2a3 的边界 discharge/stream retirement 成功后才返回 device_complete；a5 保持既有完成判据。C ABI、reset、workspace retirement 谓词及默认启用策略不变。原诊断失败导致已完成 run 工作区永久 quarantine 的阻塞已解决。

远端两个线程均已解决；作者更正“仅保留到 context 销毁”的错误说法，并撤回用旁证认定 A5 失败原因的过强结论。旧 head A5 失败不当作新 head 失败，也没有重试旧 job。

## 验证范围

作者报告新增独立目标两个 case 各首次执行通过，八个 host runtime 构建/链接成功；manager未运行测试。源码检查显示用例调用生产 WorkspaceManager 和 DrainOutcome，但测试自己的 settle_run 重现 C API 的 fact 发布过程，**并未调用生产 C API**。因此它验证状态模型及错误合成，不独立覆盖 C API 接线回归；“旧规则会失败”只能指改变测试内发布规则，不能解释为已实测旧产品提交失败。生产接线结论来自逐调用点静态检查与编译，而非该用例直接覆盖。

已执行 case 均已消费，不重跑。既有 probe 未发布成功 drain fact 的问题已由作者记录为前存问题，本 PR 保持其 combined-error 报告语义，不扩大当前范围。

2026-09-25 12:00 UTC 复核：当前 GitHub CI 19成功、1跳过，全部完成；三面反馈无新增阻塞，两个线程resolved，达到Ready。#2446的旧A5重试问题仍独立等待用户授权，无POST。

## User merge confirmation — 2026-09-28

GitHub confirms merged as `8cf4ed4c001fbee52a2cf5b0eb5e2722674277dd` at 2026-09-28T02:35:07Z. Reviewed head remained `53a271a17cf95b27bdb7a185cfff4b9bb1a38a93`, all blocking feedback resolved and CI19success/1skip completed before merge. No manager merge or new tests performed. Original temporal session now owns a finite design-only audit for the next DFX deliverable.
