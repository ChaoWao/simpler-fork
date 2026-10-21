> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# DFX a8f1ceff9：验收修订与依赖整合审查

## 当前操作授权 — 2026-10-15T04:50:05.249103+00:00

用户在明确获知原rebase拒绝原因及操作边界后回复“所以应该允许啊”，已授权原DFX作者重基到最新main。已核main653a24c4a/all36无新覆盖，11文件交集逐项核对；同UUID54.78%无需compact，04:49:16 livegate通过，原PID665313启动。旧工具拒绝仅留历史，不再是待用户确认项；不绕过新出现的工具拒绝。最终复审及CI仍必需，发布时相对base单commit。

## 历史检查点：合入后、操作授权前（已被上方授权取代）

#2571 已由用户合入78365f79c，完整补丁与已审a002一致；main653a24c4a另含#2584 operation-lease context校验。此前a002 CI已全部完成。原DFX工具审批阻塞仍未解除，本记录仅更新依赖事实，不执行或绕过重基。下一次获准整合应以届时最新main核查，不再使用历史P4分支目标；仍需保留生产者停止证明、多collector credit与#2584上下文校验。

## 历史 a8 提交结论

精确 head `a8f1ceff92a787a828edf01153d06f10611f0794` 的三项阻塞验收修订接受。此前 `55ac021b1` 的功能审查结论继续适用；尚未审查重基后的最终树，也未运行本地测试或完成该交付的自然 CI，故不是 Ready。

- 四后端记录保留 predecessor/successor 的有序身份，只把 observed、unfired、query_rc=0 同时成立的记录计入真实提前入队；否定验收检查两个方向。
- 每个 child 从本轮 byte offset 读取完整日志行，在相同窗口观察两次 chip.run 收尾后才判定没有 joined span。已追到 onboard/sim finalize 发出点及 host logger 有序队列；最终四文件与已审 WIP hash 一致。
- ArgsDump 失败回滚通过生产 undo_admission_attempt 驱动隔离与 writer 已取得所有权两类失败，并从真实 C wait 接口验证额度 generation 前进；正常撤销对照不误唤醒。作者报告编译/lint，不冒充 manager 执行测试。

## 2026-10-15：更新到已静审的 a002 依赖（只读核对）

旧目标 `28cbb7033` 只保留作历史计划。当前候选 P4 目标为 `a002acc470421ecf0252593392734169f4064ffe`，已静审，CI 尚在运行。本轮只读核对该目标相对旧目标的变化与 DFX22提交交集，没有执行 rebase 或其他替代整合。

新增交集为 A3/A5 sim runner、onboard/sim runner base、sim base header、ChipSwimlaneCollector header/implementation，共7文件。关键语义变化是将“执行成功、快照有效”与“生产者已全部停止、存储可回收”分开：模拟器 AICPU/AICore 全 join 后，失败路径传 `device_producers_stopped=true`；失败快照不读，bank 可丢弃归还；停止未知仍隔离。原 DFX a8 的相关调用仍是旧签名，必须整合新参数而保留 DFX 多 collector / credit 逻辑，不能只解决文本冲突。

整合后的具体检查点：两个 sim 调用点 → base teardown → run boundary → collector close 全链传递停止证明；onboard 不凭 host 收尾伪造停止证明；失败丢弃不能发布有效快照；既有多 owner credit/backpressure、通知与关闭寿命不丢失。最终树仍需独立审查和对应 CI。既有工具拒绝未被解除，本只读核对不构成执行授权或工具审批通过。

## 历史具体整合计划

只在原 temporal worktree、原 UUID 的 `codex/dfx-joined-launch-integration` 分支操作。已存在备份 tag `dfx-pre-reconcile-a8f1ceff9`。将旧 P4 基线 `00937af8d8720af99fc55ef1282574da4082d8ae` 之后的 22 个 DFX 提交重放到最新 P4 `28cbb70333f49a582a8b804a7bb3c1c14a912002`，随后核 range-diff、冲突解决和最终精确 head，再按已有授权发布一个完整 DFX PR。保留外部作者 P4 提交，不写 main/P4 分支；现有两个 untracked 项保持原状。操作前仍须刷新 main/all-open PR 和同会话上下文门禁。

两个改动集合的交集为六个文件，目标侧实际 diff 已读：

| 文件 | 目标 P4 变化 | DFX 要保留的变化 |
| --- | --- | --- |
| python/bindings/task_interface.cpp | ExecutionContextRefused 绑定及 validate 接口 | diagnostic credit 等待绑定 |
| onboard/host/device_runner_base.cpp | RunResourceSlots 迭代器层级修复 | 多 collector 准入/回滚/terminal bank |
| sim/host/device_runner_base.cpp | arena bank 迭代器层级修复 | 对应诊断准入及通知 |
| src/common/worker/chip_worker.cpp | context refusal 专用异常 | credit entries 及容量满重试结果 |
| src/common/worker/chip_worker.h | context refusal 类型 | NativeLaunch 与 credit entries |
| test_chip_run_lane_joined_launch.cpp | generation 初始化、context/析构语义及 bank 等待测试 | 多诊断 owner 配对验收 |

这些是不同语义区域；不能因此承诺无文本冲突或无集成缺陷。最终必须保留两侧语义并审查整合结果。

## 历史工具审批阻塞（已获用户操作授权）

原作者尝试 rebase 到此前 `6d6c43ba3` 时，Claude Code 自动审批明确拒绝：`[Merge Without Review]`。未绕用 manager shell、cherry-pick 或其他工具执行同一变更。当时将目标更新为已审 `28cbb7033` 并等待操作授权；随后用户已授权且已完成重基，当前见 #2588 复审。功能实施合同已批准，不再重复请求架构批准。
