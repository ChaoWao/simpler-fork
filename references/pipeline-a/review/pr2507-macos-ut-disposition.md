> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# #2507 macOS UT：历史失败已由 #2509 修复

核对时间：2026-10-09T06:54:01.218690+00:00。

## 失败证据

#2507 head603be21c，workflow37768447086，macOS job113283523566。原始日志显示 CTest 297个目标中1个失败：test_a2a3_scope_stats_retained_runs，具体用例 ScopeStatsRetainedRuns.ConsecutiveRunsEachPublishTheirOwnRecords。run4502准入被拒，日志明确 both unpublished export slots are in use；fx.begin返回false。其余同套用例不是这次失败原因。

## 原因及修复

测试连续结束两个run后立即准入第三个，未等后台writer发布前两份输出。结束采集不等于文件已发布，因此两个未发布额度可能仍占用；准入拒绝符合生产合同，测试错误依赖writer恰好已调度完成。

#2509只修改该测试：先暂停writer以确定占满两份，再恢复并flush，确认published=2后开始第三个run；原来各run记录数量与隔离断言保留。未增大生产额度或改变运行时逻辑。

#2509已合入main60167bcc1，当前远端main仍为此提交。实际下载修复PR macOS job113300911649日志：A2/A3、A5 retained-runs目标分别通过，296/296 CTest目标通过。不能把#2507旧job红灯改写为绿灯，但当前main已包含对应修复。无须再派重复作者或重跑已合入#2507的历史job。

[历史失败job](https://github.com/hw-native-sys/simpler/actions/runs/37768447086/job/113283523566) · [修复PR](https://github.com/hw-native-sys/simpler/pull/2509) · [修复后macOS job](https://github.com/hw-native-sys/simpler/actions/runs/37773768949/job/113300911649)
