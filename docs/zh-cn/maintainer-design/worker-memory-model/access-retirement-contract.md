# 访问、完成与资源退休契约

> **发布快照：2026-10-19。** 设计、实现状态与历史调查须按正文各自基线阅读；不代表当前 main 全量验收。参见[阅读入口与已知更正](../README.md)。

本文是 [分线契约 C/D](../track-boundaries.md) 的细化，规定各 consumer 的目标验收；**不是已实现声明**。实施状态和唯一归属见 [work-items](../work-items.md) §6。来源快照与取舍见 [跨方案评估](../review/2026-09-28-cross-plan-lessons.md)。

公共入口见 [用户面契约](user-surface.md)，图边界见 [GE-R1/R2](graph-execution-contract.md)。本契约不增加实施线、runner、公共 completion 类型或新的 wire 字段。

## 1. 身份、授权与接纳

源登记描述 backing 的 canonical identity/generation、位置、有效范围与授权；目标 attachment 描述某 endpoint/context generation 下的可访问地址、机制、范围与使用保留。相同 backing 被不同 Worker 使用时 identity 不变。源地址不是跨进程身份，device ordinal 也不能替代 context generation。

接纳顺序：

```text
快照调用描述 → 全调用/全路径预检 → 受保护的绑定点重核 live/grant/generation
  → 原子取得使用保留并登记失败清理义务 → 发布异步工作
```

预检覆盖所有参数及每段传输的结构、方向、授权、范围、几何与实际 backend 能力；在 host 内容复制、DMA/send 或 kernel launch 前拒绝不支持的组合。首段搬运前保留后续必需的目的和中间存储；资源不足按外层既有 API 的阻塞或拒绝合同处理。

描述符快照不等于数据已经就绪。可以提前验证、选择 adapter、预留资源；读取或快照 tensor 内容必须有 producer 已完成的证明。绑定点的重核与逻辑关闭协调，不能用先前预检或缓存 attachment 代替当前 live 检查。

实际权限是本次操作所需权限与既有 grant 的约束关系，不能由 INPUT/OUTPUT 标签扩大。构造 view 不分配、不复制、不创建新 backing；显式 COPY 写入已有 destination 保持其 identity，新的 allocation 才产生新 identity。

## 2. 完成事实与消费边

 | 事实 | 证明内容 | 允许的动作 |
 | ---- | -------- | ---------- |
 | `SourceReadDone` | 这一段不再读取源 | 解除这一段的源使用引用；其他使用仍须保留 |
 | `DestinationReady` | 目的完整且对实际消费者可见 | 放行依赖这份数据的消费；不是 consumer 已完成 |
 | `OperationQuiescent` | 所有已发起步骤均不再访问相关资源 | 允许失败清理或退休对应使用；不是数据成功 |

run 业务结果、执行域是否可继续接纳、资源是否已静止分别记录。异常、取消请求、超时、send 返回、event 被触发，都不能单独推导上述所有事实。逻辑事实可由同一后端证明同时满足，不要求三个物理 event。

证明限定本次 operation 与 endpoint/session/context generation；重复 copy 不递增 backing generation，旧 operation 的完成不能放行新 operation。

### 多段复制

```text
H_A → NetworkCopy → H_B → H2D → D_B → device consumer
```

- H2D 读取 H_B 前必须证明网络接收目的已就绪；send 返回只按后端合同证明源读取是否结束。
- H_B 保留到网络写与 H2D 读取均已结束；D_B 的保留继续覆盖 device consumer 和必要的输出回拷。
- 提前 enqueue 后继须同时证明顺序、可见性和异步失败阻断。单纯排入 event wait 不证明数据成功；只有 host completion 的后端在既有 host 调度点确认后再提交后继。
- 每段 adapter 提供自己能证明的事实；上层不能用一次 run 完成替代未完成复制，也不能用复制结束替代 kernel 结束。

网络路径和每段 H2D 各自验布局；合法 strided view 不自动意味整条 COPY 路径支持相同 stride。不得一段按完整 span 传输、另一段按 numel 分配而继续携带旧 stride。

## 3. 部分提交、关闭与失败清理

执行事实至少区分未提交、部分提交、已证明完成、失败且已静止、失败但静止未知。每个 stream 分别记录已提交 wait、已 launch 工作与已 record fence；本 run 尚未 launch 不意味着没有在飞命令。

清理义务先登记后发布；资源取得阶段可回滚登记，已发出的写入、DMA/send、wait 和 kernel 不能回滚成“从未发生”。一段失败不得放行依赖不完整目的的消费；预先排队的后继必须有已证明的失败阻断机制。取消只停止尚未提交的步骤。

没有静止证明时保留使用引用、平台句柄、费用与 cleanup journal；不得复用相关块，也不得对部分写入自动重试，除非已证明重试语义与幂等性。释放失败不抹账，实际 unmap/free 结果决定后续重试动作。

