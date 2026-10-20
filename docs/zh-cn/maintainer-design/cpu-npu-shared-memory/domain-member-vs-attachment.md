# 域成员、rank 与 attachment：三个概念该怎么分

> **发布快照：2026-10-19。** 设计、实现状态与历史调查须按正文各自基线阅读；不代表当前 main 全量验收。参见[阅读入口与已知更正](../README.md)。
>
> **状态**：🔒 **设计裁决**。回答一个具体问题：`GlobalDomainMember` 该加什么维度。
> **结论与提问不同向** —— 该加的维度不在成员上。
> **锚点**：[issue #1836](https://github.com/hw-native-sys/simpler/issues/1836)（V1/S1 的 GitHub 面）。
> **上游原理**：[总纲](../logical-physical-separation.md) §1（物理资源 ⊥ 逻辑 Worker）；分线判断见 [`track-boundaries.md`](../track-boundaries.md)。
> **物理依据**：[hardware-constraints.md](hardware-constraints.md) §2「CPU 不能当 HCCL rank」、§5。
> **契约**：[domain-membership.md](domain-membership.md) §10（端点表达形式）。
> **标注**：✅ 已核实（附 `file::symbol`）｜🟡 待验证｜⬜ 提案。
> **锚定约定**：沿用 work-breakdown —— 一律 `file::symbol` 或合并 commit，不用行号。
> **基线**：`upstream/main@cc9bfc45`。

**读这份文档的理由**：`domain-membership.md` §10 要求成员以
`EndpointId = session_id + worker_path + deployment` 表达，而 wire 上是四个裸整数。
把 §10 的字面要求直接套到 `GlobalDomainMember` 上会加错字段 —— 本文说明为什么，
以及正确的落点在哪。

## 1. 结论

 | 问 | 答 |
 | -- | -- |
 | `GlobalDomainMember` 该加 `deployment` 吗？ | ❌ **不该** —— DEVICE 内不分引擎（§2），HOST 进不了这张表（§3） |
 | 那该加 `address_space` 吗？ | ❌ **也不该** —— 按 §3，该表恒为 DEVICE，字段恒真 |
 | 缺的到底是什么？ | **attachment 这一轴**（§4）。域的 wire 上只有「谁是 rank」，没有「谁挂在这块 window 上」 |
 | §10 那条要求作废了吗？ | **没有。** 它的 `worker_path` 那半独立成立（§5.3），`deployment` 那半落在 attachment 上 |

## 2. ✅ DEVICE 内不分引擎 —— 访问能力上 AICPU ≡ AICORE

判定分界是 **HOST DDR vs DEVICE GM**，不是三值的部署枚举。仓库里唯一真正求值 capability
的地方自己就是这么写的 —— `comm_endpoints::DefaultRegionAccessService::_evaluate_host_vmm_copy`
把两个 device 值并成一组，错误消息原文是 *"host VMM copy requires a **device** provider"*：

```python
if query.provider.deployment not in (DEVICE_AICORE, DEVICE_AICPU):
    ... UNSUPPORTED_ENDPOINT_RELATION, "host VMM copy requires a device provider"
if query.consumer.deployment is not HOST_CPU or not query.same_node:
    ... UNSUPPORTED_ENDPOINT_RELATION, "host VMM copy requires a same-node host consumer"
```

⇒ 三值枚举在唯一做判定的那一处塌成两值，而这两值仓库里已有名字：
`AddressSpace.HOST` / `AddressSpace.DEVICE`（`_task_interface` 导出，已随
`BufferDescriptor` 跨进程走 wire）。

**`EndpointDeployment` 并不因此作废** —— 它管的是另外两件事，都不是「能不能访问」：

 | 用途 | 归属 | 例 |
 | ---- | ---- | -- |
 | 谁有权发起 / 谁拥有控制路径 | **归属**，不是访问 | `comm_region::validate_single_owner_region_shape` 拒非 `DEVICE_AICPU` provider —— 因为 worker-chip region 由 AICPU 侧控制路径创建，不是因为 AICore 够不着 GM |
 | 发哪些 cache maintenance 指令 | **每个访问点**的属性 | a2a3：AICore 写→AICPU 自动可见；host DMA/SDMA 写→AICPU 须 `cache_invalidate_range`；AICore 自身 cache 不 `dcci` 数据到不了 GM（见 `docs/hardware/cache-coherency.md`） |

两者都不是成员表的字段：前者属 attachment，后者属 W4 的 arch 抽象。

## 3. ✅ `members` 不是参与者列表 —— 它就是 rank 表

五处把「成员」和「HCCL rank」焊成同一个东西：

 | 位置 | 约束 |
 | ---- | ---- |
 | `global_comm_domain::validate_member_table` | 要求 `domain_rank` 稠密等于 `range(len(members))` ⇒ **成员下标 ≡ rank** |
 | `global_comm_domain::validate_descriptor_table`（由 `Worker::_import_global_domain_node` 以 `rank_count=len(command.members)` 调用） | **每个成员必须有且仅有一个 descriptor** ＝ 一块导出的 window |
 | `Worker::_prepare_global_domain_node` | 按 `member.local_worker_id` 向 `WorkerType.NEXT_LEVEL` 发 `CTRL_GLOBAL_DOMAIN_PREPARE` ⇒ **每个成员必须是一个 chip 子进程**；`local_worker_id` 是**路由地址**，不是坐标 |
 | `Worker::_allocate_global_domain` | `global_device_rank` 取自 `node.global_device_ranks[local_worker_id]` ⇒ 每个成员必须是一张卡 |
 | `global_comm_domain::GLOBAL_DOMAIN_MAX_RANKS` ＝ 64 | ＝ `task_interface::COMM_MAX_RANK_NUM`，是 `CommContext.windowsIn[]`/`windowsOut[]` 的**定长数组 ABI**，不是成员数上限 |

而 [hardware-constraints.md](hardware-constraints.md) §5 已定：**CPU 不能当 HCCL rank**
（每 rank 必须 `aclrtSetDevice` 绑一张卡），"host 加入通信域只能是同一逻辑域上的一种
capability view"。

⇒ **往 `members` 里塞一个 HOST 条目，上面五条全破**：它没有 window 可导出（第 2 条）、
没有 chip 可路由（第 3 条）、没有 `global_device_rank`（第 4 条），还会白占一个 64 槽 ABI
名额（第 5 条）。

**所以 HOST 永远不会出现在这张表里。** 那么在成员上加 `address_space` 就是一个恒为
`DEVICE` 的字段 —— 它不是文档，是**诱饵**：后来的读者会以为 `HOST` 是可能取值。

> 这跟计划里「占位只接受 `DEVICE_AICORE`」是同一件事的两个面。那个占位之所以别扭，
> 不是因为值选错了，是因为**这个位置上没有要区分的东西**。

## 4. ⬜ 缺的是 attachment 轴

\#1770（`comm_region.py`，merged `e4ab544a`）已经把正确形状落在树上了：
**`members`（谁参与）＋ per-part `attachments`（`role` ＋ `adapter_kind` ＋ `adapter_profile`）**。
host 消费者在那里的表达是 `CONSUMER` ＋ `OWNER_DELEGATED_COPY`/`HOST_VMM_COPY`
（`comm_region::_validate_consumer_attachment`）—— **它不占 rank，它挂在别人的 window 上**。

⛔ **2026-09-07 更正：这句已经不对了，而同一份文件 `:105-106` 早就写着 ✅ 已交付。**
`#1879` 交付了域 wire 上的 attachment：`GlobalDomainCommand.attachments`
（`global_comm_domain.py:215`）· 校验 `:647` · 编码 `:686-688` · decode 恢复 ·
生产者 `worker.py:9848`/`:9889` 传 receiver-node × rank 矩阵。

⭐ **这条为什么值得留着而不是删掉**：它被照抄进了看板的 `W2` 格，再进了重组后的
`RA-8`，于是「缺 attachment 上 wire」作为**当前缺口**活了下来 —— 而按它去做，
会**重新设计一个已经在树上的协议**。
判据：**一份文件内部的自相矛盾比跨文件的更危险** —— 跨文件时读者会去对表，
同一份文件里读者只会读到先看见的那半。

✅ **而且地基已经半铺好、正死着**：`Worker::_prepare_global_domain_node` 把每张卡的
window base 存进 `_GlobalNodeDomainState.local_window_bases`，
**全仓只有声明与写入两处命中、零读者**（正对照：同结构的 `mapping_sizes` 在
`tests/ut/py/test_global_comm_domain.py` 有一处引用）。L3 host 已经拿到本地 window
的基址了，只是没有词汇把它表达成一次参与，也没有访问原语去解引用它
—— 那是 device VA，host 直接 deref 不了。

⇒ **host attachment 的两个前置分别是「词汇」（本文 §5）与「访问原语」**
（[work-breakdown.md](work-breakdown.md) 的 W1lite：`ld/st` 真映射 vs `rtMemcpy` 镜像）。
只加词汇会得到一个解引用不了的基址。

## 5. ⬜ 设计

### 5.1 三个概念归位

 | 概念 | 是什么 | 载体 | 基数 |
 | ---- | ------ | ---- | ---- |
 | **rank** | 导出一块 window、参与 fabric peer import 的设备 | 现有 `GlobalDomainMember`，**不加字段** | 1..64（ABI 硬限） |
 | **attachment** | 挂在这个域的 window 上做读写的端点 | ✅ **已交付（#1879 `5cb8110d`）**，与 `members` 平行 | ⛔ ~~每个 rank 隐含一条 PROVIDER；host 各自一条 CONSUMER~~ —— **实际是 receiver-node × rank 矩阵（M×N）**，见 §5.2 抬头更正 ① |
 | **deployment** | 谁发起 / 发哪些维护指令 | attachment 的字段，**不是 rank 的字段** | — |

按 [§4 规则 4](../README.md)（1:1 不新增名字）：rank 与 attachment 基数不同
（一个 rank 可以被多个端点 attach），**配得上两个名字**；而 `deployment` 与
`address_space` 在成员这个位置上是 1:1 塌缩，**不配**。

### 5.2 attachment 记录的形态

复用 #1770 已冻结的三元组，不另造词：

```text
DomainAttachment {
    node_worker_id : i32     # 落在哪个节点（沿用现有路由地址空间）
    address_space  : u32     # HOST | DEVICE —— 判定函数真正读的那一维
    role           : u32     # PROVIDER | CONSUMER（沿用 comm_endpoints::AttachmentRole）
    adapter_kind   : u32     # 沿用 comm_endpoints::AdapterKind
    adapter_profile: u32     # 沿用 comm_endpoints::AdapterProfile
}
```

> ⛔ **本节有两句被实现证伪，而实现是对的**（核验于 `upstream/main@5cb8110d`，
> 交付者 #1879）。原文保留在下面并划掉，更正紧随其后 —— 这两处不是实现跑偏，
> **是裁决当时想窄了**。
>
> #### 更正 ①：形态是 receiver-node × rank 的矩阵（M×N），不是线性的一条一条
>
> 原文（§5 表格与本节）写「每个 rank 隐含一条 PROVIDER；host 各自一条 CONSUMER」，
> 那是线性形状。实现是**按 receiver node 行、rank 列的矩阵**
> （`global_comm_domain.py`：`attachments` 与 `members` 平行，
> `validate_attachment_table(attachments, members)`，`attachments_for_node()` 按
> `rank_count` 切行）。⭐ **理由是判定的答案按 pair 变化，线性形状表达不了**：
> 实测（两 L3 节点 × 两 rank，`a2a3sim`）——
>
> | 拓扑 | 结果 |
> | ---- | ---- |
> | 两节点同 host（＝ #1879 测试用的 `127.0.0.1`） | **4/4 全解出** `OWNER_DELEGATED_COPY` / `HOST_VMM_COPY` |
> | 两节点不同 host（真跨机） | **只有对角线**（节点访问自己的 rank）解出；**非对角线 `None`/`None`** ⇒ **2/4** |
>
> ⇒ 一个 rank 的 attachment **不是它自己的属性**，而是 `(receiver node, rank)` 这条边的属性。
> **这与本目录总纲一致**（`logical-physical-separation.md`：能力挂在**边**上而非任何单方）——
> 所以本节当时写线性形状，是**违反了自己的总纲**。
>
> #### 更正 ②：不可达的 pair 记 `None` 并继续，不 raise
>
> 原文写「不支持就显式失败」。⛔ **照它做会让每个真正跨节点的域分配都失败** ——
> 判定引擎今天只有**一个**可达的 yes（同节点 host consumer ＋ `VMM_WINDOW`），
> 于是真跨机拓扑的非对角线全部落空。⇒ **该改的是裁决，不是代码。**
> ⚠️ 但「禁止从成员集反推后端」那半**仍然成立**，且与 #1770 的 `BackendResolver` 同源 ——
> 被证伪的只是**失败方式**（raise vs 记 `None`），不是**谁有权决定**。

- **DEVICE rank 的 attachment 可以不上 wire** —— 它由 `members` 完全决定
  （PROVIDER ＋ 无 adapter），显式打包只是冗余。**第一版只传 host attachment。**
- **`address_space` 用 `AddressSpace` 而非新枚举**，它已随 `BufferDescriptor` 走 wire。
- `adapter_*` 的取值由 `RegionAccessService` 判定，**不由声明方指定** ——
  与 #1770 的 `BackendResolver` 同一条纪律：~~不支持就显式失败~~
  （⛔ **见本节抬头更正 ②：实现记 `None` 并继续；照 raise 会让真跨机域分配全失败**），
  禁止从成员集反推后端。

### 5.3 `worker_path` 那半独立成立，且被低估了规模

[domain-membership.md](domain-membership.md) §10 反对裸索引的那条**与 deployment 无关**：
`(node_worker_id, local_worker_id)` 说的是「第 N 个 L3 下的第 M 个子」，树一变形含义就变。
这条**照旧成立**。

但它不是 S：`local_worker_id` 同时是**路由目标**（`Worker::_prepare_global_domain_node`
的 `control_payload` child index）、`device_ids` 的下标、`global_device_ranks` 的下标
—— **三重承重**。两条路：

 | 做法 | 代价 |
 | ---- | ---- |
 | 加一个自描述 `worker_path` **并列**保留索引 | 便宜，但两个标识并存，需明写哪个是权威 |
 | 用 path 替换索引 | 需补 path→child 解析，且 `Worker::_global_local_members` 的按节点过滤要改写 |

⇒ **这条应与 attachment 分开立项**，不要因为都源自 §10 就捆成一件。

### 5.4 wire 演进代价

✅ `global_comm_domain::GLOBAL_DOMAIN_VERSION` 在每个解码入口都是**严格相等**比较，
无协商、无向后兼容路径；所有节点由同一次 mpirun 拉起同一份 build ⇒ **不存在版本
skew，也不存在兼容悬崖**。加字段就是撞版本号。

⛔ **本目录此前的「时间窗口正在关闭」论证已于 2026-08-14 撤回** —— 依据是
「#1623/#1624 在同一协议上叠加」，而 **#1623 merged `cc9bfc45` 根本没碰
`global_comm_domain.py`**。⇒ 本文的改动**不带时限**。

## 6. 与既有文档的对表

 | 文档 | 需要改什么 |
 | ---- | ---------- |
 | [issue #1836](https://github.com/hw-native-sys/simpler/issues/1836) | ⚠️ 正文列的两条动机中，**「同一 chip 的两个 deployment 视图（`DEVICE_AICORE` vs `DEVICE_AICPU`）没有拼写方式」不成立** —— 按 §2，两者访问能力相同，不需要在成员上拼写。**另一条动机（`HOST_CPU` 参与者没有拼写方式）成立**，但落点是 attachment 而非成员字段 |
 | [w2-w5-inversion.md](w2-w5-inversion.md) §1 | 病因写作「缺的正是 `deployment` 那一维」。**症状描述正确，归因需换**：缺的是 attachment 轴；`deployment` 在成员上是塌缩的 |
 | [domain-membership.md](domain-membership.md) §10 | §10 讲的是**端点**的表达形式，正确；需补一句划清边界：域的 `members` 是 **rank 表**不是端点集，端点集由 attachment 表达 |
 | [work-breakdown.md](work-breakdown.md) | W2 的「成员选择器」应明确产出 attachment，而非给 `GlobalDomainMember` 加字段 |

## 7. 未决

⚠️ **一个岔口本文选了边，但值得复核**：host 参与表达为 **attachment**（本文选择），
还是表达为**无 rank 的 member**（`domain_rank = None`）？

- 选 attachment 的依据：hardware-constraints §5 的原话（"capability view，不是变成一个
  rank"）＋ #1770 已落地的形状。
- 选无-rank-member 的依据："参与者"在概念上更直观。代价是
  `validate_member_table` / `validate_descriptor_table` / `rank_count` /
  `_global_local_members` 四处都要学会过滤，且此后每个读 `members` 的人都得知道
  「里面有不是 rank 的东西」。

⬜ **同一 window 被同一 chip 的 AICPU 与 AICORE 分别读写时，cache maintenance 由谁决定**
—— 按 §2 这不是成员/attachment 的字段，而是 W4 的访问点属性；但 attachment 是否需要
携带一个提示给 W4，本文未定。
