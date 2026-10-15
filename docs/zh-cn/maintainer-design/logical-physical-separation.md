# 总纲：逻辑组件 ⊥ 物理资源

> **发布快照：2026-10-15。** 设计、实现状态与历史调查须按正文各自基线阅读；不代表当前 main 全量验收。参见[阅读入口与已知更正](README.md)。
>
> **状态**：📐 **设计原理**（非某个特性的设计文档）。它统摄 worker 模型、通信域、
> 内存模型三块 —— 这三块现在分散在不同文档里各说各的，本文给它们一个共同的判据。
> **来源**：2026-07-24～27 系列设计讨论中逐步析出，由用户在 07-27 明确表述。

## 1. 原理

```text
逻辑层    Worker · 通信域 · Tensor            ← 用户看到的、编排的
   ↕                                          Attachment / capability 边
物理层    host DDR Buffer · device GM Buffer · 互连通道   ← 硬件事实
```

> **逻辑组件不"拥有"物理能力；potential capability 由部署、互连、backing/backend
> 和 runtime support 决定，actual capability 还需要授权、live
> Attachment/materialization、runtime probe 与 lease。能力挂在逻辑端点与物理资源的边上。**

```text
potential = deployment × interconnect × backing/backend × runtime support
actual    = potential ∩ authorization ∩ live Attachment/materialization
            ∩ runtime probe/lease
```

在通信域上，这条原理的**规范落地形式**就是
[feature_roadmap.md](feature_roadmap.md) 的
`AttachmentPlan = (member EndpointId, CanonicalBufferIdentity) -> capability + attachment`
——键是「谁 × 哪块资源」这条边，值才是能力。**本文说的是原理，那是它的具体形式，
不是两套判据。**

通信域的组成也沿用同一规范：

```text
CommDomain = MemberSet + ResourceBundle + AttachmentPlan + TransportPlan
             [+ DeviceRankMap]
DomainRegionSet = ResourceBundle 中按 owner 组织的 Buffer-backed entry 子集
```

`ResourceBundle` 还可以包含 communicator 与 adapter state；因此
`DomainRegionSet` 不与 `ResourceBundle` 同义，也不能在 CommDomain 公式中替代它。

## 2. 我们此前每一条发现，都是它的实例

| 发现 | 在本原理下是什么 |
| ---- | ---------------- |
| **worker 的 level 是逻辑概念** | Worker 是逻辑组件；level 是编排树位置，**与部署正交** |
| **potential / actual capability 分层** | 就是那条**绑定规则**本身（规范形式 = `AttachmentPlan`）；互连矩阵只覆盖 potential 的必要门槛 |
| **`Buffer` vs `Tensor`** | `Buffer`/backing = 物理 allocation；`Tensor` = 逻辑视图（**`BufferRef` / `TensorView` 两名已废弃，统一为 `Tensor`**）。owner-side `BufferHandle` 只管理生命周期；一个 Buffer 可被多个 `Tensor` 引用 |
| **不含 level 的互连矩阵** | 绑定规则的**查找表** |
| **`ResourceBundle`** | **一个逻辑域组合多种异构资源**；`DomainRegionSet` 只收纳其中按 owner 组织的 Buffer-backed entries，communicator/adapter state 仍属于 bundle |
| **`halHostRegister` 不支持 VMM VA** | **物理资源有固有能力边界** —— 逻辑层不能许诺硬件给不了的 |
| **CPU 不能当 HCCL rank** | CPU 是**访问视图**，不是额外 rank |

## 3. 检验标准（最有用的部分）

> ### 任何把「逻辑组件」和「物理资源」焊在一起的 API，都是错的

回头看，我们抓到的每个问题都能用这一句解释：

| 问题 | 焊死了什么 |
| ---- | ---------- |
| `create_worker_chip_region` / `create_worker_chip_queue` | **层级（逻辑）** 焊进了 **传输（物理）** |
| `worker.py` 的 `level == 2 ⇒ ChipWorker ⇒ NPU` | **编排位置（逻辑）** 焊进了 **部署（物理）** |
| 「一块内存同时 VMM-import + host-register」 | **逻辑域** 许诺了 **物理资源**给不了的能力 |
| `FORK_SHM` 一个标签两种写语义 | **两种物理资源**（`MAP_SHARED` / COW）焊进了 **一个逻辑分类** |
| 依赖键用 `Tensor.data`（物化地址） | **物理地址** 冒充了 **逻辑身份** |

**用法**：评审任何新 API 时问一句 —— *它的参数里，哪些是逻辑的、哪些是物理的？
两者是不是被一个名字/一个枚举绑死了？*

## 4. 三处应用

| 领域 | 逻辑组件 | 物理资源 | 绑定判据 | 文档 |
| ---- | -------- | -------- | -------- | ---- |
| **Worker 模型** | Worker（level=编排树位置） | CPU 进程 / NPU chip | 部署 | [cpu-npu-shared-memory/domain-membership.md](cpu-npu-shared-memory/domain-membership.md) §2 |
| **通信域** | 域成员（`EndpointId`） | `ResourceBundle` 中的 Buffer、communicator、adapter state 与相关互连；`DomainRegionSet` 只是 Buffer-backed entry 子集 | `AttachmentPlan`：`(member, CanonicalBufferIdentity) → capability + attachment`，配合 `TransportPlan` 选路（互连矩阵只是必要门槛） | 同上 §3/§4 |
| **内存模型** | `Tensor`（视图，跨 edge 走线的就是它） | `Buffer` 的 backing；`BufferHandle` 只是 owner-side 管理对象 | capability 组合 | [worker-memory-model/p1b-corrected-design.md](worker-memory-model/p1b-corrected-design.md) §0 |

