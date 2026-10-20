# P1-B 最终设计：BufferHandle / BufferRef 内存模型

> **发布快照：2026-10-19。** 设计、实现状态与历史调查须按正文各自基线阅读；不代表当前 main 全量验收。参见[阅读入口与已知更正](../README.md)。
>
> **状态**：⬜ **目标设计**（PR #1453 现状 + 两轮 review 修补 + ABI 化简收敛而成）。
> 本文讲 **「设计是什么」**；**「相对 PR 现状要改什么」**见
> [p1b-review-findings.md](p1b-review-findings.md)。
> **上游契约**：[../worker-memory-model.md](../worker-memory-model.md)。
> ⛔ [bufferhandle-abi.md](bufferhandle-abi.md) 是**第一版 schema，已被本文取代**，仅存档。
> **设计来源锚点**：PR #1453 head `cc65ce68`；**实现对表锚点**：
> PR #1599 commits `57a89fda` / `9fe7bb9f`。
> 文件名中的 “P1-B” 是历史命名；当前 [feature roadmap](../feature_roadmap.md) 把本文
> 冻结的 identity/descriptor/wire shape 归入 **P1-A**，把 registry/lifetime 归入 **P1-B**。
>
> ## ✅ 本文是 P1-B **target descriptor/wire-shape 权威**
>
> **权威边界要收窄理解**：本文取代第一版 schema，锁定
> `CanonicalIdentity`、`BufferHandleDescriptor`、`BufferRef` 的 target layout、字段语义与
> 可执行不变量。它**不是** current-main 实现状态账本，不覆盖独立的
> remote-buffer wire，也不会因为 layout 已对表就证明所有 runtime materialization
> 路径已兑现。实际合并状态以 `upstream/main` 与对应 public docs 为准。
>
> **PR #1599**（`Add: the Buffer/Tensor wire ABI, its codec, and create_buffer`
> —— P1-B 按 findings §8 拆分后的 **PR-1**；head `4b740674`，已 rebase 到 `b5261a7f`）
> 落的 `static_assert` **与本文形状一致**：
>
> | — | 本文 | #1599 `static_assert` |
> | - | ---- | --------------------- |
> | `OWNER_INSTANCE_ID_BYTES` | 8 | ✅ 8 |
> | owner worker path | 进程侧 `path_id` | ✅ `uint32 owner_worker_path_id`（offset 48） |
> | `version` / `visibility` | 删除 | ✅ 均不存在 |
> | `CanonicalIdentity` | 32 B | ✅ `sizeof == 32`，`buffer_id@8`、`generation@16` |
> | `BufferDescriptor` | ~88 B | ✅ `sizeof == 88`，`magic@0`、`address_space@2`、`access@3`、`backend_kind@4`、`identity@8`、`nbytes@40`、`body_len@52`、`body@56` |
> | `Tensor`（线上视图） | ~144 B | ✅ `sizeof == 144`，`buffer@0`、`byte_offset@88`、`ndims@96` |
>
> ⚠️ **本文用的是旧类型名。#1599 已执行[上游 §1.1](../worker-memory-model.md) 的改名**：
> `BufferHandleDescriptor` → **`BufferDescriptor`**、`BufferRef` → **`Tensor`**、
> 设备侧 128 B POD 的 Python 名 → **`ChipTensor`**。本文正文的 `BufferRef` /
> `BufferHandleDescriptor` 一律按此对应读；**字段语义与不变量不受改名影响**。
>
> 另外 #1599 已内含两条 findings 的修补：`validate_tensor` 统一 validator（findings §1）、
> `FORK_COW` 必须 `access == READ`（findings §5② 最小授权）。
> 并新增用户面文档 `docs/buffer-abi.md`（已进 mkdocs）—— 这是**面向用户的 how-to**，
> 与本文（target wire-shape 权威）分工不同，不构成第二份 schema。
>
> ### 对本设计两点质疑的答复（契约已定，实现仍有缺口）
>
> #### Q：`path_id` 是进程内 id，跨进程怎么解释
>
> A：**它不需要跨进程可解析。** #1599 注释:*"`owner_worker_path_id` is a **DIAGNOSTIC
> id only** — it names the owning worker in logs and post-mortems and takes part in **no
> routing, visibility or identity decision**. Its side table lives in the owning process;
> an id a consumer cannot resolve prints as `<path#N>` and is **never an error**."*
> ⇒ 消费者解析不了是**预期行为**,不是降级。旧 schema 之所以要塞 `char[64]`,是因为
> 当时把 path 放进了 **identity**;path 移出 identity 之后,它只剩诊断价值。
>
> #### Q：直接传 VMM 地址与"各进程 VA 不同"矛盾
>
> A：**ref 里没有"通用的、可直接解引用的地址字段"**；地址一律经 `backend_kind` + `body`
> **物化**得到——但**四个 backend 的物化退化为恒等**，详见 §2 的物化表与其下的说明
> （那里是本问题的完整答复，此处只给结论）。#1599 不变量:*"Carries **NO materialized
> address**. The consumer materializes `handle` to a local base, then
> `Tensor.buffer.addr = base`."* `body[0, body_len)` 装的是**物化材料**(POSIX shm 名 /
> fork 继承的 VA / device VA),由 `backend_kind` 决定如何解释;消费者据此**自行物化出
> 本进程的 base**,再缓存 `identity -> local base`。
> ⚠️ **但这是契约,不是已完成的实现**:findings §4 指出 Python 侧 `materialize` 对
> `DEVICE_MALLOC`/`VMM_WINDOW` 仍是 `base = int.from_bytes(desc.body)` —— **直接把 owner
> 的 device VA 当本地 base**,靠"该 ref 只会到达同一张卡"这条**未强制**的拓扑不变量兜底。
> **契约已正确,该路径尚未兑现契约** —— 这条必须在 P1-B 后续 PR 里闭合。
>
> ### #1599 review（2026-08-05，head `491d95a2`）：形状已对表，三条实现缺口
>
> 🟠 **状态更新（2026-08-05）**：#1599 已合入 main（`b535fa21`）。下述 ①③④ 与 findings §7 的
> 修复在 **PR #1703**（`Fix: harden the Buffer/Tensor wire gate and the create_buffer child
> check`）里，字节布局未动；②（endpoint × `address_space` 矩阵）仍然开着，见该 PR 的
> "Deliberately not here"。
>
> ### ⛔ 三条**全部未修就已合入** —— `b535fa21`，2026-08-05
>
> **本节从「PR review 待办」变成「已发布 ABI 的已知缺陷清单」。** 三条在合入版上逐条复核：
> ① **仍存在，且已在合入版上实测复现**（`shapes=(2147483649,)` `strides=(2147483648,)`
> `FLOAT32` → extent `4`、`validate_tensor(nbytes=4)` ACCEPTED）· ② `ImportRegistry.materialize`
> **仍不调 validator** · ③ `wrap_fork_inherited` **仍由 `access` 反推 `backend_kind`**。
> ⇒ **P1-A 只达成了「冻结」，没达成「不变量兑现」**（见 [`../README.md`](../README.md) §2）。
> 修法三条都**布局中立**，所以不必解冻 ABI —— 但 ① 改的是**已发布的 validator 语义**：
> 今天能通过的一批 hostile shape 之后会被拒。
>
> 上表的字节对表**仍然成立**（在 `491d95a2` 上逐条复核过 `static_assert`，合入版 `b535fa21`
> 复核结果相同）。但"形状对了"不等于"不变量兑现了"。按严重度：
>
> #### ① `tensor_extent_bytes` 溢出 ⇒ §9 不变量 4 当前不成立（冻结前必修）
>
> extent 的算法是 `Σ (shapes[i]-1)*strides[i]` 累加进 `uint64`，注释写
> *"Summed in u64 so a hostile shape/stride cannot wrap it"* —— **它会 wrap**。两个字段都是
> `uint32`，单项乘积就逼近 2⁶⁴，`(last_elem+1) * elem_size` 再溢出一次。对着该分支的头文件
> 实测（`g++ -std=c++17 -I src/common/task_interface`）：
>
> ```text
> ndims=1, shapes=(2147483649,), strides=(2147483648,), dtype=FLOAT32
>   tensor_extent_bytes  = 4
>   validate_tensor(nbytes=4) -> ACCEPTED       # 真实跨度 ≈ 16 EiB
> ```
>
> 二维变体同理（`shapes=(4294967295, 7)` / `strides=(4294967295, 2147483648)` → extent 3 B）。
> **一维即可触发，且 Python 侧可达**：`buffer.tensor(shapes=(2147483649,), dtype=FLOAT32,
> strides=(2147483648,))` 在一块 4 字节 backing 上构造成功。`tensors_overlap` 继承同一缺陷 ⇒
> 两个完全重叠的视图被判为不相交，**正是 §4.3 说的"退回旧世界的漏依赖"**。
> 修法：`__uint128_t` 或 `__builtin_mul_overflow` / `add_overflow`，溢出即拒 —— **布局中立，
> 不影响冻结**。附带：#1599 的 fuzz 用例 `ValidateNeverWalksOffArbitraryBytes` 对幸存者只断言
> `magic`/`generation`/`body_len`/`ndims`，**恰好漏掉 footprint 这一条**，所以随机字节测不出来。
>
> #### ② "一个 validator，三个边界"当前只覆盖两个
>
> 构造（bindings）✅、builder ✅、**materialize ❌**：`ImportRegistry.materialize` 既不调
> `validate_buffer_descriptor`，也没有 endpoint × `address_space` 检查（§5.2），照旧把 device VA
> 当本进程 base 返回 —— 即 [findings §2](p1b-review-findings.md) 那条 `[R]`，**#1599 未闭合**。
> 今天结构上安全（Python 拿到 descriptor 的唯一入口是会校验的构造函数），但 `buffer.h` 的头注释
> 明写自己覆盖 *"...and materialization"* —— **这句断言与实现不符**，不该留在一个要冻结的头文件里。
>
> #### ③ `wrap_fork_inherited` 由 `access` 反推 `backend_kind`，违反 §2.1 的分类论证
>
> 实现是 `FORK_SHM if access != READ else FORK_COW`。§2.1 把写语义放进 `backend_kind` 的理由正是
> *"这是分类问题…只能靠正交字段去猜，迟早猜漏"* —— 这里就是在猜。后果：**read-only 的 `MAP_SHARED`
> backing 无法表达**（被打成 `FORK_COW`，而 `FORK_COW ⇒ READ` 又锁死它日后不能放宽）。应由调用方
> 显式给出 backend（它才知道自己持有哪种 mmap）再校验组合。同处 `materialize` 的注释还把
> `FORK_SHM` 说成 *"a COW-inherited host VA"*，两个标签说反了。
>
> **④ 用户面偏差**：`Buffer.shm` 是 public dataclass 字段，`create_buffer` docstring 与
> `docs/buffer-abi.md` 都在教 `torch.frombuffer(h.shm.buf, ...)` —— 与
> [user-surface §5.5](user-surface.md) 的"统一字节访问、不得暴露底层 `SharedMemory` 对象"直接冲突。
> 过渡期可接受（视图上还没有访问器），但要么现在补 `Buffer.memoryview()`，要么在文档里登记偏差与
> 期限，**不应作为推荐写法教给用户**。用户侧的正确形态见 [user-surface §5.9](user-surface.md)。

## 0. 概念模型：三层，而不是一个 Tensor

```text
Backing    一块有身份的内存          ← 资源：谁拥有、多大、什么权限、怎么物化
   │
