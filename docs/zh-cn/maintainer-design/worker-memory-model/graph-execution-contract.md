# 内存模型对 Graph Execution 层的要求

> **发布快照：2026-10-18。** 设计、实现状态与历史调查须按正文各自基线阅读；不代表当前 main 全量验收。参见[阅读入口与已知更正](../README.md)。
>
> **这份文档是什么。** [`../worker-memory-model.md`](../worker-memory-model.md)
> 把依赖正确性的三件套 **#1375 / #1376 / #1388** 路由给了 Graph Execution
> （权威设计：`../runtime-v2-hbg/graph-execution.md`（本地参考：`../runtime-v2-hbg/graph-execution.md`））。
> **路由不是交付** —— 路由完还欠一份「内存模型对那一层要求什么」。本文补上。
>
> 读者是 GE 的实现者与这三个 issue 的实现者。**它只提要求与依据，不设计实现。**
>
> ⚠️ **链接订正（2026-08-04，2026-08-05 补记）**：本文原先两处指向
> `github.com/…/blob/**main**/src/a2a3/runtime/host_build_graph/docs/GRAPH_EXECUTION.md`
> —— 写下时**那个 URL 是 404**（该文件当时只在 PR #1444 分支上）。
> 权威现在本仓 `../runtime-v2-hbg/graph-execution.md`（本地参考：`../runtime-v2-hbg/graph-execution.md`），两处均已改指。
> ⚠️ **2026-08-05：#1444 已 merged `0659745d`，那个 path 现在真在 main 上了** ——
> 即当初的 404 判断已过期。但**改指的决定不因此翻转**：用户裁决「仓内那份不会再更新了」，
> 权威留在 `.docs/`。
> 教训**仍然成立且更普适**：外部 URL 不在链接检查器覆盖范围内，且**「404 / 不存在」是会过期的判断**
> —— 与 [`README.md`](../README.md) §4 规则 4b 的「现状符号会过期」同一类。

## 0. 三个 issue 与它们的内部顺序

 | Issue | 做什么 | 性质 |
 | ----- | ------ | ---- |
 | **#1388** | TensorMap 从「只记 writer」扩成 **outstanding-access tracker**，自动导出 **WAR** 边 | **能力增强** —— #1306 的仓库裁决为显式表达 WAR 的性能取舍；#1388 请求自动推导，不能把该取舍列为现役正确性缺陷 |
 | **#1375** | 把一条物理依赖边上耦合的两个语义拆开：**WAIT**（就绪）/ **RETAIN**（生命周期） | **表示能力** —— 传递约减的前置 |
 | **#1376** | 有界可达位图（`BL=64`）做 WAIT 边的传递约减 | **优化** —— issue 自述 **blocked by #1375** |

顺序是 **#1375 → #1376**；**#1388 与它们正交**（改的是 TensorMap 的记录内容，
不是边的语义），但落在同一批文件上。

**为什么这三件事归 GE**：它们要回答的核心问题 ——
*「一个 producer 完成之后，它的输出还要活多久？」* —— 正是 GE
不变量 5（本地参考：`../runtime-v2-hbg/graph-execution.md`）
已经在**节点集**那一层做过的切分：

 | GE 的计数器 | 回答的问题 |
 | ----------- | ---------- |
 | `remaining_nodes` | **语义完成** —— 整张图什么时候算做完 |
 | `retired_nodes` | **资源回收** —— 那块内存什么时候可以复用 |

> *"`remaining_nodes` and `retired_nodes` are not interchangeable — semantic
> completion and resource reclamation happen at different times."*

### #1375 在「边」这一层做的，就是 GE 在「节点集」那一层已经做过的同一个切分

这是本文所有要求的共同根。

## 1. 接缝在哪：GE 自己把 boundary 声明为 caller-owned

GE 的内存模型表把四类内存分得很清楚，其中两类的 lifetime 写的是 **caller-owned**：

 | GE 的内存类别 | GE 给的 lifetime |
 | ------------- | ---------------- |
 | **Boundary tensors**（图 I/O） | **caller-owned** |
 | Internal intermediates（`TensorCreateInfo` 产出） | 绑在 outer task 上，全节点完成后才回收 |
 | `OUTPUT_EXISTING` 写入的目标 | **caller-owned** |
 | Internal nodes 的 TaskArgs 存储 | 绑在 execution block 上 |

