# simpler Worker 内存模型（目标设计）

> **发布快照：2026-10-19。** 设计、实现状态与历史调查须按正文各自基线阅读；不代表当前 main 全量验收。参见[阅读入口与已知更正](README.md)。
>
> 本文规定内存模型契约，实施状态只见 [work-items.md](work-items.md)。
> 核验基线：`upstream/main@937c57efc`。Buffer/Tensor wire 与 canonical identity 已在 main，
> 公共入口包含 `Worker.create_buffer`、`Tensor(buffer, shapes=...)` 与 `Buffer.tensor` 兼容转发；不能再用「裸指针＋bool」概括当前实现。
> 调用策略按 #2460 收敛到 TaskArgs 条目；设计布局的历史证据不代表所有 backend 已验收。
> 关系：本文是 [l3l4.md](l3l4.md)
> §B（诊断：当前四类内存的零散机制与缺陷）与
> [feature_roadmap.md](feature_roadmap.md) §2.4（处方：Buffer / Attachment /
> Tensor 目标）
> 的合并规范。**排期（对齐 feature_roadmap）：schema / owner-identity 可与 P0
> 并行设计；只有 `release_buffer`/`close` 生命周期集成与最终字节钉死的 wire flip
> 以 P0.2 follow-up 为 merge gate**。整体可分步落地，但 wire boundary 的切换必须
> 单次原子完成，避免新旧 ABI 混用或 P2/P3 二次破坏。

## 0. 一句话

> 每一块 worker 触碰的内存都是一个带 stable identity 和 lifecycle 的
> `Buffer`；用户可持有 Buffer，并从它创建几何视图 `Tensor`（见 §5）。
> `TaskArgs` 的条目携带 Tensor 及本次调用 direction/tag/transfer request；
> `Tensor` 内嵌 opaque、自描述的 `BufferDescriptor` 值拷贝
> （**不携带版本号**——偏斜由构建版本一致性检查承担，见 §3）；
> 真正的 local pointer / remote sidecar / device address **只在消费该参数的那个
> endpoint 上物化**。地址空间、归属 identity、权限和生命周期都显式
> 建模，而不是靠数值区间去猜；相对消费端的可见性由 live Attachment
> 推导，不是 descriptor 字段（见 §3）。

## 1. 设计原则

1. **内存归 Worker，不归 Orchestrator。** Worker 拥有并管理 buffer 生命周期；
   Orchestrator 只负责建任务和依赖（对齐 feature_roadmap §2.3）。
2. **四类内存是 edge-scoped 的"角色"，不是存储类型。** 同一块物理内存，在
   "父→子"这条 edge 的上游是 kind3/kind4，到下游就变成 kind1。kind 由它跨的
   那条 edge 定义，不是它自身的属性。
3. **owner handle 不跨边界，descriptor 跨边界永远 opaque + 自描述。**
   `BufferHandle` 是 owner-side 生命周期对象；跨进程/跨机传的是
   `Tensor` 内嵌的 descriptor 值拷贝。每个 endpoint 用 descriptor 里的
   `backend_kind` + body 在自己地址空间物化。**不携带版本号**（§3）。
4. **能力门控，不静默降级。** 不支持的 address_space × backend × topology 组合在 dispatch 前**显式失败**，不悄悄回退到 host staging。

### 1.1 词汇表 —— **五个概念,不多不少**

