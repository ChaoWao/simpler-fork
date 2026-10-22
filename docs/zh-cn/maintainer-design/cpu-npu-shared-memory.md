# CPU ↔ NPU 共享内存与常驻编排（设计 · landing）

> **发布快照：2026-10-21。** 设计、实现状态与历史调查须按正文各自基线阅读；不代表当前 main 全量验收。参见[阅读入口与已知更正](README.md)。
>
> **状态**：⬜ **设计提案（design-only，未实现）**。这是维护者 author 并维护的规划
> 产出，**不是**交给实现者的 code prompt；它**喂**给后续拆分的实现任务。
> **来源**：与用户的设计讨论（2026-07-24/25），目标由用户给定，分层由本文提出。
>
> ## 基线**：`upstream/main@b69f4537`（含 #1638）。**实施前重新 fetch
>
> **标注约定**：✅ 已核实（附 file:line）｜🟡 待验证｜⬜ 提案/未实现。

## 本组文档

 | 文档 | 内容 | 读者意图 |
 | ---- | ---- | -------- |
 | **本文（landing）** | 目标 · 现状 · 设计 · 分层 · 路线 · 风险 | "我们在造什么、为什么" |
 | [`cpu-npu-shared-memory/hardware-constraints.md`](cpu-npu-shared-memory/hardware-constraints.md) | **硬件与平台约束**（UVM 四能力拆解 / a5 host-map 更正 / 一致性 / HBM 钉死 / 地基结论） | "硅到底允许什么"（**参考手册**，改设计前先查） |
 | [`cpu-npu-shared-memory/domain-membership.md`](cpu-npu-shared-memory/domain-membership.md) | **通信域的成员模型**（跨层级如何表达；回答"L4 建的是 L3 还是 L2 的域"） | "域怎么表达"（**契约**） |
 | [`cpu-npu-shared-memory/work-breakdown.md`](cpu-npu-shared-memory/work-breakdown.md) | **实施分解 W0–W8**（结合当前代码：现状锚点 / 规模 / 依赖 / 风险 + 依赖图与排期） | "要做哪些事、什么顺序"（**计划**，不含 prompt） |

关联：现状诊断见
[l3l4/cluster-comm.md](l3l4/cluster-comm.md) §D.1/§D.3；
目标内存 ABI 见 [worker-memory-model.md](worker-memory-model.md)（全局 roadmap P1）。

## 0. 定位与目标

> ### 本设计**不是加一种新能力，而是去掉热路径上的拷贝**
>
> **系统今天全部靠 copy 运转**，且**运转正确**：
> DAG 任务靠 `copy_to` / `copy_from`；`worker_chip_queue` 靠 ACL copy + signal counter。
>
> 在**同一个 canonical Buffer** 上，direct-map share 与 owner-delegated copy 在
> **读写效果和同步契约相同**时语义等价，只有 data path 与性能不同。因此：
>
> - 同一 Buffer 的 direct-map 路径以既有 owner-delegated copy 为正确性基线，
>   验收判据是结果逐字节相同（§4.3）；
> - direct-map 不可达时，只有在同一 Buffer 已有 copy Attachment、显式 policy 允许，
>   且最终 data path 可观察时，才可选择 owner-delegated copy；
> - explicit `Transfer` 会创建新的 Buffer identity，不属于上述等价关系，也不能作为
>   silent fallback；
> - 在这个 same-identity 范围内，share 只有性能理由，所以要**逐场景选**，不是
>   "能 share 就 share"（§1.2）。

**目标**：在**单节点**内，把 host↔device、device↔device 热路径上的拷贝去掉；
在共享区上提供**结构模板**（队列等）；并支持**常驻 worker**（`worker.run` 内的 while 循环）
跨迭代驱动收发。

> **上位原理**：[logical-physical-separation.md](logical-physical-separation.md)
> —— 逻辑组件 ⊥ 物理资源；能力挂在**边**上，规范形式是
> `AttachmentPlan = (member EndpointId, CanonicalBufferIdentity) -> capability + attachment`。

