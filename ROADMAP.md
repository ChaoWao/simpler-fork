# Kernel mode 路线与当前工作

**路线：共同执行基础 → Kernel eager → Capture / Replay → 真实业务交付。** TMR、HBG 都必须完成；eager 可以先交付，不等待 capture。

状态快照：2026-10-23 UTC，当前主线 `62565feb3`（含 #2684 已合入）；历史测量保留各自 head。总跟踪 [#2666](https://github.com/hw-native-sys/simpler/issues/2666)；设计判断以[运行时原则](runtime-design-principles.md)为准。

## 1. 现在做什么

| 工作 | 当前状态 | 下一动作 |
| --- | --- | --- |
| 普通 args-dump 错误关闭 | [#2677](https://github.com/hw-native-sys/simpler/pull/2677) 已合入 | 已交付停机证明、host-only 导出与未知丢失统计；[证据与边界](reviews/args-dump-stop-proof/REPORT.md) |
| ScopeStats 错误关闭 | [#2684](https://github.com/hw-native-sys/simpler/pull/2684) 已合入 `62565feb3` | 已交付证明门、host-owned 导出与未知统计展示；[原 head 资格与证据](reviews/scope-stats-proof-close/REPORT.md) |
| TMR / HBG / Common capture | 三项正确性贡献已审阅并发布到 fork；原 #2625/#2626/#2647 未更新，完整 capture 未验收 | 原 owner 接入各自精确父提交上的修复后重新取目标 CI/RTS/硬件证据；A5 fatal replay 阻塞未关闭；[贡献与资格边界](reviews/capture-correctness-contributions/REPORT.md) |
| I1 真实框架接线 | 入口映射已完成，实际队列接线未签收 | 对齐 PyPTO #3100 的初始化 owner，接真实 queue callback → Worker.submit；[映射](reviews/kernel-i1-entry-mapping/REPORT.md) |

**当前调度：** TMR、HBG、Common 修复均经独立评审与 manager 签收、委派发布；下一门是原 owner 接入与 feature 资格，I1 保留接口协调。Manager 只分析、派发与验收，执行全部委派；收到结果后须完成“读证据 → 接受/退回 → 派下一动作”的[闭环](manager-contributor-protocol.md)。

## 2. 依赖关系

```text
A + S1/S2 → K1/K2 共同 eager → K2 完整资格 → C1 → C2 → C3(TMR/HBG) → C4
                    │                            ↑          │
                    └→ I1 eager → I2 eager → I3  S3 owner   └→ I1 capture → I2 → I3

A + 现有 Program/模型路径 → V1 普通 Async 系统验收（不等待 kernel/capture）
```

K2 的框架资格由 I1 eager 回填；capture 消费者须先具备对应 runtime 的 C1–C3，完整交付还须通过 C4。S3 的 native module owner 已交付，图的长期引用由 C2 接入。

## 3. 阶段看板

“已交付”限于该行能力；“待验收”仍需真实链路证明。PR 合入不自动完成整个阶段。

| 阶段 | 状态 | 还差什么 / 退出条件 |
| --- | --- | --- |
| A · Pipeline 共同基础 | 已交付批准范围 | kernel 复用自然排队、逐 run 完成和安全退休；模型组合/性能由 V1 验收 |
| S1/S2 · 描述符与准备/发布分离 | 已交付 | 单次调用重新准备，metadata 独立发布；不引入 PreparedCall/cache |
| S3 · 模块保活 | native owner 已交付 | 图引用仍由 C2 接入；最后一个 run/callable/graph 使用者退出前不卸载 |
| K1 · 借用环境与参数 | 共同 native/public 接线已交付 | 不接管 caller 设备，不隐式搬运 DEVICE tensor；两 runtime 复用同一 executor |
| K2 · caller 顺序与 eager 资格 | 接线已交付，整体验收未完成 | 真实框架的前序写 → 双侧执行 → 后序读；TQ=0/1、跨流、寿命、失败和 Program 回归 |
| C1 · 可捕获设备闭包 | 部分完成 | 异步 descriptor 与逐调用 DFX 已有增量；独立握手/复位、完整错误门和真实 executor 闭包仍待验收 |
| C2 · 图资源所有权 | 候选及空 region 修复贡献待接入验收 | 图保留 code、source、bank、event 和 tensor 使用引用；捕获/销毁/部分入队失败无早退役 |
| C3 · 双 runtime replay | 未签收；两 runtime 正确性贡献已发布 | TMR 每次 reset/orchestrate；HBG 每次 restore pristine image；重复运行不回 host 编排，逐次结果正确 |
| C4 · runtime 完整验收 | 未完成，必交付 | 换参、多图、DFX、超时/故障、容量、关闭和实测收益全部有证据 |
| I1 · PyPTO / 框架适配 | 已映射，接线与资格待完成 | 真实产物 → callable → Worker/native；eager/capture 分别验证，不另造执行器 |
| I2 · pypto-lib 用例 | 待验收 | 真实输入/参考，长短序列、alias/INOUT、KV/权重/MTP 跨调用状态正确 |
| I3 · serving 请求 | 待验收 | 固定可安装四仓组合；连续请求、bucket、取消/失败/close 与正确性、吞吐、长尾 |
| V1 · HBG × 普通 Async | [#2658](https://github.com/hw-native-sys/simpler/issues/2658) 待验收 | 真实自动准入/refill/DEVICE 反馈与长序列正确；同工作量和资源下比较性能 |

## 4. 并行边界与验收

<a id="cross-repo-delivery"></a>

| 可并行工作 | 前置与边界 |
| --- | --- |
| 完整行为开发 / 验证 / 集成发布 | 每个 worktree 一个写者，任务列精确文件范围；共享接口先协调。主 agent 不兼任实现者；测试、plot、CI 辅助不能冒充多条开发线 |
| TMR replay / HBG replay | 消费共同 C1/C2；两条分别验收，不能从 TMR 通过推定 HBG 通过 |
| I2 数据和参考 / I3 请求驱动与版本清单 | 现在可准备；实际运行消费对应模式 I1 和 runtime 能力 |
| V1 普通 Async 系统验收 | 消费 A 与现有 Program 路径，不等待 I1 kernel adapter 或 C1–C4 |
| U2b Program IO / U3 Remote / U4 关闭收口 | 独立跟踪，不整体阻塞 K2；每条新增复制路径当笔证明最后使用者寿命 |

**最终交付门：** TMR/HBG × A2/A3/A5 × eager/capture 分别列证据；受支持组合通过实际 executor 和 I1 → I2 → I3，未覆盖组合明确列出。算子探针、短序列或普通 Async 通过均不能替代它。

- **参数与执行：** kernel 不隐式 tensor 搬运；HOST 控制值按调用保存。持久 KV/权重/MTP 状态是显式输入/INOUT，不能随 workspace reset；单次 kernel 不回 host 决策后续发。
- **顺序与资源：** bank 发布、握手复位和双侧完成有依赖；DMA、kernel、event wait、graph、有效 handle 的最后引用退出后才回收。部分入队和停止未知不得提前释放。
- **Replay 与 DFX：** 同地址内容变化、显式换参、多 bucket、图/eager 交替、独立执行身份和逐次输出正确；故障、超时、容量拒绝与关闭均有回归。
- **性能：** 先有完整正确性，再按相同输入、工作量、资源和软件组合测 host/DMA/device、吞吐与长尾；不以代码合入宣称收益。

## 5. 详细设计与证据

| 要看什么 | 入口 |
| --- | --- |
| 规范与调用协议 | [设计原则](runtime-design-principles.md) · [共同执行](kernel-mode-prepare-submit-design.md) · [TMR 时序](kernel-mode-tmr-flow.md) · [Manager / contributor 协议](manager-contributor-protocol.md) |
| eager 范围与逐笔交付 | [剩余工作及历史证据](kernel-eager-open-work.md) · [三项 capture 贡献](reviews/capture-correctness-contributions/REPORT.md) · [API 层次](current-api-layers.html) |
| 参数 / Buffer / Remote | [参数设计](argument-domain-policy.md) · [跨 endpoint 合同](endpoint-access-contract.md) |
| 四仓接管与候选来源 | [交接说明](kernel-mode-cross-repo-handoff.md) · [跨仓任务](kernel-mode-cross-repo-prompt.md) |
| 独立未解故障 | [AIV 现场规划](aiv-evidence-plan.md)；既有故障不因后续 CI 通过而视为修复 |
| 早期路线与逐轮记录 | [精简前冻结快照](history/roadmap-20261022.md) · [Pipeline A 来源](references/pipeline-a/run-pipeline-track-a.html#roadmap) |

本页只维护当前动作、阶段状态、依赖和退出条件；测试数字、commit、review 轮次放入对应报告。Markdown 为内容源，HTML 同次生成；后续每次更新替换对应行，不追加工作流水。