Handle     Backing 的身份 + 物化方法  ← 1 份，owner 持有真实资源
   │   .ref()                        ← 只保留“造第一个视图”这一个动作
Ref ×N     视图                      ← N 份，共享同一 identity，随 TaskArgs 走线
   │   .slice() / .transpose() / .permute() / .view() / .reshape()   ← 派生在这一层
   ↺   （Ref → Ref，纯值运算，identity 不变）
   │   materialize
Tensor     进程内的地址 + 形状        ← kernel 吃的东西
```

### 核心分离：资源身份 ⊥ 计算视图

> 派生操作属于视图层：slice/transpose/reshape 保持 backing identity。
> 公共 Buffer 仍由用户持有；视图派生不要求再次申请 backing。API 契约见
> [user-surface.md](user-surface.md)，旧 BufferHandle/BufferRef 名按本页映射读取。

`Tensor` 把两者焊在一起（每个 Tensor 自带一个 `addr`），因此**无法判断两个 Tensor 是不是
同一块 backing 的不同视图**——而这正是跨进程依赖推导、map-once 物化、ABA 检测的共同前提。

### 0.1 Handle vs Ref

 | — | `BufferHandle` | `BufferRef` |
 | - | -------------- | ----------- |
 | 是什么 | backing 的**所有权句柄** | backing 上的**视图** |
 | 数量 | **1 份** | **N 份** |
 | 谁持有 | **owner 进程**，还持有真实资源（`SharedMemory` 对象 / device ptr） | 到处跑，纯值 |
 | 走不走线 | ❌ 不走线 | ✅ **随 `TaskArgs` blob 走 mailbox** |
 | 生命周期 | **管资源生死**、scope 绑定、`generation` 归它 | 不可变值；`.slice()` 返回**新** ref |

> #### 最贴切的类比：torch 的 `Storage` : `Tensor`
>
> `BufferHandle`→Storage（物理内存），`BufferRef`→Tensor（storage + offset + shape + stride）。
> 差别只在 `BufferHandle` 管理稳定的 canonical identity，并能生成供跨进程消费的
> materialization descriptor；跨进程传输的仍是内嵌 descriptor 的 `BufferRef`，不是 owner 对象。

### 0.2 ⚠️ Ref **内嵌整份 descriptor**，不是「指向」handle

`BufferRef.handle` 是 descriptor 的**完整字节拷贝**，不是指针。

**为什么必须如此**：消费者是**另一个进程**，那里根本不存在这个 `BufferHandle` 对象。

 | — | 传统 export/import | 本设计 |
 | - | ------------------ | ------ |
 | 做法 | 先握手把 handle 传过去，消费者 import，之后才能用 ref | **ref 自带全部信息**，收到即可物化 |
 | 代价 | owner 必须**事先知道谁会用** | 每个 ref 都胖（descriptor 全部字节） |

这就是 `create_buffer` 的 **"No eager export handshake"**，也是 `BufferRef` 体积的来源。

## 1. 数据结构

### `CanonicalIdentity` — 32 B，**定长，无变长字段**

 | 字段 | 宽度 | 作用 |
 | ---- | ---- | ---- |
 | `owner_instance_id` | 8 B opaque | per-incarnation nonce，防跨 incarnation ABA |
 | `buffer_id` | u64 | owner 内唯一 |
 | `generation` | u32 | **从 1 起**；`buffer_id` 槽位复用即 `++`；**`0` 非法** |
 | `_pad` | 12 B | 对齐 / 余量 |

> **定长是设计属性，不是巧合。** identity 内没有任何长度字段，因此 hash 与 `memcmp`
> **在结构上不可能越界** —— 这个安全性质由**形状**保证，不依赖任何校验代码。

### `BufferHandleDescriptor` — ~88 B

 | 字段 | 说明 |
 | ---- | ---- |
 | `magic` u16 | **哨兵，不是版本**（`add_ref` 收裸 bytes，需廉价前导判别） |
 | `address_space` u8 | `HOST` / `DEVICE`。**安全关键** |
 | `access` u8 | `READ` / `WRITE` / `READWRITE` —— backing **授予**的权限 |
 | `backend_kind` u8 | 见 §2 |
 | `identity` | 32 B |
 | `nbytes` u64 | footprint 校验的上界 |
 | `owner_worker_path_id` u32 | **驻留 id** —— 路径本体在 owner 进程的侧表；**纯诊断**（日志 / post-mortem），**不参与 routing、可见性或 identity 判定**；消费者解析不出即打印 `<path#N>`，**不是错误** |
 | `body_len` u16 + `body[32]` | 按 backend 的物化信息 |

