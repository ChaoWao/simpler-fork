# Kernel mode：证据与撤回记录

本文件不再维护 §0 规范决策表。**所有设计判断统一到 [runtime-design-principles.md](runtime-design-principles.md)**；机制见[共同执行协议](kernel-mode-prepare-submit-design.md)，实现顺序见 [ROADMAP](ROADMAP.md)。

[旧版全文](LOCAL-EVIDENCE.md#artifact-3dd5ca4db17b)按接管时的字节原样保存，包含作者最新修正、§16.4 八轮错误记录和完整推导。旧文“以本表为准”的声明已失效；其基线、原语实验与撤回过程作为历史保留。

## 1. 证据地图

下表是入口与适用范围，不是第二张设计判断表。TQ=0/1 表示这些原语臂的对应配置，不能推广成整套 simpler executor 验收。

| 证据 | 能支持什么 | 原始记录与限制 |
| --- | --- | --- |
| 当前代码 | main 的接口、event、资源分类、复制路径、健康/超时现状 | [SOURCE_BASELINE](SOURCE_BASELINE.md)；所有当前源码事实固定在 2085182a |
| Probe B/C/D | 双侧流捕获、禁止操作、特定多图共享 event 形状 | [RESULTS](probes/RESULTS.md)及 probes 下对应脚本；D 不证明所有节点可共享一个 event 集合 |
| Probe F/G | host task queue 顺序、task update 的能力边界 | [RESULTS](probes/RESULTS.md)；task update 不是本协议换参正确性的依赖 |
| Probe H | 64-deep enqueue；8×32=256 输出；16×8=128 更新；无 reset 的 23→70 负对照 | [报告](probes/queued-prepared-results/REPORT.md)、[TQ0](LOCAL-EVIDENCE.md#artifact-67612deb0ccb)、[TQ1](LOCAL-EVIDENCE.md#artifact-d204e179ec5f)；普通算子，不是 TMR/HBG executor |
| Probe H metadata | 完成后修改 pinned source 可使 80→108 | [TQ0](LOCAL-EVIDENCE.md#artifact-31b30b7acd80)、[TQ1](LOCAL-EVIDENCE.md#artifact-9bb7f7a4e363)；修改前已经静止，不证明在飞复写安全 |
| Probe H2 | 在飞覆盖同一源错误；有 owner 的不可变 host 版本可以无显式 host 等待 | [TQ0](LOCAL-EVIDENCE.md#artifact-9be6d3a4b63a)、[TQ1](LOCAL-EVIDENCE.md#artifact-2124a1dad321)；没有显式等待不等于所有 API 永不阻塞 |
| Probe I | bank 提前发布、捕获内 bank 依赖、[80,108] 错成 [108,108]、同步 H2D 被拒后 capture 仍活跃 | [报告](probes/publication-results/REPORT.md)、[TQ0](LOCAL-EVIDENCE.md#artifact-c7bd8db579dc)、[TQ1](LOCAL-EVIDENCE.md#artifact-b8877159573e)；不冒充交替多图及真实 executor 验收 |
| Probe K | 显式 ACL 复核复制方向、被拒 op 缺失、memset 每次重放、稳定槽更新 | [TQ0 JSON](LOCAL-EVIDENCE.md#artifact-0a579ac039ff)、[TQ1 JSON](LOCAL-EVIDENCE.md#artifact-66f20318fa41)；详见下节 |
| Probe L | RTS 流饱和阻塞与 bounded capture 规模观测 | [TQ0 JSON](LOCAL-EVIDENCE.md#artifact-a66bd42b06a4)、[TQ1 JSON](LOCAL-EVIDENCE.md#artifact-410caef5f522)、[TQ0 原始输出](LOCAL-EVIDENCE.md#artifact-91b72eae5dc3)、[TQ1 原始输出](LOCAL-EVIDENCE.md#artifact-c88c25fe2901) |
| Probe M | requested 与 actual op-timeout 不同，名义排序可能失效 | [原始 JSON](LOCAL-EVIDENCE.md#artifact-82eb9bc4b048)、[脚本](LOCAL-EVIDENCE.md#artifact-2d9a5cfb363a)；只测回读，不证明何时真的触发 watchdog |

所有测量沿用其原来的硬件/SDK/产物与代码快照。此次没有重新运行板端实验。

## 2. 本轮核查的关键观测

### 同步复制被拒，不等于捕获被毁

K 的 H2D/D2H/D2D 同步臂均记录 107030 / ACTIVE / end=0 / graph=true。Part 3 replay 只出现合法 async op 的结果 `0x22`。因此实现必须检查每次发布返回值；CaptureEnd 成功不足以证明图完整。

### 队列饱和，不返回 WouldBlock

L 的 raw memcpy/memset 饱和臂：前 1792 项接受，第 1793 次调用在 40 秒观察窗口内未返回。Torch 臂 TQ=0 为 1793，TQ=1 为 5889。它们是该设备、SDK 和入口下的观测，不是 RTS 公共容量常数。

`under_limit` 独立臂停在 1792，放行后 queued_work_ran / stream_reusable 为 true；**饱和臂被终止，不能用 under_limit 的恢复结果声称那个已挂住的调用成功恢复。** Capture 臂记录到 200000 节点并正常出图，只证明这个规模内未见限制，不是无限容量证明。多流闭包能否在饱和下继续发射另一侧任务还须单独验收。

顶层旧 `probe_l_report.json` 是早期未完成报告，已归档，不作为上述数字的来源。

### 超时请求值不等于生效值

M 的 12 个请求均 rc=0；3,000,000µs 回读 4,294,967µs，5,000,000µs 回读 8,589,934µs。主线 `configure_aicore_op_timeout` 记录 actual，但排序校验仍以配置请求值为输入。仅拒绝低于一个下限不能修复上方量化造成的反转。

原始 JSON 的 `2 s <- CI's value` 是当时错误标签，**原始数据不改写，标签在这里撤回**；后续已核实的 CI 请求是 3s。请求3s却在3.93s收割的历史观测，尚不能由4.295s回读解释，必须区分 scheduler、op watchdog、stream sync 的实际触发层。未确认触发层之前，不把量化拟合当设备真实收割时刻。

## 3. 历史错误记录如何使用

旧版 §16.4 的完整八轮记录保留在[原件](LOCAL-EVIDENCE.md#artifact-3dd5ca4db17b)。本轮只增加定位与撤回说明，不把历史行号平移到新 main。

| 历史问题 | 当前去向 |
| --- | --- |
| 把静止时源修改推广到在飞复写；把冻结指针等同冻结内容 | 原则 10；协议 §5 明确源版本、稳定槽与发布区 |
| 只读一侧消费者；误判 HBG 没 reset 就不能 eager 排队 | 源码索引同时列 writer、AICPU、AICore 与两 runtime contract |
| 把图外版本→槽更新放入 capture；把异步收窄成必须走 caller | 协议 §4/§5 区分图外更新与图内独立 publication |
| 错误查询极性使断言不能失败 | 原始探针修正记录保留；每个正臂须有能区分的负对照 |
| 混淆父 run 并发、host 在途数和 B−1 发布窗口 | 原则 4/5；ROADMAP 接收 Pipeline A 的父 run 顺序保证 |
| 只改正文、漏覆盖表；压缩清单丢最后使用者 | 原则 13 与协议 §6保留完整消费者集合，包括仍有效的 caller handle |
| PrepareTail 与旧四事件图 | 撤回；当前五事件链见 TMR 时序，publication 的新增事件单独计数 |
| 旧“PMU 每 run free”与 device KernelArgs 描述 | 按固定 main 的实际入口更正；历史报告仍保留当时事实 |

## 4. 尚未获得的证据

真实 TMR/HBG kernel eager、完整 capture/replay、HBG restore 和构图副作用、最小 handshake reset/cache 契约、故障后在飞引用、DFX连续记录、A5/legacy，以及多图/多流饱和活性，都不能用以上普通算子探针替代。具体阶段与退出条件只维护在 [ROADMAP](ROADMAP.md)。