⇒ **GE 把 boundary 的生命周期整个委托出去了，而它委托给的那个「caller」，
就是内存模型。** 这就是接缝。以下六条要求全部落在这条接缝上，或落在
\#1375 拆分之后 GE 内部必须相应变化的地方。

## 2. 六条要求

### R1 —— "caller-owned" 必须被收窄为**两种生命周期之一**

**要求.** GE 文档里的 "caller-owned" 目前是一个开放词。在两生命周期模型下
（见 [`../cpu-npu-shared-memory/work-breakdown.md`](../cpu-npu-shared-memory/work-breakdown.md)
W3）simpler 管理的 owner 只能属于二者之一：**per-orch**（run 边界结合所有相关使用证明退休）或

#### per-worker**（跨 run 存活，`Worker.close()` 发起关闭并按使用证明回收）。**没有第三种 simpler owner 策略

**适用边界.** 决策 A（2026-08-03）的两种生命周期约束 simpler 管理资源的 owner 回收策略；外部借用保留原 allocation owner。graph/replay、DMA/copyback 与 attachment 的较长使用期由显式使用引用表达，不因此取得物理分配所有权。per-orch 资源不能只因被 capture 就合法跨 run 存活；不具备足够 owner 生命周期的绑定须拒绝，或显式选用已有的 per-worker 资源。