#### 明确不在里面的

 | 不存在 | 为什么 |
 | ------ | ------ |
 | `location`（local/remote） | 是**路由结果**，非 backing 的稳定属性；由 identity + endpoint registry 推导 |
 | `abi_version` / `descriptor_version` | **不支持多版本共存，亦无此计划**（§6） |
 | `visibility` | 无执行点的声明性字段 |
 | 任何已物化的地址 | ref 跨进程走，地址在别的进程无意义 |

### `BufferRef` — ~144 B

`descriptor` + `byte_offset` u64 + `ndims` + `shapes[]` + `strides[]` + `dtype`

**两条不变量**：`strides[i] > 0` 严格（**singleton 维的 stride 永不被归一**；广播 / 负步长
不支持）· `byte_offset` 是**字节**偏移且为 dtype 大小整数倍。

> 对照 `Tensor` 128 B：多出的十几字节买到**跨进程身份 + 权限 + 物化方法**。

## 2. 六个 backend：物化方法即分类依据

 | backend | `body` 存什么 | 底层能力 | **物化动作** | 恒等物化的前提 |
 | ------- | ------------- | -------- | ------------ | -------------- |
 | `FORK_SHARED` | base VA | `mmap(MAP_SHARED)` + `fork()` | **恒等**（body 即本进程 VA） | fork 继承 ⇒ 同 VA，**由 fork 语义保证** |
 | `FORK_COW` | base VA | 普通堆内存 + `fork()` | **恒等** | 同上 |
 | `POSIX_SHM` | **shm 名字** | `shm_open` + `mmap(MAP_SHARED)`（tmpfs） | **真 mmap**（唯一真正建立映射的） | 无 —— 本进程自己映射，VA 与 owner 无关 |
 | `VMM_WINDOW` | device VA | CANN VMM / Fabric-V2 shareable handle | **恒等** | 🔴 **ref 不得离开 owner 那张卡的进程** —— 拓扑不变量，**当前未强制** |
 | `DEVICE_MALLOC` | device VA | `aclrtMalloc` | **恒等** | 🔴 同上，**当前未强制** |
 | `REMOTE_SIDECAR` | — | （P2） | 拒绝 | — |

