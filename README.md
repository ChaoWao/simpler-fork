# Simpler 运行时设计资料

2026-10-23 的设计与进度快照。此分支 `design/kernel-runtime` 专门保存规划资料，不进入产品代码 PR，也不代表设计已全部实现。

## 从这里开始

| 阅读目的 | 文件 |
| --- | --- |
| 判断设计是否正确 | [运行时设计原则](runtime-design-principles.md)，唯一规范性来源 |
| 跟踪远端实施与最终验收 | [Issue #2666](https://github.com/hw-native-sys/simpler/issues/2666)，与 ROADMAP/HTML 同步 |
| 看统一顺序、当前状态与验收门 | [ROADMAP](ROADMAP.md) / [路线图 HTML](kernel-mode-roadmap.html)：runtime K/C、四仓 I1–I3、普通 Async V1 |
| 看四仓接管来源与候选归属 | [接管说明](kernel-mode-cross-repo-handoff.md) / [HTML](kernel-mode-cross-repo-handoff.html) |
| 获取四仓协作任务 | [提示词](kernel-mode-cross-repo-prompt.md) / [HTML](kernel-mode-cross-repo-prompt.html) |
| 看下一步的 PR 边界 | [Kernel eager 剩余工作](kernel-eager-open-work.md) |
| 理解 C++、so、Python 层次与收口方向 | [API 层次与审查 HTML](current-api-layers.html) |
| 理解 program/kernel 调用与 capture 目标 | [共同执行协议](kernel-mode-prepare-submit-design.md) / [架构与调用流程 HTML](kernel-mode-unified-design.html) |
| 理解 TMR 顺序与 replay 合同 | [TMR 时序](kernel-mode-tmr-flow.md) |
| 理解统一 Tensor/Buffer 参数模型 | [参数设计](argument-domain-policy.md) / [参数设计 HTML](simpler-argument-design.html) |
| 理解 local/remote、HOST/DEVICE 与 transfer | [跨 endpoint 合同](endpoint-access-contract.md) / [映射合同](argument-mapping-contract.md) / [术语](CONTEXT.md) |
| 查证据和历史纠错 | [证据索引](kernel-mode-design.md)、[reviews](reviews/)、[probes](probes/) 的文字报告 |
| 理解 AIV 尚未解决的证据缺口 | [现场取证规划](aiv-evidence-plan.md) |

HTML 已生成，可下载后用浏览器打开。GitHub 的文件页显示 HTML 源码；本分支没有启用 Pages 或修改站点设置。

## 快照边界

- #2671、#2677、#2684 已合入；#2684 merge 为 `62565feb3`，其资格仍归最终 head `5a1c5c089` 与代码等价 head `77aba9a8a`，不把历史测试挂到新 main。普通 ScopeStats 证明门、host-owned unknown 导出、restart epoch 拒绝与 HTML 消费的验证、Claude 评审及 exact-head CI 见[原报告](reviews/scope-stats-proof-close/REPORT.md)与[意见处置](reviews/scope-stats-proof-close/review-disposition.md)。
- [当前 Manager / Contributor 协议](manager-contributor-protocol.md)落实用户的明确要求：主 agent 只分析、派发、协调和验收；代码、测试、实验、文档、集成与发布均委派执行。接到交付后必须读取证据、接受/退回并派下一动作，不能以派发代替完成；未知发布结果先读远端再幂等重试，不承诺 final 后通知会自动开启下一轮。
- TMR `28a9de54a`、HBG `7d1dae515`、Common `dbfbaf877` 三项正确性贡献已实际 Claude 审阅、独立 CPU 验证并发布到自己的 fork；[精确父提交、比较、证据和下一 owner](reviews/capture-correctness-contributions/REPORT.md)。原 #2625/#2626/#2647 未更新，A5 fatal replay 及完整 capture 未签收，原 #2647 head80a 当前无 checks；这些贡献没有单独开微型 PR。历史 [Hub 讨论稿](references/pipeline-a/multi-agent-work-principles.md)不授权安装或启动任务。
- Kernel的远端跟踪为[#2666](https://github.com/hw-native-sys/simpler/issues/2666)，仓库重点工作总览为[#2670](https://github.com/hw-native-sys/simpler/issues/2670)。普通恢复的stop-proof、manifest未知loss表达及既有AIV分别记账。

- 历史路线记录固定 main `7503761a6c01a6d07ccd8d9c82b8060da843d1af`，含 #2653；#2660的代码和测试另固定 main `3b5836465`。完整API层次审查保留页内基线；当前候选验证见上条，不把历史只读核对或旧测量移挂到当前head。
- #2638/#2640/#2645/#2643/#2588 已合入，历史测量保留原 head。[#2649](https://github.com/hw-native-sys/simpler/pull/2649) 已合入 `0cf537df1`；审阅 head `e768fab28`（功能验证树 `8f89cc639`；最终差异仅旧注释修正）：Runtime descriptor 携带 swimlane level；保留 #2588 的 HostApi 调用快照、PMU 布局与诊断准入。rebase 后 C++ 378、Python 3501、两架构各 6 个 RTS 桩探针、A3 9 个实板 case、A5 3 个 DFX sim 通过；无本地 A5 silicon 主张。inline 已修正并 resolve，最终 head CI 19 success / 1 预期 skip。macOS A5 首轮 600 秒总 session 超时；只重跑后通过，代码与预算未改，详见证据记录。见 [本笔边界](kernel-eager-open-work.md#c1-swimlane-invocation)。历史 [#2642](https://github.com/hw-native-sys/simpler/issues/2642) 仍待根因诊断，不能据旧日志豁免新 CI。
- [#2653](https://github.com/hw-native-sys/simpler/pull/2653) 已合入 `7503761a6`；最终 head `dcdb55ef2` 的 CI 19 success / 1 deploy skipped，包括 DFX-off 构建。历史功能验证仍归 `08d239293`；首次 AIV 仍未分类，证据未因 CI 全绿撤回。详见[本笔合同与证据](kernel-eager-open-work.md#c1-pmu-invocation)。
- TMR eager #2624 已合入 `fdcbdcc95`；未合入候选为 TMR capture #2625、shared resources #2626、HBG #2646/#2647（#2646/#2647 仍 draft；#2626 non-draft）。框架 #2605 的 128-token 输出仍不一致。候选代码和主线资格分别记账。
- `SOURCE_BASELINE.md` / `source-baseline.json` 是 **2026-09-18 的历史核查**；共同执行、TMR 与参数设计中的源码事实亦保留各自基线。最新交付进度以 ROADMAP / kernel-eager-open-work 为准，不能把旧事实表当当前缺陷清单。
- 历史报告保留原 commit、测量和限制，不能因上传日期更新就当作当前 head 的验收。
- 原 Pipeline A manager 目录计划移除。137 份文字来源已保存于本地参考快照，公开副本排除 manager 会话流水及调度控制材料；完整 12,193 文件归档及原始日志仅本地保存。`references/pipeline-a/` 为冻结证据，后续实施顺序、状态和验收统一在 ROADMAP 维护；[四仓接管说明](kernel-mode-cross-repo-handoff.md)保存来源与候选快照。
- 文档里的“只留本地”指不进入产品代码 PR。本分支是用户授权的文档备份例外，不改变产品 PR 范围。

## 后续更新

后续重要设计文档更新并完成校验后，同步备份到本分支，无须每次重新询问。使用增量提交保留历史，仍不进入产品代码 PR。详见[持续备份约定](PUBLISHING.md)。

## 收录和省略

收录设计正文、预生成 HTML、文字证据报告与少量依赖文档。未上传 backup、实验脚本、原始日志、设备日志、trace、二进制、构建缓存或完整源码快照。

为使阅读独立于本地目录，收录文件之间的链接已重新定位；未收录的证据链接指向 [本地证据索引](LOCAL-EVIDENCE.md)，明确说明材料没有上传。绝对工作目录和内网地址已泛化，原本地文档不改动。

[PUBLISH-MANIFEST.json](PUBLISH-MANIFEST.json) 记录每份来源与发布文件的 SHA256。正文除链接适配、路径泛化和行末格式外保留原内容；不借此重写历史结论。