## 5. 一条重要的推论：**降级必须显式，或语义等价**

逻辑组件请求某种能力，而物理资源给不了时，只有两个正当出路：

| 出路 | 前提 |
| ---- | ---- |
| **显式失败** | 总是正当 |
| **降级到另一种物理实现** | **必须语义等价** —— 否则就是静默改变行为 |

在本仓的具体形态，**判据是 Buffer identity**。用
[feature_roadmap.md](feature_roadmap.md) 规范的 `TransportPlan` adapter 命名
（**不要另造名字**）：

| adapter | 语义 | Buffer identity | 可否作为等价选路 |
| ------- | ---- | --------------- | ---------------- |
| **HCCL collective** | 对指定的既有 Buffer 执行 device collective | adapter 本身不创建新 Buffer identity | ⛔ collective 的操作语义不同 |
| **direct-map / device-peer** | 另一端点直接映射 / peer 访问同一个 canonical Buffer | **保持原 identity** | ✅ 仅与同一 Buffer 上等效读写的 owner-delegated copy 互换 |
| **owner-delegated copy** | 委派 owner 读写**同一个既有 canonical Buffer** | **保持原 identity** | ✅ 仅与同一 Buffer 上等效读写的 direct-map 互换 |
| **explicit transfer** | **分配目标 Buffer 并复制过去** | **创建新 identity** | ⛔ **不在等价关系内** |

- **direct-map ↔ owner-delegated copy 的等价范围**（见
  [cpu-npu-shared-memory.md §1.2](cpu-npu-shared-memory.md)）严格限于：两者作用在
  **同一个 canonical Buffer**，具有相同读写效果并沿用同一同步契约。满足这些条件时，
  显式 data-path policy 可以允许二者互换，但 runtime 的最终选择必须可观察。
- ⛔ **explicit transfer 不是 direct-map 的降级形态**。它产生新的
  `CanonicalBufferIdentity`（wire 类型名为 `CanonicalIdentity`），依赖边、别名判定、生命周期归属全部随之改变，而且两端没有
  隐式 coherence——**那是换语义，不是降级。**
- **跨 identity 边界不得静默选路**：必须按**显式 policy** 选择，且选择结果
  **对调用者可见**。
- 其余不等价降级（如静默 host-staging 替代 peer access）**一律禁止** —— 这条已写进
  [worker-memory-model/p1b-corrected-design.md](worker-memory-model/p1b-corrected-design.md)
  的 backend 门控矩阵「不静默回退」。

> **两条教训**（都属 §3 检验标准，只是发生在**词汇层**而非 API 层）：
> ① 本节早前只写"share ↔ copy 等价"，没把 transfer 单列 —— "copy" 一词同时指
> "写进已有 Buffer" 与 "造一个新 Buffer 再写"，**一个词两个语义**。
> ② 补救时我又自造了 "Attachment / copy / Transfer" 三分法，而 roadmap 规范里
> **早已有 `TransportPlan` 的四个 adapter** —— **同一个概念两套名字**，和 ① 是
> 同一类错误的镜像。**先查规范,再造词。**

## 6. ⚠️ 一个流程教训

本原理是在**踩过一次坑之后**才被明确表述的：`cpu-npu-shared-memory.md` §3.1 曾假设
「一块内存同时被 VMM peer-import 和 host-register」，**违反了本仓 ABI 早已写明的
`HOST × VMM_WINDOW = ❌` 能力矩阵**，而**两轮对抗式一致性 review 都没抓到** ——
因为它们分别在各自文档组**内部**查一致性，**从未跨组核对物理能力断言**。

> ### ⇒ 一致性审查必须专设「跨文档物理能力断言」这一维
>
> 逻辑层的文档很容易在不知情的情况下许诺硬件给不了的东西。

## 7. 本原理决定**什么是一条 track** → 已拆出

> **2026-08-14 拆分**：本节原有 174 行（占全文 58%），母文件到 302 行触发
> [`README.md`](README.md) §4 规则 5。**已移入
> [`track-boundaries.md`](track-boundaries.md)**。
>
> **拆分依据不只是长度** —— §1–§6 回答「原理是什么、怎么查一个 API」，
> 拆出的那半回答「**工作该怎么分线**」；而且它是**会 churn 的那一半**
> （本周被重写两次，§1–§6 未动）。
>
> 一句话留在这里，因为它是本原理的**上一层用法**：
>
> > 物理资源与逻辑概念解耦 ⇒ **一个 worker 能不能访问一块资源，由「它部署在哪」×
> > 「那里到资源物理位置的连通性」判定**；逻辑树位置只决定**谁有权发起**。
> > ⇒ 这条同时决定**代码对不对**（§3）与**工作怎么分线**
> > （[`track-boundaries.md`](track-boundaries.md)）。