> **「ref 不携带已物化地址」的准确含义**：ref 里**没有一个通用的、消费者可以直接
> 解引用的地址字段**；地址一律由 `backend_kind` + `body` **物化**得到。
> 但**物化的代价因 backend 而异**：`POSIX_SHM` 真做 mmap；其余四个的物化**退化为恒等**
> —— body 里存的就是一个 VA，消费者直接用。
>
> **恒等物化不是免费的，它是一条前提**：`FORK_*` 的前提由 fork 语义**保证**；
> **device 两个的前提（ref 只到 owner 那张卡）当前完全未强制**，靠注释兜底
> —— 见 [findings §4](p1b-review-findings.md)。**契约正确，该路径尚未兑现契约。**
>
> **跨 importer 的 VA 本来就不同**（VMM/Fabric import 每个 importer 各异），
> 所以一旦 device ref 真的跨了进程，恒等物化就是**读到一个在本进程无意义的数值**。
> 这正是必须 offset 寻址、禁绝对指针的原因。

### 2.1 `FORK` 拆成两个，因为 Linux 写语义相反

 | — | fork 后子写父可见？ |
 | - | ------------------- |
 | `FORK_SHARED` | ✅ 共享同一物理页 |
 | `FORK_COW` | ❌ **写时复制，子写父不可见，且静默** |