**用户给定的边界条件**：

 | 维度 | 结论 |
 | ---- | ---- |
 | 基座内存 | **device HBM 映射给 host**，不需要 host-local 内存 |
 | host 访问方式 | **映射 + 拷贝并存**（控制面映射、数据面拷贝） |
 | 参与方 | host↔device、device↔device、host↔host，**均同节点** |
 | 跨机 | **不做**——本设计只建立单节点 Attachment；跨机显式 Transfer / remote transport 属于全局 roadmap P2/P3 范围，不伪装成共享同一 Buffer。⚠️ 这是**范围选择**，不是「物理不可能」：跨机域**已在双机真硬件上建立**（#1456→#1623→#1624 栈），可用 adapter 集合见 [domain-membership.md §4.1](cpu-npu-shared-memory/domain-membership.md) |
 | 首选 arch | **a5 优先**（🟡 a5 host-map 是**外部驱动假设，未验证**，且 **simpler 侧 adapter 尚未实现**——见 §7.0 / 约束文档 §2），**再按同一接口实现 a2a3** |
 | 结构 | 用户定，runtime **提供各类模板** |
 | 常驻 | `worker.run(orch_fn)` 内 while 循环，读队列→dispatch→回推结果 |
 | 与现有 `create_worker_chip_queue` | **重构/取代**（迁移路径见 §6） |

## 1. 本设计在改什么：两个**正交**的轴

**「消费者怎么知道数据好了」和「字节怎么从 A 搬到 B」是两件独立的事。
本设计只改第二件，第一件原封不动。**

### 1.1 两个轴

#### 轴一：同步 —— 消费者怎么知道数据好了

 | — | 机制 | 谁在用 |
 | - | ---- | ------ |
 | **隐式** | 靠**任务边界** —— kernel 被调度起来这件事本身就意味着数据就位；**同步是调度器免费给的** | 现有 DAG 任务 |
 | **显式** | 靠 **signal counter** —— 消费者必须自己 `wait`，因为它**一直在跑、从不结束**，借不到任务边界 | 现有 `worker_chip_queue` + 常驻 L2 任务 |

#### 轴二：数据移动 —— 字节怎么过去

下表中的 `copy` 专指通过 Attachment 读写**既有 canonical Buffer** 的
owner-delegated copy，`share` 专指直接访问**同一个 Buffer**；创建新 Buffer 的
explicit `Transfer` 不在这张二维表里。

 | — | 做法 | 字节 |
 | - | ---- | ---- |
 | **copy** | `rtMemcpy` / ACL copy | **两份** |
 | **share** | 双方映射同一块物理内存 | **一份** |

### 1.2 四个格子

```text
              同步:隐式(任务边界)      同步:显式(counter)
  copy    ①  现有 DAG 任务 ✅          ②  现有 worker_chip_queue ✅
  share   ③  本设计要做                ④  本设计要做
              ↑                          ↑
              └──── 这两列(同步机制)一个都不改 ────┘
```

 | 格 | 说明 |
 | -- | ---- |
 | **① 隐式+copy** | `copy_to` → `submit` → `copy_from`。今天的主力 |
 | **② 显式+copy** | `payload_write`（onboard 是 **ACL copy**，host 是 importer 而非直接解引用）+ `counter_notify`。**今天就在跑** |
 | **③ 隐式+share** | 同步仍靠 `submit`/完成；**任务前后的两次 copy 没了** |
 | **④ 显式+share** | 同步仍靠 counter；**执行期的 ACL copy 换成 host 直接 LD/ST** |

> #### ⇒ 本设计 = 在同一个 canonical Buffer 上从上面一行移到下面一行。左右两列不动
>
> 在读写效果也相同的前提下，direct-map 与 owner-delegated copy 的同步机制和
> Buffer identity 都不变，所以语义不变；变的只是字节走了哪条路。

#### ⚠️ 但「等价」的适用范围必须钉死：判据是 **Buffer identity**

上面那句等价**只在同一个目标 Buffer 上成立**。把它推广到"任何搬数据的操作"就错了。
按 [feature_roadmap.md](feature_roadmap.md) 规范的 `TransportPlan` adapter 划分：

 | adapter | 做什么 | Buffer identity |
 | ------- | ------ | --------------- |
 | **direct-map / device-peer** | 让另一个端点**直接映射 / peer 访问同一个 canonical Buffer 的 backing** | **保持原 identity** |
 | **owner-delegated copy** | 委派 owner **读写同一个既有 canonical Buffer** | **保持原 identity** |
 | **explicit transfer** | **分配目标 Buffer 并复制过去** | **创建新 identity** |

