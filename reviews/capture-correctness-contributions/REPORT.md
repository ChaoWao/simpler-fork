# 三项 capture 正确性贡献：审阅、发布与待接入边界

快照：2026-10-23 UTC。主线仍为 `62565feb3b14553ed736aab1eb008c5785bcf330`（#2684 merge）。本轮实现、实验、独立验证和发布全部由 contributors 完成，manager 读取证据、退回修复并验收；规划材料单独备份。

## 当前结论

三项完整缺陷修复均已实际 Claude 审阅、独立 CPU 资格验证、manager 接受，并发布到自己的 fork。**原 #2625 / #2626 / #2647 分支尚未更新；本表不是完整 capture 或硬件验收。** 没有另开三笔微型 PR，也未将整个 capture 功能复制成另一套实现。

| 贡献 | 冻结提交 / 发布分支 | 精确父提交与原工作 | 修复与下一 owner / 动作 |
| --- | --- | --- | --- |
| TMR 录事件分配失败注入 | [`28a9de54a`](https://github.com/ChaoWao/simpler-fork/commit/28a9de54af839e1bc5319641379948a9ba608724) · `fix/tmr-capture-allocation-fault-site` | #2625 published `71f5b4fc6b8ac6ac2419086539ccf0c9af6efb57`；[仅修复比较](https://github.com/ChaoWao/simpler-fork/compare/71f5b4fc6b8ac6ac2419086539ccf0c9af6efb57...28a9de54af839e1bc5319641379948a9ba608724) | #2625 原 owner 采纳三文件修复；真实分配调用在优化构建仍存在，catch-site witness 证明命中故障点，保留 Partial / poison / retention / retry 检查。然后验证原 PR 集成 head 的 CI 与未关闭的 A5 fatal replay 超时 |
| HBG restore 错误 / 后继 gate | [`7d1dae515`](https://github.com/ChaoWao/simpler-fork/commit/7d1dae515b4e7f3471313e15eb303d46f51e87d7) · `fix/hbg-capture-restore-failure-gate` | integration `84b4653bbf935658b8aa71307f53f1c4e8719641` = #2647 `80a08550b` + main `62565feb3`；[仅修复比较](https://github.com/ChaoWao/simpler-fork/compare/84b4653bbf935658b8aa71307f53f1c4e8719641...7d1dae515b4e7f3471313e15eb303d46f51e87d7) | #2646/#2647 原 owner 采纳六文件修复，保留其 feature stack；真实 device-entry restore 失败在 attempt 之前也进入 retirement 汇总，并在后继执行前 poison。然后在目标集成 head 证明 RTS / 硬件 replay 与失败生命周期 |
| Common 空 region 重用 | [`dbfbaf877`](https://github.com/ChaoWao/simpler-fork/commit/dbfbaf87757818d37550bd95c763d0bccd23c4ac) · `fix/kernel-resource-empty-region-reuse` | #2626 published `f701832b091290d3db9f20eabe678700a4e7a043`；[仅修复比较](https://github.com/ChaoWao/simpler-fork/compare/f701832b091290d3db9f20eabe678700a4e7a043...dbfbaf87757818d37550bd95c763d0bccd23c4ac) | #2626 原 owner 采纳三文件修复；非零 offset region 可 shrink / clear / repeated clear / restore，原 capacity 与 backing 保留，后续 region 拒绝不部分发布。然后验证原候选接入和 exact-head CI |

以上链接均是固定提交；不能把修复分支的 CPU 结果当成原 PR 已采纳后的 CI。HBG 比较的父提交是明确的本地集成基线，不是原 #2647 直接 head；贡献只有其后的单个修复 commit。

## 独立资格与实际审阅

| 精确候选 | 新 head 的独立结果 | 作者的补充结果 | 反向对照与审阅边界 |
| --- | --- | --- | --- |
| TMR `28a9de54a` | Release `-O2 -DNDEBUG` 与 Debug `-g` 各 8 targets：169 passed / 3 existing skips；检查两架构优化后的真实间接 allocation call、catch 与 rethrow；源码/二进制哈希保存 | 同 head 两构建配置及 hooks 通过 | 两配置 × 两 TMR 架构，删除真实 allocator call 的四个 mutant 全红。实际 Claude 最终 0 Must / 0 Should；其审阅时 Debug 尚在运行，后续独立 Debug 结果才补齐，不能倒写审阅原文 |
| HBG `7d1dae515` | 四个 Release 核心 targets：139 passed / 9 existing A3 skips；8 个核心与邻接 CTest targets 通过；六文件哈希核对 | 同 head 406/406 CTest；产品扩展 exact stamp 后 Python 41 passed / 66 skipped；六源码与 29 个 runtime/extension 哈希核对 | 真实旧 entry 反向对照及三种 lower-API OR 条件 × 两架构 mutant 各击中对应回归；后者为作者执行、独立读取日志。实际 Claude 对 lower FatalFailure 覆盖与 contract 两项 Should 复评后 0/0；不将旧 `81e52b086` 的 133/9 移到新 head |
| Common `dbfbaf877` | Normal 与 Release 各 68 passed / 0 skipped：state 57、binder 6、validation 5；三源码/六二进制哈希保存 | 同 head 两配置与 hooks 通过 | 独立执行八个真实 TU mutants 的 head 是 `e0d6e223c`：原 offset guard、跳过 empty-kind、丢原 capacity、提前发布 × 两配置均红。`e0→dbfb` 只有 header 注释 +4/-2，runtime/test 源码和六二进制一致；新 head 重新 build/run，未宣称重跑 mutants。实际 Claude 复评关闭两项注释 Should 后 0/0 |

报告与实际 Claude 最终文本：[TMR](tmr-final-review.md) · [HBG](hbg-final-review.md) · [Common](common-final-review.md)。独立资格回执：[TMR](tmr-independent-qualification.md) · [HBG](hbg-independent-qualification.md) · [Common](common-independent-qualification.md)，保留其交接时状态，后续验收/发布见本报告。原始构建、测试 XML、反向对照、汇编、设备/CI 原始日志和完整工作目录只保存在本地，不上传。

### TMR 优化反例不可丢失

第一次 entry counter 只证明进入回调：删掉显式 allocation 后，后面的字符串分配仍会抛异常，使测试错误地保持绿色。第二次改成 catch-site witness，但 GCC 10.3.1 Release 确实消除了直接 new/delete，独立构建变红、汇编也没有该调用。最终通过 volatile 函数指针调用真实 allocator，并在 catch 中见证实际 bad_alloc；Debug 与 Release 的缺失调用 mutant 均红。

实际 Claude 曾声称直接显式 operator-new 不能消除；编译反例推翻后已撤回。其最后一条非阻塞建议仍把退回直接调用描述为“silent green”，当前 catch witness 下实际会红，manager 已拒绝该推理。最终机制按真实编译和运行证据签收。原 macOS CI 才是平台故障复现；Linux 两配置 proof 不替代未来目标 head 的 macOS CI。

### HBG 与 Common 的保留边界

HBG 的修复覆盖 restore 失败早于 attempt 的 status 丢失，以及 Ready→Poisoned 的 FatalFailure / runtime status / teardown status 三个独立条件；不能据此声称所有设备故障、hang 和硬件资源回收都已验证。Common 的 allocation 断言是 provider/device arena 的分配与释放，不代表 host vector 零分配。Common review 对 first-call cleanup 一定锁住 `closing_` 的泛化未被当作证据；重复请求的原子性与正确注释直接由代码确认。

## 原候选与真正的下一门

| 原工作 | 本轮新读到的状态 | 不能跳过的门 |
| --- | --- | --- |
| #2625 | Open / non-draft，head `71f5b4fc6` 未改 | owner 集成贡献；#2669 已 closed/not_planned 只是把责任并回 #2625，**不代表 A5 fatal replay hang 修好**。仍须可信设备身份、task-submit 独占诊断、准确 source/runtime 身份和定向 A5/A3 回归；本轮没有硬件运行 |
| #2626 | Open / non-draft，head `f701832b` 未改 | owner 集成 Common 修复，复核 target/runtime consumers，再取目标 head CI；已有旧 head 检查不覆盖该增量 |
| #2646 / #2647 | 仍 Draft，head 分别 `6cb08c739` / `80a08550b`；后者 exact head 当前 0 check-runs | owner 接入 HBG 修复并完成 feature stack、RTS 和硬件资格；旧 body CI 不能冒充 `80a` 的检查 |
| I1 / I2 / I3 | 仍须真实 queue → Worker/native 消费接线和真实 workload / request 验收 | 不以本轮 CPU 缺陷修复替代框架、MTP/KV 长序列或 serving 正确性 / 性能；未另行分配新产品任务 |

主线 #2684 的最终 head `5a1c5c089`、代码等价测量 head `77aba9a8a` 与 CI run [39889814912](https://github.com/hw-native-sys/simpler/actions/runs/39889814912) 保持历史归属，merge 为 `62565feb3`。本轮三条 fork contribution 的局部 CPU proof 不写入 mainline CI 资格字段。

当前规范与执行合同见[原则](../../runtime-design-principles.md)、[ROADMAP](../../ROADMAP.md)与[Manager / Contributor 协议](../../manager-contributor-protocol.md)。完成通知必须进入 manager 的验收与下一动作循环；发布三项贡献尚不等于原 feature 合入或 capture 路线完成。