⇒ `FORK_COW` **天然只能 `READ`**。把物理语义放进 `backend_kind` 而非靠 `access` 暗示，
是因为这是**分类问题**：一个标签下两种内核行为，只能靠正交字段去猜，迟早猜漏。

> 重申：**只有 `POSIX_SHM` 真的做了跨进程映射**，其余四个是恒等物化。
> `FORK_*` 的前提由 fork 语义保证；**device 两个的前提必须变成可执行的检查（§5），
> 不能只写在注释里** —— 这是 P1-B 后续 PR 的必闭项。

## 3. 物化：自描述，无握手

descriptor 内嵌在每个 ref 里，所以消费者无需事先知道任何事、owner 无需知道谁会用。

```text
① 扫 blob → 取出每个 ref 内嵌的 descriptor
② materialize(descriptor):
      key = identity（32 B 原文，精确比较，不是范围猜）
      命中缓存 → 返回                    ← map-once
      未命中 → 按 backend 物化 → 缓存
③ 重写 blob:  addr = base + byte_offset，strides 原样搬 → Tensor
```

三类消费者的终点：**L2 chip** → Tensor blob → kernel｜**Python SUB** → `MappedArg`
（视图原点的可写内存）｜**L4→L3** → 不物化，只 re-export。

### 3.1 同 handle 的多个 ref：**映射一次，Tensor N 个**

 | 层次 | 数量 | 谁保证 |
 | ---- | ---- | ------ |
 | **物理映射** | **1 次** | `ImportRegistry` 按 identity 缓存（map-once） |
 | **Tensor 结构** | **N 个**（每 ref 一个） | 逐 ref 造，各带自己的 addr / shape / strides |

```text
        物理页                                ← 一份，零拷贝
          ↑ mmap 一次
        base
          ├─ Tensor#1  addr=base+0        shapes=(M,N)    strides=(N,1)
          ├─ Tensor#2  addr=base+0        shapes=(M/2,N)  strides=(N,1)   ← 上半
          ├─ Tensor#3  addr=base+off      shapes=(M/2,N)  strides=(N,1)   ← 下半
          └─ Tensor#4  addr=base+0        shapes=(N,M)    strides=(1,N)   ← 转置
```

#### ⇒ 一个 Storage，N 个 Tensor —— 与 torch 同构

三个后果：**零拷贝**（切视图不移动数据，`transpose` 只换 strides）·
**写互见**（`FORK_SHARED`/`POSIX_SHM`/device 上，重叠区立刻可见）·
⚠️ **这正是 alias 风险的来源**（kernel 眼里是 N 个独立参数，但可能重叠）。

## 4. 依赖正确性模型 —— 三层分工

> 常见误解：「多个 Tensor 共享地址，依赖还能对吗？」
>
> ### 关键：L3 的 TensorMap 已经不看地址了
>
> 地址共享是**物化之后、L2 进程内**的事实；建依赖发生在 **L3、物化之前**，
> 对着 **BufferRef**，键是 **canonical identity**。两者不在同一层。

 | 层 | 在哪 | 拿什么建键 | 时机 | 管什么 |
 | -- | ---- | ---------- | ---- | ------ |
 | **L3 TensorMap** | L3 orchestrator | **canonical identity + footprint** | 物化**前**，对 BufferRef | **任务之间** |
 | **L2 OverlapMap** ⚠️ | 每个 chip 进程内 | 已物化 Tensor 的 **byte range** | 物化**后** | **chip 内**任务间。显式反依赖契约见下 |
 | **提交时 alias 校验** | 提交点 | 同一 TaskArgs 内各 ref 的 footprint | 提交时 | **同一任务内**（§5.3） |

> 本节规定三层职责，不给外部 GE 维护第二份状态。GE-R1–R6 交接要求见
> [graph-execution-contract.md](graph-execution-contract.md)，本板归属 RA-6 / RA-12 / RA-15 / DS-4 / EX-7。
> 自动 WAR 是能力增强；当前调用者通过 `add_dep` 或 `add_inout` 表达反依赖，见仓库 `docs/war-anti-dependency.md`。
> \#1375/#1376 的 wait/retain 与边归约实现归外部 runtime，不能作为整条 P1 不可开工的笼统前提。

## 5. 校验模型：**结构保证 优先于 校验保证**

