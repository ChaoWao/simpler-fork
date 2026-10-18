# BufferHandle / BufferRef 逻辑 schema（**已被取代**，存档）

> **发布快照：2026-10-18。** 设计、实现状态与历史调查须按正文各自基线阅读；不代表当前 main 全量验收。参见[阅读入口与已知更正](../README.md)。
>
> ## ⛔ SUPERSEDED（2026-07-30）
>
> ### 本文不再是 ABI 权威。唯一权威 = [`p1b-corrected-design.md`](p1b-corrected-design.md)
>
> 本文冻结的是**第一版**逻辑 schema。其后的设计复核（见 corrected-design §6）
> 削掉了 `version` 与 `visibility`、把 owner 收窄到 8B、把完整 worker path 换成
> 进程内 `path_id`。**PR #1599（`Add: the BufferHandle/Tensor wire ABI, its codec,
> and create_buffer`，历史上从 #1453/P1-B 拆出的 PR-1；按当前 roadmap 归 P1-A
> identity/wire + minimal allocation-lifetime slice）实现的是后者**，其 `static_assert` 已把字节
> 钉死:
>
> | 字段 | 本文（旧） | #1599 实际 |
> | ---- | ---------- | ---------- |
> | `owner_instance_id` | 16 B | **8 B**（`OWNER_INSTANCE_ID_BYTES = 8`） |
> | owner worker path | `char[64]` 完整路径 | **`uint32 owner_worker_path_id`** |
> | `version` | 有 | **无** |
> | `visibility` | 有 | **无** |
> | `CanonicalIdentity` | 96 B | **32 B** |
> | `BufferHandleDescriptor` | 216 B | **88 B** |
>
> **保留本文的唯一理由**是审计:它记录了被否决的字段与当时的理由,便于日后追问
> "为什么没有 version / visibility"。**任何新工作都不得引用本文的字段表。**
> 下方原文保持未改,以免破坏存档价值。
>
> **状态**：⛔ **已被取代**（原标注：🔒 逻辑 schema 冻结候选，design-only）。**这是维护者 author 并维护的
> 规划产出，不是任务**——冻结 schema = 规划工作，非交给实现者的 code prompt。冻结的是
> *logical field set + 语义 + enum 值 + endianness 约定 + 演进规则*；**wire 精确字节偏移 /
> padding 不在此钉死**——留给 P1 实现的 wire flip PR。
> **基线**：`upstream/main@81999716`（fetch 于 2026-07-21；schema 首稿在 `13197445`，其后
> \#1424/#1426/#1423/#1427 均为 L3 comm / remote timeout / early-dispatch / daemon send，
> **均不触碰 buffer ABI**，对本 schema 无影响）。
> **来源（source of truth 顺序）**：[feature_roadmap.md](../feature_roadmap.md)
> §2.4（**权威字段集，冲突以它为准**）＞
> [worker-memory-model.md](../worker-memory-model.md) §3/§9（合并规范）＞
> `src/common/task_interface/tensor.h`（现状 wire，`main@13197445`）。
> **characterization 输入**：tasks/task-scenetest-singleton-alias.md（本地参考：`../tasks/task-scenetest-singleton-alias.md`）。
> **用途**：一次设计评审的对象——reviewer 据此可写 P1 实现而无需再拍字段。

本文是 [worker-memory-model.md](../worker-memory-model.md) §3 的展开（landing 见该
文 §3 的指针）。worker-memory-model.md 讲"为什么是 typed handle"；本文只做
"字段冻结成什么样、怎么演进、怎样的 handle 该被拒"。

## 1. 冻结范围 vs 非目标

### 本文冻结（评审通过后不得再拍）

- 完整 logical field set（§3）——字段的存在、语义、正交性。
- 每字段的逻辑宽度、endianness 约定、enum 值分配、取值域、**冻结后演进规则**（§3/§8）。
- `backend_descriptor` 的 tagged/versioned union 结构 + backend kind 枚举 +
  能力门控矩阵（§4）。
- canonical identity 与 dependency-key 规则（§5）。
- 5 条锁死的设计判断（§2）。

### 本文明确不做（属 P1 实现，受 P0.2 follow-up merge gate 阻）

- wire 精确 struct 偏移 / padding / `tensor.h` byte 改（§7 只给**建议编码**，不钉死）。
- `release_buffer()` / `close()` 生命周期集成、in-flight retain / deferred-free
  的代码。
- bindings（`task_interface.cpp`）、`runtime_maker.cpp`、`remote_wire.h/.cpp`
  的落地。
- guard ②（`child_memory + worker=-1` 拒绝 + owner 表）——那是 **G2**，旧 ABI
  早期补丁，最终被本 schema subsume 但独立先行。
- HeapRing / SharedBufferArena 重构；strided remote wire。

## 2. 锁死的设计判断（契约级，评审后不翻案）

这 5 条是之前多轮 consistency review 反复纠正过的，写进契约以防 P1 实现重踩：

1. **`local` / `remote` 不是字段。** 它是相对*消费 endpoint* 的路由结果，import
   时由 `owner_instance_id + owner_worker_path` ＋ 当前 endpoint/session registry
   推导。locality 不是 allocation 的稳定属性，**不得固化进 handle header**。

2. **canonical allocation identity = `owner_instance_id + owner_worker_path +
   buffer_id + generation`，跨 edge 不变。** 同一 backing storage 的 view 保留该
   identity。**dependency key 用 canonical identity ＋ byte footprint，不用
   materialized VA**——否则 alias / retain-release / ABA / 依赖键都会分裂。

3. **显式 stride，禁止静默规范化 singleton-dim。** `BufferRef` 携带每维显式
   stride；size==1 维的 stride 值按原样保留，不被 rehost / `.view()` 归一。
   （与 ST-1 结论对齐；现状 wire `Tensor.strides[]` 已是显式且 `>0`，泄漏点在
   Python rehost `.view(shape)` 层，见 §6。）

4. **version / backend 未知必须明确失败。** `abi_version` 或 `backend_kind` 落在
   未识别取值 → import/dispatch 前显式拒绝，**不静默降级、不猜**。reserved
   字段/位冻结后新增 backend 时使用，不破坏旧端点兼容（旧端点见到未知 tag 走
   "未知必须失败"路径，是安全拒绝而非误解析）。

5. **`owner_instance_id` 本地 P1 也必须真实生成。** 一个固定宽度 opaque
   nonce/epoch，每次 owner incarnation 都不同，防止 Worker/session 重建后
   `owner_worker_path + buffer_id + generation` 三者同时复用造成的跨 incarnation
   ABA。**P2 只为这个已冻结 opaque identity 加 registry/routing 语义，不得再扩
   handle header。**

## 3. Logical field set（`BufferHandle`）

逐字段与 feature_roadmap §2.4 对齐。**正交维度必须分列**：内存空间
（`address_space`：host/device）⟂ 可见性（`visibility`：private/shared）⟂ 归属身份
（`owner_instance_id + owner_worker_path`）——三者不得塞进同一个 enum。

**endianness 约定（冻结）**：所有多字节整数 **little-endian**（a2a3 / a5 目标
硅片与 host 均为 LE，与现状 host-native struct 一致）。`owner_instance_id` 是
**opaque 字节序列**，只按字节做相等比较，无 endianness 语义。

 | 字段 | 逻辑宽度 | 取值域 / enum 值 | 语义 | 冻结后演进规则 |
 | ---- | -------- | ---------------- | ---- | -------------- |
 | `abi_version` | u16 LE | ≥ `1`（`0` 保留=非法） | 整个 handle 的 wire 版本 | 任何不兼容布局变更 bump；未知 version 明确拒绝；协商取双方 min 支持版本或拒绝 |
 | `buffer_id` | u64 LE | 单个 owner incarnation 内唯一，`0` 保留=非法/未分配 | 稳定分配身份 | slot 复用前**必须**先 `generation++`；宽度冻结 |
 | `generation` | u32 LE | 从 `1` 起单调递增，`0` 保留=未初始化 | ABA / stale / slot-reuse 检测 | 每次 `buffer_id` slot 复用递增；回绕视为 owner 换 incarnation（换 `owner_instance_id`） |
 | `owner_instance_id` | 16 B opaque | 非全零 nonce/epoch | owner incarnation 身份 | 只做 bytewise 相等比较；宽度冻结；本地 P1 也真实生成 |
 | `owner_worker_path` | length-delimited：u16 LE 长度 + ≤`PATH_MAX_BYTES` UTF-8 | 形如 `L4/L3[2]/L2[5]`；长度 ≤ 上限 | 归属层级路径（可跨 epoch/重启复用，故需配 `owner_instance_id` 消歧） | 有界 length-delimited；校验 length ≤ 上限；`PATH_MAX_BYTES` 一次定死 |
 | `address_space` | enum u8 | `HOST=0`,`DEVICE=1`；`2..255` reserved | 纯内存空间 | 新值从 reserved 取；未知值明确拒绝 |
 | `visibility` | enum u8 | `PRIVATE=0`,`SHARED=1`；`2..255` reserved | 哪些 worker 可见 | 同上 |
 | `nbytes` | u64 LE | > 0（`0` 仅空/哨兵） | backing 总字节数 | 宽度冻结 |
 | `access` | enum u8 | `READ=0`,`WRITE=1`,`READWRITE=2`；`3..255` reserved | 权限 | 与 `TensorArgType`（依赖）/`ArgDirection`（H2D/D2H）二元性收敛；未知值拒绝 |
 | `backend_descriptor` | tagged/versioned union（见 §4） | tag ∈ §4 枚举 + reserved | 物化方式 | 结构见 §4；未知 tag / descriptor_version 明确拒绝 |

> `local/remote` **刻意不在此表**——见 §2 判断 1。

### 3.1 `BufferRef`（参数视图，`TaskArgs` 携带的东西）

`TaskArgs` 携带 **`BufferRef(handle, byte_offset, shape, strides, dtype)`**——
handle 上带偏移/形状/**显式 stride** 的视图，而非裸 `addr + child_memory` 布尔。

 | 字段 | 逻辑宽度 | 取值域 | 语义 |
 | ---- | -------- | ------ | ---- |
 | `handle` | §3 的 `BufferHandle` | — | 被视图的 backing；identity 由它承载 |
 | `byte_offset` | u64 LE | `[0, handle.nbytes)` | 视图原点相对 backing 基址的**字节**偏移 |
 | `shape[]` | 每维 u32 LE，`ndims` ≤ `MAX_DIMS` | 每维 ≥ 0 | 视图逐维形状（元素） |
 | `strides[]` | 每维 u32 LE，长度 = `ndims` | **每维 > 0 严格**（stride=0 广播 / 负步长不支持） | 逐维**元素**粒度显式 stride；singleton 维不归一（§2 判断 3） |
 | `dtype` | 现有 `DataType` enum | — | 元素类型 |

> **与现状 wire 的一处需 P1 调和**：现 `Tensor.start_offset` 是**元素**粒度、
> stride 也是元素粒度；roadmap 的 `BufferRef.byte_offset` 是**字节**粒度。P1
> 实现须择一并统一（建议：handle 层 `byte_offset` 字节、view 内 stride 保持元素
> 粒度以复用现有寻址 `(byte_offset/dtype_bytes + Σ coords·strides)·dtype_bytes`）。
> 这是**实现调和点，非字段冲突**——两者都携带显式 stride，契约不变。
> `MAX_DIMS` 现为 5（`tensor.h::MAX_TENSOR_DIMS`）；冻结时定死上限。

## 4. `backend_descriptor`（tagged / versioned union）

**结构（逻辑，冻结）**：

```text
backend_kind     : enum u8       # 见下表；未知 tag → 明确拒绝
descriptor_version: u8           # 该 backend body 的版本；未知 → 明确拒绝
body_len         : u16 LE        # body 字节数，有界 ≤ DESC_MAX_BYTES
body             : body_len 字节  # 逐 backend 物化字段（length-delimited）
reserved         : 定长 reserved  # 冻结留位，未落地 backend 用；旧端点不解析
```

**backend kind 枚举（冻结值）**：

 | tag | `backend_kind` | body 物化字段（示意，byte 布局留 P1） | 典型 `address_space` |
 | --- | -------------- | ------------------------------------- | -------------------- |
 | 0 | `FORK_SHM` | 继承映射基址 token + 区间 | HOST |
 | 1 | `POSIX_SHM` | shm name/fd token + 大小 + 映射 hint | HOST |
 | 2 | `VMM_WINDOW` | shareable-handle token + device 窗口偏移 | DEVICE |
 | 3 | `REMOTE_SIDECAR` | session/endpoint token + 远端窗口 id + staging hint | HOST / DEVICE(arch-gated) |
 | 4 | `DEVICE_MALLOC` | device buffer id / VA token | DEVICE |
 | `5..` | reserved | — | 未知 tag 必须拒绝 |

### 4.1 能力门控矩阵（`address_space` × `backend_kind`）

#### 不支持的组合 dispatch 前显式失败，不静默回退 host staging

 | — | FORK_SHM | POSIX_SHM | VMM_WINDOW | REMOTE_SIDECAR | DEVICE_MALLOC |
 | - | -------- | --------- | ---------- | -------------- | ------------- |
 | **HOST** | ✅ | ✅ | ❌ 拒绝 | ✅（host-staging，future） | ❌ 拒绝 |
 | **DEVICE** | ❌ 拒绝 | ❌ 拒绝 | ✅ 同机跨进程 | ⚠️ **arch-gated** | ✅ 同机/child |

⚠️ `DEVICE × REMOTE_SIDECAR`：**arch-gated 且 remote-transport-gated**。跨机 remote-L3
sidecar 的真数据面**当时（写作时）仅 sim**（daemon 硬拒非 sim），跨机后端未实现。⚠️ **该断言 2026-08-05 已作废**（#1688 `e78d8437` 把门换成 `host_tcp`，且旧 `"sim"` 本就是真 TCP）；本文是**已 SUPERSEDED 的存档**，不再更新正文，此处仅就地标注以免被当作现状引用。**#1424 只交付了 A3
*单机* L3 comm-window 的 V2 Fabric handle 后端（host-local 跨进程），≠ 跨机 remote sidecar
打通**——别把它当 REMOTE_SIDECAR 的使能 PR（见 `feature_roadmap.md` #1424 条：该基础不等于
Remote L3/L4 跨机链路）。故此组合 = arch 不满足即 dispatch 前拒绝，且跨机路径仍待 remote
transport 落地；对齐 worker-memory-model §6 与 §8 的 arch-gated 硬约束。

## 5. Canonical identity 与 dependency-key（冻结规则）

- **canonical allocation identity** = `owner_instance_id + owner_worker_path +
  buffer_id + generation`。**跨 edge 不变**；同一 backing 的每个 view 都复用它。
- **`buffer_id` 唯一性作用域** = 单个 owner incarnation 内。跨 incarnation 由
  `owner_instance_id` 区分（同 path/id/gen + 不同 instance_id ⇒ 不同 backing）。
- **bounds / overlap** 按 `byte_offset / shape / strides / dtype` 计算（元素→字节
  footprint）。
- **dependency key = canonical identity ＋ byte footprint**，**绝不用 materialized
  VA**（VA 随 endpoint 物化而变，用它会让 alias / retain-release / ABA / 依赖键
  分裂）。
- **overlapping writable alias**（两个 `access∈{WRITE,READWRITE}` 的 ref，canonical
  identity 相同且 byte footprint 重叠）必须**显式序列化**或 dispatch 前**拒绝**。

## 6. characterization 输入（ST：要消除的旧 behavior）

来自 task-scenetest-singleton-alias.md（本地参考：`../tasks/task-scenetest-singleton-alias.md`），
作为本 schema 落地时"知道要杀哪些旧行为"的输入（**ST 只刻画，不选契约；契约由
本文定**）。**已 pin 成回归（partial）**：`tests/ut/py/test_scene_test_rehost.py`
（`test_char_*`，device-free，随全套 15 passed）。**范围限定**：这批测试只覆盖
Python rehost / `make_tensor_arg` 层；**C++ orchestrator 的依赖键推导
（`TensorKey::local_host`，`orchestrator.cpp`）在真实 submit 内运行，device-free
够不到 → canonical-identity / dependency-key 行为尚未 characterize，是 ST 未完成的
一块**。

- **ST-1 · singleton stride 被丢弃（两处）**：measured——singleton 维 stride 值
  **到不了 wire**：① `make_tensor_arg` 只读 `shape + data_ptr`、strides 由 shape
  推 row-major（`Tensor.make`），torch 原 stride 从不被读；② rehost `.view(shape)`
  再规范化一次。`as_strided((4,1),(1,7))`（contiguous，singleton stride=7）经两条
  路径都得 stride `(1,1)`；仅 singleton stride 不同、shape/值相同的两 tensor
  **consumer-visible geometry（shape / stride / start_offset）相同**（inactive 维与
  padding 字节 unspecified，故不是 byte-for-byte 相等）。→ 与 §2 判断 3（保真）
  冲突；**没有 consumer 依赖 singleton stride 值**（它在构造处即被丢），故 P1 保真
  是"新增能力"而非"改会破坏谁"——P1 实现须让 stride 从 torch → wire 直通、不经
  `.view` re-canonicalize。
- **ST-2 · 非重叠共享-storage 无 backing identity（partial）**：measured——`base[:4]`
  / `base[4:]`（同 storage、byte-range 不重叠）：alias guard 按 byte-range overlap
  判定 → **不拒绝** → rehost 拆成两个独立 born-shared buffer（backing 关系丢失）；
  `make_tensor_arg` 输出里唯一的 backing 定位是 raw addr（`Tensor.data`），两 view
  得 `base+0`/`base+16` 两个不同地址、无独立 backing-identity 字段。**注意**：这只
  刻画 `make_tensor_arg` 输出，**未触及依赖键推导**——单凭它不能证明 P1 必须改什么
  （P1 可加 canonical identity 而 VA 仍是 `base`/`base+16`）。§5 的 canonical
  identity + dependency-key(canonical，非 VA) 是要补的 gap，但其旧行为的 pin 需要
  能跑 orchestrator 的测试（cpput 或 onboard），本 PR 未做。

## 7. 建议 wire 编码（**不冻结**，P1 flip PR 钉死）

仅供 reviewer 估算，**字节偏移/padding 不是本文契约**：

- cache line 1 放热路径身份/几何（`abi_version`,`buffer_id`,`generation`,
  `address_space`,`visibility`,`access`,`nbytes`,`ndims`,`dtype`）。
- `owner_instance_id`(16B) + `owner_worker_path`(length-delimited) +
  `backend_descriptor`(length-delimited) 放冷区。
- 现 `Tensor` 128B、byte 43 `child_memory`、cache line 2 有 `_pad_cl2[36]` reserved
  （`tensor.h:115`）——`child_memory` 单 bit 被 `address_space`+`owner_*`+
  `backend_descriptor` subsume，reserved 区是承接新字段的自然落点。
- wire 变更边界（P1 必经，同一 PR）：`tensor.h`、`python/bindings/task_interface.cpp`、
  `runtime_maker.cpp`、`remote_wire.h/.cpp`（worker-memory-model §9）。

## 8. Worked example：L4 → L3 → L2（identity 不变，角色/物化随 edge 变）

一块 L4 分配的 1MB shared buffer，跨两条 edge 下发。**canonical identity 四元组
逐 edge 不变**，只有 role / `backend_descriptor` 物化 / `BufferRef` 视图变。

```text
L4  create_buffer(1MB) → BufferHandle H
    H.owner_instance_id = 0x9F..（L4 本 incarnation nonce，真实生成）
    H.owner_worker_path = "L4"
    H.buffer_id = 42, H.generation = 1
    H.address_space = HOST, H.visibility = SHARED, H.access = READWRITE
    H.backend_descriptor = {FORK_SHM, ...}
    submit(L3, args=[BufferRef(H, byte_offset=0, shape=[..], strides=[..])])
      # 对 L4：H 是 kind3

