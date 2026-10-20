# 分线：本原理决定**什么是一条 track**

> **发布快照：2026-10-19。** 设计、实现状态与历史调查须按正文各自基线阅读；不代表当前 main 全量验收。参见[阅读入口与已知更正](README.md)。
>
> ## 从 [`logical-physical-separation.md`](logical-physical-separation.md) §7 拆出（2026-08-14）
>
> —— 母文件到 302 行触发 [`README.md`](README.md) §4 规则 5 的拆分评估。
> **拆分依据不只是长度**：§1–§6 回答「原理是什么、怎么拿它查一个 API」，本文回答
>
> ## 「工作该怎么分线」**—— 两种读者意图；而且本文是**会churn 的那一半
>
> （本周被重写两次，§1–§6 未动）。
>
> **上游原理**：[`logical-physical-separation.md`](logical-physical-separation.md) §1 的
> `potential = deployment × interconnect × backing × runtime` 公式与 §3 的检验标准。
> **实施状态真源**是 [`work-items.md`](work-items.md)；README 负责登记与里程碑。本文只谈分线判断，不记进度。
>
>
> **新增 2026-08-07，由用户指出。** §3 的检验标准是拿来查 API 的；本文说的是它的
> **上一层用法** —— 它同时决定工作**怎么分线**。我此前只用了前者，代价见下。

**原理的可执行形式（用户 2026-08-07 表述）**：

> 物理资源（内存、通信域）与逻辑概念（Worker）解耦；
>
> ## 一个 worker 能不能持有或访问一块资源，由「它部署在哪」× 「那里到资源物理位置的连通性」判定
>
> 逻辑树位置（level / worker_path）只决定**谁有权发起**，不决定**能不能**。
>
> ⚠️ **本文 373 行，已越过 [`README.md`](README.md) §4 规则 5 的 300 行拆分触发线（2026-09-07）。
> 这次不拆，理由写在这里 —— 不是没命中。**
>
> 自然的拆法是：`§0` 操作面（分线结论）留下，`§1–§3` 推导史移出。**但推导史有 14 处
> 入链引用**（`§1` ×8 · `§2a` ×2 · `§1a` · `§2` ×2 · `§3`），而本轮已经在动 6 份文档
> ＋ 5 条守卫；再拆第 7 份、同时改 14 处跨文件锚点，是在一次改动里叠第二种失败面。
>
> ⭐ **而且拆分的收益此刻是负的**：`§1–§3` 记的正是「分线判据变过三次、每次为什么变」，
> 而 `§0` 是这轮刚立的结论 —— **读 §0 的人最需要顺手看到它凭什么这么定**。
> 等 `§0` 稳定几轮之后再拆，那时推导史才真的只是附录。
>
> **拆分条件（写死，避免下次又「这次也不拆」）**：`§0` 连续两轮无实质修改，
> 或本文超过 450 行 —— 满足任一条即拆，并在同一次改动里更新那 14 处锚点。

## 0. 三条实施线：职责与交接契约

> **本节是操作面：分线的结论。** §1–§3 是推导过程，历经三次更正，保留是因为
> 它们记着每次更正的成因；**要用分线的人只需要读本节**。
>
> 工作项清单在 [`work-items.md`](work-items.md)（唯一真源，一项一归属）；
> 交付里程碑在 [`README.md`](README.md) §2；场景验收在 [`l3l4.md`](l3l4.md)。

### 0.1 职责边界

