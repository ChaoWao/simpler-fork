# P1-B（PR #1453）修补清单

> **发布快照：2026-10-15。** 设计、实现状态与历史调查须按正文各自基线阅读；不代表当前 main 全量验收。参见[阅读入口与已知更正](../README.md)。
>
> **状态**：⬜ **review 产出，面向 PR 作者**。这是「要改什么」；
> 「改成什么样」见 [p1b-corrected-design.md](p1b-corrected-design.md)（最终设计）。
> **来源标注**：`[R]` = 用户 review｜`[C]` = 我的 review｜`[S]` = schema 作者定性。
> **基线**：PR head `cc65ce68`，merge-base `c440a5dd`。

## 1. 校验边界模型 —— 最核心的缺口 `[R]`

**现状**：`TaskArgs.add_ref`（`python/bindings/task_interface.cpp:880`）只做两件事：

```cpp
if (packed.size() != sizeof(BufferRef)) throw ...;   // 仅长度
std::memcpy(&r, packed.c_str(), sizeof(BufferRef));
check_access_subset(r.handle.access, tag);            // commit 7 加的，仅此一项
```

未校验：`abi_version` / `descriptor_version` / `backend_kind` / `address_space` /
`visibility` / `access` 值域 / `path_len` / `body_len` / `ndims` / `dtype` /
`strides>0` / view footprint。**§4.1 能力矩阵只在 Python `__post_init__`** ——
hand-packed bytes 与线上 blob **完全绕过**。

**后果（可越界）**：`CanonicalIdentityHash::operator()` 与 `operator==` **直接信任
`path_len`**（u16，可达 65535），而数组只有 `PATH_MAX_BYTES=64` → **越界读**。
触发面是任何 272 B blob。

