# 当前 Manager / Contributor 执行协议

**生效依据：用户明确要求主 agent 只分析和派发，实际开发由子 agent 并行承担。** 本协议规定执行分工；运行时设计判断仍只以[设计原则](runtime-design-principles.md)为准，阶段与依赖见[ROADMAP](ROADMAP.md)。历史 [Pipeline A 多 agent 讨论稿](references/pipeline-a/multi-agent-work-principles.md)只作来源，不授权安装 Hub、登记远端任务、启动 timer 或恢复自动调度。

## 1. 角色与硬边界

| 角色 | 职责与边界 |
| --- | --- |
| 主 agent / manager | 分析目标与当前代码、检查既有工作、确定任务合同和依赖、派发、裁决范围冲突、审阅证据、验收、向用户汇报；不写产品代码、测试、规划文档，不执行调试实验、构建、cherry-pick、提交或 PR 发布 |
| 实现 contributor | 领取一个完整可观察行为；负责所分配范围内的实现、必要测试、产品文档、缺陷定位与验证；独立 worktree 单写者，不越界修改共享文件 |
| 文档 contributor | 修改当前规划、生成 HTML、校验链接，按已有授权备份到 fork 的设计分支；规划材料不进入产品 PR |
| 集成 / 发布 contributor | 专属集成 worktree；收齐已停止写入的精确 commit，处理集成/rebase、构建与完整验证、真实 Claude review、意见修复协调、fork push、PR 与 CI；主 agent 审阅签收后执行下一动作 |
| 独立 reviewer | 不承担被审 diff 的实现；实际调用 Claude 的 `/review-pr`，记录精确 head、意见和处置。CI 通过、作者自查、Codex 子 agent 审查不能代替这项独立评审 |

主 agent 阅读代码与证据可以很深入；遇到需要改动或运行实验来回答的问题，派给相应 contributor。集成和修复也属于执行任务，不留给主 agent 扫尾。

## 2. 派工合同与真正的并行

每项任务启动前记录：

- 目标及用户可观察的完成行为；明确不包含的范围。
- canonical Issue/PR、当前 main 与候选 head、查验时间；先核对既有 owner，避免重复开发。
- contributor、独立 worktree、唯一 writer、精确可写文件；共享接口的唯一 owner 与依赖。
- 必要测试、产品文档、失败分支、反向对照、硬件和性能证据；不能只交实现片段让主 agent 补齐。
- 交付物、验收条件、集成 / 发布执行者和独立 reviewer；没有实现缺口时如实报告，不制造小 PR。

**开发并行是两个或更多 contributor 各自承担完整行为实现。** 一个人实现、其他人只补测试 / plot / 监控 CI，是任务内协作，不算多条开发路线。依赖不允许实现并行时，manager 说明具体共享资源或接口，安排其他已授权的独立交付。

一个 worktree 同时只有一个 writer。同文件默认串行交接；即使修改不同函数，也先由 manager 明确 owner。范围扩大或发现共享依赖时先报告并调整合同，不能私自进入另一个 worker 的文件。每个硬件运行执行架构预检与 `task-submit`，独立保存原始日志、源码/运行时身份及占用记录。

## 3. 交接、集成与验收

```text
任务 / scope revision / contributor：
目标与精确写入范围：
worktree / branch / commit SHA / 已停止写入：
行为、必要测试与产品文档：
验证命令、环境、结果、证据路径：
未覆盖组合 / 失败 / blocker / 下一动作：
集成执行者 / review head / 集成 head：
manager 回执：接受、退回修复或依赖等待；理由与后续 owner：
```

manager 必须保持主动验收循环：**收到结果 → 读取精确产物和证据 → 接受 / 退回 → 派下一 owner 或发布动作，或记录具体外部 blocker**。派发不等于完成；“收到”和自报完成不等于验收。仍有已授权、未受阻的协调工作时，主 agent 不能结束当前工作。子 agent 通知不保证在主 agent final 后自动启动新 turn。

交接 commit 停止写入，修复另报新 head。按任务与产物 SHA 去重；恢复先读实际 GitHub / worktree 状态。发布或 tracker 更新若返回未知结果，先读远端 SHA / 正文确认是否已生效，再决定重试，不能盲目重复 push、建 PR 或覆盖更新；未知仍未解除时保留回执和下一读取动作。

由委派的集成 / 发布执行者完成集成、验证，并可创建或更新代码 PR，以运行真实 Claude 评审和 CI；意见由相应 owner 修复。head 改变后重新判断需要重跑的验证和评审，不能把旧 head 的成功移到新代码上。主 agent 收齐功能证据、意见处置与最终 exact-head CI 后签收；发布执行者再将 PR 标为 ready 并交用户审核。上述修改和发布均不由主 agent 执行，不自动合入。

一项完整行为可以由多个 contributors 协作，默认仍是一笔有意义的 PR；不能按 agent 人数机械拆成不完整 PR。独立功能可分别成 PR，父路线只有在各项及端到端验收通过后才完成。

## 4. 当前调度边界

| 交付线 | 当前状态与下一门 |
| --- | --- |
| ScopeStats 普通错误关闭 | #2684 已合入 `62565feb3`；原 head 的 CI / 资格仍归[原报告](reviews/scope-stats-proof-close/REPORT.md)，不转移到下列 fork 修复 |
| TMR / HBG / Common 正确性贡献 | 实现 owner 交冻结源，独立 validator 实际 Claude 评审并退回修复，manager 接受，发布 contributor 将三项修复推到自己的 fork；[精确贡献/比较/资格](reviews/capture-correctness-contributions/REPORT.md) |
| 原 feature 接入 | #2625/#2626/#2646/#2647 原 owner 保持分支责任；尚未采纳本轮修复，不因 fork 发布宣称 feature ready。A5 fatal replay 超时、HBG exact-head CI / RTS / 硬件及整体 C4 仍待证明 |
| 共同接口与后续任务 | 任务启动前再定唯一 writer 和精确文件范围；不能因发现缺口自动占用他人的代码。I1 实际队列接线仍待协调，不制造并行完成的主张 |
| 集成 / 发布与文档 | 独立委派，root 不执行。产品分支、tracker body 和设计备份分别留精确远端回执；结果未知先读取，再幂等续做 |

本表记录当前边界；测试数字、修复轮次与各 head 的结果只进 scoped report，不在协议或 ROADMAP 追加流水。运行时阶段只有各自实际验收通过才完成。
