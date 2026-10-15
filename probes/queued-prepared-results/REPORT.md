> **历史实验记录。** 以下测量保留原环境/基线，设计解释可能已撤回；当前结论见[证据索引](../../kernel-mode-design.md)，规范见[原则](../../runtime-design-principles.md)。本文旧源码行号不指向当前 main；原始日志未修改。

# PreparedCall 排队与 capture 原语实验

日期：2026-09-15。设计见 [两层执行接口](../../kernel-mode-prepare-submit-design.md)。

后续针对 staging 覆盖、多 bank publication 和同步 H2D 捕获错误的验证见 [Probe I](../publication-results/REPORT.md)。

## 问题与范围

验证：多个已准备调用能否先排入 device 队列；两个侧流能否通过完整入口/出口共用 scratch；capture 能否记录 metadata publication；replay 是否依赖 host prepare 再执行。

NPU 探针使用真实 torch_npu 算子、两个侧流和 event，没有调用 Simpler 的 AICPU executor，不是 kernel mode 完整实现。它不验证 AICore-first 的内部握手、TMR/HBG serializer、DFX collector 或失败后的 device gate。

## 环境与原始记录

- 硬件经仓库 onboard-arch-precheck 检查为 a2a3。
- CANN 9.0.0，torch 2.7.1+cpu，torch_npu 2.7.1。
- 所有 NPU 调用通过 task-submit 独占所分配设备运行。
- TQ=1 最终实验使用 device 15；TQ=0 最终实验使用 device 3。两个配置不是同一物理设备上的性能对比。
- [NPU 探针源码](../../LOCAL-EVIDENCE.md#artifact-40b0c25daf49)。
- 探针 SHA256：`6af90ded174438a4750224d6ca8616c3d1f399e29a61e934b74214a76a4dcbd7`。
- [TQ=1 完整日志](../../LOCAL-EVIDENCE.md#artifact-9bb7f7a4e363)，[TQ=0 完整日志](../../LOCAL-EVIDENCE.md#artifact-31b30b7acd80)，均 exit=0。
- 第一轮尚未加入 metadata capture 的记录：[TQ=1](../../LOCAL-EVIDENCE.md#artifact-d204e179ec5f)、[TQ=0](../../LOCAL-EVIDENCE.md#artifact-67612deb0ccb)，均 exit=0。

## 实验结果

| 项目 | 方法 | TQ=0 | TQ=1 |
| --- | --- | --- | --- |
| 提前 enqueue | caller 先等未放行的 ExternalEvent，随后连续 submit 64 个 prepared 调用，flush host task queue 后 query 首末 completion，最后才放行 gate | 首末都 pending；64 份输出正确 | 同左 |
| 多次 replay 提前排队 | 捕获 8 个调用，共用两个 scratch；gate 未放行时提交 32 次 replay，每次保存 8 份输出 | 256/256 正确，host prepare 计数未变 | 同左 |
| 更新稳定 device 参数源 | 每轮先 D2D 更新 8 个 bias source，再 replay；16 轮之间不做 host sync，最后统一读取结果 | 128/128 正确 | 同左 |
| host metadata capture | 图内 pinned-host H2D 发布 bias，再执行调用；host prepare 只在图外执行 | 输出 80，prepare 计数未变 | 同左 |
| DMA 源改写对照 | 前次 replay 已完成后，把上述 pinned-host 源从 71 改到 99，再 replay | 输出变为 108 | 同左 |
| 缺少 reset 的负对照 | 捕获不 reset scratch 的调用；两次 replay 都成功提交 | 首次 23，第二次 70，捕获成功但数值错 | 同左 |

prepare 在探针中计算 scale 和 `bias = 3 * seed + 7`；两个侧流分别计算乘法与加法，caller 在 join 后合并输出。最后一项故意保留前次 scratch 内容，验证检查能够发现 replay 状态残留。

“64 项先排队”不是根据提交用时猜测：所有 submit 返回后，首末 completion 都没有完成，直到另一个 stream record ExternalEvent 才放行。源码使用 `caller.npu_stream` flush host task queue，避免把尚未发到 CANN 的 torch 队列项误算成已提交的设备工作；它不等待 device gate。

## 对当前软件准入的验证

工作树 simpler `3e08ede7`，未修改生产源码；独立构建并运行原有测试：

```text
ChipRunLaneTest.DirectCapacityTwoPreparesSuccessorAndBackpressuresThird
```

[测试输出](../../LOCAL-EVIDENCE.md#artifact-3ad4cb3622b7)：1/1 通过。该测试明确断言：

1. 第一次 submit 已 launch。
2. 第二次 submit 只 prepared，尚未 launch。
3. 第三次 submit 进入等待，尚未 prepare。
4. 放行第一次 wait 后，第二次才 launch、第三次才 prepare。

同一准入形状也在本地 upstream/main `e4bc6be0` 的 ChipRunLane、ChipWorker 和 DeviceRunnerBase 代码中确认。这个测试证明现有行为由软件刻意实施，不是测量设备队列容量。

构建使用 C++17 的仓库 CMake 配置和 `--parallel 2`，构建记录见 [配置](../../LOCAL-EVIDENCE.md#artifact-51067296a9da) 与 [编译](../../LOCAL-EVIDENCE.md#artifact-e17c73f11af1)。未重新构建或替换已安装 runtime 动态库。

## 得到的设计结论

- 允许多个完整调用提前进入 device 队列有真实原语依据；单一 mutable 工作区可在完整闭包之间按 device 顺序复用。
- host completion/reaping 可以只负责释放资源，无需承担启动下一次 launch 的职责。
- host-derived 结果可作为 metadata copy 的源被 capture 使用，无需 replay 再运行 host prepare。
- 该 DMA 路径不会替调用方保证 source bytes 不变：PreparedCall 源内存必须有 owner，并保持不可变。观察只覆盖本次 pinned-host 路径，不泛化为所有参数 API 的快照策略。
- “普通 replay 没有 host prepare”不代表没有 metadata DMA，不能只记录两个计算 launch 而漏掉发布和 reset。
- 更新测试仅覆盖固定地址参数源的内容变化，不证明任意 AICPU HostArgs task update、内嵌 tensor 地址 rebinding 或 HBG 拓扑更新已经可用。

## 复现

从工作区根目录执行；需已存在含 torch_npu 的项目本地 venv：

```bash
simpler/.claude/skills/onboard-arch-precheck/check.sh a2a3
mkdir -p docs-kernel-mode/probes/queued-prepared-results/ascend
export ASCEND_PROCESS_LOG_PATH="$PWD/docs-kernel-mode/probes/queued-prepared-results/ascend"
task-submit --device auto --device-num 1 --max-time 180 --timeout 300 \
  --env TASK_QUEUE_ENABLE=1 --env ASCEND_PROCESS_LOG_PATH \
  --run 'source simpler/.venv/bin/activate && python docs-kernel-mode/probes/probe_h_queued_prepared_calls.py'
```

将 TASK_QUEUE_ENABLE 改为 0，在新进程运行另一配置。程序对正向结果和两个负对照均有断言，失败返回非零；task-submit 的最终 `exit` 也必须检查。
