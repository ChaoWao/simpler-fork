# 分线：本原理决定**什么是一条 track**

> **发布快照：2026-11-05。** 本轮为资源、部署通信、执行与跨仓目标的跟踪交接；不代表当前 main 全量验收。源码事实按各自 SHA 阅读，资源进度查 GitHub。参见[阅读入口](README.md)。
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
> **业务进度以 GitHub 为准**；[`work-items.md`](work-items.md) 保留目标与验收索引，README 负责登记与里程碑。本文只谈分线判断，不记进度。
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

## 0. GitHub 交付归属与共同契约

[#2716](https://github.com/hw-native-sys/simpler/issues/2716) 的四方向承载完整目标。资源设计直接跟踪 [#2721](https://github.com/hw-native-sys/simpler/issues/2721) 及 [资源入口](work-items.md#2-资源与访问github-入口)，不维护本地 RA 进度；GitHub 方向入口 保留验收索引。场景所需基础修改随场景交付，不因跨模块而重复拆 owner。历史编号见 [交接对照](review/ra-handoff.md)。

跨仓模型整体目标由 [#2658](https://github.com/hw-native-sys/simpler/issues/2658) 跟踪，Qwen 旧 Q1–Q4/C2b 只留 [验收交接](review/qwen-handoff.md)。#2429/#2666 仍负责 vLLM 接入与执行资源，不能把整体验收重新分配给本地进度轨。

部署/会话与通信/数据面分别对接 #2724 与 #2722；旧 DS 目标和评论见 [交接对照](review/ds-handoff.md)。部署侧提供真实 session/endpoint 信息，通信侧定义域成员、事务与释放结果；额外 lease、规模和组合类型必要性仍按评论讨论。

### 0.1 责任边界

- 公共 Buffer/Tensor、逐调用请求、内部绑定和关闭：#2460；子项 #2888 清理公共 COW 数据路径，#1931 校验 child free 身份。
- 共享区间与必要访问／同步：#2720。queue、发布协议和背压由应用或上层库定义；runtime 自己的调度、取消和 drain 仍由 runtime 负责。
- 平台映射：#2495 下 #2496/#2497；只约束实际需要该能力的组合。
- 通信域 ownership：方向 2 的 #824；远端 lease 和回收与 #2722 交接。
- 执行资源：#2666 的共同 prepare/launch/retain/release、#2883 的资源分类及 #2658 的真实模型流水组合。通用 kernel/eager-capture 实现仍按既定范围排除。

### 0.2 四处交接契约

**契约 A：域生命周期。** #824 与 #2722 共用 owner、使用接纳、最后使用和失败清理规则。本地域可先交付，远端完整验收仍需 lease／事务证据；不得另建互不协调的寿命模型。

**契约 B：基础同步与常驻循环。** #2720 定义公共区间及必要可见性，#2723／#2723 定义单 L2 Run 多轮交互、停止与排空。应用定义 queue／握手协议；示例使用两者，但不产生公共 queue 工厂承诺。

**契约 C：访问结果。** 本地绑定、显式复制和 #2722 远端访问都必须能分辨实际路径、身份及完成条件；不得静默复制或伪造共享映射。是否需要公共 plan/capability 对象，由 [设计问题](https://github.com/hw-native-sys/simpler/issues/2721#issuecomment-6651217998)决定。源读取结束、目的可见和整体静止分别证明，不要求机械增加 event 类型。

**契约 D：关闭与资源保留。** #2460 与 #824 统一逻辑关闭、接纳使用、最后使用证明及可重试清理义务。allocation owner、内部映射与实际使用引用分开；OS／驱动真实限制保留，不新增公共授权对象。具体 registry、锁序和 journal 组合须在相关实现中明确，表的数量不是验收目标；已有 consumer 的在飞保护不等待完整 U4。

### 0.3 场景交接与验收

Host 场景 #2657/#2730/#2731、域接口 #2594 及传输 #2621 保持完整目标的唯一交付归属。公共模型只提供共同身份、范围、上下文、内部绑定与寿命规则；实际注册、workspace、完成与异常清理随消费者验收。

临时／持久／异步使用必须有可验证的寿命，是否拆分 Arena 尚在 #2721 讨论。device workspace 与用户 Buffer 的预算和最后使用不能互相代替。公开 ResourceBundle、plan、capability 也须有必要场景才能成为交付要求。

具体联合验收见 [访问与退休契约](worker-memory-model/access-retirement-contract.md)；逐旧编号归属由守卫核对 GitHub 入口及 GitHub 方向入口 索引。接口依赖写清所需产物，不把整个 Issue 的关闭当作统一前置。下方 §1 起保留具名基线的历史推导，不作为独立排期。

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