共同的关闭与退休要求见 [访问与退休契约 §3](access-retirement-contract.md#3-部分提交关闭与失败清理)。

**不满足会怎样.** GE 今天不回收 boundary，所以**现状是安全的** —— 但这份安全
**是偶然的**：它安全是因为 GE 恰好什么都不做，而不是因为它检查过所有者是谁。
一旦 boundary 出现 per-worker 的情形（持久通信窗口就是），
「caller-owned」这句话不再足以判断任何事。

#### 要写成不变量的形式**：**GE 不得把 boundary buffer 纳入任何回收判据

这条今天成立，但只作为「caller 拥有」的隐含推论存在，没有被写下来。

### R2 —— RETAIN **不是完整的生命周期故事**：attached Buffer 被图看不见的东西持有

**要求.** 回收判据不得是 `retired_nodes` 单独成立，而必须是
**`retired_nodes 归零 ∧ 所有相关的外部使用引用归零`** 的合取；外部使用包括 attachment/grant、未完成的 DMA/copyback 与 graph/replay 保留。不是把这些引用强行塞进同一计数器，而是 owner 的回收判据必须观察它们。一次 run 的语义完成不代表全部使用退休。

**依据.** 内存模型五概念中有三个（`CanonicalBufferIdentity` / `Attachment` /
**物化形态**——Chip 上即 `ChipTensor`）**存在的唯一理由就是跨进程**。一条 task 的 RETAIN 边只在
**一个 orchestrator 的图内**成立；而 attach 进 CommDomain 的 Buffer 会被
**另一个进程的 task** 读。**RETAIN 边在原理上就无法表达这件事** ——
它看不见对端。

**不满足会怎样.** 按 `retired_nodes` 单独回收，会把仍被远端读的 Buffer 释放掉。

#### 强度（决策 C 之后）.**2026-08-03 拍板**Attachment 由域代管

（[`../worker-memory-model.md`](../worker-memory-model.md) §5.7）：
`attach_buffer` / `detach_buffer` 不进 public surface。
⇒ **attachment 引用的所有者唯一 —— 有域时是域，worker 树上是 Worker，两种情况下
都不是用户** ⇒ 用户无法手工减掉一个正被对端读的引用
⇒ **本要求是一条真不变量，不是「用户别做傻事」的约定。** 决策 C 的依据 ② 就是这条。

**现状判定.** GE 的 internal intermediates 是**图私有的**（一次 `alloc` 的整块），
不会被 attach；boundary 又是 caller-owned、GE 不回收。⇒ **今天踩不到。**
但这同样是 R1 那种偶然的安全：**GE roadmap 上任何一次「boundary 早回收」
的优化都会踩中它。** 所以要求写成前置条件：**若 GE 将来要参与 boundary
的回收判定，必须先能观察 attachment 引用；在此之前不得做该优化。**

### R3 ⭐ —— alias / overlap 的单位必须是 `CanonicalBufferIdentity + 字节区间`，**不是地址**

**要求.** 凡是回答「这两个 tensor 是否别名 / 是否重叠」的判断，单位必须是
`(CanonicalBufferIdentity, offset, length)`。**本地地址只在同进程内是它的
合法代理。**

**依据.** 这是两个地方的**同一个判断**，今天各用各的单位：

 | 出处 | 今天用什么 |
 | ---- | ---------- |
 | GE 的 **alias partition**（boundary contract） | **地址** —— *"An internal tensor is matched to its boundary slot **by address**; when two slots alias, first-match resolves it by lowest index"* |
 | **#1388** 的 WAR 推导 | "overlapping tensor **regions**"（单位未定义） |

而内存模型的**物化形态**（Chip 上即 `ChipTensor`）说的正是：**同一块物理内存在
不同进程有不同的本地地址，但只有一个 `CanonicalBufferIdentity`。**

**不满足会怎样.** 用地址判重叠，在跨进程（attached buffer）下会把**同一个
Buffer 的两个视图看成两块不相交的内存** ⇒ **漏掉 WAR 边** ⇒ 静默算错。
这正是 #1306 的失效模式，只是跨进程版本 —— 而 #1388 的全部意义就是消灭
\#1306 那一类。**用地址实现 #1388，等于在跨进程场景下把它刚修好的洞重新打开。**

> ⚠️ 这条同时是对 GE 的要求：GE 的 alias partition 与 #1388 的 overlap
> **必须共用同一个单位**，否则同一份别名事实会有两个互相不认的答案。

### R4 —— 对 per-worker Buffer 的**写**必须是一个 node，不能是 submit 前的 host 动作

**要求.** 任何对 per-worker（跨 run 存活）Buffer 的写入 —— 包括**复用前清零**、
包括 `set_tensor_data()` —— 必须表达为图里的一个 node。

**依据.** 跨图定序**只对 node 免费**：设备执行严格串行（`S(N)` happens-before
`S(N+1)`），所以作为 node 的写天然排在上一张图之后。**host 侧动作没有这个保证** ——
而 GE 的 boundary 恰恰是「caller 在 submit **之前**准备好的」，即 host 侧。

**不满足会怎样.** 见
[`../cpu-npu-shared-memory/work-breakdown.md`](../cpu-npu-shared-memory/work-breakdown.md)
判断六：#1587（`2a650f2d`）合入后 `P(N+1) ‖ S(N)` 已是 main 上的现实，
host 侧的清零会与前一张图的执行**数据竞争**。

**与 #1388 的关系.** #1388 的验收里已有一条
*"`set_tensor_data()` waits for a pure `INPUT` reader of the target region"* ——
说明 host 写**已经**被纳入 WAR 视野。**本要求是把这条从「同图内」扩到
「跨图，且对象是 per-worker Buffer」。**

### R5 ⭐ —— #1375 的 flags 必须能进 GE 的 **CSR**

**要求.** WAIT / RETAIN 的 per-edge 标志必须能在 GE 的 CSR fanout 表上表达。

**依据.** GE 的图节点完成路径**同时**做了两件 #1375 要拆开的事：

```cpp
uint32_t begin = topo.fanout_offsets[node_index];
uint32_t end   = topo.fanout_offsets[node_index + 1];
for (uint32_t e = begin; e < end; ++e) {
    release_consumer(exec->nodes[topo.fanout_indices[e]].slot);   // 既释放 readiness，又推进 retire
}
```

`fanout_indices[]` 是一个裸的索引数组，**没有声明任何放 flag 的位置**。

> ⚠️ **订正（2026-08-04 首记；2026-08-05 按合入版 `0659745d` 的头文件复核，结论不变）**：
> 本条此前写作「裸的 **`uint32`** 索引数组」，**不准确** —— `off_fanout_indices` 是
> `uint32_t`，但那是**该数组在 Definition blob 里的偏移**；
> **元素类型是 `uint16_t`**（`graph_definition_array<uint16_t>(definition,
> definition.off_fanout_indices, definition.edge_count)`，消费侧
> `const uint16_t *fanout_indices`）。
>
> 这让本条的结论**更紧，不是更松**：位宽只有 16。但同时也给出了一个此前没有的事实 ——
> `GRAPH_MAX_NODES = 1024` ⇒ 索引只需 **10 位**，**高 6 位在数值上是空的**。
> ⇒ 下面「取高位做 flag」那条不再是空想，但它会把
> **`GRAPH_MAX_NODES ≤ 1024` 从一个容量常量变成一条 ABI 不变量** ——
> 今天没有任何东西声明这一点，校验器也只查 `< task_count`。
> **要走这条路必须先把该约束显式化并加校验**，否则是在隐式假设上叠 flag。
⇒ **GE 内部是这条耦合的第二份实现** —— #1375 若只改 tmr 的 fanin/fanout 表示，
GE 这条路径会保持旧语义，两条路径对同一张图给出不同的生命周期。

**不满足会怎样.** #1375 的验收 7（*"a2a3 和 a5 tensormap_and_ringbuffer 保持语义
对齐"*）**没有覆盖 GE** —— 而 GE 是第三份实现。这是一个**验收范围的缺口**，
不是实现细节。

**可选形状**（不在此裁决）：`fanout_indices` 取高 6 位做 flag（**前提见上方订正框**：
须先把 `GRAPH_MAX_NODES ≤ 1024` 显式化为 ABI 不变量并加校验）；或加一条平行的
flag 数组；或让 RETAIN-only 边根本不进 CSR（因为 #1375 明说
*"A RETAIN-only edge must not create a readiness fanout node"* —— CSR **就是**
readiness fanout，所以这个方向可能最自然）。

### R6 ⭐ —— GE 的图是**静态**的，`BL` 窗口不适用：约减应在**记录期**做完整传递约减

**要求.** #1376 必须显式声明 graph node **在不在** `submit_seq` 窗口内。
建议的答案：**在窗口外，且不需要位图**。

**依据.** 两条硬事实：

1. **GE 的 internal node 没有 `PTO2TaskId`** —— 它们不占 window slot、不上传，
   在设备上 materialize。而 #1376 的整套机制建立在
   *"Assign every dependency-graph task a monotonically increasing `submit_seq`"*
   之上。GE 的 node 没有这个东西，硬造要在记录期分配。
2. **GE 的拓扑在 host 记录期就已完整、静态、C++/STL** —— CSR
   (`fanout_offsets` / `fanout_indices`) 是**录制时算好烘进 definition 的**。

⇒ **#1376 用有界位图，是因为它面对的是动态提交流、且在 AICPU 热路径上**
（issue 自述 *"A full transitive closure is too expensive for the AICPU hot path"*）。
**这两条前提对 GE 都不成立** —— GE 有完整 DAG，在 host 上，一次性。

#### ⇒ GE 可以做精确的完整传递约减，根本不需要 `BL` 的近似，也不承担窗口丢失

**这是一个白拿**，而且方向相反：不是「GE 要跟上 #1376」，而是
**GE 上能拿到 #1376 拿不到的完整约减**。#1376 的 `BL=64/128/256` 测量计划
**不应把 GE 的图算进去** —— 混进去会低估 BL 的收益（GE 部分本可 100%）。

## 3. 反向：这三个 issue 对内存模型提出的要求

只有一条，但是硬的。

**#1375 的验收 2** 要求：*"`A`'s output remains alive until `C` releases it,
**including after scope end**"* —— 即一个 RETAIN 关系可以**跨越 scope 结束**。

⇒ 问题：**这构成第三种生命周期吗？** 内存模型只认 per-orch 与 per-worker。

### 裁决：不构成 —— 它是 per-orch 内部的一个粒度，不是新的用户可见生命周期

scope 是 run 内部的结构，scope end 不是 run end；RETAIN 跨 scope 但仍在
**同一个 run 的 fence 之内**被回收。⇒ 它落在 per-orch 里。

### 要求：#1375 实现时不得让这个内部粒度泄漏成用户可见的第三种寿命

具体地：不得出现「显式延长某个 Buffer 存活到 run 之外」的 API —— 那是
per-worker 的职责（W3），走 worker 级入口，不走依赖边。

## 4. 未决

1. **R5 的具体形状**（CSR 加 flag / 平行数组 / RETAIN-only 不进 CSR）——
   需要 GE 实现者判断，本文只确立「必须能表达」。
2. **R3 的代价** —— 把 alias/overlap 的单位从地址换成
   `CanonicalBufferIdentity + 区间`，在**同进程**热路径上是否引入额外间接。
   同进程下地址是合法代理，所以大概率可以保留地址作为快路径 +
   canonical identity 作为判据来源；但没有测量。
3. **#1388 的 access-entry 计数与持久 Buffer** —— #1388 要求
   *"Replace output-only entry counting with access-entry counting"* 并重估
   pool/bucket 尺寸。**长期存活的 per-worker Buffer 会长期挂着 reader/writer
   entry**，这对 entry-pool 高水位的影响未评估。

---

**上游**：[`../worker-memory-model.md`](../worker-memory-model.md) ·
[`../README.md`](../README.md) §1.4