**修补**：一个 validator，三个边界（builder / 接收 / materialize），同一份实现。
清单与不变量见[最终设计 §5](p1b-corrected-design.md#5-校验模型结构保证-优先于-校验保证)。

> **验收**：喂随机 272 B blob 给 `add_ref` 与 materialize，**全部干净拒绝，
> 无崩溃、无 ASan 报告**。

## 2. 端点能力模型 `[R]`

### 现状**：`ImportRegistry.materialize`（`buffer_handle.py:725`）**不看 `address_space`

就把 device VA 当进程地址返回：

```python
if desc.backend_kind in (FORK_SHM, DEVICE_MALLOC, VMM_WINDOW):
    # DEVICE_MALLOC / VMM_WINDOW: ... (the ref must only reach that chip
    # — a topology invariant).
    base = int.from_bytes(desc.body, "little")
```

**"a topology invariant" 写在注释里、无人执行。** `orchestrator.py:330` 的 `submit_sub`
无 capability 检查；G2 的 `_child_prov_check_dispatch` 只守 `submit_next_level`。
device ref 进 Python SUB → `MappedArg` → `torch.frombuffer` 吃 device 地址 → **段错误**。

**修补**：endpoint × address_space 许可矩阵 + **两道防线**（提交时拒 = 主防线；
materialize 兜底 = 纵深）。矩阵见[最终设计 §5](p1b-corrected-design.md#5-校验模型结构保证-优先于-校验保证)。

## 3. 生命周期与代际 —— **明确的契约违反** `[R]` `[S]`

冻结 schema 写死 `generation` **从 1 起、`0` 保留=未初始化**，而实现：

```python
identity = CanonicalIdentity(oid, buffer_id, path, 0)          # generation = 0
return wrap_fork_inherited(va, ..., generation=0, ...)
```

**live handle 带保留值 `0` 出厂** ⇒ `generation` 存在的唯一目的（检测 `buffer_id` 槽位
复用 / ABA）**空转**。叠加同处注释承认的回收语义（*"auto-reclaimed once every downstream
consumer has completed and the scope ends"*）⇒ **backing 可在 scope 后重用而旧 ref 仍
"有效"**，本该兜底的字段恰好关着。

**修补**：generation 启用（从 1 起、槽位复用即 `++`、validator 拒 0）· scope 逃逸禁止 ·
`release_buffer` + in-flight retain · POSIX import mapping 随 handle 释放。

> ⚠️ **这不是「P2 增强」，是当前已暴露 ABI 的正确性前提。** `BufferHandle` 语义已 ship，
> 事后收紧就是破坏性变更。**仅在 PR body 里列 follow-up 不足以保证安全。**

## 4. `copy_to` / `copy_from` `[R]`

**现状**：不校验 handle 的 `nbytes` / `access` / `backend`；**把父进程 host VA 直接发给
fork 后的 L2**，而 post-init 创建的 SHM 在 child 中**不保证同 VA、不保证已映射**。

**修补**：① 校验 handle（`offset+len ≤ nbytes`、`access` 允许该方向、backend 本进程可达）；
② **不得跨 fork 传裸 host VA** —— 控制消息携带**可物化的 Host BufferRef**（推荐，复用主链路
与同一套校验），或 child 侧用 staging buffer。

> 这是主链路「地址只在使用它的进程里产生」原则的**唯一漏网路径**。
>
> **2026-07-30 更新（#1557 `68ab1b43` 已合入 main）**：设备侧的 range 校验机制
> **现成了**。`_ChildProvEntry` 现在记录每个 allocation 的 `malloc_size`
> （CommDomain 的 allocation id 映射到 window/buffer 的 extent，`live_extent()`
> 取最宽的 live 角色），`copy_to`/`copy_from` 走
> `_child_prov_require_live_range`，按 `base <= ptr && ptr + nbytes <= base + extent`
> **包含式**准入。
> ⇒ 修补①「`offset+len ≤ nbytes`」**不要另起一套** validator，应接到这条路径上；
> `access` 方向与 backend 可达性是它尚未覆盖的两项，那才是 P1-B 需要新增的。
> 修补②（不得跨 fork 传裸 host VA）**不受影响，仍然成立** —— #1557 收紧的是
> *device* 指针的准入，与 host VA 跨 fork 是两回事。

## 5. access 与 alias 约束 `[R]`

**现状**：`check_access_subset` 在 `add_ref` 时验一次，但 **`set_tag()` 能在验证之后把
INPUT 改成 OUTPUT**；普通 COW rehost **固定生成 `READWRITE`**；两个重叠 writable view
可同时作为 OUTPUT 并行调度。

**修补**：① **在最终提交点重新验证最终 `(tag, access)` 组合**（或让 tag 在 add 后不可变）；
② 最小授权（`FORK_COW` 天然只能 `READ` —— 见[最终设计 §2](p1b-corrected-design.md#2-六个-backend物化方法即分类依据)
的 backend 拆分）；③ 同 backing 的重叠写：串行化或拒绝（与 §6 耦合）。

## 6. 依赖键精度 `[C]` —— **必须与 §5 一起做**

**现状**：`infer_deps` 本地路径 `TensorKey::local_host(hash(identity))` ——
**buffer 粒度，不含 `worker_id` / `offset_begin`**。

| 场景 | 结果 |
| ---- | ---- |
| device 分片到 N chip | ✅ 并行（键含 `worker_id`） |
| **host buffer 切 N 片给 N worker** | ❌ **串行**（同键 WAW） |

⚠️ **重要限定（修正早前表述）**：这**只是并行度退化，正确性上反而是修复**。
详见[最终设计 §4.3](p1b-corrected-design.md)。

⚠️ **设计耦合**：现在的粗键**误打误撞在替 §5 兜底**（同 backing 的任意重叠写都被串行）。
**若为恢复分片并行而精化到 offset 粒度，必须同时实现 footprint 重叠判定，
否则会从「过度串行」直接掉进「漏依赖 → 数据竞争」。**

**修补**：一次做完 —— ① 本地键纳入 footprint（`TensorKey.offset_begin` **字段已存在**，
remote 路径在用）；② 同时实现 byte-range 重叠判定；③ 过渡方案 `local_host` 带 `worker_id`
（**不能替代**重叠判定）。

## 7. 拓扑与容量

| 项 | 现状 | 修补 |
| -- | ---- | ---- |
| **L4 仅含 local L3 时 `create_buffer` 被拒** `[R]` | 查 `_chip_shms`/`_sub_shms`，**未查 `_next_level_shms`** | 补上；**并修测试** —— `test_l4_reexport.py:48` 加 dummy SUB **掩盖了正常 L4→L3 拓扑** |
| **`PATH_MAX_BYTES = 64`** `[C]` | 标注 "revisit before final freeze" | 叠加 §1 后**从容量问题升级为越界触发面**。若采纳[最终设计 §1](p1b-corrected-design.md#1-数据结构)的 path 驻留化，该问题**结构性消失** |

## 8. 【要求】尽可能拆分 PR

**这不是建议，是要求。** 客观依据：本仓 review 规则对 **Core churn > 1000 行**判定为
`❌ Oversized PR (core logic)` —— 本 PR **Core 3048 行 / 90 文件 / 总 churn 6714**，
**超阈值 3 倍**，无法在一次评审中通盘持有。

**有利条件**：作者已把工作组织成 **7 个干净的 commit**，所以拆分主要是
「把某些 commit 提升为独立 PR」，成本很低。

### 8.1 建议的拆法

| — | 内容 | 来源 | 规模 | 能否独立 land |
| - | ---- | ---- | ---- | ------------- |
| **PR-1** ⭐ | **ABI 地基**：三类型 + codec + registry + view 代数 + `create_buffer` + 测试 | commit 1 | `+1680/−72` | ✅ **几乎纯新增，行为零变更** |
| **PR-2** | capability matrix + `access⊆granted` 守卫 | commit 7 | +128 | ✅ 纯增量校验 |
| **PR-3** | device 内存所有权 Orchestrator→Worker | commit 4 | ~300 | 🟡 依赖 PR-1 类型，**不依赖 wire flip** |
| **PR-4** | 删 `create_host_buffer` / MAP_HOST 子系统 | commit 6 一部分 | ~250 | 🟡 删独立子系统 |
| **PR-5** | **wire flip + 全部消费端适配** | commit 2/3/5 + 6 剩余 | ~1800 core + 3400 test | ❌ **拆不动**（原子切换） |

### 8.2 拆分的判据（每个 PR 都要能回答）

1. **能独立评审吗** —— 不看其它 PR 能否判断对错？
2. **能独立回滚吗** —— 单独 revert 会不会留下半个系统？
3. **单独 land 了会怎样** —— 是可用能力、还是未接线的 dead code（后者要说明可接受）？

### 8.3 为什么 **PR-1 最该单拆**

**它是一个冻结的 wire ABI** —— 字段集、枚举值、字节偏移一旦合入就难改。
它应当**在自己的上下文里被评审**，而不是淹没在 3400 行测试迁移里。
且 `+1680/−72` 几乎纯新增，评审风险极低。

> **即使最终不拆**：PR 描述必须**明确要求按 commit 评审**（作者已给了阅读顺序，这点做得好），
> 并告知评审人 —— **真正要盯的是 commit 1 的约 1258 行核心，其余是机械跟随**。

## 9. 【要求】逐项论证每个新增的必要性

**背景**：ABI **冻结后删字段是破坏性变更**，所以「以后可能有用」**不是保留的理由** ——
恰恰相反，**冻结前是删除的最后窗口**。

**要求作者对下表每一项作答**：

```text
预计使用场景是什么？  当前有真实调用点吗？  若无 —— 为什么现在就加，而不是需要时再加？
```

**判据**：

| 情况 | 处置 |
| ---- | ---- |
| 有真实调用点 | ✅ 保留 |
| 无调用点，但有**明确的近期场景 + 时间点** | 🟡 保留，但在文档中写明该场景 |
| 无调用点、无明确场景 | ❌ **删** —— 冻结前删是免费的，冻结后是破坏性变更 |

### 9.1 待论证清单

#### 枚举值

| 项 | 待答 |
| -- | ---- |
| `BackendKind::REMOTE_SIDECAR` | P1 **直接 decode-reject**。为什么现在就占枚举位？P2 再加会破坏什么？ |
| `FORK_SHM` vs `POSIX_SHM` | 都是 host 共享内存，**各自的适用场景边界**是什么？何时该选哪个？ |
| `VMM_WINDOW` vs `DEVICE_MALLOC` | 都是 device，**区别的使用场景**？消费者物化路径完全相同（都是取 body 里的 VA），那么这个区分在**执行层面**买到了什么？ |
| `AccessMode::WRITE`（只写不读） | 哪个真实场景用它？与 `READWRITE` 的差别在哪被执行？ |
| `Visibility::PRIVATE` / `SHARED` | **grep 结果：bindings 中出现 0 次，无任何判定点。**要么给出执行点，要么删 |

#### 字段

| 项 | 待答 |
| -- | ---- |
| `abi_version` + `descriptor_version` | **两个**版本号。多版本共存的场景是什么？（见[设计 §6](p1b-corrected-design.md#6-版本策略不携带版本号)：本系统无滚动升级，偏斜由 `SIMPLER_BUILD_COMMIT` 承担） |
| `owner_instance_id` 16 B | 为什么是 16 B 而不是 8 B？碰撞预算是按什么算的？ |
| `body[96]` | 实际用量：`POSIX_SHM` 是 shm 名（短），其余是**一个 u64**。96 B 是按什么场景定的？ |
| `PATH_MAX_BYTES = 64` | 按最深的**现实**路径量过吗？（叠加 §1 后它还是越界触发面） |

#### API 表面

| 项 | 待答 |
| -- | ---- |
| `create_buffer` / `alloc_shared_tensor` / `alloc_child_tensor` | **三个**分配入口的边界是什么？用户如何判断该用哪个？ |
| `make_ref_arg` / `make_tensor_ref` | 两个的区别与各自场景？ |
| view 代数 6 个方法（`slice`/`transpose`/`permute`/`view`/`reshape`/`ref`） | 每个当前**有 in-repo 调用点吗**？无调用点的，是否可以先不进 P1？ |

> **我的判断已写在[设计 §7](p1b-corrected-design.md#7-化简的账)**（可减重 ~47%），
> 但**结论应由作者论证后自行得出** —— 本节要的是**论证过程**，不是接受我的结论。
> 若作者能给出我没想到的场景，那这些字段就该留。

## 10. 文档一致性 `[C]`

| 项 | 问题 | 修补 |
| -- | ---- | ---- |
| **规范性引用不可达** | `src/common/task_interface/buffer_handle.h` 声明权威 schema 是 `.docs/L3-new/...`，但 **`.gitignore:31` 忽略 `.docs`**，PR 未提交其中任何文件 —— **wire ABI 的权威定义对他人与 CI 都不可达** | 指向 **PR 内新增的** `docs/buffer-handle-abi.md`（+119 行，**尚未在 main 上**，随本 PR 一同落地），或把字段表内联进 header |
| **缺迁移映射** | `create_host_buffer` / `free_host_buffer` / `orch.malloc` / `orch.device_handle` **硬删除、无 deprecation 垫片** | 加旧→新映射表 + release notes |

## 11. 执行顺序建议

| 优先级 | 做什么 | 为什么 |
| ------ | ------ | ------ |
| **0** | **§9 逐项论证** + **§8 拆 PR** | 都是**决定「做什么」**的前置：论证结果会改变要实现的字段集，拆分决定评审单元。**先做这两件，后面的工作量才是确定的** |
| **1** | [设计 §1](p1b-corrected-design.md#1-数据结构) 的 **path 移出 identity** | **一刀两得**：减重 + **结构性消灭 §1 越界类**；冻结后再改=破坏性变更 |
| **2** | §1 validator + §2 endpoint 检查 | 越界与段错误必须先堵 |
| **3** | 其余 ABI 化简（[设计 §7](p1b-corrected-design.md#7-化简的账)） | 同属冻结前窗口 |
| **4** | §3 generation + 逃逸禁止 | 已发布语义不可事后收紧 |
| **5** | §5 + §6 **一起做** | 单独做任一个都会出事 |

### 两条顺序上的关键依赖

> **① 减重必须在 validator 之前或同时做** —— 否则会先写一个 validator 去守 `path_len`，
> 然后又把 `path_len` 从 identity 里删掉，**白写一遍**。
>
> **② §9 的论证必须在动手之前** —— 如果论证结论是删掉若干枚举值/字段，
> 那么 validator 的清单、测试、文档都会跟着变。**先定字段集，再写守卫。**

## 12. 给作者的一句话总结

这个 PR 的**方向和核心结构是对的**（见[设计 §0](p1b-corrected-design.md#0-概念模型三层而不是一个-tensor)），
问题集中在三类：

| 类 | 内容 | 性质 |
| -- | ---- | ---- |
| **安全** | §1 越界 · §2 段错误 · §3 生命周期 · §4 裸 VA | **摘 draft 的门槛** |
| **精度** | §5 alias + §6 依赖键（**必须一起做**） | 正确性与并行度的取舍 |
| **规模** | §8 拆 PR · §9 论证每项新增 | **冻结前的最后窗口** |

**最需要作者主动做的是 §9** —— 其余各条我已给出具体修法，唯独「这个字段/枚举值到底需不需要」
只有作者知道预期场景。**冻结之后，这个问题就再也没有便宜的答案了。**

## 13. #1599（PR-1）落地状态对表（2026-08-05，head `491d95a2`）

> 🟠 #1599 已合入 main（`b535fa21`）。下表中 **§7、§10 的 stale 文本、以及 §13.1 的 N1 / N2**
> 已在后续 **PR #1703** 修复（含 `create_buffer` 的首批测试）；**§2 端点矩阵、§9 逐项论证、
> N3 / N4 仍然开着**。

本清单的基线是 PR #1453；#1453 按 §8 拆分后，**PR-1 = #1599** 已提交。逐条状态：

| § | 内容 | #1599 状态 |
| - | ---- | ---------- |
| §1 | 一个 validator，三个边界 | 🟡 **2/3** —— 构造 + builder 有；**materialize 无**（详见[设计](p1b-corrected-design.md)顶部 review ②）。越界类问题按 §1 建议**结构性消失**（path 已移出 identity，identity 定长） |
| §2 | endpoint × `address_space` 能力矩阵 | ❌ **未做**。`ImportRegistry.materialize` 照旧把 device VA 当本进程 base 返回；PR 的"不做清单"里也没提这一项 |
| §3 | generation / 生命周期 | 🟡 `generation` 从 1 起 + validator 拒 0 ✅；`release_buffer` / in-flight retain / scope 逃逸 ❌（PR 明示不在范围）。当前 `buffer_id` 单调不复用，故 generation 恒为 1 |
| §4 | `copy_to` / `copy_from` | ❌ 未触及 |
| §5 | access 与 alias | 🟡 ②最小授权（`FORK_COW ⇒ READ`）✅ 且在 decode 处执行；①提交点复验、③重叠写串行/拒绝 ❌（无 wire，够不到） |
| §6 | 依赖键精度 | ❌ 未触及（仍是地址键）。`tensors_overlap` 已提供，但**零调用点且被溢出击穿** |
| §7 | `create_buffer` 未查 `_next_level_shms` | ❌ **未修** —— 新方法 `worker.py:8261` 照抄了 `create_host_buffer` 的旧谓词（`level >= 3 and not _chip_shms and not _sub_shms`）。**只有 L3 子进程的 L4 拓扑会被直接拒绝**，且新方法没有任何测试 |
| §8 | 拆 PR | ✅ 已按 PR-1 拆出，Core churn 1200 行 / 15 文件，且近乎纯新增 |
| §9 | 逐项论证每个新增 | ❌ **未答**。`wrap_fork_inherited` / `wrap_vmm_window` / `remote_sidecar_tensor` / `host_ptr_nbytes` / `tensors_overlap` 五个符号在全仓 **零调用点、零测试**；`Worker.create_buffer` 亦无测试。冻结前是删除的最后窗口 |
| §10 | 文档一致性 | 🟡 `docs/buffer-abi.md` 已进仓 + mkdocs（规范性引用可达，本条解决）；但新增 3 处 stale：`buffer.py:430` 与 `test_buffer.py:9,13` 引用**并不存在**的 `pack`/`unpack`，`docs/buffer-abi.md:141` 写 `simpler.task_interface.Tensor`（与同文件 §Status 和 `test_wire_tensor_stays_off_the_public_submit_surface` 自相矛盾） |

### 13.1 新增缺陷（不在原 §1–§12 里）

| # | 内容 | 严重度 |
| - | ---- | ------ |
| **N1** | `tensor_extent_bytes` 溢出 ⇒ `validate_tensor` 接受 footprint 远超 backing 的视图（一维即可触发，Python 可达，已实测） | **必修** —— 布局中立，冻结前修最便宜 |
| **N2** | `BufferDescriptor::operator==` 用未 clamp 的 `body_len` 做 `memcmp`；今天不可达（构造即校验），但这是本头文件唯一一处"长度字段界定读取范围" | 应修 |
| **N3** | `BufferDescriptor` / `Tensor` 定义了值相等却没绑值哈希（nanobind 在建类型后才挂方法，`__hash__` 仍是 identity 哈希）；`__eq__` 未用 `nb::is_operator()`，与异类比较会抛 `TypeError` 而非返回 `NotImplemented` | 建议 |
| **N4** | `remote_sidecar_tensor` 用 `owner_worker_id` 构造 `owner_instance_id`，与 `mint_owner_instance_id` 上方"必须全宽随机"的不变量直接冲突 ⇒ 同一远端 worker 的两次 incarnation 得到相同 identity | 建议（P2 前登记） |

### 13.2 结论

**方向与形状都对，字节对表逐条一致**；卡住合并的是 N1 与 §7 两条 —— 都很小，且都是"冻结之后会变贵"的那类。
§9 的逐项论证是唯一只有作者能给的东西，合并即关窗。
