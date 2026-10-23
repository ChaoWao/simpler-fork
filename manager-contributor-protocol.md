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

manager 接到通知后读取产物和证据，明确接受、退回或等待依赖；“收到”和 agent 自报完成不等于验收。交接的 commit 不再继续写，后续修复另报新 head。通知按任务与产物 SHA 去重，恢复会话先核对实际 GitHub / worktree 状态。

由委派的集成执行者完成集成、完整验证和真实 Claude 评审；意见由相应 owner 修复。head 改变后重新判断需要重跑的验证和评审，不能把旧 head 的成功移到新代码上。主 agent 收齐功能证据、意见处置与最终 exact-head CI 后签收，发布执行者提交有完整行为的代码 PR，交用户审核；不自动合入。

一项完整行为可以由多个 contributors 协作，默认仍是一笔有意义的 PR；不能按 agent 人数机械拆成不完整 PR。独立功能可分别成 PR，父路线只有在各项及端到端验收通过后才完成。

## 4. 当前调度边界

| 交付线 | 当前状态与下一门 |
| --- | --- |
| ScopeStats 普通错误关闭 | #2684 已合入 `62565feb3`；保留原 qualification head 与[证据](reviews/scope-stats-proof-close/REPORT.md)，不重新声称修复其他 DFX 或 AIV |
| TMR capture | `/root/tmr_capture_delivery` 负责 #2625 完整交付的正确性贡献；已批准 `test_native_admission.cpp` 与 `tests/ut/cpp/support/native_admission/transport.{h,cpp}`，修正依赖 SSO 的分配失败注入，证明实际注入边界及 Partial / poison / 保留 / retry 行为。先交本地 candidate，折入已有完整 capture PR；独立 A5 超时仍诊断中，无 A5 silicon 完成主张 |
| HBG capture | `/root/hbg_capture_delivery` 负责 #2646/#2647 完整交付的正确性贡献；当前本地栈集成与具体缺陷 / 文件范围提案尚待签收。独立证明 restore、所有权与关闭，不由 TMR 结果代替 |
| 共同 C1/C2 接口 | 尚未划给任一实现者；发现交叉时由 manager 决定唯一 owner 和消费依赖，禁止两边各做一套 |
| 集成 / 发布 | 是独立委派职责；代码交接后才准入，不由主 agent 执行 |

本表是当前派工边界，不是 capture 阶段完成记录；详细实现状态随各任务交接更新到 ROADMAP 对应行，不在此追加工作流水。
