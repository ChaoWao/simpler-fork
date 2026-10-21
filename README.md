# Simpler 运行时设计资料

2026-10-21 的设计与进度快照。此分支 `design/kernel-runtime` 专门保存规划资料，不进入产品代码 PR，也不代表设计已全部实现。

## 从这里开始

| 阅读目的 | 文件 |
| --- | --- |
| 判断设计是否正确 | [运行时设计原则](runtime-design-principles.md)，唯一规范性来源 |
| 看实施顺序和当前状态 | [ROADMAP](ROADMAP.md) / [路线图 HTML](kernel-mode-roadmap.html) |
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

- 最新路线状态固定 main `7d27e610411c3219137d492eafcb0342bad681d7`，含 #2619/#2620/#2628/#2600/#2638/#2640/#2645/#2643/#2588/#2649/#2648。API 层次的完整源码清单仍保留页面内注明的固定基线，不能把本轮模块寿命与路线更新当成 API 全面重审。
- #2638/#2640/#2645/#2643/#2588 已合入，历史测量保留原 head。[#2649](https://github.com/hw-native-sys/simpler/pull/2649) 已合入 `0cf537df1`；审阅 head `e768fab28`（功能验证树 `8f89cc639`；最终差异仅旧注释修正）：Runtime descriptor 携带 swimlane level；保留 #2588 的 HostApi 调用快照、PMU 布局与诊断准入。rebase 后 C++ 378、Python 3501、两架构各 6 个 RTS 桩探针、A3 9 个实板 case、A5 3 个 DFX sim 通过；无本地 A5 silicon 主张。inline 已修正并 resolve，最终 head CI 19 success / 1 预期 skip。macOS A5 首轮 600 秒总 session 超时；只重跑后通过，代码与预算未改，详见证据记录。见 [本笔边界](kernel-eager-open-work.md#c1-swimlane-invocation)。历史 [#2642](https://github.com/hw-native-sys/simpler/issues/2642) 仍待根因诊断，不能据旧日志豁免新 CI。
- 当前待审阅 [#2653](https://github.com/hw-native-sys/simpler/pull/2653)，head `dcdb55ef2`（原功能/全量验证 head `08d239293`）/ main `7d27e6104`：本次 PMU event group 使用已有 effective DFX 快照，经 descriptor 发布，AICPU 显式消费并复位本次 accounting；host admission 不再改共享 header/counters。C++ 378、Python 3501、A3/A5 普通 sim 105/102 项通过；task-submit A3 普通 sweep 238 passed / 20 skipped，另 public/DFX/probes 通过。最终提交重建后 4 个 PMU public case 与 6 个探针通过。首次 owned-HBG AIV UB-address 故障仍未分类；同设备复跑、main/candidate 交替小样本均通过，不能据此宣布无回归。原 CI 18 success / 1 failure，失败仅两架构 DFX-off 未使用 runtime 参数；修订各加一行 `(void)runtime;`，DFX-off 构建/冒烟和恢复默认后的 DFX-on 冒烟均通过，新 CI 待验收。无本地 A5 silicon 主张；原 macOS 通过归旧 head。见[本笔合同与证据](kernel-eager-open-work.md#c1-pmu-invocation)。
- 当前未合入的候选实现：TMR #2624/#2625，shared resources #2626，HBG #2646/#2647（后三项 draft）。框架 #2605 的 128-token 输出仍不一致。候选代码和主线资格分别记账。
- `SOURCE_BASELINE.md` / `source-baseline.json` 是 **2026-09-18 的历史核查**；共同执行、TMR 与参数设计中的源码事实亦保留各自基线。最新交付进度以 ROADMAP / kernel-eager-open-work 为准，不能把旧事实表当当前缺陷清单。
- 历史报告保留原 commit、测量和限制，不能因上传日期更新就当作当前 head 的验收。
- `references/pipeline-a/` 是依赖文档的只读快照，用于理解共同原则和交接边界；不建立第二份独立维护的 Pipeline A 计划。
- 文档里的“只留本地”指不进入产品代码 PR。本分支是用户授权的文档备份例外，不改变产品 PR 范围。

## 后续更新

后续重要设计文档更新并完成校验后，同步备份到本分支，无须每次重新询问。使用增量提交保留历史，仍不进入产品代码 PR。详见[持续备份约定](PUBLISHING.md)。

## 收录和省略

收录设计正文、预生成 HTML、文字证据报告与少量依赖文档。未上传 backup、实验脚本、原始日志、设备日志、trace、二进制、构建缓存或完整源码快照。

为使阅读独立于本地目录，收录文件之间的链接已重新定位；未收录的证据链接指向 [本地证据索引](LOCAL-EVIDENCE.md)，明确说明材料没有上传。绝对工作目录和内网地址已泛化，原本地文档不改动。

[PUBLISH-MANIFEST.json](PUBLISH-MANIFEST.json) 记录每份来源与发布文件的 SHA256。正文除链接适配、路径泛化和行末格式外保留原内容；不借此重写历史结论。