> ### 能用结构消灭的约束，就不要留给 validator 去守

 | 约束 | 由什么保证 |
 | ---- | ---------- |
 | identity hash / 比较不越界 | **结构** —— identity 定长无长度字段（§1） |
 | 未知 backend 不被误解析 | 校验 |
 | view 不越出 backing | 校验 —— `byte_offset + extent ≤ nbytes` |
 | 非法 `address_space × backend` | 校验 —— 能力矩阵 |
 | device ref 不进 host endpoint | 校验 —— endpoint 矩阵 |

**一个 validator，三个边界，同一份实现**：builder（`add_ref`）· 接收（blob 解析）·
materialize。

### 5.1 能力矩阵（`address_space` × `backend_kind`）

不支持的组合**在 dispatch 前显式拒绝，不静默回退 host staging**：

 | — | `FORK_*` | `POSIX_SHM` | `VMM_WINDOW` | `DEVICE_MALLOC` |
 | - | -------- | ----------- | ------------ | --------------- |
 | **HOST** | ✅ | ✅ | ❌ | ❌ |
 | **DEVICE** | ❌ | ❌ | ✅ | ✅ |

### 5.2 endpoint 能力矩阵（endpoint × `address_space`）

 | endpoint | HOST | DEVICE |
 | -------- | ---- | ------ |
 | L2 chip | ✅ | ✅ **仅其 owner chip** |
 | Python SUB（host 进程） | ✅ | ❌ **提交时拒绝** |
 | L4→L3 转发 | ✅ 不物化，仅 re-export | ✅ 同左 |

**两道防线**：提交时拒绝（用户拿到**错误**而非崩溃）+ materialize 兜底（防未来新路径绕过）。

### 5.3 `access ⊆ granted` 与任务内 alias

- **在最终提交点**验证最终 `(tag, access)` 组合（不是 add 时验一次——tag 事后可改）
- **同一 TaskArgs 内**两个重叠的 writable view → **拒绝或显式串行**
  （TensorMap 管不到：它们属同一任务节点，没有先后关系可建）

## 6. 版本策略：**不携带版本号**

三件常被混为一谈的事：

 | — | 目的 | 本系统 |
 | - | ---- | ------ |
 | ① **版本协商** | v1/v2 同时在线，按版本号选布局 | ❌ **不需要** —— 无滚动升级、无在线更新 |
 | ② **偏斜检测** | 构建产物不一致时快速失败 | ✅ 由 **`SIMPLER_BUILD_COMMIT`** 承担 |
 | ③ **blob 哨兵** | 任意 272 B 能否冒充 BufferRef | ✅ 由 **`magic` + validator** 承担 |

**`SIMPLER_BUILD_COMMIT` 严格优于手工版本号**：**自动**（不依赖记得 bump）、
**全覆盖**（任何布局改动）。

✅ **这已由 #1523（`ae29a2c0`，已合入 main）确证并加固** —— 标题即
*"refuse a `_task_interface` built from a different revision of the tree"*。
其 commit message 记录的正是这类事故：editable install 把扩展钉在安装时
（`editable.rebuild = false`），而 `python/simpler/*.py` 实时读取；切分支 / rebase 后
Python 与固定二进制错配、**没有任何东西会重建**，改动过的 struct 布局使
**属性读成 0 且全程无报错**。原文举的实例是「装在 #1309（从 `CallConfig` 删掉
`block_dim`）之前的 worktree，切到含它的 main 之后」。

> ⇒ **偏斜检测已由 main 用「构建版本一致性检查」解决，且比 per-struct 版本号更彻底。
> BufferHandle 不需要再自带一套。**

**服务边界佐证**：L3↔L2 chip / L3↔SUB / L4↔本地 L3 **均为同一次 `pip install` 的产物**；
唯一独立部署的边界（L4↔remote L3 跨机）**尚未走 BufferRef**（P2）。
⇒ **当前全部边界的构建一致性都有保证，为「多版本共存」付的费没有对应收益。**

> P2 跨机落地时，**握手交换 build commit 并拒绝不匹配 peer** 仍优于 per-struct 版本号
> （一次检查覆盖全部结构）。

## 7. 化简的账

 | — | 现状 | 最终 |
 | - | ---- | ---- |
 | `CanonicalIdentity` | 96 B | **32 B**（path 移出；instance_id 16→8） |
 | `BufferHandleDescriptor` | 216 B | **~88 B**（identity 瘦身 + body 96→32 + 删双版本号 + path 驻留化） |
 | `BufferRef` | 272 B | **~144 B（−47%）** |

**path 的去处是主要杠杆**：嵌在 descriptor（64 B 上限）→ ref ≈ 208 B（−24%）；
**驻留化**（descriptor 只存 `u32 path_id`，本体在侧表）→ ref ≈ 144 B（−47%）。
本设计取后者。

