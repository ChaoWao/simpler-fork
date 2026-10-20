# Simpler 运行时设计资料

2026-10-20 的设计与进度快照。此分支 `design/kernel-runtime` 专门保存规划资料，不进入产品代码 PR，也不代表设计已全部实现。

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

- 最新路线状态固定 main `7e8038e9aae4872e7ba7515d0a66ee05687eee05`，含 #2619/#2620/#2628/#2600/#2638/#2640/#2645/#2643。API 层次的完整源码清单仍保留页面内注明的固定基线，不能把本轮模块寿命与路线更新当成 API 全面重审。
- #2638/#2640/#2645/#2643 已合入，历史测量保留原 head。当前待合入 [#2649](https://github.com/hw-native-sys/simpler/pull/2649)，head `4e4fdaee9`：每次 Runtime descriptor 携带 swimlane level；A5 HBG phase 存储、RunControl 与导出使用同一值。最终 C++ 340、Python 3495、两架构各 6 个真实库探针通过；A3 普通 sweep 236 passed / 20 skipped、最终 head 定向上板 7 项通过；A5 bind/DFX sim 另记，无本地 A5 silicon/macOS 通过主张。CI 待验收。见 [本笔边界](kernel-eager-open-work.md#c1-swimlane-invocation)。历史 [#2642](https://github.com/hw-native-sys/simpler/issues/2642) 仍待根因诊断，不能据旧日志豁免新 CI。
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
