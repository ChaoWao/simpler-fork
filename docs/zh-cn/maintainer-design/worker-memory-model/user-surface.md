# 用户面契约：分配 API 与 `Buffer` 的可见性

> **发布快照：2026-10-19。** 设计、实现状态与历史调查须按正文各自基线阅读；不代表当前 main 全量验收。参见[阅读入口与已知更正](../README.md)。
>
> 来源：原 [`worker-memory-model.md`](../worker-memory-model.md) §5。

## 5. 用户面契约：分配 API 与 `Buffer` 的可见性

> **上游词汇**：[feature_roadmap.md](../feature_roadmap.md) §2.3（生命周期）／§2.4
> （`Buffer` / `Tensor` / `Attachment` / `ChipTensor` / `Transfer`；本文词汇见 §1.1）。
> 本节定的是**用户看到什么**，不重复那里的类型定义。

### 5.1 公共 Buffer、Tensor 与调用条目

契约来源：[#2460](https://github.com/hw-native-sys/simpler/issues/2460)（正文 2026-09-28）；实施归属 RA-13/RA-14，状态只见 work-items.md。

 | 对象 | 用户可见契约 |
 | ---- | ------------ |
 | Buffer | 存储 identity、capacity、HOST/DEVICE、owner/backend/grant；用户可持有 |
 | Tensor | 同一 backing 的 dtype/shape/stride/offset 几何视图，不承载可变调用策略 |
 | TaskArgs 条目 | 本次调用 direction/tag 与显式 transfer request |
 | runtime binding | 私有 endpoint-local 解析、地址与 retention；ChipTensor 可作为迁移期内部 ABI，旧入口经兼容适配 |

描述性 Tensor 不自动取得 allocation ownership；保留它或 Python wrapper 不延长 HeapRing scope。

**源登记与目标接入分层**：源 backing 的 identity、位置、范围和授权独立于消费者；submit 在目标 endpoint/context generation 下验证并建立/复用 attachment。同 backing 跨 Worker 保持 identity，目标地址与 grant 不回写成新的 backing。接纳必须原子取得使用保留，见 [访问与退休契约 §1](access-retirement-contract.md#1-身份授权与接纳)。

### 5.2 API 契约与已交付边界

代码口径为 `upstream/main@937c57efc71ad01a01c3a72072df075d2a5dd24f`：公共 `Tensor(buffer, shapes=..., dtype=..., strides=..., byte_offset=...)` 由 #2479/#2480 提供，`Buffer.tensor(...)` 是兼容转发；`python/bindings/task_interface.cpp::make_tensor_view` 共用几何与范围 validator。构造复制 descriptor，不分配/搬运 backing。`TaskArgs.add_tensor(..., transfer=...)` 保持调用条目级 request；`Worker.create_buffer` 与 `alloc_shared_tensor` 的 owner/HeapRing 边界不变。

目标分层遵循 §5.1。#2482 已提供 `Buffer.wrap(address=..., nbytes=..., location=worker.device_location)` 源登记与 direct-L2 submit 时接入；#2587 将 location 身份推进到 native environment，#2578/#2599 提供 borrowed RTS 接入；这不自动赋予跨 Worker/Remote 授权，也不转移外部分配或 borrowed context 的 ownership。close 拒绝在飞或未知完成的 source uses，外部分配由调用方保活和回收。#2612/#2614 补齐 prepare 失败与未启动 launch 的可证明释放；执行异常的后续仍归 #2615/RA-15。其余 consumer 与兼容移除边界见 [RA-13/RA-14](../work-items.md)。无论入口形状如何，必须保留有效源登记、provenance 校验与 accepted-use retention。Remote wire 拒绝门不替代 network→H2D 验收。

显式 COPY 写入已有 destination 时保持该 destination identity；只有新分配 destination 才有新 identity。

### 5.3 三条硬约束（来自 roadmap §2.3/§2.4，收窄 surface 时不得违反）

1. **`create` / `attach` / `transfer` 三语义必须可区分。** roadmap 明写
   *"public surface **可以更小**，但这三个语义必须保持可区分"*——收窄被许可，
   **塌成一个入口不行**。分水岭是 identity：`attach` 与 `copy_*` **保持** identity，
   **新分配 destination 的 transfer 创建新 identity**，所以它的名字必须刺眼。
2. **submit 不做 silent copy。** 目标 endpoint 没有满足要求的 attachment 时
   **明确失败**，用户**或域 plan** 必须**显式**请求 transfer/proxy
   （roadmap §2.4 不变量 5）。
   ⇒ 这是一个**析取**：**域 plan 显式声明就已满足不变量**，不要求用户手调 attach。
   Attachment 不进 public surface（§5.7 决策 C）后，「显式」由 `AttachmentPlan`
   （有域）或**传播本身**（worker 树）承担；用户能观察到的是**失败本身**
   （capability 不足即拒绝下发），而不是 attach 动词。
   ⚠️ 这条禁的是 silent **copy**，不是 silent **attach** —— 见 §5.7。
3. **`release` 是 Buffer 级事务，不是 view 级。** 一个 Buffer 上有 N 个视图；
   `release_buffer(view)` 作用于该视图**背后的 Buffer 登记**，不是单个 view。对自有 backing，物理释放须经 owner 和最后使用者证明；对外部借用，撤销登记不转移 allocation 所有权，也不是 device fence。文档与错误信息必须显式消歧。

### 5.4 视图派生

视图派生保持 backing identity，改变几何；Buffer 保持公共可见。不得因为新增 per-call transfer 而修改已有 Tensor 或先前 TaskArgs 条目。

### 5.5 三条连带契约

 | 项 | 契约 |
 | -- | ---- |
 | **统一字节访问** | 用户读写数据一律经 `view.memoryview()` / `view.torch()`，**不得暴露底层 `SharedMemory` 对象**。理由有二：① 暴露对象等于允许用户从 owner 背后释放；② device backing 上根本没有 shm 对象，**暴露它会让用户代码按 backend 分叉** |
 | **`copy_to` / `copy_from` 收视图，不收指针** | 否则 kind4 的 device 指针必须外露，**直接违反「`Tensor` 不携带已物化地址」**。收视图还顺带覆盖区间准入——视图天然就是 `base + byte_offset + extent`，provenance 校验退化为 identity 查表，不必再按指针数值区间判定 |
 | **所有权不变** | simpler 自有 Buffer 由对应 owner 管理，close 经最后使用者证明回收；外部借用保留原 allocation owner，内部登记不自动保活调用方内存。`Tensor` 是不可变描述性值，wrapper/GC 不替代 accepted-use/grant retention。scope-managed backing 仍服从 HeapRing 生命周期 |

### 5.6 `release_buffer` 事务顺序（roadmap §2.4）

```text
逻辑关闭当前登记 → 保留已接受使用及有效独立 grant → 证明各自最后使用者结束
  → 按所属合同撤销 attachment / unmap → 自有 allocation 由 owner free；借用仅撤销登记
```

submit 接纳时保留本次实际使用的 backing/attachment，不靠保存描述性 view 保活。引用按 [访问与退休契约](access-retirement-contract.md) 的源读取结束、目的就绪和整体静止分别退休；run 报错不能推导物理释放安全。有效独立 grant 不因关闭另一个 handle 而失效，其与源关闭的关系由明确的授权合同决定。

`buffer_id` 复用前递增 `generation`，普通 copy 不递增；stale handle、重复 release、FAILED-CLOSED 与 close-time cleanup 有确定语义。unmap/free 失败保留原句柄、费用与 journal，未知静止不得复用。正常完成后必须证明资源可回收，不能靠永久保留通过验收。

simpler 管理的 owner registry **必须区分两种 lifetime**；外部借用只记录授权与使用保留，不接管外部 allocation 的回收策略：

 | lifetime | 归属 | 关联 |
 | -------- | ---- | ---- |
 | **persistent owner-managed Buffer** | Worker，跨 run 存活 | 这正是 issue #824 / [cpu-npu-shared-memory/work-breakdown.md](../cpu-npu-shared-memory/work-breakdown.md) W3 的 per-worker 归属（两种寿命之一） |
 | **run / scope-managed BufferLease** | 单次 run | 与 `_RunResources` 的 per-run 所有权对齐 |

> **一个扁平的 handle 字典不满足本节**——它既表达不了两种 lifetime，也没有 retain
> 计数可言。

### 5.7 决策 C ✅ **Attachment 由域代管**（2026-08-03 拍板）

**`attach_buffer` / `detach_buffer` 不进 public surface。** `Attachment` 仍是五概念
之一，但它**不是用户概念**：有域时经 `AttachmentPlan` 在建域时**批量声明**，
worker 树上由**传播本身**建立（两个上下文见下）。

**决策的精确形式不是「要不要 Attachment」，而是「逐条录入 vs 批量声明」** ——
因为 `AttachmentPlan` 的键是 `(member EndpointId, CanonicalBufferIdentity)`，
**键本身就是端点级的**，它就是一张逐端点 attachment 表，只是成批声明。
⇒ `attach_buffer(view, endpoint)` 与 `AttachmentPlan` 是同一件事的两种录入方式。

**三条依据**：

 | # | 依据 |
 | - | ---- |
 | ① | **attach 是跨进程控制流量**。public 化后它必须自带 `_control_reservation` / 事务性 / 回滚 / `generation` / poison —— 而**域已经有这整套**（#1456 的 2PC）。再开一条 public 路径 = 第二份实现 |
 | ② ⭐ | [`graph-execution-contract.md`](../worker-memory-model/graph-execution-contract.md) **R2** 要求节点退休与所有相关使用引用共同满足回收条件；attachment 是其中跨进程的一类。域代管该引用，不意味着替代 DMA/copyback 或 graph/replay 的保留；用户能手调 `detach_buffer` ⇒ 用户可减掉正被远端读的引用，R2 退化为「用户别做傻事」 |
 | ③ | 域今天在**成员与 buffer 两个维度都不可变**（`GlobalDomainCommand` 携带定长 member 表 + 固定 buffer 列表，2PC 一次定死）⇒ 逐条 attach **没有可作用的对象**。⚠️ **仅对域成立**，见下 |

**代价（已接受）**：域建好之后才出现的 Buffer、成员后加入、capability 变更，
都只能用**建新域 / 换代**表达。可接受的理由：持久域的价值是**窗口复用**，
buffer 是窗口内的稳定切片而非每 run 新建（#824 的「复用前清零」正说明这一点）。

#### ⭐ 两个 attachment 建立上下文——依据 ③ **只覆盖了其中一个**

上面三条依据是围绕 **CommDomain** 推的。但**普通 tensor 背后的 Buffer 不一定在任何
域里**：它由父 Worker 分配，沿 worker 树传播给子 Worker。**worker 树不是不可变的** ——
父随时可以新分配一个 tensor，下一次 `submit` 就传给子。⇒ **依据 ③ 在树上不成立。**

**结论仍然不变，但树的理由是另一条**：

 | 上下文 | Attachment 怎么建立 | 引用持有者 | 为什么不需要 public `attach_buffer` |
 | ------ | ------------------- | ---------- | ----------------------------------- |
 | **CommDomain**（成员间） | `AttachmentPlan` 建域时**批量声明**，2PC | 域 | 依据 ①②③ |
 | **Worker 树**（父→子） | **传播即 attach** —— 即 §2 递归规则的「子必须在下一条 edge 上重新 export/import/map」 | **Worker** | 依据 ①② ＋ **传播本身已是声明，再 attach 是同一件事声明两遍** |

依据 ①（attach 是跨进程控制流量、已有既成机制）与 ②（R2 要求引用所有者唯一 ——
树上是 Worker，同样**不是用户**）**两边都成立**；只有 ③ 是域专属的。

##### 载重区分：**不变量 5 禁的是 silent *copy*，不是 silent *attach***

roadmap 自己的定义就划开了这条线：

- *"`attach_buffer` 为同一 Buffer 建立 endpoint-local Attachment，**不复制数据**"*
- *"`transfer` 显式复制到目标 physical resource，**返回新的 Buffer identity**"*

⇒ **capability 允许时随 edge 建立 attachment 是合规的**（零拷贝、identity 不变、
无隐藏代价）；**capability 不允许时必须明确失败**，由用户显式 `transfer`（有真实
代价；新分配 destination 才产生新 identity）。不变量 5 要防的是**隐藏的数据搬运**，不是隐藏的映射建立。

> 这条同时限定了「传播即 attach」的边界：它**只在 capability 已经存在时**成立。
> 一条 capability 不足的 edge（例如跨机 device peer）**不得**由 submit 自动补上
> 一次 transfer —— 那正是不变量 5 要拒绝的 silent copy。
>
> **连带更正**：§5.3 硬约束 2 此前从不变量 5 推出「⇒ 用户必须有东西可以拿去
> attach」。**那个 `⇒` 是错的** —— 不变量 5 原文是「用户**或**域 plan」，一个析取；
> 域 plan 显式声明即满足。**该不变量从未强制 `attach_buffer` 进 public surface。**

###### 对硬约束 1 的自检**（`create` / `attach` / `transfer` 三语义必须可区分）：**仍满足

attach **没有消失，只是不再是用户调用的动词**——它有自己的键
（`(EndpointId, CanonicalBufferIdentity)`）、自己的值（capability + attachment），
以及两种录入形态（`AttachmentPlan` 的一条声明 / 一条 edge 的 export-import-map），
与 `create_buffer`（新 allocation + 新 identity）和 `transfer`（复制 + 新 identity）
三方互不重叠。**分水岭仍是 identity 与是否复制**：attach 两者皆不动，
`create` 造新 identity，`transfer` 复制且造新 identity。roadmap §2.3 明写 *"public surface **可以更小**，但这三个语义必须保持
可区分"* ⇒ **本次收窄在其许可范围内。**

> ⚠️ **与 [`feature_roadmap.md`](../feature_roadmap.md) 的已知分歧**：roadmap 的 API
> 清单里仍列 `Worker.attach_buffer(...)` / `Worker.detach_buffer(...)`（约 431–432 行），
> 并在其后解释三动作的区别。那是**收窄前的形态**。roadmap 是 🔒 作者的 PM 真源，
>
> ###### 本文不代改**—— 差异在此显式登记：**public surface 以本节为准，语义定义以 roadmap 为准

### 5.7b 命名与归属

transfer 的调用作用域由 #2460 的 U1a 定义，归 RA-5；不另列待裁决。registry 归一问题见 track-boundaries.md 契约 D。

### 5.8 kind1 / kind2 的位置

kind2 = 普通 local tensor，打 local-only 标记，`submit` / `add_tensor` 处
**拒绝下发**（今天纯约定、零校验）。kind1 由"把上级 kind3/kind4 视图传进
`submit` args"产生，消费端按该 `(endpoint, CanonicalBufferIdentity)` 的
**capability + live Attachment** 校验本 edge 是否被授权（**不是**读描述符上的
`visibility` 字段——该字段已删，见 §3）。

**那份 Attachment 从哪来，取决于 edge 处在哪个上下文**——见 §5.7 的两个上下文表：

 | edge 的上下文 | Attachment 来源 |
 | ------------- | --------------- |
 | 成员间（**有域**） | `AttachmentPlan` 中该 `(member EndpointId, CanonicalBufferIdentity)` 的条目 |
 | **父 → 子（worker 树，无域）** | **传播本身建立**——即 §2 递归规则里那句「子必须在下一条 edge 上重新 export/import/map」 |

### 5.9 分配归属：由 **fork 时序**决定，不由 API 口味决定（2026-08-05 讨论产出）

> 起因是一个很自然的质疑：*"一般不都是 torch 转成我们吗，为什么要转 torch？"*
> 答案是**两个方向都不叫"转"**——`torch.frombuffer` 只拿指针 + shape/dtype，零字节搬运。
> 真正决定形态的是**这块内存是谁、在什么时候分配的**。

#### 判据

```text
内存在 fork 之前就存在且可共享   → torch 分配也行，wrap 一下即可，零拷贝     （torch → 我们）
内存在 fork 之后才需要          → 要么我们分配，要么拷贝，没有第三条路      （我们 → torch）
```

理由是纯时序的，不是设计偏好：`Worker.init()` 是 eager 的，子进程地址空间在 `init()` 那一刻定型；
之后 `torch.randn(...)` 拿到的页在子进程里**根本不存在**——不是地址对不上需要平移，是没有这一页。
**这正是 `create_buffer` 必须落在 `POSIX_SHM` 上的原因**：只有它有名字，可以事后 `shm_open` + `mmap`，
是[设计 §2](../worker-memory-model/p1b-corrected-design.md) 里**唯一真正建立映射的 backend**；
`FORK_*` 的恒等物化前提（同 VA）只在 fork 之前分配时成立。

 | 场景 | 谁分配 | 入口 | 拷贝 |
 | ---- | ------ | ---- | ---- |
 | 输出 buffer、scene test 输入、KV cache / ring、control region / arena、一块 backing 切 N 个视图 | **Worker** | `alloc_shared_tensor` / `create_buffer` | 无 |
 | pre-fork 的大块 foreign 内存（预加载权重、上游 pipeline 交来的 tensor） | torch / 外部 | `wrap_fork_inherited` | 无（**必须 pre-fork**） |
 | fork 之后才拿到的 foreign torch tensor | torch | 只能 rehost | **有**——今天 scene test 的唯一路径 |

**输出 buffer 是最强的一格**，也最容易被忽略：它根本没有"原始 torch 数据"可转，你是在申请一块空内存
给 kernel 写。今天 `_RehostedTaskArgs` 对输出照做一遍 `create_host_buffer` + `frombuffer`，
**本来就已经是"我们分配、torch 来看"**，只是绕了一圈。

今天 rehost 的五条限制——每 tensor 一次 `copy_`、必须 contiguous、任意两 arg storage 相交即拒、
`.view()` + `make_tensor_arg` 两处丢 stride、创建时急切广播——**没有一条是设计选择**，
全是"太晚才拿到 foreign 内存"的连锁后果。

#### `.torch()` 是**渲染**，不是转换，而且是可选糖

契约是 §5.5 那条：视图要能给出一个 host 字节窗口，**或者对没有 host 映射的 device backing 明确拒绝**。
`.memoryview()` 就能满足；`.torch()` 只是省掉用户手写
`torch.frombuffer(v.memoryview(), dtype=...).view(v.shapes)` 这一行，理由是用户写 golden 比较时
大概率就在用 torch。**`simpler` 不依赖 torch**（`buffer.py` 连读 torch tensor 都是 duck-typing），
所以它必须是惰性导入，且 torch 缺席时整个包照常工作。

⚠️ 落地时三个细节：① dtype 映射不满射（`FP8E8M0` / `FP4E2M1` 在 torch 里没有对应物，必须干净失败或
退化成 `uint8` 字节视图）；② `frombuffer` 出来的 tensor 持有 memoryview 引用，不先释放就
`close()`/`unlink` 会抛 `BufferError`（今天 `_RehostedTaskArgs.release()` 那段
`del view` → `release()` → `except BufferError` 就是在跳这个舞），**这条必须写进契约**；
③ device backing 上 `.torch()` 必须**报错**而不是返回什么——否则用户代码按 backend 分叉，
这正是 §5.5 禁止暴露 `SharedMemory` 对象的第二条理由。

#### 用户代码形态

```python
# ① 绝大多数 scene test 作者：零改动。框架代付一次拷贝，白拿 stride 保真 + 构造校验 + identity 依赖键
def generate_args(self, params):
    return TaskArgsBuilder().tensor("x", torch.randn(M, N)).tensor("out", torch.zeros(M, N))

# ② 在意那次拷贝的人：让框架替你分配，原地填
def generate_args(self, params, alloc):
    x = alloc((M, N), torch.float32)
    x.torch().normal_()                       # 落的就是最终那几页物理页
    return TaskArgsBuilder().tensor("x", x).tensor("out", alloc((M, N), torch.float32))

# ③ 新能力：一块 backing 切多个视图（今天被 _reject_aliased_tensors 一律拒绝）
top, bottom = big.slice(rows=(0, M // 2)), big.slice(rows=(M // 2, M))
ta.add_tensor(top, INPUT); ta.add_tensor(bottom, OUTPUT_EXISTING)   # 不相交 → 合法并行

# ④ pre-fork 大权重（pypto-serving 类）：顺序是硬约束
w = torch.load(...).share_memory_()           # 必须在 init() 之前，且必须 MAP_SHARED
worker.init()
wb = wrap_fork_inherited(w.data_ptr(), w.nbytes, access=READWRITE)   # → FORK_SHM，零拷贝
ta.add_tensor(wb.tensor(w.shape, dt, strides=w.stride()), INPUT)     # strides 要显式传
```

**不要做的三件事**：碰 `h.shm.buf`（device backing 上不存在）· 再传裸 `data_ptr()`（地址在别的进程
无意义）· 在 orch fn 里用 torch 算数据（对依赖推导完全不可见）。第一条在 #1599 上是**当前的实际写法**，
属过渡期偏差，见[设计](../worker-memory-model/p1b-corrected-design.md)顶部 review ④；
现在就要基于 `create_buffer` 写代码的话，**把 `h.shm.buf` 包一层自己的 helper**，
将来换成 `v.memoryview()` 只改一处。
