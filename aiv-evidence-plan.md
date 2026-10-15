# AIV 故障后续规划

当前状态补记（2026-10-12）：#2536 已合入 main `86e77362`。后续产品回归直接消费主线 DFX；原 overlay 方式只保留为历史验证来源。AIV 未定因，诊断合入不代表故障修复。


更新：2026-10-12。状态：记录后回到 kernel eager 主线；不继续无边界扩大复现矩阵。

## 目标与已有证据

目标是让下一次故障留下可关联、可判别、尽可能可重放的现场，不把日志增加等同于根因已确定。

- #2536 原审阅 head `280ee528`，验证基线 main `e0a5e9a16`；已合入 `86e77362`。公共 host 记录覆盖 TMR/HBG；新增设备 dispatch 明细只覆盖 HBG shutdown。历史验证数据保留原基线。
- 上传记录给出 child 地址范围、长度与代码指纹；run failure 给出执行身份；HBG shutdown 给出 running/pending token、bank、函数入口和物理 core。
- 旧 Network CI 的原始 plog 已确认 TMR VEC UB 越界。故障 PC 恰好等于同次上传的 244 字节 child 的 end-exclusive 地址。此前 HBG 有相同地址特征，但不能据此认定共同根因、指令 fall-through 或缓存失效。
- 同轮一般 onboard/DeepSeek 的 device 8 失败位于 AICPU bootstrap；只有 507018，缺少对应设备日志，仍未定因，不能并入 AIV。
- 现场与限制：[旧 CI 核查](reviews/callable-fault-dfx/main-e0a5e9a/ci-triage.md)、[本轮验证](reviews/callable-fault-dfx/main-e0a5e9a/REPORT.md)。

## 当前能证明什么、缺少什么

| 已有信息 | 能证明 | 不能证明 |
| --- | --- | --- |
| 成功 H2D 的 host image、代码范围/指纹 | 本进程准备上传的代码及目标地址 | AICore 实际取指字节，上传后是否被覆盖 |
| 注册/释放及 run 身份 | handle、image、失败完成检查的关联 | 该 run 一定是首个故障源，所有外部使用者都结束 |
| HBG AICPU dispatch 快照 | 采样时发布的 running/pending 与 bank | AICore 实际接受值；跨线程原子快照 |
| CANN PC、异常寄存器与错误文本 | 设备报告的异常类别和位置 | PC 的报告语义等于当前正在执行指令，或根因直接来自 kernel 算术 |
| 本地占用采样 | 采样时可见的任务/PID/设备关系 | 采样间隔内没有外来占用；远端未采样设备也独占 |

## A1：先建立完整现场归档（下一项诊断工程工作）

范围是测试/CI 运行包装和制品保存；独立于 kernel 产品代码。#2536 提供关联记录，归档器保存证据；不把每个产品 PR 携带的本地 DFX overlay 当长期机制。

每次测试开始前：

1. 为一次运行创建独立目录与 manifest，记录 source/head/base、dirty patch、已加载包路径、编译配置、PTO-ISA/CANN/driver 版本、精确命令、环境白名单、随机种子及测试顺序。
2. 保存实际使用的 host/AICPU/AICore 二进制、child 代码和可用的符号/反汇编，并记录 hash。只保存 Git SHA 不足以证明进程加载了该版本。不要为了归档重编译替代原文件。
3. 保存调用参数的 shape/dtype/stride/offset、身份/分配范围、控制 scalar 和必要输入/生成方式。输入字节若不完整，标明“不可精确重放”，不能拿 seed 代替非确定性输入。
4. 从注册开始保存完整 host 日志，独立设置 ASCEND_PROCESS_LOG_PATH。硬件工作一律通过 task-submit；队列和 NPU PID/PPID/设备映射采样在 task-daemon 外运行，包含其他占用者。

失败时：

- 先封存当前 host/device 日志、产物、配置和顺序，再安排 rerun、清缓存或重建。错误态不追加冒险的 device D2H；不能为了取现场延误已有必要的有界退出。
- 每条关联键至少保留 PID、device、context/run identity、callable/image、时间范围；不能用另一轮同地址代替本轮证据。
- CI 必须上传各失败作业自己的原始现场，不能只有 Network 有 artifact 而一般 onboard/DeepSeek 没有。部分归档也输出缺项清单，不以“上传成功”宣称现场完整。
- 本地回归继续携带 #2536 的诊断能力；此前未合入时的组合验证保留独立 checkout/overlay 来源；当前直接记录所用主线和产品改动的准确 revision/patch。

验收：一次可控测试失败能生成完整 manifest/缺项清单，保留真实产物与故障前日志，并可在测试机重建同版本、同输入、同调用顺序的重放命令。归档失败不掩盖原始测试错误。物理地址/并发调度无法恢复时明确标注，不能承诺确定复现。

## A2：仅针对仍缺的证据加入设备消费记录

进入条件：完整现场仍不能区分“发布正确、消费错误”和“发布内容本身错误”。这是后续需要实现与评估的工作，不是 #2536 的既有能力。

- 设计固定容量的 AICore 消费记录：实际接受的 token、run/report identity、函数入口、必要参数定位及进入/返回阶段；以该 core 为唯一写入者，避免共享热写和无界日志。
- 明确可见性、cache-line 所有权、保留代际、覆盖时机和安全读取条件。故障时不能假定 D2H 一定安全或成功；无记录/旧记录不能被解释为“没有执行”。
- HBG/TMR 使用相同证据含义，针对各自实际协议接线。AICPU 发布值与 AICore 消费值必须按同一身份对应，不能只按 slot 或地址匹配。
- 先用正常运行及定向故障验证记录归属，再测开销与时序扰动；不在 dispatch 热循环加 printf。需要 ABI 改动时三侧共同重建并版本核对。

退出标准：能根据一次现场支持或排除一个具体假设；保留“不足以定因”的结论。该项不阻塞 kernel eager 的共同执行接线。

## 工作分离

产品主线：Pipeline A 基础 → kernel eager → capture/replay。AIV 是持续跟踪的正确性风险；每轮验证如再次出现须归档、分类、披露，不能用后续绿跑抵消。无需在每个主线 PR 重启全套复现研究，也不把已知失败当作自动可合入许可。
