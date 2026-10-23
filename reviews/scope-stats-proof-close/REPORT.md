# 普通 ScopeStats：按停机证明关闭

PR [#2684](https://github.com/hw-native-sys/simpler/pull/2684)，最终候选 `5a1c5c0898691776ed307f0e560cd62609f50060`，基线 `71077b04a5e4dcd5dcae55a0fa262b3d59c93f4d`。消费已合入 #2677 的 host stop 协议。完整代码验证与 Claude 复审在 `77aba9a8a` 完成；最终 head 只同步 A5 测试说明中的一个词，实现、测试体和构建输入不变。**actual Claude 最终 approve；exact-head CI 全部通过。PR 已于 2026-10-23 04:34:53 UTC 合入，merge `62565feb3b14553ed736aab1eb008c5785bcf330`；以下验证保持原 head 归属，不移挂到 merge。**

## 交付行为与边界

- 两个 runner 的普通 close 消费 execution-complete 或独立 producer-stop 证明。有证明时维持 quiesce、held 恢复与 resident threads；均无证明时使用 stop(false)，完成已 handoff 的 host 收集后 join，不追赶迟到 publication、不读取最终 shared/held device 状态。
- 未知关闭只导出本次 host-owned records；fatal/drop/total 为 null，沿用 partial_cut_unknown / counts_unknown，host received/retained 分列。容量取 admission 值快照，四项一起已知或一起 null。空 host window 不造 artifact，并精确移除同 prefix 的旧 ScopeStats JSONL，保留其他文件。
- 普通 admission 独立保存 optional epoch（含 producerless required=false），拒绝并报告重启后旧 epoch 的 queued records。无 begin_run 的 legacy 直接 collector 保留原行为。实际 onboard failed fence 会 poison runner，内部 collector restart 测试不表示允许第二次健康公共硬件调用。
- checked 正常 metadata 在导出前冻结，保留七 key 格式与 HBG producerless metadata-only。HTML 不把未知统计当 Healthy/0；保留 positive fatal，按 epoch/ring/site 配对，不跨 run 拼出假峰值。legacy 缺 epoch 保持兼容。
- retained quarantine/settlement、device free/abandon 和计算错误优先级不变；host join 或 collection verdict 不授予设备释放权限。onboard 无独立 producer-stop 正证明，该正控来自 sim/CPU seam；正常 silicon 走 execution-complete。

## 当前完整资格

| 检查 | 结果与归属 |
| --- | --- |
| 全量 C++ UT | `77aba9a8a`：394/394，70.50 s |
| 全量 Python UT | 同 head：3549 passed / 192 skipped，204.71 s |
| A3 / A5 sim | 65 / 61 个 resource sessions，各含两 runtime L2 组，全部通过 |
| 聚焦 boundary | 四 architecture/runtime 目标 × 15 cases = 60 passed；邻接 collector/read-failure/snapshot 通过 |
| 实际 JSONL→HTML | 12 passed；正常七字段 fixture HTML 与基线字节相同 |
| A3 task-submit | public ScopeStats 4 cases、ScopeStats scene、普通 sweep 154 sessions + 两组 L2（43/1、60/0）通过；sweep 633.23 s |
| 现场身份 | devices 0–3；20 份源码/实际 runtime hash、HEAD 前后一致；保留 occupancy 和设备日志；任务退出释放锁 |
| 其他 | axis、strict docs、全变更适用 hooks 通过；最终 docstring 单文件 hooks 另通过 |
| 独立 review | actual Claude 对完整 `77aba9a8a` approve；最后 docstring 修正已实际独立复核 approve |
| 最终 CI | `5a1c5c089` [run39889814912](https://github.com/hw-native-sys/simpler/actions/runs/39889814912) attempt 1 completed success；18 check-runs success / 1 expected deploy skip，另有 CodeRabbit status success。旧 head 检查不移挂 |

四个 CPU 目标在 HBG build 中也注入真实 ScopeStats producer，只证明共同 boundary；不声称 HBG 存在设备记录 producer。不在 silicon 人造 hang，无本地 A5 silicon、性能收益或 AIV 根因/发生率主张。

## 负对照和独立审阅

1. 基线 `07eb531d5`：真实 runner/AICPU producer 四目标 48 cases，16 个 proof/default-drain/producerless 正控通过，32 个 unknown-close 断言失败。有限 gate/predicate 控制迟到发布，不用 sleep 排序或无限 producer。detached-shadow probes 检查禁止的 terminal/held 读取。最初错误 CMake target 名未进入执行，不计作产品失败。
2. 组合后 48 cases 通过。snapshot 回归在正确目标的基线失败，修复后不再导出被修改的 host mirror。初次误改 device pointer 而未改 detached host shadow 的探针不是有效负对照，修正后才保留红结果。
3. 首轮 `25d31c653` 虽完整 UT/sim/板测/CI 均绿，actual Claude 仍 Request changes：旧 epoch restart 污染、空 unknown 残留旧文件及文档漂移。新四目标回归在该 head 48 旧正控通过 / 12 新断言失败，修复后 60 通过。
4. 父级展示复核增加 positive fatal 和跨 epoch 配对回归。真实 HTML 各先红后绿；原 140-slot 假峰值消失，真实 40-slot 峰值保留。
5. 完整复审重新逐项推导并批准；仅余 A5 测试说明的一词同步，归同一产品提交。见[首轮](claude-review-25d31c653.md)、[代码复审](claude-recheck-77aba9a8a.md)、[最终 docstring 复核](claude-final-doc-recheck-5a1c5c089.md)及[意见处置](review-disposition.md)。

## 并行成果与验收

Boundary 子项 `2f29ae9a` / `0b754ea2` 仅拥有测试和 CMake；plot 子项 `24b713fa` / `3efa6e62` / `6f93d00f` 拥有实际渲染及 Python 回归。主执行读原始 red/green，集成 collector/runner/snapshot/docs，修正子项最初误用的 C++ enum 名为真实 wire token，统一资格。另一个 agent 只读监控 exact-head CI，不能代替 actual Claude。所有必要修复集入一个功能 PR，不按 agent 机械拆 PR。

## 历史与证据保留

`25d31c653` 的历史成绩：C++394/394，Python3533/192，A3sim65/A5sim61，A3 task-submit public4/scene/154sessions+L2，18 hash 前后一致；[CI39885375125](https://github.com/hw-native-sys/simpler/actions/runs/39885375125) 19 success / 1 deploy skip。其 Request changes 和验证均保留原 head，不覆盖最终审阅。中间 `77aba9a8a` 的 CI 独立封存，不作为最终 head CI。

原始 red/green、单测/sim/板测日志、hash、occupancy、实际 Claude JSON 和 CI JSON 本地按 head 独立归档并列 SHA256；fork 设计备份只发布本报告与文字 review，不上传源码、设备现场、原始日志或凭据。产品 PR 不含规划 HTML/报告。

本地最终原始证据包：`final-qualification-77aba9a8a-doc-final-5a1c5c089.tar.gz`，769 个文件、2,039,684 bytes，SHA256 `509d266f2e9648a228c181c99e6f71c59153c6f206334db70e92a13db7dd9ed8`。包含 device logs、occupancy、20-file 身份、实际 review 和最终 CI JSON；只留本地，不上传 fork。初轮25证据包独立保留，不覆盖。