- **可互换的只有前两者**：③ share 走 direct-map、① copy 走 owner-delegated copy，
  两者都作用在**同一个 canonical Buffer** 上，谁都不产生新 identity。在相同读写效果和
  同步契约下，依赖图、`CanonicalBufferIdentity`、OverlapMap 全部不变。这是显式 data-path
  policy 下的性能选择；runtime 的最终选路必须可观察。
- **explicit transfer 不在等价关系里**。它产生**新的 `CanonicalBufferIdentity`**，于是依赖边、
  别名判定、生命周期归属全都变了。**把 direct-map 降级成 explicit transfer 不是降级，
  是换语义。**
- **跨 identity 边界不得静默选路**：必须按**显式 policy** 选择，并把结果**告知调用者**。
  见 [logical-physical-separation.md](logical-physical-separation.md) §5。

### 1.3 ⚠️ share 不总是更快 —— 要逐场景选

在上述 same-identity 等价范围内，data path 是性能选择，应按数据特征选，而非
"能 share 就 share"：

 | 数据 | 更快的方式 | 为什么 |
 | ---- | ---------- | ------ |
 | **小块控制面**（head/tail、flag、描述符） | **host 直接 LD/ST**（share） | 免 DMA 启动开销；单次几字节 |
 | **大块 payload** | **`rtMemcpy` / DMA**（copy） | **DMA 带宽远高于 host CPU 逐字节过 PCIe** |

> **这才是 `ResourceBundle` 将 control/payload 表达为两个 Buffer 的首要理由** ——
> 两块区**最优的数据移动方式本来就不同**。即使硬件允许一块内存两用也应该拆；
> §2 的物理约束（VMM VA 不可 host-register）只是**顺带印证**了它。

### 1.4 常见误解（我自己踩过）

 | 误解 | 更正 |
 | ---- | ---- |
 | 「persistent kernel 需要 share」 | ❌ 它需要的是**显式同步**（counter）；今天用 owner-delegated copy 也能正确运行（格 ②），direct-map 只是另一条 data path，性能须按 §1.3 选择 |
 | 「同一 Buffer 的 direct-map share 必然引入新的可见性语义」 | ❌ 同步契约不变时，direct-map 与 owner-delegated copy 可保持相同语义 |
 | 「direct-map 不可达时总能静默 copy」 | ❌ 仅同一 Buffer 上已有 copy Attachment、显式 policy 允许且选路可观察时可替换；explicit Transfer 会创建新 identity，必须显式 |

## 2. 硬件与平台约束（摘要 → 详见子文档）

> **全文**：[`cpu-npu-shared-memory/hardware-constraints.md`](cpu-npu-shared-memory/hardware-constraints.md)

**本设计的每一条决策都能追溯到某条硬约束**，没有一条是风格偏好：

 | 约束 | ⇒ 推出的设计决策 |
 | ---- | ---------------- |
 | **VA 不保证一致**：HAL 契约明确允许非 identity VA（✅ `runtime_maker.cpp:709-712` 自述）；跨进程 VMM/Fabric import **每个 importer VA 各异** | ⇒ **offset 寻址是硬性的**（§3.3），禁绝对指针 |
 | **无自动迁移**：数据永在 HBM，host 每次解引用都是 PCIe 事务 | ⇒ **控制面 / 数据面分离**（§3.5） |
 | **一致性不对称、分代不同、sim 与 a5 都测不出**；且**一致性 ≠ 顺序** | ⇒ **同步 runtime 独占**，API 只暴露 publish/observe（§3.4）；⇒ **a5-first 三条纪律**（§5） |
 | **`dcci` 整条 64B cacheline** | ⇒ **counter 每 64B 单写者**（§3.3） |
 | **HBM 物理钉死**，无换页，不够即硬失败 | ⇒ **模板一律有界 + 背压**（§4.1）；容量前置决定 + 预算可观测 |
 | **CPU 不能当 HCCL rank** | ⇒ host 是**部署视图**，不是 rank（成员模型见 §3.0） |
 | **level 不携带物理含义**（编排树位置）；**部署 × 互连**只是访问潜力的必要门槛 | ⇒ 成员用 **`EndpointId = session_id + worker_path + deployment`**，完整 capability 还须结合 backing/backend、runtime support 与 live Attachment 条件；**禁止以 level 判定共享能力**（§3.0） |