> #### 命名决定（2026-07-30）**：**`BufferRef` 就是 `Tensor`,统一叫 `Tensor`
>
> 设计所倚赖的类比就是 torch 的 **`Storage : Tensor`** —— 既然 `Buffer` ≡ `Storage`,
> 视图**必须**叫 `Tensor`;叫 `BufferRef` 反而打破了这个类比。
> ⇒ **`BufferRef` 与 `TensorView` 两个名字一并废弃。**
>
> ⚠️ **订正（2026-08-04，`~~LocalTensor~~` 废弃）**：本节此前把「某端点物化出的
> 本地地址描述」命名为 `LocalTensor`。**该名字作废** —— 它与
> [`../.claude/rules/codestyle.md`](https://github.com/hw-native-sys/simpler/blob/937c57efc71ad01a01c3a72072df075d2a5dd24f/.claude/rules/codestyle.md) **规则 13**
> 及作者的 [`feature_roadmap.md`](feature_roadmap.md) 词汇表直接冲突，后者明写
> `ChipTensor`「**也不创造 `L2Tensor`/`LocalTensor` 同义词**」。
> 更根本的是**它违反本节自己的 1:1 判据**：物化形态**没有一个统一的第五类**，
> 每个 endpoint context 各有其名（下表），`LocalTensor` 只是一把不该存在的伞。

 | 名字 | 是什么 | torch 有吗 |
 | ---- | ------ | ---------- |
 | **`Buffer`** | 一块有身份、有 owner、有 lifecycle 的内存 | ✅ `Storage` |
 | **`Tensor`** | `Buffer` 上的视图（`+byte_offset/shape/stride/dtype`），跨 edge 走线，**不含地址** | ✅ `Tensor` |
 | **`CanonicalBufferIdentity`**；（wire 类型名 `CanonicalIdentity`） | 跨进程稳定的身份 | ❌ |
 | **`Attachment`** | `(endpoint, Buffer)` 的访问关系 + capability。**不在 public surface**（§5.7 决策 C）——有域时经 `AttachmentPlan` 批量声明，worker 树上由**传播本身**建立 | ❌ |
 | **物化形态**（**不是单一类型**） | 由消费 endpoint 的 context 命名：Chip → **`ChipTensor`**（带 GM 地址的 POD）· Python SUB → **`MappedArg`** · L4→L3 纯转发 → **不物化** | ❌ |

**最小性证明**：后三个概念**全部且仅仅来自「跨进程」这一件事** ——

- **`CanonicalBufferIdentity`**：地址不能跨进程 ⇒ **身份必须独立于地址**
- **`Attachment`**：「能访问」不再天然 ⇒ 变成**每个端点各自的关系**
- **物化**：同一视图**在每个进程物化成不同地址** ⇒ 必须有一个与 `Tensor` 不同的类型
  承载地址；**但它按 context 分名，不是一个通用类型**

**单进程时这三个全部退化消失，模型塌回 torch 的两层。** 没有一个是偏好——
每一个都能指出「删掉它，跨进程的哪件事就表达不了」。

#### 判据：只有基数不是 1:1，才配拥有独立名字

 | 关系 | 基数 | 判定 |
 | ---- | ---- | ---- |
 | `Buffer` → `Tensor` | **1 : N** | ✅ 一块内存，多个视图 |
 | `Tensor` → 物化形态 | **1 : N** | ✅ 一个视图，每个消费端点物化一份 —— **但每份按 context 命名**（`ChipTensor` / `MappedArg`），**不是一个叫 `LocalTensor` 的通用类型** |
 | `Buffer` → `Attachment` | **1 : N** | ✅ 一块内存，每个端点一种访问关系 |
 | `Buffer` ↔ `BufferHandle` | 1 : 1 | ❌ 同一物；`BufferHandle` 仅在讨论「谁持有资源」时出现 |
 | `Tensor` ↔ ~~`BufferRef`~~ / ~~`TensorView`~~ | 1 : 1 | ❌ **已废弃** |
 | `Buffer` ↔ ~~`Backing`~~ / ~~`MemoryResource`~~ | 1 : 1 | ❌ **已废弃** |
 | `ChipTensor` ↔ ~~`LocalTensor`~~ / ~~`L2Tensor`~~ | 1 : 1 | ❌ **已废弃（2026-08-04）** —— 同义词，见本节开头的订正 |
 | `CanonicalBufferIdentity` ↔ `CanonicalIdentity` | 1 : 1 | ⚠️ **显式配对的例外**——概念名／wire 类型名，须成对出现，见[总纲 §1](logical-physical-separation.md) |

> ##### 新增类型名之前先问：它与最近的已有概念是不是 1:1
>
> **只有基数不同（1:N）才意味着独立的生命周期或独立的身份** —— 那**必然**需要独立名字。
> 1:1 时**默认不新增**；确需区分「概念」与「wire 表示」时，**必须在词汇表里显式配对**
> （如 `CanonicalBufferIdentity` / wire 名 `CanonicalIdentity`），
> **不得让两个名字在正文里自由混用** —— `BufferRef` / `TensorView` / `Tensor` 三名混用
> 正是没做这一步的后果。
>
> ⚠️ **还要再问一句：这个名字是不是一把「伞」？** 如果它的每个实例在各自的 context
> 里已经有名字，那这把伞就是第 N+1 个同义词。`LocalTensor` 就是这样翻的车 ——
> 它想统称的两个东西已经叫 `ChipTensor` 与 `MappedArg` 了。

#### ⚠️ 连带强制：设备侧 POD 必须让名

`Tensor` 这个名字曾被**设备侧 POD**（`tensor.h`）占着。按
[`codestyle.md`](https://github.com/hw-native-sys/simpler/blob/937c57efc71ad01a01c3a72072df075d2a5dd24f/.claude/rules/codestyle.md) 规则 13 的三 context 命名：

 | context | 名字 |
 | ------- | ---- |
 | L3+ 递归调度：`Buffer` 上的视图，跨 edge 走线 | **`Tensor`**（无前缀） |
 | L2 chip runtime：物化出 GM 地址的 POD，不跨 edge | **`ChipTensor`** |
 | L0 core：消费上面那个 `ChipTensor` | **不新建 `CoreTensor`** |

这是**公开 Python 名的破坏性改名**，迁移说明必须显式列出受影响面
（`isinstance`、类型标注、`from _task_interface import Tensor`）。

> ##### ✅ 实现现状（2026-08-04 更新）—— 已在 main 上收口，两侧同名
>
> **#1681 `b3bf3e7a`（`Rename: apply the Worker/Chip/Core layered naming contract`）
> 把 C++ 侧的 POD 本身改名了**：`tensor.h` 现在是 `struct alignas(64) ChipTensor`，
> `static_assert(sizeof(ChipTensor) == 128)`，绑定也变成
> `nb::class_<ChipTensor>(m, "ChipTensor")` —— **C++ 与 Python 同名同义**。
>
> ⇒ **本节此前记的两个问题都已消失**：① 我 2026-08-04 早些时候写的
> 「#1599 靠绑定层改名，被规则 13 判为命名缺陷（*"Binding a C++ `Tensor` as Python
> `ChipTensor` … is a naming defect"*）」—— **该缺陷已由 #1681 修掉**；
> ② 「两个 `Tensor` 同名不同层、只能靠 `namespace simpler` 区分」—— **也不再成立**，
> 现在 `buffer.h` 的线上视图叫 `Tensor`、`tensor.h` 的设备 POD 叫 `ChipTensor`，
> **名字本身就是区分**，引用不再需要层次限定。
>
> \#1681 同时把数字层级标识符整体迁走（`L3L2Queue*` → `WorkerChipQueue*`、
> `L3L2Region*` → `WorkerChipRegion*`、`L3HostMappedRegion*` →
> `WorkerHostMappedRegion*` 等），**无兼容别名**，wire 值不变。
> 设备日志文本与架构叙述保留 `L3-L2` 拼写 —— 规则 13 允许拓扑描述用数字层级。

## 2. 四类内存 = edge-scoped 角色

 | Kind | 含义 | 目标表示 | 分配入口 |
 | ---- | ---- | -------- | -------- |
 | **1** | 本次 run 借用的参数 | `Tensor` | 从上级 kind3/kind4 handle 构造 ref，再传入 `submit(...args)` |
 | **2** | 当前 Worker local | `LocalBuffer`（local-only；**由 Worker 侧登记，不是 handle 字段**） | 普通 host/torch tensor，显式 local-only 标记 |
 | **3** | 与选中下级 shared | `SharedBuffer` | `Worker.create_buffer` / `alloc_shared_tensor` |
 | **4** | 某个下级私有（child-device） | `ChildBuffer` | `Worker.alloc_child_tensor` + `copy_to`/`copy_from` |

**递归规则（edge-scoped，模型核心）**：

```text
父 kind3  ── 跨一条 edge ──▶  子 kind1（shared view）
父 kind4  ── 跨一条 edge ──▶  子 kind1（child-device view）
父 kind2  ──────────────▶  禁止直接下发（submit 前拒绝）
kind3 要再传给孙子       ──▶  子必须在下一条 edge 上重新 export/import/map
                              （没有跨层透传的自动可见性）
root L2 SceneTest        ──▶  无上下级边界，kind2 可直接当参数
```

## 3. `BufferDescriptor` / `Tensor` ABI —— 字段集**不在本文**

> ## ⚠️ 唯一权威 = [worker-memory-model/p1b-corrected-design.md](worker-memory-model/p1b-corrected-design.md)
>
> 本节**早前维护了一份自己的字段表**，与权威文档冲突（列了已被删除的
> `abi_version` 与 `visibility`）。**一个字段集只应存在一处** —— 表已移除，
> 本节只保留"为什么要 typed handle"这层动机，字段定义一律去权威文档查。
>
> 这与 [`bufferhandle-abi.md`](worker-memory-model/bufferhandle-abi.md) 被作废
> 是同一类错误：**同一个 ABI 被三处各写一遍，必然漂移。**

### 3.1 动机：三个正交维度，不可塞进一个 `address_space`

内存空间（host/device）⟂ 归属身份（`owner_instance_id` + owner 标识）⟂ 访问权限
（`access`）。它们各自独立变化，合并成一个枚举就会出现"表达不了的组合"。

**`local/remote` 不是 descriptor 字段** —— 它是相对消费 endpoint 的**路由结果**，
import 时由归属身份 + endpoint/session registry 推导。对齐 feature_roadmap §2.4：

#### locality 不是 allocation 的稳定属性，不得固化进 handle

### 3.2 typed descriptor + owner handle 治好的当前缺陷（动机记录，非字段表）

 | 当前缺陷（`file:line` = 现状） | typed handle 如何解 |
 | ------------------------------ | ------------------- |
 | kind1 不校验来源 | 稳定 `buffer_id` + canonical identity 可按 id 验证 |
 | fork 后未注册指针在 child 不可见；slot 复用（ABA） | `generation` 失配即拒 |
 | `_rewrite_blob_host_addrs` 曾按数值区间猜、会误改 device VA（#1397 的 `child_memory` skip 是 band-aid） | typed `address_space` 是正解 |
 | `child_memory` 只 1 bit、无 owner（`tensor.h:107`，byte 43）→ device 地址可送错 worker | 归属身份（**guard ②：✅ #1430 `c8c7b735`**，§9）；worker path 跨 epoch 复用 ⇒ 必须带 `owner_instance_id` |
 | 裸指针无 size | `nbytes` |
 | `TensorArgType`（依赖/权限）与 `ArgDirection`（H2D/D2H）语义二元 | `access` 收敛 |
 | 每 backend 物化方式靠约定 | `backend_kind` + 自描述 body |

> **两处曾经列出、后被权威文档删除的字段，及其理由**（记录于此以免有人再加回来）：
> · **`abi_version`** —— 偏斜检测由**构建版本一致性检查**承担（同一棵树编译出的
> `_task_interface` 与 Python 必须匹配，不匹配直接拒绝启动），**不需要 per-struct
> 版本号**；本仓不存在"在线升级单个 struct"的场景。
> · **`visibility`（private/shared）** —— 可见性不是 handle 的稳定属性，与
> `local/remote` 同理，是**相对消费 endpoint 的推导结果**；固化进 handle 会与
> `AttachmentPlan` 的 `(member, CanonicalBufferIdentity) -> capability + attachment`
> 重复且可能矛盾。

`TaskArgs` 携带的是
**`Tensor(descriptor, byte_offset, shape, strides, dtype)`** ——descriptor 识别
Buffer，ref 另带偏移 / 形状 / **stride**（strides 显式携带，**不静默规范化
singleton-dim stride**，见 §9），而不是裸 `addr` + `child_memory` 布尔。

## 4. 物化（materialization）—— 为什么"对端无感"

同一个 `Tensor` 跨 edge 传下去，**每个 endpoint 按 backend descriptor 把它
解析成本地具体地址**：

- 本地 fork 子进程：backend=fork-shm/POSIX-shm → 解析成子进程自己 mmap 的 VA
  （即 `_rewrite_blob_host_addrs`；#1397 后已跳过 `child_memory`，P1 进一步改成
  仅当 `address_space==host` 才改写，device 一律不碰）；
- 远端 session：backend=remote-sidecar → 解析成远端导入的窗口/staging 缓冲；
- 设备侧：backend=device-malloc/VMM-window → 解析成 device address。

上层 submit 时**不需要知道对端是 L2、本地 L3 还是远端 L3**（对齐 D.3 的
"逻辑域 + capability views"）——但"操作是否受硬件支持"不能被隐藏，由
capability validation 门控。

---

**§5 用户面契约** → [`user-surface.md`](worker-memory-model/user-surface.md)
**§6–8 backend 矩阵 / 范例 / 边界** → [`backend-matrix.md`](worker-memory-model/backend-matrix.md)
**§9 落地节奏** —— 保留在下方。

## 9. 落地节奏

 | 步 | 内容 | 何时 |
 | -- | ---- | ---- |
 | guard ① | `_rewrite_blob_host_addrs` 排除 `child_memory`/device VA（数值撞 host range 也不改写） | **✅ 已交付（#1397，`worker.py:470` via `TENSOR_CHILD_MEMORY_OFFSET`）** |
 | guard ② | 拒绝**歧义/错-worker** child_memory 下发（解析后**唯一** eligible target 放行；**零/多/wrong-worker/stale-before-reuse 才拒**，严格 ABA 留 P1）+ typed `(worker_id,ptr)` provenance | **✅ 已交付（#1430 `c8c7b735`）**——旧 ABI 上独立补；`free`/`release_domain` 均 revoke-before-native-free |
 | **P1** | 上面整个 owner `BufferHandle` 生命周期 + `BufferDescriptor`/`Tensor` 字段 ABI（含 `owner_instance_id`、`generation`、显式 strides；**无版本号**）分步落地；远程字段在 local backend 保持 inert | **schema/owner-identity 可并行设计；wire flip + `release_buffer`/`close` 集成以 P0.2 follow-up 为 merge gate** |

wire 变更清单（P1 必经）：`tensor.h`（offset 43 的 `child_memory` 字段）、
`python/bindings/task_interface.cpp`、`runtime_maker.cpp`、`remote_wire.h/.cpp`
—— 四处边界须同一 PR 改，故必须一次落地。

**ABI 冻结前必须补齐（否则"一次成型"不成立）**：

1. `magic` 哨兵 + 字段宽度 / endianness / reserved / `backend_kind` + 自描述 body
   （**不是版本号**：本仓无滚动升级，偏斜由构建版本一致性检查承担）；
2. `Tensor(..., byte_offset, shape, strides, dtype)`——显式 stride，禁止静默
   规范化 singleton-dim；
3. 生命周期：`release_buffer()` / in-flight retain + deferred-free / `generation`
   reuse(ABA) / `close()` cleanup 语义；
4. alias identity / bounds / overlap / dependency-key 规则（与 SceneTest rehost 的
   overlap 拒绝、TensorMap 依赖推导对齐）。
   ⇒ **对下游那一层的要求已写成契约**：
   [`worker-memory-model/graph-execution-contract.md`](worker-memory-model/graph-execution-contract.md)
   —— 六条要求（#1375/#1376/#1388 的输入），其中 **R3** 就是本条：
   别名/重叠的单位必须是 `CanonicalBufferIdentity + 字节区间`，**不是本地地址**。