被删/瘦身的字段与理由：

 | 字段 | 处置 |
 | ---- | ---- |
 | `owner_worker_path`（66 B） | **移出 identity + 驻留化** —— 对唯一性**零贡献**（`owner_instance_id` 已是 per-incarnation nonce）；其价值是可读性/路由。**且此举结构性消灭越界读类问题** |
 | `descriptor_version` | **删** —— 单仓自洽下纯开销 |
 | `abi_version` | **改语义为 `magic`** —— 哨兵，非版本 |
 | `visibility` | **删** —— 无执行点的声明性字段 |
 | `owner_instance_id` 16 B | **→ 8 B** —— u64 epoch（时间戳+pid+随机）已足够 |
 | `body[96]` | **→ 32 B** —— 实际只存一个 u64 地址或短 shm 名 |

> 化简后 `BufferRef` ≈ 144 B，与 `Tensor` 128 B **同一量级**——
> 多出的字节买到跨进程身份 + 权限 + 物化方法，交换划算。

## 8. 生命周期

 | 规则 | — |
 | ---- | - |
 | **generation** | 从 1 起；`buffer_id` 槽位复用即 `++`；validator 拒 `0` |
 | **scope 绑定** | `alloc_shared_tensor` 的 handle 在 scope 结束即失效；继续使用 → 明确报错（generation 失配），**不得静默访问被回收的 backing** |
 | **release + in-flight retain** | 有未完成消费者时不可释放；释放后旧 ref 必被拒 |
 | **import mapping** | 随 handle 生命周期释放，不是拖到 `Worker.close` |

## 9. 本设计依赖的不变量（**全部可执行，无一只存在于注释**）

> ⚠️ **兑现状态（2026-08-05 傍晚更新，基线 `main@4d0de8ea`）**：
> ✅ **第 4 条已兑现** —— #1703（`02f90ef5`）把 extent 改成 saturating 运算、哨兵按名拒绝，
> `tensors_overlap` 两端同样饱和（我在合入版头文件上实测：两组 hostile shape 皆 rejected，
> 六种合法形状 extent 逐字节不变）。
> ✅ **另有两条在 #1704（`4d0de8ea`）后成立得更硬**：第 8 条的「不传 untyped/unguarded 裸地址」
> 现在有 per-backend body schema 兜（address-bearing backend 必须恰好 8 字节且非零基址 ⇒
> 截断指针不再能过 validator）；reserved 区归零规则同时覆盖 descriptor 的 `body` 尾部与
> `Tensor` 过 `ndims` 的 `shapes`/`strides` 槽位（`_pad` 显式排除，因为
> `CanonicalIdentity::_pad` 要故意容忍以保证同一 backing 两次 decode 键相同）。
> ✅ **第 5 条 —— 提交时防线（PR #1729 ✅ merged `aa1d7c7d`）＋ materialize 兜底（PR #1744 ✅ merged `0c367015`，2026-08-08）**：
> host endpoint 由 `_reject_device_args`（`orchestrator.py:460/474`）＋ `mapped_args_from_blob`
> 的 DEVICE 拒绝两道把关；chip endpoint 由 `_child_prov_check_dispatch` 按精确
> `(target_worker_id, ptr)` 键校验，未登记指针 fail-closed。**materialize 兜底 ＝ #1744 的
> `ImportContext(is_host_endpoint, owning_chip_instance_id)`** —— `materialize()` 现在直接判：
> host endpoint 一律拒 DEVICE backing，device endpoint 只认自己 fork 时拿到的那一个 owner nonce。
> 该 PR 已合入，`python/simpler/buffer.py:527` 那句 *"until it lands, that invariant rests on
> the caller"* 的自陈已作废（措辞已随 #1744 更新）。⚠️ **粒度上限（#1744 review 期间
> CodeRabbit 发现，2026-08-08）**：这条检查是 Worker-grained，不是 chip-grained ——
> `owner_instance_id` 按 Worker incarnation 铸一次、不按 chip，`device_ids=[0, 1]` 这类
> 多卡 Worker 的每个子 chip 拿到同一个 nonce，故挡得住「别的 Worker 的 device buffer」和
> 「host endpoint 试图物化 DEVICE backing」，但区分不了同一多卡 Worker 下的兄弟 chip。
> 完整关闭需要 wire ABI 新字段（P1-A 已冻结，需单独裁决）或一个 live per-chip 指针
> allowlist（范围明显更大），两者都不在 #1744 范围内，已记入 `ImportContext`/
> `materialize()` 的 docstring。
> ⚠️ **用户裁决 A（2026-08-07）：本条不再是 wire flip 的硬前置**，降为 P1-B 的普通退出条件。
> 依据是「⛔ 硬前置」当初点名的危害（cutover 让「device VA 交给 host endpoint」从不可达变成可达）
> **恰好落在两道防线齐全的那一行**；剩余缺口是 §5.2 的第二道防线，防未来新路径绕过，不是今天可达的洞。
> 继续把它挂在 flip 前面，等于让 flip 永久依赖它后面的 `ImportContext`——与把本条从 P1-A 移入 P1-B
> 时用的同一条原则（**遗留要往后拆，不能让前面依赖后面**）相悖。
>
> ⚠️ **归属订正（2026-08-05，用户裁决）：本条曾被我记作「P1-A 判 DONE 的唯一剩余条件」——
> 那是把依赖图接反了。** 判定所需的信息**不在 wire 上**：`owner_instance_id` 是不透明 nonce，
> `owner_worker_path_id` 头注释明写 *"DIAGNOSTIC id only … takes part in **no** routing, visibility
> or identity decision"*，`address_space` 只有 HOST/DEVICE 两值、不区分哪张卡。
> ⇒ endpoint × `address_space` 判定**只能**由 registry/owner authority 提供上下文，那是 **P1-B**
> 要建的东西。把它记成 P1-A 的遗留，等于让**前一个里程碑永久依赖后一个**，P1-A 就再也关不掉。
>
> ### 原则：遗留要往后拆，不能让前面依赖后面
>
> ⇒ **P1-A 的退出条件是「冻结 layout ＋ 走完自己的合入门」，已达成**；本条是 P1-B 的退出条件之一。
> ~~且是 #1453 wire flip 的硬前置~~ —— **该前置已于 2026-08-07 撤销，见本节顶部的裁决 A**；
> 归属结论（本条属 P1-B，不属 P1-A）不受影响。（见 [`../README.md`](../README.md) §2 的
> P1-A / P1-B 两行。）