L3  收到 BufferRef(H)                       # 对 L3：kind1（shared view）
    canonical identity 仍 = (0x9F.., "L4", 42, 1)   ← 不变
    按 backend=FORK_SHM 在自身地址空间物化 H（得本进程 VA v_L3；VA 不入依赖键）
    要再喂 L2 → L3→L2 edge 上重新 export（kind3 不透传）
    alloc_child_tensor(L2[k]) → BufferHandle C
      C.owner_instance_id = 0xB7..（L3 nonce），path="L4/L3[2]"，id=7，gen=1
      C.address_space=DEVICE, visibility=PRIVATE, backend={DEVICE_MALLOC,..}

L2  收到 BufferRef(H') 和 BufferRef(C)       # 都是 L2 的 kind1
    H' canonical identity 仍 = (0x9F.., "L4", 42, 1)  ← 三层不变
    H' → shared view（读写 L3 映射区，物化成 L2 的 VA v_L2；v_L2≠v_L3 但 identity 同）
    C  → child-device view（L2 自己的 device buffer）
```

**关键**：`H` 跨三层换的是"角色 + 物化 VA + BufferRef 视图"，**identity 四元组
不变**；依赖推导用 identity 而非 v_L3/v_L2，故 alias / retain-release 不分裂。

## 9. 负例（期望拒绝行为，≥4）

reviewer 据此可为 P1 写拒绝路径测试：

1. **Forged owner（越权可见）**：import 的 `BufferRef.handle.visibility=PRIVATE`
   且 `owner_instance_id + owner_worker_path` 不在本条 edge 的 shared 集合 / 消费
   endpoint 的 registry 未知 → **import 时拒绝**（visibility + 归属身份校验，非
   VA 校验）。

2. **Stale generation**：`buffer_id=42` 命中一个 live slot，但 `handle.generation
   < owner 当前该 slot 的 generation`（backing 已 release + slot 复用）→ **拒绝**
   （§3 `generation` 语义）。

3. **跨 incarnation ABA**：`owner_worker_path="L4"`、`buffer_id=42`、`generation=1`
   全部撞上，但 `owner_instance_id` 与 owner 当前 incarnation 不符（owner 重启换了
   nonce）→ **拒绝**：identity 四元组第一元不同即不同 backing（§2 判断 5 正是防
   这个）。

4. **Overlapping writable alias**：同一 canonical identity 的两个 `BufferRef`，
   `access` 均含写且 byte footprint（`byte_offset/shape/strides/dtype`）重叠 →
   **dispatch 前显式序列化或拒绝**（§5）。

5.（附加）**未知 backend / version**：`backend_kind` ∈ reserved 或 `abi_version`
   高于本端支持 → **明确失败**，不静默解析（§2 判断 4）。

## 10. 冻结前 checklist（对齐 worker-memory-model §"ABI 冻结前必须补齐"）

 | # | 项 | 结论 | 负责 |
 | - | -- | ---- | ---- |
 | 1 | `abi_version` + 字段宽度 / endianness / reserved / tagged backend descriptor | ✅ 冻结（§3/§4；endianness=LE，opaque id bytewise） | 本文 |
 | 2 | `BufferRef(byte_offset, shape, strides, dtype)`——显式 stride，禁静默 singleton normalize | ✅ 契约冻结（§3.1/§2-3）；⚠️ **实现调和点**：byte vs element 偏移粒度（§3.1 注） | P1 impl |
 | 3 | 生命周期：`release_buffer()` / in-flight retain + deferred-free / `generation` reuse(ABA) / `close()` cleanup | 🟡 **语义已定**（§3 `generation` + roadmap §2.4）；**代码集成受 P0.2 follow-up merge gate 阻**（§1 非目标） | P1 impl（gated） |
 | 4 | alias identity / bounds / overlap / dependency-key 规则 | ✅ 冻结（§5）；与 SceneTest rehost overlap 拒绝 + TensorMap 依赖推导对齐 | 本文 + ST 验证 |
 | 5 | `owner_instance_id` 宽度 + 本地真实生成 | ✅ 冻结（16B opaque，§3/§2-5） | 本文 |
 | 6 | 能力门控矩阵（address_space × backend） | ✅ 冻结（§4.1；DEVICE×REMOTE arch-gated） | 本文 |
 | 7 | `PATH_MAX_BYTES` / `DESC_MAX_BYTES` / `MAX_DIMS` 具体上限值 | 🟡 **未决**：需 P1 定死具体数（现 `MAX_TENSOR_DIMS=5`） | P1 impl |

**评审 exit**：§10 全部 ✅ 或"🟡+明确负责方"、§9 负例期望写清、§8 identity 跨三层
不变——满足即"reviewer 能据此写 P1 实现而无需再拍字段"。
