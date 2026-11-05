# 分布式部署与通信：旧 DS 目标交接

> **发布快照：2026-11-05。** 本轮为资源、部署通信与跨仓目标的跟踪交接；不代表当前 main 全量验收。源码事实按各自 SHA 阅读，资源进度查 GitHub。参见[阅读入口](../README.md)。

本地 DS 独立进度轨已退役。部署/会话与通信/数据面分别以 #2724、#2722 为整体跟踪入口；本文保存旧编号、验收要求和具名历史证据，不维护实施状态或排期。

## 交接证据与动作

2026-11-05 UTC 读取两个方向的正文、评论、负责人及原生子项。正文均有其他负责人，本轮只发评论，回读确认发布内容且正文未变。来源哈希与评论身份见 [来源清单](../goal-sources.json)。没有新建 Issue、改动负责人或原生关系，也没有新增产品测试或推进代码审计基线。

| 方向 | 已核对的交接动作 | 后续触发 |
| ---- | ---------------- | -------- |
| [#2724](https://github.com/hw-native-sys/simpler/issues/2724) | [部署与会话评论](https://github.com/hw-native-sys/simpler/issues/2724#issuecomment-6652724365)已发布并回读；保持既有正文和 #2659 子项 | 负责人对 device lease 必要性、最小交接信息、退出/释放观察边界给出决定时更新合同 |
| [#2722](https://github.com/hw-native-sys/simpler/issues/2722) | [通信与数据面评论](https://github.com/hw-native-sys/simpler/issues/2722#issuecomment-6652726385)已发布并回读；保持既有子项及负责人 | 负责人对成员事务、规模需求、组合类型必要性和失败释放边界给出决定时更新合同 |

评论承接目标与问题，不表示负责人已接受所有旧方案，也不表示功能已完成。已有代码和 CI 结论继续按其原 SHA 与场景范围阅读。

## 逐项交接

| 旧编号 | 目标 | GitHub 入口 | 验收与边界 |
| ------ | ---- | ----------- | ---------- |
| DS-1 | MPI 会话的 session/epoch registry | [#2724](https://github.com/hw-native-sys/simpler/issues/2724) | 会话与 epoch、有效/过期任务、关闭确认/退出/资源释放分别计证；既有 supervisor/Direct MPI 交付对接 #2659。 |
| DS-2 | device lease ＋ rank inventory ＋ 冻结 global rank | [#2724](https://github.com/hw-native-sys/simpler/issues/2724) / [#2722](https://github.com/hw-native-sys/simpler/issues/2722) | 部署侧清点实际 endpoint/设备与占用归 #2724；通信侧定义本域成员、rank 与冻结条件归 #2722。是否需要独立 lease/registry 按真实争用与恢复场景确认。 |
| DS-3 | remote transfer / attachment：两条 copy 语义分清 | [#2722](https://github.com/hw-native-sys/simpler/issues/2722) | 区分 owner-delegated copy、真实映射与 device-peer/collective；范围、身份、上下文、完成及失败中间资源须可追踪；共同模型消费 #2460。 |
| DS-4 | 远端 domain lease、断连与回收 | [#2722](https://github.com/hw-native-sys/simpler/issues/2722) | 域 owner、接纳使用、最后使用、断连/失败回收与可重试债务；与 #824/#2460 共用合同，部署侧只提供会话/退出观察。 |
| DS-5 | A3 HCCS/RoCE ＋ A5 UB/URMA profile | [#2722](https://github.com/hw-native-sys/simpler/issues/2722) | 平台/runtime/backend 支持矩阵；正常传输、并发、多域、完成与异常清理分别验收；已有子项保留原归属。 |
| DS-6 | mpirun 启动与发现 ＋ 跨-L3 域接口 | [#2724](https://github.com/hw-native-sys/simpler/issues/2724) / [#2722](https://github.com/hw-native-sys/simpler/issues/2722) | mpirun 启动、发现、任务路由与结果返回归 #2724；域创建/查找/复用及跨层描述归 #2722。普通 TCP/daemon 不预先删除；Group 迁移按 #2659。 |
| DS-7 | 移除或重构 64-rank device ABI 上限 | [#2722](https://github.com/hw-native-sys/simpler/issues/2722) | 独立规模扩展问题交 #2722；先确认实际消费者、目标规模及 ABI/容量兼容，不能作为已支持规模的统一前置。 |
| DS-8 | host 参与域的语义（非对等成员） | [#2722](https://github.com/hw-native-sys/simpler/issues/2722) | Host backing、注册/映射、workspace、完成与寿命随 #2657/#2730/#2731 场景验收；Host 不占 NPU rank，copy-capable 路径不等待全部 direct-map。 |
| DS-9 | 成员事务执行（唯一设计归属） | [#2722](https://github.com/hw-native-sys/simpler/issues/2722) | 成员/代际一致、按场景确认 READY 与部分失败清理；保留已有事务而不重复设计。通信侧定义事务，部署侧提供真实 session/endpoint 信息。 |
| DS-10 | 跨节点异构域的组合与验收 | [#2722](https://github.com/hw-native-sys/simpler/issues/2722) | 跨节点通信场景由 #2722 承接，消费 #2724 的部署/session 结果与 #2721 的公共资源合同；共同验收实际访问、完成和失败回收，不预设 ResourceBundle 或公共授权对象。 |
| DS-11 | 同步与异步模型（SDMA / URMA gate） | [#2722](https://github.com/hw-native-sys/simpler/issues/2722) | 区分暂存、提交、传输完成及消费者可见；资源选择与使用前验证按现有接口，不能恢复已放弃的编译开关或强制 capability API。 |

只有旧 DS-2 与 DS-6 本身混合了两种职责，故明确拆为部署与通信两部分，不建立两个完整目标的重复 owner。其余按具体场景归一个方向；#2724 的退出结果和 #2722 的域释放结果是接口交接，不是两个方向对同一资源各建一套权威。

## 跨方向验收合同

- 部署侧提供真实 session/epoch、endpoint 与实际设备部署/占用信息；通信侧按本域定义成员、rank、导入/建链及失败结果。设备 inventory 不等于 communication rank，额外 lease/registry 的必要性仍待设计确认。
- 成员清点、建域事务、远端域最后使用与失败清理必须按目标场景联验。保留原有验收关系，不把“整个 #2724/#2722 关闭”或“全量清点完成”变成所有传输工作的开工门槛。
- 公共身份/范围/内部绑定由 #2460、域生命周期由 #824 对接。真实 OS/驱动映射与完成/可见性/寿命分开；额外公共权限、ResourceBundle 或 plan/capability 对象须有必要性证据。
- 64-rank 扩展保留为 #2722 的规模问题；支持目标与实现形状待场景核定。平台 direct-map 仅约束实际选择该路径的场景。
- 基础同步、通信完成及 runtime 自己的资源安全属于运行时；应用 queue、发布协议与背压属于消费者。跨 run/多机存在基础不替代并发、多域与故障验收。

## 交接前的具名证据快照（静态历史）

以下完整保存交接前工作表的 11 行，依赖和状态只代表各行原日期/原 SHA 下的观察。该快照不再更新，也不能用作当前实施进度或恢复旧 DS 排期；当前目标入口以本页逐项交接和对应 GitHub 为准。

## 原工作表与验收背景

### 历史分布式工作表

| ID | 工作项 | 吸收的旧编号 | 依赖 | 状态 |
| -- | ------ | ------------ | ---- | ---- |
| **DS-1** | MPI 会话的 session/epoch registry | `P2-A` | — | 🟡 **Direct MPI session 与清理观测已有多项交付，supervisor 闭环仍有明确残留**。main `63ffeb8a8bd9` 含 #2656 session_epoch、#2665 逐 rank 报告、#2741 `8f592545b9` 独立 health timeout，以及 #2792 `4383189c98` 在 shutdown echo 前停止 heartbeat sender、controller drain 精确 echo。health timeout 和消息 drain 不等于外部关闭期限；mpi_direct_supervisor.py 主等待仍为 `proc.wait()`，异常终止中的带 timeout wait 是阳性对照。#2598 CLOSED 未合入；#2659 的正文尚未回填这些交付，持久账本、独立关闭期限、故障/远端残留确认、Group 迁移与删除仍按方向 4 验收。候选 #2791 head `096efb9757a5` 是 startup artifacts / drained completion 子集。 |
| **DS-2** | device lease ＋ rank inventory ＋ 冻结 global rank | `P2-B` 的清点半 | DS-1 | ⛔ **生产清点/冻结验收未满足**。归属为 endpoint inventory、device lease、global rank 编号；PREPARE/COMMIT/ABORT 的执行归 DS-9。 |
| **DS-3** | remote transfer / attachment：两条 copy 语义分清 | `P2-C` · `U3` | DS-2 · 接口 #2460〔逐调用绑定、身份与实际使用保留合同〕 | ⛔ **U3 合并验收未满足**。现有 owner-delegated socket copy 不能算 device-peer/collective。#2460 要求 producer/receiver/version 成对迁移、保留 entry request/direction/grant/identity/range、不得 receiver 默认 H2D 或把 host transport storage 标成 DEVICE；network→H2D 故障时保留中间资源。 |
| **DS-4** | 远端 domain lease、断连与回收 | `P2-D` 远端半 · `#824` 远端半 | DS-3 · 接口 #824〔域 owner、接纳使用与最后使用合同〕 · **DS-9** | ⛔ **远端 lease/断连回收退出条件未满足**。与 #824 共用域生命周期；DS-9 提供建域事务。依赖表示完整验收，不妨碍本地 owner 转移原型先交付。 |
| **DS-5** | A3 HCCS/RoCE ＋ A5 UB/URMA profile | `P2-E` · `D.2` 的 backend 半 | DS-4 | 🟡 **URMA 完成协议、多 backend workspace 与 A2/A3 HBG SDMA 已有新交付**。main `63ffeb8a8bd9` 含 #2779 `42803935df` 逐实际队列 BB/CQE 完成目标、#2790 `9609da8a8b` 分离 SDMA/URMA/RDMA workspace、#2828 `349125e005` HBG SDMA；#2775/#2739 CLOSED。原 #2650/#2675 仍 OPEN，须对已合入内容去重。余量分别归 #2787 URMA 生命周期、#2823 RDMA 清理、#2824 RDMA 真机/CI、#2833 重叠子域、#2673 AIV 队列映射、#2855 batch/flush、#2783 handshake 文件及 #2800 bootstrap。#2854/#2860 跟踪现有 PUT/GET 停滞；#2858 只补诊断。A5 CI 具名日志中五个 URMA ST 通过、ChunkedFetch 跳过；详见 [范围证据](track-2716.md)。正常传输、并发、多域和异常清理分别计证，不外推全 backend PASS。 |
| **DS-6** | mpirun 启动与发现 ＋ 跨-L3 域接口 | `A.1` 残留 · `D.4` · `C.2` 的 L4 半 | — | 🟡 **统一域接口及 Buffer/Tensor 跨 run 消费已合入，部署闭环仍按方向 4 推进**。main `63ffeb8a8bd9` 的 #2639/#2668 已覆盖 #2594 的统一创建、查找、复用、域 Buffer、跨层描述和使用登记；#2594 CLOSED，不能继续列为 #2668 在飞。#2120 CLOSED 未合入；#824 原需求应对照 #824 已交付行为结项核对，不新增 detach 路线。方向 4 的 #2659 仍承接 supervisor 清理与 Group 迁移；普通 TCP/daemon 不预先删除。#824/#2460、DS-3/9 是共同合同，完整业务目标不重复拆 owner。 |
| **DS-7** | 移除或重构 64-rank device ABI 上限 | `P3⑤` | DS-2 | 🔵 **独立规模扩展包**。device rank ABI 的 64 上限需要独立兼容与容量验收；不阻止小规模异构域先行。 |
| **DS-8** | host 参与域的语义（非对等成员） | `D.3` | 接口 #2721〔所需存储寿命与访问合同；具体方案按 #2721 讨论核定〕 · 条件 #2496〔仅 A5 direct-map 分支；copy-capable host attachment 走 `OWNER_DELEGATED_COPY`，不需要它〕 | ⛔ **Host 路径全组合验收未满足**。#2657/#2730/#2731 按 2026-11-04 正文：普通 Host Buffer，native/mapped 描述分离；#2730 同进程同步 MTE，#2731 同 host 全员完整 Buffer 注册、非 self PTO URMA、独立 Host workspace 与异步 completion。首版每 consuming L2 一套 Host registration，第二独立 backing 拒绝；方法形状为方案，不冒充 main API。#2714 为 Stage 1 候选，最新 head 见 README；旧 head 的缺陷/测试不外推。#2685、#2818、#2820、#2796 是不同层级的 A3 Host 候选，须按完整场景去重。跨 rank 与 overlap 不能借用本地 MTE 成绩，Host 不占 NPU rank；copy-capable 路径不等待 #2496 全部 direct-map。当前与历史验收编号迁移见 §6，设计见用户面 §5.2。 |
| **DS-9** | 成员事务执行（唯一设计归属） | `W5` · `P2-B` 的事务半 | DS-1 · DS-2 | 🟡 **PREPARE/IMPORT/COMMIT/ABORT 事务已有，生产闭环未完成**。_allocate_global_domain 的普通/MPI ABORT 失败都调用_record_unreclaimable；SPL-1.6 的“只写 stderr 后继续”已被源码反证。epoch/lease、冻结成员、全员 READY、失败资源可追踪仍需与 DS-1/DS-2 联验，不重复设计现有事务。 ⛱ **挡住 `DS-4` `DS-10`**。 |
| **DS-10** | 跨节点异构域的组合与验收 | `D.5` 残留 · `P3②` 跨节点半 | DS-4 · 接口 #2721〔所需存储寿命与访问合同；具体方案按 #2721 讨论核定〕 · **DS-9** | 🟡 **跨机基本栈已交付**（#1456/#1623/#1624）。跨节点资源组合仍须 session、成员事务、实际访问与失败回收共同验收；#2721 澄清公共组合语义，不预设 ResourceBundle 类型。 |
| **DS-11** | 同步与异步模型（SDMA / URMA gate） | `D.2` 的 opt-in 半 | — | 🟡 **URMA 默认编译、显式域资源和使用前验证已交付**。main `63ffeb8a8bd9` 的 #2825 `3338ca33fa` 按 #2674 10 月 29 日确认范围删除 A5 URMA 编译开关，保留 `workspace_engines=("urma",)` 资源申请及 `device_context(..., workspace_engines=...)` 使用检查；初始化失败、空 workspace、非 dense-prefix 组合明确拒绝。#2674 CLOSED，不再要求额外 capability query / enable_urma API。SDMA 默认编译，A2/A3 enable_sdma 与 A5 按域选择仍控制资源；RDMA 仍可选。当前 URMA 限 A5 TMR L3 dense-prefix，HBG、L4 async、任意子集及 single-rank provider 路径不因此完成。#2738 的 A2/A3 上下文统一仍独立，#2855 batch 暂存不等于已提交/已完成。 |