**通信路线（用户 2026-10-13 选择）**：mpirun 负责启动；通信沿用原生 CANN ACL Runtime + PTO。
DS-6 定部署/发现与跨-L3 控制接口；DS-1 的 session owner 位于 MPI group/runtime，不要求常驻 daemon。
DS-5 承接各原生 backend 的能力验收；SHMEM 仅为实现参考，不增加库适配任务。HCOMM/HCCL 不另立必选建域路线。
RA 的 Buffer/Attachment identity、ownership 与 completion 合同对所有 backend 生效。
实现证据集中在 [通信方案 D.4](l3l4/cluster-comm.md#d4-remote-l3-通信mpirun--共享窗口)，状态仍只记 work-items。

 | 线 | 前缀 | 统一负责 | **不负责**（交给谁） |
 | -- | ---- | -------- | -------------------- |
 | **资源与访问** | `RA-` | Buffer/Tensor · 所有权 · Attachment · capability 判定与 view · adapter 选择 · copy/Transfer · arena · 预算 · **用户可见的访问结果** | 跨节点会话与 rank（`DS`）· 谁在什么时候跑（`EX`） |
 | **分布式会话与通信域** | `DS-` | session/lease · 成员与 rank · 建域事务 · 远端回收 · 通信 profile · 规模扩展 | **capability 判定本身**（`RA`，它是唯一权威，`DS` 只消费）· 常驻循环（`EX`） |
 | **执行与常驻运行** | `EX-` | admission · 异步准备 · control 响应 · completion/finalize · STOP/drain | 资源身份与访问权（`RA`）· 跨节点事务（`DS`） |

⚠️ **本重组明确排除 kernel 模式与 HBG 硬件需求。** runtime/调度器、DFX、Qwen 跨仓、CI
**不进这三条线** —— 它们仍按 [`README.md`](README.md) §1.1 占行，但不受本节的交接契约管辖。
理由：它们的归属在别的仓或别的团队，硬塞进来会让「一个工作项一个归属」失去意义。

### 0.2 四处必须写死的交接契约

契约 A–D 分别约束域寿命、同步循环、访问结果与资源 close。共同的 [访问、完成与资源退休契约](worker-memory-model/access-retirement-contract.md) 是各 consumer 的验收要求；它细化交接，不声明已实现或另建工作项。

#### 契约 A —— 域生命周期：`RA-12` ⟷ `DS-4`，共享一份设计

 | — | 归属 | 内容 |
 | - | ---- | ---- |
 | 本地独立 ownership | **`RA-12`** | 把域从 `_RunResources` 的账本转成 Worker 级 owner；显式、可枚举的 registry；admission / retain-release / budget / close-journal 锁序 |
 | 远端 lease、断连与回收 | **`DS-4`** | lease 续约与超时 · 断连后的资源回收 · epoch 代际 |

⛔ **只有一份生命周期设计文档，两项共用**；`DS-4` 不得自建第三种 owner-lifetime。
本地 owner 转移不要求远端 lease 先完成；DS-4 的完整退出条件仍需远端事务与回收证据。PR 登记和实现范围见 RA-12。

#### 契约 B —— 同步原语 vs 常驻循环：`RA-11` ⟷ `EX-2`，三方切分

 | — | 归属 |
 | - | ---- |
 | 同步原语（publish/observe/wait，由 counter `notify/test/wait` 提升为 Buffer-backed Attachment 通用 API）＋ 首个有界 SPSC 示例 | **`RA-11`**，只此一份工作项与验收清单 |
 | queue schema · opcode · batching · backpressure policy · persistent-kernel 业务循环 | **上层库 / 用户** —— **不进 Buffer/Attachment 基础 ABI** |
 | 常驻循环本身的契约（`orch_fn` while 循环 · STOP · drain） | **`EX-2`** |

⇒ **`RA-11` 与 `EX-2` 不重复排期**：前者做原语，后者做循环，示例同时用到两者。

#### 契约 C —— `RA-10` 不是一个阶段，是一条接口要求

「向用户暴露最终 plan / capability / completion」**随每条访问路径一起交付**，
是 `RA-3`（本地 adapter）· `RA-5`（显式 Transfer）· `DS-3`（远端 copy）的**验收条件**，
不是排在它们后面的一格。⛔ **runtime 可按显式 policy 选 adapter，但不能 silent copy
或隐藏 identity 变化** —— 这一条对三者同时成立。源读取结束、目的可消费、操作已静止须分别证明；完成事实及 network→H2D 的失败阻断见 [共同契约 §2](worker-memory-model/access-retirement-contract.md#2-完成事实与消费边)。

#### 契约 D —— close 与 owner registry：`RA-15` ⟷ `RA-12`，先答「一个还是两个」

issue #2460 的 `U4` 写明「**`Buffer.close` 必须最终经由 owner registry，而不是绕过它直接 unlink/free**」；
`RA-12` 负责 **Worker 级的域 owner registry**。两件事都要一个「谁拥有、谁能关」的登记表。

 | — | 归属 | 登记的是什么 |
 | - | ---- | ------------ |
 | Buffer / allocation 的 close 与物理释放 | **`RA-15`** | owner / import / attachment 的 use 记录 ＋ cleanup journal |
 | 域（CommDomain）的 per-worker ownership | **`RA-12`**（远端半 `DS-4`，契约 A） | 域 handle 的 owner 转移 ＋ admission / retain-release / budget |

**共同约束**：统一逻辑关闭、接纳使用、最后使用证明与可重试清理规则，清理义务可组合且恰好履行一次；allocation owner 与 attachment/grant/graph 的使用持有者分开。互不协调的 owner-lifetime 与 close journal 才是 `P2-D` 要避免的问题，表的数量本身不能证明正确。

⛔ **组织形状仍待裁决**：一个 registry，或服从同一生命周期权威的类型化 registry。推荐后者作为可选实现形状；具体 owner、锁序、journal 组合和独立 grant 的关闭合同须在 `U4` 动工前明确。此待决项不阻止各 consumer 先交付已有的在飞保护与失败保留。

**资源边界补充**：RA-7 拆的是 L3 hierarchical HeapRing；RA-16 管 device context 的 ExecScratch/HostStaging；RA-15 管用户 Buffer 的 logical close。三者的容量和最后使用者证明不能互相代替。EX-7 扩大 admission 时消费 RA-16 的资源约束，但本地 L3 #2464 不是 #2466 的硬前置。accepted/prepare/enqueue/执行容量分别核算；旧版本、graph 引用与释放失败块的计费，以及部分提交的最后使用者证明，见 [共同契约 §3–§5](worker-memory-model/access-retirement-contract.md)。

**#2594 域接口交接**：主归属 DS-6；RA-14 管公开入口与兼容，RA-12 管复用/跨 run owner，RA-13/DS-3 管域 Buffer 的本地/远端描述与授权，RA-15 管接纳/最后使用/释放，DS-9 管建域事务。CommDomainRef 是身份而不是 grant；借用描述符不取得 ownership，已接纳使用必须保留到最后消费者。#2120 与 #2594 共用域 owner 合同；#2598 的三种清理结果仅为 DS-1 观测，不代替远端 lease 或 actual free。registry 组织仍按上面的契约 D 待裁决，不以统一 API 自动拍板。

**U4 交接门**：U1a → U1b → U2a → U2b；U1b → U3；U2b 与 U3 的 network→H2D 合并验收 → U4。工作项是聚合归属，U1b/U2a 不能被聚合依赖误读成循环；本表中的 RA-15 同时依赖 RA-13 与 DS-3。契约 D 的裁决不因 #2440/#2452 的 workspace ledger 存在而自动解决。

### 0.3 判据：一个工作项一个归属

> **一个工作项恰好属于一条实施线。它可以被任意多个交付里程碑引用，
> 但状态只在 [`work-items.md`](work-items.md) 维护一份。**
> 别处只许写 ID ＋ 一句话，不许复述状态。

**一个旧编号可以被拆给两条线，但拆分必须声明** —— 在吸收列写明「本地半 / 远端半 /
前半 / 后半」或用 `①②` 标明是哪一半。**未声明的多归属就是缺陷**，因为它与「两个格各自
以为自己拥有它」在文本上完全一样。由 `check-single-owner.py`（本地参考：`check-single-owner.py`） 机器保证。

⛔ **这条判据的代价是实测出来的**：重组前「跨 run 域生命周期」在 **5 个格 / 4 条轨**上
各维护一份状态（`P1-E` · `D.5` · `W3` · `W5` · `P2-D`）。其中三处在 PR #2120 开出来的那天
就错了，而**没有任何一处被更新** —— 因为更新哪一处都不是「唯一真源」。

⚠️ **拆分声明必须紧贴它修饰的那个编号。** 守卫的第一版把限定词按**整格**检查，
于是 `RA-12` 那格里 `#824` 后面的「本地半」替**没有声明的 `P2-D`** 作了保。
负对照当场抓到。⇒ **一个限定词只为它旁边那个编号说话。**

## 1. 推论：只有一个判定函数 —— ⛔ **依据已于 2026-08-14 重写**

> ⛔ **本文此前那张「五个求值时刻」的表不成立**（核验于 `upstream/main@3f4686c1`，detached 只读）。
> 表里 **两行的符号在仓库中不存在**、**一行的两个「时刻」在代码里本就是一个结构**、
> **一行答的是另一个问题**。原文保留在 §1a 以便追溯，因为 §2 的合并裁决建立在它上面。

**原理本身不变**（§1 的 `potential = deployment × interconnect × backing × runtime` 公式照旧）：
逻辑树位置只决定谁有权发起，不决定能不能。**变的是「这个原理在代码里已经收拢成一个函数」这个
事实判断** —— 它今天不成立。

**实际状态（每条都可 grep 复核，0 命中一律附阳性对照）**：

 | 原表写的 | 代码里的实际 |
 | -------- | ------------ |
 | `RegionAccessService`（计划 region 时） | ✅ 存在，但**全仓只有 1 个 evaluator**（`_evaluate_host_vmm_copy`，`comm_endpoints.py:636`）＋ **1 个调用点**（`:811`）⇒ 是「一个函数求值一次」，不是「五次中的一次」 |
 | `ImportContext`（物化 import 时） | ✅ 存在，但**只有 2 个字段**（`buffer.py:562-563`：`is_host_endpoint` ＋ `owning_chip_instance_id`），与上者**只共享一个字符串枚举** |
 | `AttachmentPlan`（建域时） | ⛔ **此名不存在**。真名 `MemberAttachmentPlan`（`comm_endpoints.py:540`），**且它自带 `adapter_kind` ＋ `adapter_profile`**（`:543-544`）⇒ **原表分开列的「建域」与「选 adapter」两个时刻，代码里本就是同一个结构** |
 | `TransportPlan`（选 adapter 时） | ⛔ **全仓 0 命中**（阳性对照：`MemberAttachmentPlan` 在 `comm_endpoints.py` 7 处 ＋ `comm_region.py` 3 处 ⇒ 搜索有效）。代码、注释、docs 皆无 |
 | `comm_profile`（跨节点断言） | ⚠️ 存在，**但不是同一个函数** —— 见 §1b |

⇒ **真相**：**一个函数在一处求值，外加约十几个各自独立的门，横跨两种语言，靠注释与惯例
而非构造彼此对齐。** 「五处分散所以要收拢」这个前提本身不成立。

⚠️ **但错不在契约文档 —— 这一条 2026-08-14 二次更正** ：
[`cpu-npu-shared-memory/domain-membership.md`](cpu-npu-shared-memory/domain-membership.md) §3

### 明写它引的是 [`feature_roadmap.md`](feature_roadmap.md) 的规范形式

（`CommDomain = MemberSet + ResourceBundle + AttachmentPlan + TransportPlan`），
**它是目标契约，从未声称已实现**。
⇒ **错在把规范当成现状引用的地方** —— 本文原表与 [`README.md`](README.md) §1.5 把这些名字
列成「同一函数在仓库里**出现**的五处」。**契约文档没有问题，看板有问题。**
⭐ 判据：**引用一个名字时必须说明它是「代码里有」还是「契约里要」** ——
两者在句子里长得一样，但一个可以 grep、另一个不能。

### 1a 判定引擎今天还不是引擎（原三条硬证据 —— ⛔ **两条已被 AR1 推翻，2026-09-07 复核 `f45d43a67`**）

> ⛔ **本节原本是「先别接，中心还没成立」的论据。三条里两条已经不成立了** ——
> 这是**代码追上了文档**，不是文档写错过。判据没变，**结论反过来了：中心已经成立，可以接。**

1. ⛔ **「扩展点是假的」已推翻。** 原文引的 `evaluate_region_access` **全仓 0 命中**
   （阳性对照：同名族的 `evaluate_buffer_access` 在 `comm_endpoints.py` 4 处 ＋ `buffer.py:622`）。
   今天是 `RegionAccessService` **Protocol**（`comm_endpoints.py:658`）＋ 至少两个实现
   （`DefaultRegionAccessService` `:698`、可注入的静态 service `:774`）。
2. ⛔ **「4/6 个 `BackendKind` 结构性绕过引擎」已推翻，且是同一处改动的后果。**
   `return ()` 在 `comm_endpoints.py` **0 命中**（阳性对照：`return (` 10 处 ⇒ 搜索有效）。
   不被支持的 backend 现在**返回一个带 `unsupported_reason` 的候选**
   （`_explicit_transfer_candidates`，`:1022`）而不是空元组 ⇒ **service 每次都被问到，
   拒绝理由由它给出**。空候选短路没有了 ⇒ **注入一个宽松 service 现在真的能放宽东西。**
3. ✅ **「引擎的答案不权威」仍然成立**（本条**复核后保留**）：`comm_region.py:765`
   仍然 `if provider.deployment is not DEVICE_AICPU` 硬拒，而判定侧仍把三值写成
   `in (DEVICE_AICORE, DEVICE_AICPU)`（`comm_endpoints.py:960` `:1049`、`buffer.py:618`）
   ⇒ **引擎批准的 `DEVICE_AICORE` 会在物化时被拒**。
   ⭐ **更硬一层同样成立**：C++ `AddressSpace`（`src/common/task_interface/data_type.h:39-42`）
   **只有 `HOST`/`DEVICE` 两值**，且是 wire 共享类型 ⇒ **三值在 wire 上无处可去。**

⭐ **这一节的教训不是「我写错了」，是「一条以现状为前提的建议有保质期」** ——
「先别接」的三条论据里有两条在四周内被 AR1 拆掉，而本节**没有任何机制会告诉我这件事**。
⇒ **判据：凡是「现在还不能做 X，因为 Y」的结论，都要把 Y 写成可 grep 的形式并标口径 SHA**，
否则它会以「已复核」的姿态继续挡着一件其实已经能做的事。

⛔ **「词汇两两零交集」已过时 —— 交集是单向长出来的**（2026-09-07 重测 `f45d43a67`，
原口径见下）：

 | 词 | `comm_endpoints.py` | `buffer.py` | `global_comm_domain.py` |
 | -- | ------------------- | ----------- | ----------------------- |
 | `deployment` | 44（原 41） | **8（原 0）** | **6（原 0）** |
 | `address_space` / `AddressSpace` | 1，仍只在 docstring | 21（原 23） | **8（原 0）** |

⇒ **`deployment` 已经渗进另外两个模块，`AddressSpace` 没有渗回 `comm_endpoints.py`。**
而「唯一的代码级联系是一个字符串枚举」这句**已经明确不对**：`buffer.py` 今天从
`comm_endpoints` import **10 个名字**，其中包括**查询类型 `BufferAccessQuery`、
服务实现 `DefaultRegionAccessService`、候选枚举器 `buffer_adapter_candidates`**
—— 也就是判定引擎本身，不只是它的拒绝词汇。

⭐ **而这个分离是刻意的、写在代码里的** —— `ImportContext` 的 docstring 原文
（`buffer.py:553-558`）：*"The two mechanisms share only this rejection vocabulary, not a live
registry object"*，理由是它**建在 fork 出的子进程里**，早于
`EndpointRegistry.from_snapshot()` 的 `_require_ready_for_region_planning()` 前置能成立；
且 registry 的拓扑快照**无 per-chip 身份**。
⇒ **本文此前把它叙述成「同一个函数散落五处」，是文档偏离了代码。**

⚠️ **但这段 docstring 的引文本身要重新定位**（2026-09-07）：*"share only this rejection
vocabulary, not a live registry object"* —— **后半句仍然对**（`ImportContext` 建在 fork 出的
子进程里，拿不到 `EndpointRegistry` 的 live 对象），**前半句已经不对**：共享的不止拒绝词汇，
还有整个 `BufferAccessQuery` → `buffer_adapter_candidates` → `evaluate_buffer_access` 判定链。
⇒ ⭐ **「共用判据」与「共用 live registry 对象」是两件事，今天第一件成立、第二件不成立** ——
本节的边界论证必须按这条重画，不能再用「只共享一个枚举」当论据。

#### 1b `comm_profile` 问的是另一个问题（名字撞了，函数没同一）

 | — | 入参 | 问什么 | 输出 |
 | - | ---- | ------ | ---- |
 | `_validate_global_comm_capability`（`worker.py:634`）· `resolve_global_comm_capability`（`global_comm_domain.py:132`） | **两个字符串** `(platform, comm_profile)` | 这块**硅**能不能实现这个 profile（`a3-fabric-v1` 要求真 A3、非 sim） | `raise` |
 | `_evaluate_host_vmm_copy`（`comm_endpoints.py:636`） | `(backend_kind, provider.deployment, consumer.deployment, same_node)` | 这**两个端点之间**能不能这样访问 | `RegionAccessDecision` |

**入参无一相同；一个非关系型、一个关系型。** 前者是**平台后端支持度**门，后者是**端点关系**门。
⇒ 它们被同一个词命名了（`resolve_global_comm_capability` vs `RegionAccessReasonCode`），
**这正是它看起来「已共用判定函数」的原因**。

## 2. 两条 track 仍是一条 —— 但理由要换；**L4 集群不并入**

⛔ **本段的前提翻过两次，最新一次是 2026-09-07（复核 `f45d43a67`）。**
2026-08-14 记的是「§1 的表塌了 ⇒ 原论证（两条都要 W2）不再成立 ——
`buffer.py` 与 `comm_endpoints.py` 今天并没有共用判定函数」。
**那句话现在不对了**：AR1 之后 `buffer.py` 从 `comm_endpoints` import **10 个名字**，
其中 `BufferAccessQuery` / `DefaultRegionAccessService` / `buffer_adapter_candidates`
**就是判定引擎本身**，`select_adapter`（`buffer.py:621-622`）逐候选调 `evaluate_buffer_access`。
⇒ **原论证复活了，而且比 2026-08-07 提出时更硬** —— 当时是「两条都需要 W2」，
今天是「两条已经在跑同一份实现」。
⚠️ **反向依赖仍为 0**（`comm_endpoints.py` 不 import `buffer`）—— 这一点在 L4 那半是关键判据，见下。

✅ **合并裁决因此有两条独立理由，任何一条单独成立即可**：

> **`CanonicalIdentity`（`src/common/task_interface/buffer.h:118-123`，32 B：`owner_instance_id`
> ＋ `buffer_id` ＋ `generation`）不含任何「资源在哪」的信息。**
> ⇒ 资源侧的 wire **结构性地无法表达 capability 的输入**；判定所需的上下文只能由
> registry/owner authority 供给。**这不是「两边互不知情」，是「不合并的话判定函数永远拿不到
> 它要的输入」** —— 比原理由硬，且不依赖那张表。

 | 原来的分线 | 它其实在做什么 |
 | ---------- | -------------- |
 | **CPU↔NPU 共享内存**（W0–W8） | capability 的**物理输入**（W0 部署探测、互连矩阵）＋ 判定引擎本身（W2 / #1696）＋ 域的成员/backing 载体 |
 | **worker 内存模型 P1** | capability 的**资源侧身份**（`BufferDescriptor` / `Tensor`，已冻结）＋ 第二个求值点（`ImportContext`） |

⛔ **L4 集群（`l3l4.md` 的 A–E 九章）不并入**，理由是**它问的根本是另一个问题**：

结论仍然是**不并入**，但**支撑它的依据换了一套** —— 原来的三条里两条已被 #1879
`5cb8110d` 杀掉，重新推导如下（核验于 `upstream/main@d1eb826f`）。

**唯一存活的原依据**：`GlobalDomainMember`（`global_comm_domain.py`）四个字段**全是树位置 ＋
rank**，**无 `deployment`、无 `address_space`** —— ✅ 仍成立，而这正是 attachment 裁决要保住的
那条（attachment 与 `members` 平行、rank membership 不变，见
[`domain-member-vs-attachment.md`](cpu-npu-shared-memory/domain-member-vs-attachment.md) §5.2）。

⛔ **两条已死，不要再照它们复核**：

 | 原依据 | 现状（实测 `d1eb826f`） |
 | ------ | ----------------------- |
 | `global_comm_domain.py` **零包内依赖**（只 import `enum`/`struct`/`dataclass`）；把 `EndpointDeployment`/`EndpointRegistry`/`RegionAccessService`/`AdapterKind`/`AttachmentRole` 一起 grep 它加四个 cluster 模块 ＝ **0 命中** | ⛔ **已死**：`:18` `from _task_interface import AddressSpace` · `:19` `from .comm_endpoints import AdapterKind, AdapterProfile, AttachmentRole` |
 | `_allocate_global_domain` **只调 `resolve_global_comm_capability`**，**从不碰 `RegionAccessService` / `EndpointRegistry`** | ⛔ **已死**：`_global_domain_attachment_matrix`（`worker.py:9173`）内部取 `self._get_endpoint_registry()` 并构造 `BackendResolver(registry, self._get_region_access_service())`（`:9190`），由 `_allocate_global_domain` 调用。⭐ **这条桥是 #1879 新建的** —— 该函数在 `5cb8110d~1` 上 **0 命中**（阳性对照：同 commit 同文件 `_allocate_global_domain` 1 命中 ⇒ grep 姿势没错） |

连带作废：「**两棵树只在 `worker.py` 一处相遇**」—— 它们现在也在 `global_comm_domain.py` 相遇。

⇒ **重新推导后的理由，而且它比原来的强**：§3 的合并判据是「**同一件事被写第二遍 ⇒ 该并线**」。
而 #1879 **不是把 capability 判定重写了一遍，是消费了它** ——
`_global_domain_attachment_matrix` 的 docstring 原话
*"The endpoint planner remains the authority for adapter selection"*。
⇒ 新形状是「**一个权威 ＋ 多个消费者**」，**这正是这条 track 想要的缝**；
**缝能用，恰好说明不必并线** —— 若当初并了线，就不会有这条可被消费的边界，
只会有一个内部函数调用。

⚠️ **判据也要跟着换**：以前「不并入」靠「两棵树互不认识」来证；现在它们互相认识了，
所以要看的是**方向**：**依赖是单向的（cluster → capability 判定），且被依赖的一侧仍是唯一权威**。
⇒ **一旦出现反向依赖（capability 判定 import cluster 模块），或者出现第二个判定实现，
§3 的判据就该触发** —— 那才是并线的信号。

⚠️ **仍成立的一处文档层重复**：[`l3l4/memory-kinds.md`](l3l4/memory-kinds.md) 在规定
`BufferHandle` / canonical identity（`owner_instance_id` ＋ `buffer_id` ＋ `generation`）——
那是 worker 内存模型的词汇与交付物。**这是文档重复（一份权威、另一份改引用），不是代码层碰撞。**

### 2a ✅ 那条边已由 #1879 补上（2026-08-18）

> ✅ **已交付：#1879 `5cb8110d`** —— `_append_endpoint_topology` 现在遍历
> `worker._mpi_worker_ids`（`worker.py:6381-6383`），按
> `_normalize_node_identity(group.spec.hosts[rank.rank])` 定 node identity，
> 建 HOST_CPU 条目 ＋ 走 `_append_device_endpoint_topology` 建 device 条目。
>
> #### 本节以下的缺陷描述是交付前的记载，保留以便追溯
>
> ⛔ **而 #1879 的 PR body 四条 bullet 一个字都没提这件事**（它讲的是 attachment 矩阵、
> `PREPARE_EXPORT`、MPI/非 MPI 同一 wire 形状、删 `DomainAttachment` 别名）——
> ⇒ **只读 PR 描述会以为这条边还开着**。本目录就是这样把它当成「下一步」多挂了一天。
>
> #### 判据：一条缺陷是否已闭，只能对代码核，不能对 PR 描述核
>
> ⚠️ **它带来的一处结构性变化**：本节原来的论点是「补这条边是 XS」，而 #1879 顺手做掉它
> 说明**这条边本来就该和 attachment 轴一起做** —— attachment 表按 receiver-node × rank
> 组织，而 MPI 拉起的 L3 若不在端点注册表里，它的行就无从解析。⇒ **两者不是巧合相邻，
> 是同一个数据结构的两半。**

`_append_endpoint_topology`（`worker.py:6346`，**全仓唯一的拓扑构建者**）遍历
`_next_level_workers`（`:6362`）与 `_remote_worker_specs`（`:6365`），
**不遍历 `_mpi_l3_groups`** —— 而同一文件里 **7 处**用的是
`_remote_like_worker_ids()` ＝ `remote | mpi`（`:4780-4781`）。

⇒ **经 `add_mpirun_worker_group` 拉起的 L3 及其 L2，在端点注册表里根本不存在**，
于是任何跨节点 region 计划都看不到它们。

**判为缺陷而非设计，三条依据**：
① `register(RemoteCallable)` **明确同等对待两条路径**（`:6523` 检查
`_remote_worker_specs or _mpi_l3_groups`，错误消息原文 *"…add_remote_worker or
add_mpirun_worker_group"*）· ② `MpiL3GroupSpec` **已带齐建端点所需的全部信息**
（`hosts` ＋ `device_ids_by_rank`，`:718-722`）· ③ `tests/ut/py/test_worker/test_comm_endpoints.py`
里 **`mpi` 0 命中**（两种拼法皆 0）⇒ 无测试覆盖，不是有意排除。

> ⚠️ **阳性对照的数字要连拼法一起写**，否则会被读成「计数变了」：`remote` 区分大小写是
> **19 处 / 16 行**，不区分是 **34 处 / 28 行** —— 同一份文件的两种量法。
> 而该文件自 **`c9e5f3cc`（2026-08-08，#1696）起一次都没被改过**（`git log -- <该文件>`
> 只有那一条），所以计数**不可能**变。结论两种量法下都不变：**`mpi` 恒为 0**。

✅ **已实测（2026-08-18，`upstream/main@d6e27325`，在一棵编译过的树上跑；探针事后已删）**
—— ⚠️ **这份实测量的是 `d6e27325`，即 #1879 之前**，所以它记录的是**缺陷存在时**的现场；
\#1879 之后同一探针应当看到 MPI 的 worker id 出现在拓扑里。
此前本节记「未能实测：本树无编译好的 `_task_interface`」，该限制已解除。
探针无网络、无设备（`add_mpirun_worker_group` 只登记 spec，不拉 `mpirun`），
**同一次运行里带阳性对照**：

```text
_remote_worker_specs : 1        _mpi_l3_groups : 1
_remote_like_worker_ids(): [0, 1, 2]      ← 三个都在

endpoint topology entries: 6                ← 全部属于 remote 那一个
  L4 · L4/L3[0] · L4/L3[0]/L2[0]×2 · L4/L3[0]/L2[1]×2

control  (remote L3 worker_id=0) present: True   → 5 条
subject  (mpi    L3 worker_id=1) present: False  → []
subject  (mpi    L3 worker_id=2) present: False  → []
```

> ⚠️ **配方注：探针要用非 loopback 的 host，否则 node identity 全塌成 `local`。**
> `_normalize_node_identity`（`comm_endpoints.py:139-149`）的判据是 `is_loopback`，
> 所以 **整个 `127.0.0.0/8` 块**（`127.0.0.1` 与 `127.0.0.2` 一样）**＋ `::1` ＋ 字面
> `localhost`** 全部返回 `"local"`；非 loopback 才返回规范化后的地址串。
>
> ⭐ **这不是缺陷** —— 那些 rank 确实同机，标 `local` 是对的。**但它会让读者误判**：
> 「MPI rank 明明在 `127.0.0.2`，却报 `node=local`」看起来像 identity 丢了。
>
> ⛔ **而它的实际代价比标签更重**：#1879 的 MPI 分支用
> `_normalize_node_identity(group.spec.hosts[rank.rank])` 定 node identity ⇒
> **一份全 loopback 的 MPI spec 会让每个 rank 都落在同一个 node 上**，
> 于是 attachment 矩阵**只走得到「同节点」那一档**：实测同 host 时 **4/4 全解出**，
> 真跨机时**只有对角线解出、非对角线 `None`（2/4）**
> （见 [`cpu-npu-shared-memory/domain-member-vs-attachment.md`](cpu-npu-shared-memory/domain-member-vs-attachment.md) §5.2 更正 ①）。
> ⇒ **用 loopback 探针会安静地把自己关在「全部解出」那一档里，看不到真正有意思的那一半。**
>
> ⇒ **要看到不同 node identity，必须用非 loopback** —— 这正是 #1882 那条跨机测试
> 用 `10.0.0.1` / `10.0.0.2` 的原因。

⭐ **阳性对照就在同一次运行里**：`_remote_like_worker_ids()` 认得全部三个 worker，
而拓扑里**只有 remote 那一支**的 6 条 —— 所以这不是「探针没跑起来」，
是**跑起来了并且看见了缺口**。⇒ **静态判断成立，且现在可以写进 PR 描述。**

⭐ **实测顺带纠正本目录一处记法**：`_append_device_endpoint_topology`（`:6371`）
**每个 L2 发两条**——`DEVICE_AICORE` ＋ `DEVICE_AICPU`（`:6379-6381`），
不是只发 AICORE；`_endpoint_topology_sort_key` 的 `deployment_order` 也已是**三值**
（`HOST_CPU:0, DEVICE_AICORE:1, DEVICE_AICPU:2`）⇒ AICPU 条目是排序的一等公民。
这解释了上面「每个 L2 ×2」的计数。
⚠️ **对 §1 那条「registry 快照没有 per-chip 身份」的论证没有影响** ——
**两条都不带 `owner_instance_id`**（只有 `include_self` 那条 HOST_CPU 条目带）。
⇒ 凡引用「device 条目只有 `DEVICE_AICORE`」的地方都要改；本节已改。

⚠️ **2026-08-14 补一条更强的证据：盲区活过了 `#1624`** —— 该 PR（merged `43094d44`，标题就是 *"dispatch MPI worker groups through mailbox"*）改了 `worker.py` **501 行**、新增 `mpi_group_mailbox.py` 559 行，**主题正是 MPI**；而 `_append_endpoint_topology` 在其后仍**只遍历 `_next_level_workers` 与 `_remote_worker_specs`**（实测于 `43094d44:python/simpler/worker.py`）。；⇒ **一次专做 MPI 的大改动都没碰它**，说明它不是「等 MPI 支持成熟再接」的有意留白，而是**没人注意到端点注册表这条路径** —— 与「`register(RemoteCallable)` 同等对待两条路径」「`MpiL3GroupSpec` 信息已齐」「测试 0 覆盖」三条合起来，**判缺陷的依据现在有四条**。；⭐ **顺带**：`#1624` 对 `global_comm_domain.py` **只有 +4 −1**（加 `GLOBAL_DOMAIN_MAX_BUFFERS` ＋ 修 `decode_domain_command` 误用 `MAX_RANKS` 校验 buffer 数那个真 bug），**成员 wire 一字未动** ⇒ 与 §1b 的判断一致：**跨机栈三条（#1456 · #1623 · #1624）全部合入，无一碰成员格式**。；

⇒ ~~**补这条边是 XS、有正确性判据**~~ ✅ **已由 #1879 补上**；而「**合并三棵树是大动作，且按上面的结构没有收益**」这半**仍然成立** —— 本节的整体结论（不整合、只补边）**被交付方式证实了**：**补的正是一条边，而三棵树仍是三棵。**

### 2b ⭐ 唯一值得「整合」的地方：把矩阵落进代码

[`domain-membership.md`](cpu-npu-shared-memory/domain-membership.md) §4.1 的互连能力矩阵
`(部署A, 部署B, 互连) → {D, C, T, H}` 是**这三份文档里真正的资产** ——
**整张表没有一个 `level` 字**，只依赖部署与物理拓扑，正是 §1 公式要的形状。

#### 规模差距已量化**（见该节新增的对表）：**本表 20 格，代码今天真正做判定的是 1 格

四个 `BackendKind` 连候选都产不出（`_adapter_candidates:899`），`H` 列在代码里没有载体，
`T` 列整列生来带 `unsupported_reason`。

⭐ **而本表比代码多一样东西：它记录「没查过」** —— 跨机 `D`/`H` 标 🟡 并注明
*「不是保守，是没查过」*。**代码里「未探测」与「不支持」完全同形**
（都走到 `UNSUPPORTED_DEPLOYMENT_COMBINATION`）⇒
**落地时「未探测」必须是一个不同的返回值**，`NO_IMPLEMENTED_DIRECT_MAP_PROBE` 是对的先例。

⇒ **这才是「资源模型 ↔ capability 判定」该整合的东西：把契约往代码里落，不是把文档并到一起。**

## 3. ⚠️ 教训：把原理当 checklist 用，重复就是结构性必然

`work-breakdown.md` §4 的标题一度是 **「三个交叠提醒（勿各做一套）」** —— 三次记录到这两条线
撞在一起，三次的处理都是**记一条提醒**而不是**合并**。而本文 §1 的
`potential = deployment × interconnect × …` 公式**早就写着**答案。

> ### ⇒ 判据：当你第二次为同两条 track 写下「别各做一套」时，那不是提醒该加，是分线该改
>
> 提醒依赖人记得；分线不依赖。

这与 §6 是同一个错误的两层：§6 是**两轮 review 各自在文档组内部查一致性**，
本文是**两条 track 各自在内部排期** —— 都是「在子集内自洽」被当成了「全局自洽」。