1. identity 跨 edge 不变，且**定长**
2. ref **不携带可由任意消费者直接解引用的通用 materialized-address 字段**；
   endpoint-local address 只在通过 backend/address-domain 校验后产生
3. `strides[i] > 0`，singleton 维不归一
4. `byte_offset` 为 dtype 整数倍，且 view footprint ⊆ `nbytes`　✅ **已兑现**（#1703 `02f90ef5` 的 saturating 运算；#1704 又补上 shm object size 核对 ⇒ `nbytes` 本身也不再只凭 owner 自报）
5. **device ref 只到其 owner chip**（endpoint 矩阵执行）　✅ **提交时已强制（#1729），materialize 兜底已合入（#1744 merged `0c367015`，2026-08-08）**。**不再是 wire flip 的硬前置**（用户裁决 A） —— `materialize` 现由 `ImportContext` 做 endpoint × `address_space` 检查。**判定所需的信息不在 wire 上**（`owner_instance_id` 是不透明 nonce · `owner_worker_path_id` 按设计只作诊断 · `address_space` 只有 HOST/DEVICE 不区分哪张卡）⇒ 只能由 registry/owner authority 供上下文,那是 P1-B 的定义域——`ImportContext` 把这份上下文收窄成「一个已知 nonce」而非完整 registry 查询，因为 chip 子进程从不是 `Worker`，只消费父 L3 Worker 铸的 buffer。~~**是 #1453 wire flip 的硬前置**~~ —— **2026-08-07 撤销**：#1729 已在提交时闭合该条点名的危害，兜底转为 P1-B 普通退出条件，**该退出条件已由 #1744 兑现**。⚠️ **粒度上限**：本检查 Worker-grained 不是 chip-grained（同一多卡 Worker 的兄弟 chip 共享一个 nonce），详见上方 #1744 段落
6. **`access ⊆ granted`** 在最终提交点成立
7. **`generation ≠ 0`**，槽位复用必递增
8. 跨进程只传 identity + typed materialization descriptor，不传 untyped、unguarded
   裸地址；`FORK_*` / device backend body 中的 address-bearing material 必须受
   exact address-domain / owner-chip gate 约束（`copy_*` 路径同样不得绕过）

## 10. 对照冻结 schema

 | 锁死判断 | 本设计 |
 | -------- | ------ |
 | ① 无 `location` 字段 | ✅ |
 | ② canonical identity 跨 edge 不变、依赖键按身份 | ✅ **+ footprint**（§4） |
 | ③ 显式 stride，禁静默 normalize | ✅ |
 | ④ 未知 **backend** 明确失败 | ✅ 三边界统一 |
 | ④′ ~~未知 **version** 明确失败~~ | ⛔ **作废**（§6）——不支持多版本共存 |
 | ⑤ `owner_instance_id` 真实生成 | ✅（16 B → 8 B） |
 | （附）`generation` 从 1 起 | ✅（§8） |

> §7 的化简改变字节布局，但**不动任何一条语义判断**（除 ④′ 明确作废）——
> 是「同一套语义、更少的字节」。