🟡 **a5 host-map 的状态必须分两条说**（详见子文档 §2）：**驱动侧支持 = 外部假设，本仓未
验证**（待 §7.0 的 W0 出结论）；**simpler 的 a5 adapter = 不存在**（`src/a5/` 内 grep 不到
`register_device_memory_to_host`，走 `DeviceRunnerBase` 返回 `nullptr` 的 base default）。
⇒ 设计可按 a5-first 推进，但**不得把它当作已具备的能力描述现状**；仓库里那 6 处
"a5 没有 halHostRegister"的注释，**要等 W0 通过且 adapter 落地后**才谈删除。

## 2.5 现状盘点：三条路各缺什么

 | 现有机制 | 是什么 | 缺什么 |
 | -------- | ------ | ------ |
 | `orch.allocate_domain`（CommDomain）；✅ `orchestrator.py:370` / `worker.py:4799` | device HBM window，经 VMM/Fabric-V2 在 **chip 子进程间**共享；kernel 经 `windowsIn/Out[]` 用 TGET/TPUT 访问（✅ `comm_context.h:44`，注释 `:11-17` 明说 **device-kernel-only**） | **host 拿不到映射**——只收到 `device_ctx` + `local_window_base` **两个整数**（✅ `worker.py:1123-1132` / `:4897`）；映射只发生在 fork 出的 chip 子进程内（✅ `worker.py:1103`）。且 `workers` **强制为 chip 子集**（✅ `worker.py:4816-4822`） |
 | `orch.create_worker_chip_region`；✅ `orchestrator.py:415` | host↔**单个** chip 的点对点区域 + signal counters | onboard 上 host 是 **importer/copier**（ACL copy），**非 CPU 直接解引用**（✅ `docs/l3-l2-orch-comm.md:148-151`）；点对点，非多方 |
 | `orch.create_worker_chip_queue`；✅ `orchestrator.py:421` | 其上的 SPSC 队列（descriptor ring + payload arena + head/tail counters，32B `WorkerChipQueueDescSlot{seq,opcode,payload_offset,payload_nbytes}`），由**常驻 L2 AICPU orchestration task** 消费 | 结构**由 runtime 写死**，用户不能自定义元素/布局；只支持 host↔单 chip |

✅ **关键正面事实**：`host_build_graph` runtime **已经在生产路径上**用
`halHostRegister` 让 host CPU 直接 `memcpy` device 内存
（`runtime_maker.cpp:712`、`pto_runtime2.cpp:235-256`）。
⇒ **这条路是通的、已验证的**，只是没被抽象成通用能力。风险因此大幅降低。

相关历史提案：**#861**（`HostDeviceMappedRegion`；其 **a5 = `-ENOTSUP` 桩**基于**待验证**
的假设——本设计下 a5 应改为真实实现）、**#803**（host↔device 通道）、**#824**（通信域跨
`Worker.run` 持久化）。D.3「host+device 同域」当前状态 = ❌ 未实现。

---

## 3–4 设计细节与分层 → 子文档

 | 内容 | 去 |
 | ---- | -- |
 | 域的成员模型（MemberSet / ResourceBundle / AttachmentPlan / TransportPlan） | [`domain-membership.md`](cpu-npu-shared-memory/domain-membership.md) |
 | 硬件与平台约束（UVM / host-map / host-register） | [`hardware-constraints.md`](cpu-npu-shared-memory/hardware-constraints.md) |
 | 实施分解 W0–W8 + 依赖图 | [`work-breakdown.md`](cpu-npu-shared-memory/work-breakdown.md) |

原 landing 中的设计细节（§3–4）已移至上述子文档。