逻辑关闭拒绝通过被关闭登记接纳新的使用。已接受的使用保留到各自最后消费者结束。有效独立 import/map grant 按自己的授权合同处理；关闭一个 handle 不得误撤销另一有效 grant。源关闭与独立 grant 的关系必须在 backend 合同中显式说明，不能通过删除共享地址缓存隐式决定。

allocation owner 与 use/grant 持有者分开。外部借用不转移物理 allocation 所有权，内部保留不能自动延长调用方分配的寿命；释放借用描述符不是 device fence。graph/replay 的使用引用必须显式保留，但不能据此夺取 allocation ownership。

**契约 D 的约束**：Buffer/domain 共享逻辑关闭、使用接纳、最后使用证明和可重试清理规则；清理义务可组合且恰好履行一次。一个或多个物理 registry 的组织仍见 [契约 D](../track-boundaries.md)，表的数量不能代替上述证明。

## 4. 预算、流水容量与 capture

RA-16 按实际 owner、预算域、写入时刻和最后使用者管理内部 workspace。评审至少覆盖以下资源：

 | 资源 | 最后使用者要求 |
 | ---- | -------------- |
 | 调用描述/值快照、frame 数据 | 覆盖最后读取该字段的准备或设备步骤；不能与同 frame 的状态字段混为一个寿命 |
 | upload 源与 staging | 保留到 DMA 不再读取；host API 返回不自动授予覆盖权 |
 | 执行 scratch、控制/结果记录 | 按 kernel、copyback、finalizer 或 collector 的实际最后读取退休 |
 | 旧版本、graph 保留块 | 覆盖图存活与在飞 replay；一次 eager stream 完成不释放图引用 |
 | release-unconfirmed/quarantined 块 | 持续计费并拒绝复用，直到取得所需的释放/静止证明 |

实际占用包括活跃块、必要旧版本、保留块和对齐开销。HOST/pinned、DEVICE、代码/配置常量及 provider 自有资源按各自合同核算；不能漏账、重复计算共享 backing，或通过统一预算强迫所有资源进入同一物理池。

RA-7 的 hierarchical HeapRing、RA-15 的用户 Buffer 与 RA-16 的内部 workspace 保持所有权边界。容量足够、scope 结束或 run 业务失败都不能单独授予复用权。

EX-7 分别声明 accepted 请求、host prepare、native prepared、已 enqueue 和实际执行的容量及资源消耗。扩大 admission 必须满足账本与最后使用者约束；特定后端的 bank/event 数量不是全局深度定义。

Capture 要求被捕获的操作及等待边能由 device replay 完成。普通 host RPC、接收回调和 Python 调度不因被包装成 COPY 就可捕获；缺少合法 bridge 时在图外完成并证明输入就绪，或拒绝该 capture 路径。该要求不把 kernel 实现纳入三条线。

## 5. 随 consumer 交付的验收

每条访问路径交付时必须同时证明正常回收与失败保留，不能等 U4 再补安全。验收使用真实 producer/consumer 入口；fake 复用同一协议合同，但不得直接手填期望的产物字段冒充生产链路。

 | 归属/交接 | 必测反例 | 配对阳性对照 |
 | --------- | -------- | ------------ |
 | RA-3/RA-13；DS-3 | 后一个参数无效，前一个参数不得先复制；预检后源关闭；源 identity 被目标接入重新创建 | 合法整组绑定，授权范围内 submit，跨 Worker 保持 identity |
 | DS-3 → RA-15/RA-16 | send 返回但 receiver 未 commit；H2D 仍读中间块；第二段失败；旧完成记录晚到 | 对应 ready 后消费、最后读取后回收；同 backing 连续两次 copy 正确 |
 | EX-7 → RA-16 | wait 已提交而 launch 失败；kernel 已提交而 fence record 失败；取消后仍有 DMA | 正常 fence/drain 证明后恰好释放相关使用，资源能重新接纳 |
 | RA-13 → RA-15 | 同调用混合 owned/external，完成未知；逻辑关闭与 submit 竞争 | all-owned 完成可释放；外部描述符撤销不误 free；正常借用执行可退休 |
 | RA-15/RA-12；DS-4 | 独立 grant 仍有效、graph/replay 仍引用、unmap/free 失败时提前清账 | 最后使用者结束且平台释放成功后清账；关闭一个 grant 不伤及另一个 |
 | RA-16；EX-7 联验 | 第一轮产物未消费时第二轮覆盖、身份串轮 | 第一轮消费后第三轮可复用，旧 host 副本仍保留原身份和内容 |

CPU fake 证明协议、引用与拒绝路径；device cache visibility 必须有相应硬件证据。模拟器的外部地址与 onboard native allocator 登记内地址分别标明，不能互相代替证明。DFX 若承诺 run 返回时产物完整，异步收集必须保持该合同或明确交付新的 flush 边界；DFX 实现仍按 work-items §7 归属。
