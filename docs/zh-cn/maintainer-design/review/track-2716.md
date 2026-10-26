# #2716：范围、现有交付与本地合同对照

> **发布快照：2026-10-26。** 设计、实现状态与历史调查须按正文各自基线阅读；不代表当前 main 全量验收。参见[阅读入口与已知更正](../README.md)。
>
> 定点核验：2026-10-26，main `daf2f8418907592b5b881792bd9a2a0a5ea8f19d`。#2716 正文的源码参考仍为 `340276ecc45ff965d7b71db786ea02b21a1f9fb5`；不能据该旧基线忽略随后合入的 #2665/#2656。完整目标、owner 与业务进度以 GitHub 为准；本地实施/验收证据只在 [工作项](../work-items.md)，本文保存范围判断与下一动作。仅纳入目标 main 的 PR；未发布评论、变更 issue 或运行产品测试。

## 1. 范围：四个能力目标，三套关系

[#2716](https://github.com/hw-native-sys/simpler/issues/2716) 管 L3+ 的共同架构和四个完整能力方向。编排树、任务 DAG、资源访问关系继续分开；四方向是交付组织，不是四个代码目录，也不是先做完方向 1 才能启动其余方向。

 | 方向 | 原生子树（本次完整递归） | 应交付什么 | 与本地 RA/DS/EX 的关系 |
 | ---- | ------------------------ | ---------- | ---------------------- |
 | #2721 公共基础 | #2460、#1931、#2720 | 编排/调度/任务下发的公共合同，资源 identity/view/access/lifetime；可独立验收的公共重构 | RA、EX 的公共合同；不把所有触及 Buffer 的 PR 收进此方向 |
 | #2722 通信与后端 | #2594、#824、#2621、#2657、#2708 → #2673、#2674 | 建域、跨 run 复用、owner-local transfer、Host sparse fetch、真实 L2 backend/通信 kernel/完成与释放 | DS 域/backend 合同，消费 RA 和 EX；完整场景仍在原 issue |
 | #2723 L2 常驻执行 | 本次子列表为空；阳性对照 #2721 有 3 项、#2722 有 5 项 | 单个 L2 Run 内持续接多轮 work；完成关联、复用、背压、停止与异常清理 | EX-2 主索引；需要的 Region/control 改动随场景交付 |
 | #2724 多级多机部署 | #2659 | L4→本地/远端 L3→L2 的提交/结果、进程与会话复用、生命周期、故障清理与迁移 | DS-1/DS-6；需要的资源与控制改动随部署目标交付 |

树共 16 个节点、15 条原生边，逐父节点比较了子集合；#2708 的两项保留嵌套，未提升成根 track 子项。方向三没有原生子项只证明尚未拆子项，不证明没有已有代码或没人推进。

**任务切分**：一项完整需求一个 issue，可拆多个 PR。#2621 的 owner 初始化/访问/保活/退休、#2657 的 Host 注册/通信 setup/fetch completion/DAG 接入都留在方向二。模块 owner 仍参与评审；不能把代码目录归属当目标归属。只有有独立用途且能独立验收的公共基础才另列方向一。RA/DS/EX 保留为本地合同索引，不再把同一场景分成几套独立进度。

## 2. 边界与既有设计

- **公共资源与应用协议的边界保留**：simpler 管资源、访问、顺序/可见性、完成与安全退休；应用管布局、消息协议、queue 算法与模型策略。必要的通信 kernel、后端队列、注册、资源 pin 和完成桥接仍是 runtime 工作，不能把整条 #2657 或 #2621 说成上层应用需求。
- **Region 方案未定**：#2720 明确分析需求与完整方案；#2635 是候选实现。资源/应用边界是本地设计准则，不是 GitHub 已批准全部 Region API 的事实。方向三也没有预先接受 SPSC 作为公共 ABI。
- **三种“常驻”分别验收**：L3 进程跨 L4 任务存活；同一 Worker 多次 run；一个 L2 Run 内多轮 work。一个 L4 任务触发一次 L3 编排，不能为了“常驻”默认改成单 L3 Run 接所有 L4 任务。
- **通信路线按具体 backend 说明**：现有 ACL/PTO 路线不引入 SHMEM 库依赖；#2657 的 Hcomm/URMA 资源 setup 是候选 fetch backend 的能力，不能因旧材料“不另建 HCOMM/HCCL 必选路线”而将其排除。控制 transport 名称不证明设备数据路径。
- **Group 迁移有退出条件**：#2659 针对 MPI Group 与 Direct 收敛；普通 TCP/daemon 不预先删除。逐 rank 自报释放、进程退出观察、外部账本持久化和异常回收是不同结果。
- **外部上下文**：#2670 是父级总览；#2495 及 #2496/#2497/#1425 是外部能力/阻塞上下文；通用 eager/capture #2666、Program/Async #2429、AICPU #2631/#2632、AICore scheduler #2667、DFX #995、跨仓联验 #2658 保留原轨。必要的 L2 通信接入不能被这一排除吞掉。

## 3. main 源码证据与判定边界

以下链接固定在本次 main SHA；源码存在与测试定义存在不冒充本次硬件 PASS。

 | 合同 | 已复核的代码与结论 | 不能推出什么 |
 | ---- | ------------------ | ------------ |
 | RA-4 | #2672 `71077b04a5e4`，[DomainConfig.capture](https://github.com/hw-native-sys/simpler/blob/daf2f8418907592b5b881792bd9a2a0a5ea8f19d/python/simpler/comm_domain.py#L28) 拒绝非 64 字节倍数的 buffer/window 并检查总容量 | 不证明 buffer 内信号布局正确、通用 cache coherence 或自动 padding |
 | RA-15 / #1931 | [child control](https://github.com/hw-native-sys/simpler/blob/daf2f8418907592b5b881792bd9a2a0a5ea8f19d/python/simpler/worker.py#L3603) 的 FREE 仍传裸 Q 到 cw.free；相邻 COPY 使用 descriptor/materialize 为阳性对照 | 父 API 验证不代替 child 对收到的 frame 验证 |
 | RA-12 / #824 | worker.py `detach_persistent_domain` 0 命中，`retain_after_run` 10 行阳性对照；[配置复用](https://github.com/hw-native-sys/simpler/blob/daf2f8418907592b5b881792bd9a2a0a5ea8f19d/python/simpler/worker.py#L10475) 已有 | #2639 的统一接口或 #2668 的在飞修改不等于 #824 全部验收 |
 | EX-2 / #2723 | [Host 示例](https://github.com/hw-native-sys/simpler/blob/daf2f8418907592b5b881792bd9a2a0a5ea8f19d/examples/workers/l3/worker_chip_message_queue/test_worker_chip_message_queue.py#L168) 一次 submit 后多轮 enqueue；[chip 循环](https://github.com/hw-native-sys/simpler/blob/daf2f8418907592b5b881792bd9a2a0a5ea8f19d/examples/workers/l3/worker_chip_message_queue/kernels/orchestration/worker_chip_message_queue_orch.cpp#L266) 处理 DATA/STOP 并 drain | 示例已存在，通用公共常驻方案、故障与支持矩阵仍未被此示例全部证明 |
 | DS-1 / #2659 | #2665 `ce181c8cdacf`，[cleanup ledger](https://github.com/hw-native-sys/simpler/blob/daf2f8418907592b5b881792bd9a2a0a5ea8f19d/python/simpler/mpi_direct_cleanup.py#L85) 保存每 rank 身份、序号和 UNKNOWN；[supervisor](https://github.com/hw-native-sys/simpler/blob/daf2f8418907592b5b881792bd9a2a0a5ea8f19d/python/simpler/mpi_direct_supervisor.py#L280) 在 MPI world 外收集 | ledger 在内存，最终打印；主等待 `proc.wait()` 未设关闭期限。报告不是持久账本、逐进程退出证明或完整恢复 |
 | DS-1 / RA-8 | #2656 `daf2f8418907`，[session view](https://github.com/hw-native-sys/simpler/blob/daf2f8418907592b5b881792bd9a2a0a5ea8f19d/python/simpler/worker.py#L7840) 观察 hub 状态、推进 epoch、拒绝失效 registry；materialization 在提交边界复核 | session epoch 不是设备 lease / remote reclaim 的全部生产闭环 |
 | DS-5 / #2708 | [A5 URMA init](https://github.com/hw-native-sys/simpler/blob/daf2f8418907592b5b881792bd9a2a0a5ea8f19d/src/a5/platform/onboard/host/comm_hccl.cpp#L849) 仍调用 manager Init，未指定新的队列布局；现有 manager 调用为实现阳性对照 | 后端可初始化不证明多 AIV 同 peer 安全或 compiled-capability 公共查询已经交付 |

## 4. 观察与下一动作

这是本轮维护者动作表，不重建业务状态队列。实施状态见工作项；人员沿用 GitHub 既有 owner，本地只记录角色。

 | 对象 | 本轮证据／范围 | 下一动作与责任角色 | 重新核验触发 |
 | ---- | -------------- | ------------------ | ------------ |
 | #2460、#1931 | U1a–U4 合同继续适用；child FREE 边界仍在 main | 既有资源 owner 按共同 identity/lifetime 交付；维护者按 RA-13/RA-15 验收，不因方向重组重开计划 | 新 main 交付或 issue 验收更新 |
 | #2720 / #2635 | PR OPEN `0bc9b0f690f3`；旧评审只对 `850a36bd9` | Region owner 明确场景、公共机制与应用协议后再核候选；维护者重审当前 head，不能沿用旧竞态结论 | 方案确认或 PR head 更新 |
 | #2594 / #2668、#824 / #2120 | #2639 已合入；#2668 OPEN `d4a77803bf1f`，#2120 OPEN `cd69b1e0ecf9` | 域 owner 合流 retained/use/free 合同；维护者优先按 #2594 七项验收审 #2668，明确 #2120 兼容/重叠 | main 合入或 head 变化 |
 | #2621 | owner-local 第三方参考是 fork patch，未当作 main 交付；native 与第三方可独立验收 | 原 owner 保留完整场景；无须等待原生 A3 RoCE 才交付第三方扩展，生命周期与进度分别举证 | main PR 或具名集成证据 |
 | #2657 / #2714、#2685 | #2714 OPEN `cf384c94a5e9` 仅单设备同步本地读；#2685 OPEN `88736dbcce54` 为 A3 Host 域且依赖 #2668 | fetch owner 分清本地读取基座、跨 rank 传输、issue/completion 与 overlap；维护者不以局部 PR 关闭整条需求 | 对应 PR head、依赖合入、延迟 completion 测试证据 |
 | #2708 → #2673/#2674 | 队列并发与 compiled capability 两个独立目标；#2675 `cd5e26f0d9ac`、#2650 `e22f05a79837` 有完成/寿命重叠 | backend owner 合流 ABI/lifetime 方向，分别验证两子项；不得把 PER_PEER 修复计作 shared-pool 并发 | 设计/PR 关联或具名 ON/OFF 和并发验收 |
 | #2723 | 单 Run 多轮示例已在 main，完整通用方案尚需分析 | 方向 owner 从真实示例提炼 work/completion/stop 合同，不把 L3 mailbox idle 优化替代目标 | 方案、具体交付或组合验收出现 |
 | #2724 → #2659 / #2665、#2656、#2598 | 两项新 main 交付分别提供清理观测与 session identity；#2598 OPEN `7857e73fc5a1` 仍为 Group/TCP 账本 | 原 owner 回填已交付子集并确定持久化/期限/故障闭环下一项；维护者对 #2598 提供收窄/去重判断，不新增并行生命周期 | 新合同/后续 PR；Group 功能和性能迁移证据 |

本轮仅核 PR 状态/head/正文范围与上述 main 源码，不将 PR 作者的验证汇报重记成维护者独立 PASS。没有发布本表动作到 GitHub，也没有自动启动后续 review/实现。

## 5. 完整性口径

root 的六项验收逐项由四方向联合承担：范围/归属、公共迁移、通信全路径、L2 常驻、L4 部署、复现与性能证据。四方向分别有 4、4、4、5 项 checkbox；#2720 四项分析/交付、#2659 总验收及七个分项都保留。#2460 原 U1a–U4、#2594 原 DOMAIN-A1–A7 不因重组丢失。新来源 ID 及完整 checkbox 原文保存于 goal-sources.json，归属在 work-items §6；它们是覆盖索引，不是新 issue 或强制逐项拆 PR。

16 个 issue 均保留现有单一 assignee；原生 blocking 查询已保存，具体依赖另核 PR 正文，例如 #2685 显式依赖 #2668，而 #2673/#2674 独立交付。16 个正文分别记录更新时间与 SHA256；树集合与提取的非空来源做独立核对。原生父子是范围，显式 blocking 是依赖，正文提到的相关项不是自动 child/blocker。根总览与各方向的“待分析”不阻止已有明确子项推进。
