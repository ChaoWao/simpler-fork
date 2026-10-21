> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# 内存归一化：从一开始覆盖 eager 与 capture

状态：用户要求完善整体设计；这是设计方向和原作者任务边界，不是新实现/发布批准。替代旧“先做不支持 capture 的 retained_temp 独立预算”待批路线。

## 为什么其他框架也需要处理

捕获任务保存了之后要再次使用的地址。host 对象离开作用域、一次运行结束、stream 同步，都不等于该图不会再用这些地址。PyTorch 使用图专用内存池，torch_npu 的 NPUCachingAllocator 也有 PrivatePool、图引用计数和捕获时的分配路由。

来源：
- [PyTorch graph memory management](https://docs.pytorch.org/docs/2.14/notes/cuda.html#graph-memory-management)
- [torch_npu NPUCachingAllocator](https://github.com/Ascend/pytorch/blob/dcff953b9394c9685b4af429f11a911a9d0aa147/torch_npu/csrc/core/npu/NPUCachingAllocator.cpp)
- 进一步的版本、来源和限制见 [框架证据](capture-memory-framework-evidence.md)。不能用 CUDA 特性推导 CANN 支持；也不能声称 simpler 自行 rtMalloc 的块会自动受框架图池保护。

## 统一设计方向

### 1. 同一管理接口，一个总预算

一个实际串行执行域的 Workspace 管理器持有自有 device 工作内存的总额度和稳定 backing blocks。program/kernel、eager/capture 共同使用；差别由资源的最后使用者表达，不各造一套 allocator。

预算按物理块计一次，包含空闲、对齐、保留和碎片；另报告 live、graph 引用和待消费者完成的子区，不能把这些重叠统计相加后双重计费。host RAM、注册代码/常量、RTS/provider 管理的内存分别核算，不能遗漏或声称已控制外部占用。

### 2. 增长追加新块，不搬迁已发布的地址

图外 prepare/reserve 在额度内追加块或使用已有空闲子区；只有准备全部成功，才发布新计划。老图仍引用 A，新调用需要更大空间时使用 B，不释放或改写 A 来冒充扩容成功。失败保留旧计划和地址。

按 simpler 已有 G6，capture 和 launch/replay 不分配或扩容底层块；所需容量在 capture 前准备。捕获中发现容量不足，明确结束失败捕获，在图外准备新容量再重新捕获，旧有效图不受影响。这是我们的设计选择，不是对所有框架/CUDA 分配能力的断言。

### 3. 图引用与运行引用同时参与释放

每个准备结果提供明确的资源引用；捕获后由图拥有者保留其地址、代码和必要静态内容。图克隆/更新、捕获失败、销毁及在途 replay 都必须有具体引用交接。最终回收须同时满足：已无可 replay 的图引用、在途执行结束、所需回拷/输出/collector 消费结束。

接口候选：准备结果拥有 `WorkspaceLease`，框架的 graph wrapper 保留 `GraphResourceLease`；名称和签名待原作者按真实入口确定。若当前接口拿不到图销毁通知，必须补显式的拥有者交接，不能从同步或 Worker.close 猜测图已死亡，也不能永久不释放。graph 仍可用时关闭 Worker 的拒绝/延迟资源释放/显式先关图行为需形成可审批合同，不能在设计中偷偷选择。

### 4. 地址稳定与内容复用分别证明

| 内容 | 可以复用的条件 |
| --- | --- |
| captured task 的静态参数/image/代码地址 | 图仍可 replay 时保留；只有显式更新协议才可改变其含义 |
| 执行 scratch | 地址保持有效；已证明串行与完整重初始化时可共享内容空间，不按 run 机械复制 |
| host 提前写入的 staging/image | 前驱仍读取时要不同子区，或将覆盖安排在有序设备任务内 |
| 输出/回拷/DFX | 以各自最后消费者为准，不能只看 kernel 结束 |
| 外部 tensor | 由外部框架持有，simpler借用；保留地址不等于替用户保存每次 replay 的历史值 |

例如图 G1 的静态 image 与 G2 不同，不能因为设备不并发就让 host 提前覆盖同一地址。两图的纯 scratch 则可以在执行串行、每次初始化及输出不别名的证明下共享。要求保留旧 replay 输出时应提供独立结果存储/拷贝合同，不能误认同一个静态输出地址能保存多个版本。

多图共享池不是任意共享地址。必须说明图执行顺序、未完成输出、跨流依赖和子区活跃区间；无法证明则使用不同子区，但仍纳入同一总预算，而不是另开无上限池。

## 原作者本次交付

1. 当前主线实际 prepare/capture/replay/destroy/close 的调用点与模式矩阵。
2. 图 owner、在途 replay、输出/collector 最后引用的完整时序与失败路径。
3. 统一 Module 的小 Interface、容量/错误/关闭合同，框架接入需增加什么。
4. 一个真实使用方的首步迁移范围，从设计上可直接用于 capture；不再提出长期独立的 retained_temp 专用预算。
5. 静态验收矩阵及明确的 SDK 未验证项，不执行测试。

总体原则来源：[运行时 G1–G6](../runtime-design-principles.md)、[workspace 分类](../workspace-memory-principles.md)。本设计不恢复已交接 P4 开发，也不改变当前已授权 DFX/上传实现。
