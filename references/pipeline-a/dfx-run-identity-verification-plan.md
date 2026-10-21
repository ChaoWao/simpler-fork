> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# §2.4 第一份新增验证：run 身份与 buffer 所有权协议

> **历史设计 / 评审快照**：保留文中的原始基线、结论与验证记录，不代表当前待办或支持范围。当前进度统一见 [路线与阶段表](run-pipeline-track-a.md) / [HTML 看板](run-pipeline-track-a.html)。

对象：[五 collector 矩阵](dfx-continuous-collection-matrix.md) 第一步。
本文只是**方案**，不含实现，也不改任何现有测试。

**实施更新：#2293 已合入（`639d0c0b0`）。** 已交付 scope_stats / swimlane 身份基础；
最终版本增加 `InitStampsTheFirstBufferOfAShortRun`，以带旧 epoch 的池化 buffer 驱动真实
init → complete_task → flush → collect，覆盖复审提出的首 buffer 漏盖章回归。
#2313（`14918c37c`）已交付 swimlane 计数与保留复用；#2321（`5048dfff5`）已交付 dep_gen 身份与保留复用，
增加七个 producer/collector 用例及零记录 replay 输出用例。单测中的 host 归还是显式模拟，不能等同于真实异步传输验证。
[PR #2325](https://github.com/hw-native-sys/simpler/pull/2325)（`235353966`）已交付 args_dump 身份、manifest epoch 与未发布 buffer 保留；
[PR #2328](https://github.com/hw-native-sys/simpler/pull/2328)（`2193fb941`）已交付 PMU 身份、CSV epoch 与相同保留协议。
args_dump 单测尚未覆盖 end-run enqueue 失败；PMU 通过无效 thread index 验证该失败出口，未测真实队列超时。
五种身份接线完成不代表本文全部验收完成：§5 设备身份可见性探针、swimlane 累计分账仍待完成；会话生命周期和合并输出继续独立推进。

## 0. 约束先写清楚

四条硬约束决定了载体选择，不是偏好：

1. **不得靠提前释放生产 claim 制造消费滞后。** 那会把被测对象（buffer 归属）和
   准入放开混在一条变更里，而准入放开是 P4 的事。
2. **不得引入新的行为开关。** 按 [`env-macro-gating`](ARCHIVE-INDEX.md#ref-55d2cf7fda905f87)，
   新增 env/macro 行为门要先获批；本方案不需要 —— 见 §1 的载体。
3. **不碰 `test_collector_residency`。** 它的状态未知：它曾被记为 baseline 与改动树
   同样 6/8 失败，该数字**已撤回** —— 六轮是日志重定向失败、pytest 未启动，重测
   baseline 为 8/8 + 3/3 全通过。既然它不是已知的既存失败，就更不能拿它当探针：
   它只能作为集成回归，不能作为根因或修复有效性的判据。
4. **身份的 wire 位置是本方案的前置条件**，不是本方案的产物：断言要对着具体布局写。
   所以矩阵第一步的 1–3 项（epoch 前移、逐 producer 链条核对、加身份字段）先落，
   本方案与它们同批设计、同批评审。

## 1. 载体：真实 collector + 真实 AICPU 生产函数

**不用 `FakeModule`。** 用它会变成「测试代码验证测试代码自己盖的章」——
通用 `ProfilerDeviceEngine` 不负责 epoch、不负责尾 flush、也不负责 host 解析。
正确的载体是已有的**真实布局** harness：

| 已有文件 | 它已经在用的真实路径 | 本方案怎么扩 |
| --- | --- | --- |
| `tests/ut/cpp/common/test_scope_stats_collector.cpp` | 真实 `ScopeStatsCollector::init/reconcile_counters/write_jsonl`；真实布局 `get_scope_stats_header` / `get_scope_stats_buffer_state`；host malloc 充当设备内存 | **Layer 1 主载体**：单实例、无 per-core 扇出，两 epoch 场景最小 |
| `tests/ut/cpp/common/test_chip_swimlane_aicore.cpp` | 真实 `chip_swimlane_aicore_reserve_task_record` / `_commit_task_record` | **Layer 2**：AICore 半边 |
| cpput 已编入的 AICPU 生产源 | `scope_stats_collector_aicpu.cpp`、`args_dump_aicpu.cpp`（CMakeLists:128,191）、`chip_swimlane_collector_aicpu.cpp`（:1132,1141）、`dep_gen_collector_aicpu.cpp`（:943） | 设备侧动作**调真函数**，不手写字段 |

「消费滞后」在这个 harness 里就是**两个 epoch 之间不做采集/对账** —— 零机制、完全可控、不碰准入。

### 1.1 host 单测证明不了什么

**设备缓存可见性证明不了。** flush / 发布的 `wmb()` / `cache_flush_range` 顺序在 host 上
全是普通内存写，测过也不构成证据。因此下列断言**不属于** cpput，归 §5 的设备探针：

- epoch 写入与 buffer 发布之间的**可见性顺序**（原 E1）；
- AICore 侧读到的是刷出后的元数据而非缓存副本；
- 记录时刻读到的 epoch 确实是本 run 的。

cpput 能证明的是**协议与归属**：谁盖章、盖在哪、host 解析出什么、buffer 何时可复用。

## 2. Layer 1：buffer 归属协议（扩 `test_scope_stats_collector.cpp`）

每条断言标注**调用哪个生产函数**。设备侧动作全部走真函数，host 侧解析走真 collector。

### 场景 A：身份不串 + 已提交的尾 buffer 不被覆盖

```text
epoch=1: set_platform_scope_stats_base(base)        ← 真函数，重置 collector-local 静态量
         scope_stats_begin/end ×1                   ← 真函数，产 1 条（远小于 capacity）
         scope_stats_aicpu_flush_buffers()           ← 真函数，尾 flush，B0 进 ready queue
         【不 reconcile、不 write_jsonl】             ← 这就是滞后
epoch=2: 产 1 条，site/task 键与 epoch=1 刻意重复
         scope_stats_aicpu_flush_buffers()
```

| # | 断言 | 调用的生产函数 | 防的是 |
| --- | --- | --- | --- |
| A1 | B0 的记录字节与其 epoch 元数据在 epoch=2 全程不变 | 读真实布局 `get_scope_stats_buffer_state` | 后继写进已提交的尾 buffer |
| A2 | epoch=2 领到的 buffer ≠ B0；B0 未回到 free pool | 真 `pop_free`（经 `scope_stats` 的懒 pop 路径） | 池把未归还的 buffer 再发出去 |
| A3 | 两个 ready entry 都在，epoch 分别为 1 / 2 | 真 `enqueue_ready_buffer` | 身份丢失 |
| A4 | `reconcile_counters()` + `write_jsonl()` 后，按 epoch 分组各 1 条；重复键各归各 epoch | 真 `ScopeStatsCollector::reconcile_counters` / `write_jsonl` | 重复键跨 run 错配；**host 解析路径漏读身份** |
| A5 | epoch=1 的记录不含 epoch=2 写入的任何值 | 同上 | 串 run |

A4 特意走到 `write_jsonl` —— 只断言内存里的结构不算接入解析路径。

### 场景 B：归还后复用（复核补充）

```text
承场景 A → reconcile + write_jsonl 消费并复制 B0 → B0 归还 free pool
         → epoch=3 领 buffer，可以拿到 B0（复用是对的）
         → epoch=3 产 1 条
```

| # | 断言 | 防的是 |
| --- | --- | --- |
| B1 | epoch=3 允许复用 B0 —— 归还之后复用不是错误 | 把「不覆盖」错写成「永不复用」 |
| B2 | **host 侧已复制的旧副本仍保留 epoch=1 的身份**，不随 B0 被复用而改写 | 身份只存在设备 buffer 里、host 副本靠指针回指 |

B2 是这一层的要点：身份必须随记录**复制进 host 副本**，而不是靠"当前 buffer 的 epoch"推断。

### 场景 C：计数分账

直接断言产出值，不去断言被删掉的旧公式：

| # | 断言 |
| --- | --- |
| C1 | epoch=1 的记录数 == **1** |
| C2 | epoch=2 的记录数 == **1** |
| C3 | 会话累计 == **2**；且 `累计已发布 + 当前 buffer 有效数 + drop == 总产出`，**式中不含 capacity** |

### 场景 D：池耗尽 / ready queue 满

滞后消费者占住全部 slot：

- D1 producer 不得覆盖任何已提交 buffer；
- D2 丢弃计入 `dropped_record_count`（真 `account_dropped` 路径），不是静默丢；
- D3 尾 flush 失败时 C3 的恒等式仍成立（真 `scope_stats_aicpu_flush_buffers` 的失败分支
  已经会 `dropped_record_count += buf->count`，见 `scope_stats_collector_aicpu.cpp:266`）。

### 场景 E：空 run

epoch=4 一条不产：无 ready entry，前三个 epoch 的计数不变，
且 epoch=4 不得成为任何别人记录的 owner。

### 本层不覆盖：完成标记

「完成标记」放哪、队列满时为何不丢、空 run 如何发布 —— **目前没有协议，本批不做**，
归矩阵第二步（会话生命周期）。同时钉一句：
**#2267 的执行终态不是 DFX 输出完成**，不能拿它当完成标记的来源。

## 3. Layer 2：AICore producer 的身份来源（扩 `test_chip_swimlane_aicore.cpp`）

这一层单独存在，是因为 **AICore 取不到身份的常规途径**：
`chip_swimlane_aicore_record_task` 是 `__aicore__` 内联，只 include
`common/chip_swimlane_profiling.h` + aicore 头，**不链接 AICPU SO**。

所以协议必须是：**AICPU 在把 buffer 发布给 AICore 之前，把 epoch 写进 buffer 的不可变元数据。**

**现有 AICore harness 不执行 AICPU prime/rotate** —— 它手写 `head.current_buf_ptr/seq`。
本层必须把真实盖章点接进来，否则又变成验证测试自己写的字段：

| # | 断言 | 调用的生产函数 |
| --- | --- | --- |
| E2 | 记录归到**它被 reserve 时那个 buffer 的** epoch，而不是"当前 epoch" | 盖章：真 `chip_swimlane_aicpu_init`（AICore prime 段，`chip_swimlane_collector_aicpu.cpp:375-395`）；轮转：真 `chip_swimlane_aicpu_on_aicore_dispatch`（驱动 static `aicore_rotate`，`:466`）；记录：真 `chip_swimlane_aicore_reserve_task_record` / `_commit_task_record` |
| E3 | **`cached_buf_seq` 的轮转代际协议不变**：初值 `UINT32_MAX`，靠 `head->current_buf_seq != cached_buf_seq` 触发重载 | 同上 |
| E4 | host 解析把 buffer 元数据里的 epoch 带进记录副本 | 真 `ChipSwimlaneCollector::on_buffer_collected` / `copy_aicore_buffer` |

E2 已经有现成的可测形状：`ChipSwimlaneAicoreTest.CommitUsesReservedBufferGeneration`
就在证明「reserve 之后 head 轮转，commit 仍落在 reserve 时那个 buffer」。
**所以「跨轮转的记录属于旧 buffer」这条不变量已经成立**，加身份后它自然推出
「reserve 在 epoch=1、commit 发生在轮转到 epoch=2 之后的记录，属于 epoch 1」。
扩这个测试即可，但**要把 head 的来源从手写改成真 prime/rotate**。

E3 是防御性的：它把「seq 是轮转代际、不是记录计数、也不是身份」固定下来。

**原 E1（可见性顺序）移出本层** —— host 单测证明不了设备缓存可见性，见 §1.1 与 §5。

## 4. Layer 3：设备上确认身份确实上了 wire（后做，最小）

**只有 Layer 1/2 过了才做。** 一个 scene test：

- 同配置、**顺序**两个 run（准入不变、逐 run 屏障不变）；
- task_id 刻意重复；两个 run 都只产未满尾 buffer；
- 断言每个 run 的 artifact 只含自己的记录，且两个 producer（AICPU / AICore）的身份都对。

**它验证不了消费滞后** —— 那是 Layer 1 的职责。这一层只回答"身份是否真的走完了
`epoch 来源 → buffer 元数据 → ready 发布 → host 复制 → 归还`这条链"。

## 5. 设备探针：host 单测证明不了的三条（一次性，不进仓）

跑在 a2a3、单 case、不进仓。这三条是 §1.1 列出的、cpput 结构上无法覆盖的部分：

1. **记录时刻读到的 epoch 是本 run 的** —— 打印 `get_platform_run_result_epoch()` 与
   host 侧该 run 的 `run_epoch` 对照。
2. **AICore 读到的是刷出后的 buffer 元数据**，不是缓存里的旧值。
3. **epoch 写入先于 buffer 发布可见** —— 原 E1。

第 1 条同时回答一个已核对但未实测的结论：五个 base 在 `kernel.cpp:76-87` 发布、
epoch 在 `:129` 发布，而所有盖章点都在 `aicpu_execute`（`:132`）之内，**所以顺序本来就是对的**
（见矩阵 §1.3 的修订）。探针是去确认这条推断，不是去修它。

## 6. 这份方案刻意不做的事

- 不提前释放 execution claim，不改任何准入。
- 不新增 env/macro 行为门（Layer 1/2 扩已有单测，Layer 3 是普通 scene test）。
- 不动 residency，也不把任何现有断言延后到会话关闭。
- **不做完成标记**：协议未定，归第二步；且 #2267 的执行终态不是 DFX 输出完成。
- 不实现会话状态机 —— 本份只覆盖身份与 buffer 所有权。
