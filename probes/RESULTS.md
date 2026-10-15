> **历史实验记录。** 以下测量保留原环境/基线，设计解释可能已撤回；当前结论见[证据索引](../kernel-mode-design.md)，规范见[原则](../runtime-design-principles.md)。本文旧源码行号不指向当前 main；原始日志未修改。

# Probe B′ — 两条侧流能否经 event 进同一次 ACLGraph capture

**结论：能。** a2a3 onboard 实测，CANN 9.0.0，torch 2.7.1+cpu / torch_npu，`task-submit --device auto`。
复跑：`task-submit --device auto --device-num 1 --run "python3 probes/probe_b2_two_side_streams_capture.py"`

## 问的是什么

E.1 裁定 kernel 模式建两条 PyPTO 自有的流——AICPU 专用流 ＋ AICore hidden 流——两条都只经 event 与调用方的流相连。
ACLGraph 捕获的是**在捕获流上入队的东西**，所以问题是：经 recorded event 关联进来的侧流工作，会不会一起被捕获。
这是 v9 §6 第 1 步 Probe B 按 E.1 加码后的形态，也是设计文档点名的"目标二单点风险"。

## 三个臂

| 臂 | 做什么 | 结果 |
| --- | --- | --- |
| **arm0 eager** | 不捕获，直接跑同一段序列 ＋ 一个捕获流上的对照 op | aicpu ✓ aicore ✓ caller_ctl ✓ |
| **arm1 capture** | 在 `torch_npu.npu.graph` 内入队同一段序列，捕获后把三个 slot 清零，replay ×3 | **3/3 replay：aicpu ✓ aicore ✓ caller_ctl ✓** |
| **arm2 负对照** | 同一段序列，但记录/等待针对**进入捕获前**取到的那条流 | aicpu ✗ aicore ✗ —— 且**零 ACL 错误** |

信号设计：每条侧流把一个 pattern `memcpyAsync` 进自己的 device slot；replay 之间同步清零。
replay 后 slot 被重新填上 ⇒ 那条侧流的工作在图里。`caller_ctl` 是捕获流自身的 op，用来证明图非空——
它若为 False，说明是 harness 坏了而不是 CANN 拒绝。

## 两条结论

**① E.1 的双流形状与 capture 相容。** 两条侧流的工作都进了图、都随 replay 重放，3/3 稳定，全程无 ACL 错误。
"目标二单点风险"在 a2a3 上以正面结果关闭。（a5 未跑——按非目标 #12，不得由 a2a3 推断 a5。）

**② `torch_npu.npu.graph` 在进入时切到它自己的捕获流。**
实测 `outer=0x…1ae0`、`capture=0x…2980`，不是同一条。所以 kernel 模式要借的"caller stream"是
**捕获上下文之内**当前的那条，不是 adapter 在 prepare/init 时看到的那条。

arm2 就是这个错误的复现，而它的失败形态是最坏的一种：**入队不报错、capture 不报错、replay 静默地什么都不做**，
只有结果不对。任何在 prepare 期缓存 caller stream、launch 时复用的实现都会掉进去。

---

# Probe D — 多张 ACLGraph 能否共享一批 context 常驻 event

**结论：能，六项全过。** a2a3 onboard，CANN 9.0.0，torch 2.7.1+cpu，跑了两轮结果一致。
复跑：`task-submit --device auto --device-num 1 --run "TASK_QUEUE_ENABLE=0 python3 probes/probe_d_two_graphs_shared_events.py"`

## 问的是什么

kernel 模式的两条侧流和四个 event 是 **context 常驻**的——建一次、活到 context 结束。
vLLM 每个 batch 桶捕一张图，所以 N 张图会 record/wait **同一批 event 对象**。
H7「分桶切换」默认这成立，但没人验过，而 H7 在最后一波。

比"能不能用"更尖的是**背靠背**那一问：两次 replay 之间不做主机同步，
于是同一个 event 在上一次 record 可能还在飞的时候被第二次 record。

## 结果

图 A 写 pattern `0xA5`、图 B 写 `0x5C`，进**同一对** device slot；共享一个 graph pool 捕获。

| 臂 | 做什么 | 结果 |
| --- | --- | --- |
| arm0 | 第二张图在同一批 event/stream 上捕获 | 未抛异常 |
| arm1 | 交替 replay `A B A B A A B`，每步同步、**中间不清零** | 7/7，每步 slot 恰好翻成该图的 pattern |
| arm2 | **背靠背 replay，中间不同步**：AB / BA / AA / BB | 4/4 |
| arm3 | replay 之后在同一批 event 上跑 eager | 过 |
| arm4 | 用**自己的 pool** 再捕一张图 C | 过，且捕获流地址与 A/B **相同** |

零 ACL 错误。（退出时的 107002 `rtModelDestroy ... ctx is NULL` 是解释器拆解顺序，
NPUGraph 析构晚于 context，与被测内容无关。）

arm1 不清零是刻意的：每次 replay 必须把 slot **翻成自己的 pattern**，
于是"什么都没做"和"跑成了另一张图"都会被抓到。

## 两条结论

**① H7 的前提**部分**成立，但 arm 2 的判据要按窄的读（2026-09-15 更正，R7）。**
交替 replay（arm1，逐步同步、逐步核对）与 eager 混用（arm3）成立。

> **arm 2 不足以证明"每次 replay 都执行了"。** 它的 `want` 始终是**第二张图**的 pattern，在同一对输出槽上、
> 只在最后同步一次之后检查。**把第一次 replay 整个丢掉，四个用例依然全过** —— 判据没有区分力。
> 所以 arm 2 的结论只能写成「这四个背靠背序列的**最终值**正确」，不能写成「共享常驻 event 在 N 张图下安全」。
> 要关掉这个风险，需要**每次 replay 的独立结果槽或执行序号**，外加一个受控 gate 保证第一次执行尚未完成时第二次确实已入队；
> 还要覆盖跨图数量不足 B、非 B 整数倍、穿插 eager 的 bank 重用。Probe I 的**同图**实验不能代替。
>
> **连带撤回我自己的一条推论**：我曾用 arm 2 论证"一套常驻 event 或许可以服务 K 张图，于是 event 预算的 Σ 可能是 K 倍悲观"。
> 那条推论建立在 arm 2 上，而 arm 2 不支持它。**Σ 仍是目前唯一有依据的上界。**

**② 本进程内三次捕获拿到的是同一条 current stream**（A、B 共享 pool，C 自己的 pool，地址全同）。
这条要和 B′ 的结论②一起读——它把那个坑变阴了：

| 实现 | 表现 | 性质 |
| --- | --- | --- |
| 在**捕获之外**（init/prepare）缓存 caller stream | 必错，且静默 | B′ arm2，已复现 |
| 从**第一次捕获内部**取到后缓存 | **今天全过** | 靠"本构建复用同一条捕获流"这个未文档化细节 |

规则不变——**每次 launch 重新取 current stream，禁止缓存**——但要知道 ⑫ ST 的负向用例
只挡得住第一种写法；第二种今天是过的，只能靠 review 或在 binder 里不留缓存的位置。

**观测边界**：单进程、单设备、三次捕获、一个 torch_npu 构建。多设备/多线程未测；
上表第二行的"今天全过"不构成承诺。本探针也从不销毁图——桶集合变化时的重捕未测，归 H7 验收。


---

# Probe C — 捕获期间什么被禁，以及它怎么失败

**a2a3 onboard，CANN 9.0.0，raw `aclmdlRICapture*` 直驱**（不经 `torch_npu.npu.graph`，每个用例独立 stream ＋独立 capture）。
复跑：`task-submit --device auto --device-num 1 --run "TASK_QUEUE_ENABLE=0 python3 probes/probe_c_forbidden_under_capture.py"`

判据：`op rc≠0` ＝当场拒绝（安全）；`rc=0 且 status ACTIVE 且 end=0` ＝**未被拒且捕获能正常结束**；
`rc=0 但 status INVALIDATED 或 end≠0` ＝**静默污染**（危险类）。

> **⚠ 2026-09-15 更正（R9）：中间那档原来写的是"＝被捕获"，那是过强的。**
> 本探针建图后**立即销毁，从不 replay**（`probe_c_forbidden_under_capture.py:~100`），所以这组返回值
> 只能证明「这个操作没被拒、且 `CaptureEnd` 能出图」，**不能证明该操作真的进了图、会在 replay 时执行**。
> 表里那几个"放行"案例本身就说明这一点：`aclrtMalloc`、别的流上的 synchronize、完全不挂 event 的侧流工作，
> 都是 `rc=0 / ACTIVE / end=0`，而其中至少"不挂 event 的侧流工作"**明确不在图里**（见结论 ③）。
> 要把结论升级成"被捕获"，需要一个**可观测的重放副作用**。

## 结果表

| 操作 | op rc | status | CaptureEnd | 判定 |
| --- | --- | --- | --- | --- |
| 空捕获 / 捕获流上 memcpyAsync | 0 | ACTIVE | 0 | 基线，图非空 |
| **`aclrtMalloc`** | **0** | ACTIVE | 0 | **不拒绝** |
| **`aclrtFree`** | **0** | ACTIVE | 0 | **不拒绝** |
| `aclrtSynchronizeStream`（捕获流） | 107027 | ACTIVE | 0 | 拒绝 |
| **`aclrtSynchronizeStream`（别的流）** | **0** | ACTIVE | 0 | **放行** |
| `aclrtSynchronizeDevice` | 107027 | ACTIVE | 0 | 拒绝 |
| `aclrtStreamQuery`（捕获流） | 107027 | ACTIVE | 0 | 拒绝 |
| `aclrtMemcpy` / `aclrtMemset`（同步） | 107030 | ACTIVE | 0 | 拒绝 |
| `aclrtCreateStream` / `CreateEvent` | 0 | ACTIVE | 0 | 放行 |
| `aclrtSynchronizeEvent` / `QueryEventStatus` | 107028 | ACTIVE | 0 | 拒绝 |
| **侧流工作、完全不挂 event** | **0** | ACTIVE | 0 | **不进图、不报错**（B′ arm2 的机理） |
| **fork 到侧流但不 join 回来** | 0 | ACTIVE | **107025** | **End 抓住，无图** |
| fork ＋ event join 回来 | 0 | ACTIVE | 0 | **可捕获** |
| **两条侧流都 join 回来（E.1 双流）** | 0 | ACTIVE | 0 | **可捕获** |
| **wait 一个在捕获开始前 record 的 event** | **107024** | ACTIVE | 0 | **拒绝** |
| THREAD_LOCAL / RELAXED 模式下的以上放行项 | 0 | ACTIVE | 0 | 未见差异 |

## 四条结论

**① `107024` 落在"等一个捕获外 record 的 event"上。** 这正是 §0 第 6 行三分支要回避的动作——
两个存活分支都明写"**不 enqueue 旧 tail wait**"。现在这条设计有了硬证据：真做了就是 107024 当场拒绝。
kernel 模式的 event 是 context 常驻的，跨捕获边界等待是**结构性**会遇到的形状，不是边角情况。

**② "launch 期零分配"在运行期毫无执法。** `aclrtMalloc`/`aclrtFree` 在捕获中返回 0、图照常生成，
GLOBAL 与 RELAXED 都一样。⑫ST 只能靠禁止-API 追踪 shim 抓，靠 CANN 是抓不到的。

**③ 危险的只有一种形状：不挂 event 的侧流工作。** 它不进图、不报错、eager 地跑掉一次。
而"fork 了不 join"反而是**安全**的——`CaptureEnd` 返 107025 且不给图。
所以 ⑨binder 的失败闭包要防的是"漏挂 event"，不是"漏 join"。

**④ E.1 的双流形状在 raw capture 下同样成立**，与 B′ 经 torch 包装得到的结论一致。

**观测边界**：THREAD_LOCAL/RELAXED 两栏我只测了在 GLOBAL 下**本来就放行**的操作，
所以"未见差异"不等于"模式不影响禁令范围"——真正的判别要拿被拒的那几项去测，这条没做。

---

# Probe F′ — 任务队列打开时，裸 ACL 调用与 torch op 还保序吗

**结论：不可靠地保序。** 违例只出现在**缓存指针**的臂上。
复跑：`TASK_QUEUE_ENABLE=1 PROBE_BUMPS=4000 PROBE_TRIALS=40 python3 probes/probe_f2_task_queue_ordering.py`

## 先证明开关真的生效

`probes/check_task_queue_effective.py`：`TASK_QUEUE_ENABLE=1` 时进程多出 **`acl_thread`** 与
**`release_thread`** 两个线程（=0 时没有），主机侧每 op 发射成本 **13.4 → 8.4 µs**，
且出现发射与落盘的时间差（16.9 ms vs 21.4 ms；=0 时两者相等）。开关确实在起作用。

> **⚠ 2026-09-17 读法收窄（来源 `run-pipeline-redesign.md` §7.2／§12）：这里的「深 4000」是 enqueued 深度，而且本探针从未驱动到耗尽。**
> 状态要分四态 —— **accepted / staged / enqueued / completed**。本探针灌进去的是**已入队**的工作，所以它证的是"入队 4000 个之后仍保序"，**不证**"host 可以有 4000 个 run 在飞"（program 侧还隔着 L3 的依赖图，见设计文档第 13 行的 2026-09-17 更正），**也不证**队列耗尽时会发生什么 —— 那一条是 **A 轨的承重未验项**：「实际 launch 遇队列压力是阻塞还是返回哪类错误」，`run-pipeline-redesign.md` §12 明写"靠 CANN 自动排队承接背压的可行性全压在它上面，本轮无设备证据"。**这是下一个该写的探针。**

## 结果（TQ=1，队列深 4000，每臂 40 次 × 4 轮）

| 臂 | 违例 |
| --- | --- |
| torch → 裸调用，**每次现取** stream ／ data_ptr | **0 / ~160** |
| torch → fork 侧流再 join（⑨binder 的形状），每次现取 | **0 / ~160** |
| torch → 裸调用，**缓存 stream 指针** | **2 次** |
| torch → 裸调用，**缓存 stream ＋ data_ptr** | **2 次**（如 `3997`、`3907`） |

违例形态：4000 次 `add_` 之后的裸 `memcpyAsync` 读到 3997 —— 三次自增**还没提交到流上**，
裸调用抢在前面。TQ=0 时四臂全 0 违例。capture＋replay 在 TQ=1 下照常工作。

## 结论

**队列打开时，从调用线程直接发的裸 ACL 调用不保证排在先前 torch op 之后。**
观察到的规律是：**现取** `current_stream()` 的臂一次没错过，**缓存**的臂会错。
裸调用本身耗时 0.03–0.08 ms、取 stream 耗时 ~0.1 ms；队列从头排空约 ~30 ms。

> **⚠ 2026-09-15 更正（R9）：原文据此排除了"取 stream 会 flush 队列"这个机制，那个推断过强。**
> 取 stream 时只需要排掉**当时剩余的** host queue，不等于从头重做整条 device 时间线，所以
> 0.1 ms ≪ 30 ms 并不能否定 flush 类机制。**保留的事实是：裸调用确实出现了顺序违例，且违例只出现在缓存指针的臂上。
> 删除机制归因。** 结论不变——adapter 必须走 OpCommand。

**因此**：⑪K8 的 adapter **不应**在 op body 里直接调 `simpler_kernel_mode_launch`，
而应走 torch 自己的入队路径（OpCommand）。"现取就没事"是**观察到的规律，不是保证**——
0/160 不是证明，机理也没有文档依据，不能拿它当契约。

**这条与 A1 是同一条规则的第二个独立理由。** 缓存 caller stream 的两种坏处：
① capture 下缓存的是错的流 → 静默空跑（B′ arm2）；② TQ=1 下会插到 torch 已发的工作前面（本探针）。

**观测边界**：单进程单卡、torch_npu 2.7.1、memcpy 型负载；未测多卡/多线程/真实算子。
0 违例的两臂只是"没观察到"，样本量不足以支撑"安全"。

---

# Probe A2 — `rtsFuncGetByName` 拿到的句柄能不能当 `aclrtFuncHandle`

**结论：能，句柄空间就是同一个。** a2a3 onboard，CANN 9.0.0，`upstream/main b28c4c4d` 构建，载体是 `vector_example`。
复跑：`gcc -shared -fPIC -o /tmp/probe_a2_shim.so probes/probe_a2_shim.c -ldl` 然后
`LD_PRELOAD=/tmp/probe_a2_shim.so python -m pytest examples/a2a3/tensormap_and_ringbuffer/vector_example --platform a2a3 --device N`，
结果落 `/tmp/probe_a2.out`（本次原始输出存 `probes/probe_a2_output.txt`）。

## 问的是什么

⑥K4 要决定用哪条 launch 路径。若 `LoadAicpuOp::Init`（`load_aicpu_op.cpp:377` 的 `rtsFuncGetByName`）
拿到的 `rtFuncHandle` 能当 `aclrtFuncHandle` 用，K4 就能走**有契约的** `aclrtLaunchKernelWithHostArgs`；
否则只能继续依赖 `rtsLaunchCpuKernel` 未文档化的快照行为（host 指针原样进 `cpu_args.baseArgs.args`），
且计划要求把 A-aicpu 升格为常驻 CI 测试。

## 为什么必须在线做

AICPU inner SO 由 bootstrap 推到**设备**，主机文件系统上没有（`find / -name 'simpler_inner_*.so'` 为空），
`cpuKernelMode=0` 的语义就是"只加载 json"。所以这次注册无法离线复现，只能观察一次真实的。
用 `LD_PRELOAD` 拦截 `rtsBinaryLoadFromFile` / `rtsFuncGetByName`，**不改 simpler 源码一行**。

## 结果（三个符号，逐个一致）

| 对 rt_handle 做的事 | `simpler_aicpu_exec` | `simpler_aicpu_init` | `simpler_aicpu_register_callable` |
| --- | --- | --- | --- |
| `aclrtGetFunctionName` | rc=0，名字**完全吻合** | rc=0，吻合 | rc=0，吻合 |
| `aclrtKernelArgsGetHandleMemSize` | rc=0，4176 | rc=0，4176 | rc=0，4176 |
| `aclrtBinaryGetFunction(rts binHandle)` | rc=0，**指针与 rt_handle 相同** | rc=0，相同 | rc=0，相同 |

注意第三行是拿 **rts 的 binary handle** 喂给 ACL 的 `aclrtBinaryGetFunction`，它不但接受，
返回的 func handle **与 rts 给的是同一个地址**。

## 一个附带的强证据：探针自己的 bug

第一版无限递归爆栈（segfault，18619 次 `rtsFuncGetByName` 且零次 arm-3 完成）。
原因是 **`aclrtBinaryGetFunction` 内部就调 `rtsFuncGetByName`**，被自己的拦截器接住。
这本身就说明 **ACL 是直接架在 rts 上的薄层**。现在的 shim 用 thread-local 递归位挡住。

结构上也吻合：`aclrtBinaryLoadOption` 与 `rtLoadBinaryOption_t` 逐字段相同，枚举值都是 1/2/3
（`ACL_RT_BINARY_LOAD_OPT_CPU_KERNEL_MODE == RT_LOAD_BINARY_OPT_CPU_KERNEL_MODE == 3`）。

## 结论与边界

**⑥K4 可以用 `aclrtLaunchKernelWithHostArgs`**，不必依赖 `rtsLaunchCpuKernel` 的未文档化行为，
计划里"把 A-aicpu 升格为常驻 CI 测试"这条前提随之解除。

**没有证明的那一半（重要）**：本探针证的是**句柄被识别、且是同一个对象**，
**没有真的用 `aclrtLaunchKernelWithHostArgs` 发起一次 AICPU launch**。
参数打包（`aclrtLaunchKernelCfg`、`placeHolderArray`）与 `rtCpuKernelArgs_t` 的对应关系未验，
真实 launch 仍需一次上板确认——但那是"怎么调"的问题，不再是"能不能调"的问题。

**观测边界**：a2a3 onboard、CANN 9.0.0、TMR runtime、单进程。a5 未跑。

---

# Probe G / G2 — 捕获之后，任务参数能不能改

**结论：能，而且设备地址和标量都能。** a2a3 onboard，CANN 9.0.0，torch_npu 2.7.1。
代码：`probes/probe_g_task_update_after_capture.py`（memcpy 版，失败）· `probes/probe_g2_what_update_can_change.py`（真算子版，通过）。

## 为什么问这个

此前**所有**设计（含非目标 #7「capture 后不提供修改 tensor 地址／scalar 的 graph update API」）
都建立在"capture 全烧死、replay 逐字重放"之上，而**这条从没被验过**。

实际上 CANN 导出 `aclmdlRICaptureTaskGrp{Begin,End}` ＋ `aclmdlRICaptureTaskUpdate{Begin,End}`，
torch_npu 绑了 `graph_task_group_{begin,end}` / `graph_task_update_{begin,end}` ＋ `ExternalEvent`，
**torchair 已经在用**（`torch_npu/dynamo/torchair/_acl_concrete_graph/acl_graph.py:374,428`）。

## 机制（torchair 的，已复现）

```
capture:  ev.wait(cs); ev.reset(cs)              # 图跑到这里会停住
          graph_task_group_begin(cs)
            <被参数化的那个任务>
          handle = graph_task_group_end(cs)

replay:   graph.replay()                          # 跑到 ev 处停下
          在另一条 update stream 上：
            graph_task_update_begin(upd, handle)
            <重新执行同一个 op，换参数>            # 就地重录任务参数
            graph_task_update_end(upd)
            upd.record_event(ev)                  # 放行
```

**所以 replay 不是"不回主机"。** 这个模式**故意**在图里埋一个设备侧等待，
每次 replay 主机都要参与并做真活。注意 torchair 是**先 `replay()` 再做 update** ——
图先跑到停点，主机在这段时间里准备更新，**这本身就是一种流水**。

## 结果

| 臂 | 改什么 | 结果 |
| --- | --- | --- |
| **G** memcpy 进 task group | 原始 `aclrtMemcpyAsync` | **失败 507009**，且 `CaptureEnd` 507903 —— **整个 capture 被作废**。memcpy 不是合法的 task-group 任务 |
| **G2-A** 换**输入张量地址** | `add(a1,b,out)` → `add(a2,b,out)` | **过**，`out[0]=17.0`（非捕获值 11.0） |
| **G2-B** 只换**标量** | `alpha 1` → `alpha 3` | **过**，`out[0]=31.0` |

## 三条结论

**① 非目标 #7 的前提不成立。** "capture 后不能改参数"**不是平台限制，是设计选择**。
设备地址和标量都可更新，机制现成、torch_npu 有绑定、torchair 在生产里用。

**② 但只有 kernel launch 类任务可更新，memcpy 不行**，而且失败方式很硬——直接作废整次 capture。

**③ 现实用法比能力窄得多。** torchair 的 `_REPLACE_FUNC_MAP` 只有 `npu_fused_infer_attention_score`
一族，`updated_param_keys` 只有 `actual_seq_lengths` 这类 host 标量，配 `get_max_workspace`（静态 workspace）
＋ `.out` 变体（固定输出地址）。**能力是宽的，他们选择只用最窄的一条。**

## 对 simpler 的直接后果（AICore 与 AICPU 两条路不对称）

- **AICore** 收 `device_k_args_`（**device 指针**）→ 内容可以靠写那块设备内存改 → 不需要 task update
- **AICPU** 收 `&kernel_args.args`（**host 指针**，`rtsLaunchCpuKernel` 由 CANN 快照）→ **内容烧进任务**
  → 写设备内存改不了它，**只能靠 task update**

而 kernel 模式的逐调用载荷（K9 header ＋ args POD）走的正是 AICPU 那条路。

**没验的一半**：AICPU launch 任务本身能否进 task group 并被 update —— 需要 kernel 模式的实现，
今天主线上是 stub，做不了。这是 ⑥K4／⑨binder 该验的第一件事。

---

# Probe H — 深队列、共享 scratch、以及 capture 下的逐调用参数通道

代码：`probe_h_queued_prepared_calls.py`（五臂）
复跑：`task-submit --device auto --device-num 1 --run "TASK_QUEUE_ENABLE=0 python3 probes/probe_h_queued_prepared_calls.py"`
（另跑一遍 `TASK_QUEUE_ENABLE=1`；两次结果逐字段一致）
原始输出：`queued-prepared-results/tq0-with-metadata.log`（device 3）、`tq1-with-metadata.log`（device 15）

## 问的是什么

用户 2026-09-15 提的三条架构问题，其中两条可以在 ACL/torch 层直接问：

1. **严格流水是平台要求吗？** KernelLaunch 有天然队列，为什么 program 模式要在 host 上等前一次跑完？
   共享的 scratch arena 会不会因此串味？
2. **capture 之后，host 每次算出来的"最终结果"怎么进入 replay？** 除了 `aclmdlRICaptureTaskUpdate`
   （Probe G2 验过，但只对 kernel launch 类任务成立、memcpy 会作废捕获）还有别的通道吗？

探针不实例化 simpler 的 AICPU executor、不改准入、不验 handshake —— 它只用真实 torch_npu 算子
＋ 两条侧流 ＋ 跨流 event 复现「一次 submit」的形状，所以它答的是**平台允不允许**，不是 simpler 今天做不做。

## 结果（五臂，TQ=0 与 TQ=1 全部一致）

| 臂 | 形状 | 结果 |
| --- | --- | --- |
| `eager` | 64 个已 prepare 的调用，共享同一对 scratch，设备被 `ExternalEvent` 闸在最前面 | **64 次 submit 全部从 host 返回**；`first_and_last_pending: true`（首尾都没跑）；闸门释放后 **64 个输出全对** |
| `capture_replay` | 8 个这样的 submit 捕成一张图，连 replay 32 次 | **256 个输出全对**；`host_build_count_unchanged: true` |
| `ordered_record_updates` | 同地址、换内容（D2D 写进 `bias`），每轮 replay 前按流序写入，16 轮 × 8 路 | **128 个输出全对**，host build 计数不变 |
| `pinned_host_metadata_capture` | 捕获区内含一次 **pinned-host → device 异步拷贝**；capture 后**改写 host 源缓冲**再 replay | `original: 80.0` → 改源为 99.0 → `after_source_mutation: 108.0`。**`dma_observes_source_bytes: true`** || `missing_reset_negative_control` | 去掉设备时刻的 `scratch.zero_()`，看探针有没有区分力 | 第一次 replay 对（23.0），**第二次错（70.0）** → `caught: true` |

## 四条结论

**① 严格流水不是平台要求。** 64 个多流、带跨流 event 依赖的提交可以全部排进队列、host 全部返回、
结果全对。program 模式今天的「单执行权」（`device_runner_base.cpp:2174` 单槽 CAS，执行权从
`c_api_shared.cpp:940` 持到 `:1123`）是**软件选择**，并且被上游 UT 钉死
（`test_chip_run_lane.cpp:273/307`）。

**② 一份共享 scratch 可以服务 N 个在飞 launch —— 条件是复位必须在设备时刻、流序内。**
64 个调用共享同一对 scratch 且全对，因为 `reset=True` 把 `zero_()` 发在设备上、排在自己前面；
第 5 臂把它拿掉，第二次 replay 立刻读到上一次的残留。
→ TMR 的三块 `DEVICE_SCRATCH` arena 不必复制成 depth 份：AICPU 的 per-run 复位
（`aicpu_executor.cpp:596-650`）本来就在设备上、在 aicpu 流序内。**真正封顶的是 `TASK_ARGS`
（`HOST_PER_RUN`），它才需要 N 份。**

**③ capture 后换参有第二条机制 —— 但它只证明了 re-read 语义，不证明可以流水化。**
一次被捕获的 **pinned host → device 异步拷贝，在 replay 时会回头读 host 源缓冲**。

> **⚠ 2026-09-15 更正（Probe H2），并且更正过两次。**
>
> 我当初据此写成"换参就是 prepare 往常驻 pinned 缓冲写字节、零 ACL 调用"。**那一步推过头了**：本臂在改写源之前调了
> `torch.npu.synchronize()`，注释自己写着 "after device quiescence"（`probe_h_queued_prepared_calls.py:201-205`）。
> H2 臂 1 实测：不加栅栏地流水化「写→replay→写→replay」，**32 轮错 31 轮**。
>
> 但我第二版又改过头了，写成"host 缓冲不能当捕获区里的 DMA 源"。**H2 臂 4／臂 5 证伪了这一版**：
> 固定不可变 pinned image 直接当捕获源、连 replay 32 次全对；多份**不可变** host 版本异步 H2D 写进稳定 device 槽也全对。
> 被证伪的只是「**无栅栏改写一份仍会被 DMA 读到的 host 源**」，不是「host 内存」。
>
> 本臂的结论因此收窄为一句**地址语义**：**捕获的 DMA 节点在 replay 时按源地址重新取数，而不是回放捕获时的字节快照。**
> 它有用，但它不是更新协议。更新协议见 Probe H2。

**④ replay 期间 host 确实完全不参与。** 第 2 臂的 `host_build_count_unchanged` 是这条的正面确认
（32 次 replay，prepare 回调零次），它是 §0 第 5 行「replay 不回主机」那条依据的实测版本。

## 没验的

- 探针用的是 torch 的 `copy_(non_blocking=True)` 与 `pin_memory=True`，**不是直调
  `aclrtMemcpyAsync` ／ `aclrtMallocHost`**。simpler 侧落地必须换成显式异步 API 并在 caller stream
  上发起，pinned 分配归谁未定。
- 64 深只证明「平台允许」，**不是 simpler 的正确性证明**：simpler 侧还要解决 `finalize` 的回拷/校验
  在多个在飞 run 下的排队（今天它和执行权绑死），以及每一项 per-run 复位是否都已在设备时刻
  —— 已知一处嫌疑：`device_runner.cpp:607` 的 `workers[i].aicore_done = 0`（见设计文档 §16.1）。
- a5 未跑（非目标 #12）。

---

# Probe H2 — 换参的正确协议：谁可以当捕获区的 DMA 源，什么时候可以改它

代码：`probe_h2_staging_update_protocol.py`（五臂）
复跑：`task-submit --device auto --device-num 1 --run "TASK_QUEUE_ENABLE=0 python3 probes/probe_h2_staging_update_protocol.py"`（另跑 `TASK_QUEUE_ENABLE=1`）
原始输出：`staging-update-results/tq0.log`、`tq1.log`

## 为什么要有这个探针

Probe H 第 4 臂证明了捕获的 DMA 节点会**按源地址重新取数**。我把它写进设计，变成了"换参 ＝ prepare
往常驻 pinned 缓冲写字节，零 ACL 调用"。**中间少了一步没验**：

```
写 staging = A → enqueue replay A → 写 staging = B → enqueue replay B → ...
```

如果 replay A 的 copy 节点还没执行，它会读到 B。**host 提交顺序不给 host store 与设备 DMA 之间任何定序关系。**
Probe H 第 4 臂恰好绕开了这一点——它在改写前 `synchronize()` 了。

探针把一条 24 级 matmul 链挡在 copy 节点前面，确保 host 从 `replay()` 返回时 DMA 真的还没执行。
臂 4／臂 5 是第二轮加的，用来检验我"host 内存不能当源"那个过度推广的第二版说法。

## 结果（五臂，TQ=0／TQ=1 一致）

| 臂 | 形状 | 结果 |
| --- | --- | --- |
| 1 · **负对照** | 一张图 ＋ **一份** pinned 源，**不加任何栅栏**地改写 | **32 轮错 31 轮**（TQ=0／1 均为 31）。round 1 要 1001 读到 **1010**，round 2 要 1002 读到 1014 |
| 2 | 同上，但每次改写前等上一次 replay 的 event（**整图粒度**：event 记在整次 replay ＋ 输出复制之后） | **全对，但 `host_waits: 31`／32** |
| 3 | **稳定 device 槽**当捕获源；版本是**独立对象**；更新是同流 D2D，排在本次 replay 前 | **全对，零显式等待**；`rounds_with_work_still_pending: 31` |
| 4 | **固定**不可变 pinned image 直接当捕获源，连 replay 32 次，全程不改 | **全对** |
| 5 | **多份不可变 pinned 版本** → 异步 H2D 写进稳定 device 槽 → replay | **全对，零显式等待** |

`host_fence_protocol_correct: true` · `device_source_protocol_correct: true` ·
`fixed_pinned_image_as_ingraph_source_ok: true` · `immutable_host_versions_into_device_slot_ok: true` ·
`unfenced_host_staging_race_observed: true` · `failed_protocol_arms: []`

## 四条结论

**① 被证伪的是"无栅栏改写一份仍会被 DMA 读到的源"，不是"host 内存"。**
臂 1 不是偶发竞争——**32 轮错 31 轮**，而且错得很远（host 跑到十几轮之后 DMA 才去读）。
这类失败在轻负载的小测试里会"看起来是对的"，正是 Probe H 第 4 臂发生的事。
但臂 4 与臂 5 同样明确：**只要没人改写那一份，host 内存当捕获源完全没问题。**

**② 固定结果重放不需要 device pristine 副本，也不需要 D2D。** 臂 4 是这条的直接证据。
把"host 缓冲不能当源"写成普适规则，会让固定参数的 replay 白背一份设备存储和一次复制。

**③ 原图换参是三个对象、两次复制。** 臂 3 的形状本身就是三级的（`probe_h2_staging_update_protocol.py:136-171`）：

```
values[i]  每个版本一个独立对象，创建后再不触碰   ← 不可变版本（PreparedCall 持有）
  ──D2D──▶ source   图捕获时烧死了它的地址        ← 稳定可变槽（graph 持有）
  ──DMA──▶ landed                                ← 发布区
```

**两次复制分属图外与图内，顺序不能搞反**——这是 P1 级别的区分：

```
图外（不被 capture 记录）： 不可变版本 ──▶ 稳定槽        ← 换参改这一步
图内（被 capture 记录）：   稳定槽 ──▶ 发布区 ──▶ reset ──▶ launch
```

臂 3 的代码就是这个顺序：`source.copy_(values[i])` 在 `graph.replay()` **之外**，
图内捕获的是 `landed.copy_(source)`（`probe_h2_staging_update_protocol.py:150-168`）。
**若把「版本 → 稳定槽」也放进捕获**，图会烧死那一刻那个版本的源地址，
于是每次 replay 都把旧版本重新盖回稳定槽——图外的更新被 replay 覆盖，换参静默失效。

写成契约：**不可变版本 → 显式 update 按流序写入 graph-owned 稳定槽 → replay。**
它排掉两种错法，而"换参就改 source"这句话对两种都不排：
prepare B 另开地址（旧图仍读已烧死的旧地址，换参无效）／prepare B 就地覆盖（破坏旧版本的不可变性，并波及其他引用它的图）。
**版本载体 host／device 皆可**（臂 5 vs 臂 3）；device 槽的好处只是让 update 天然是流操作、定序免费，
代价是额外存储和一次复制——**本轮没有证明它比 `TaskUpdate` 更快**。

**④ 整图粒度的 host 栅栏能换回正确性，但会把流水抵消——注意这只是**整图粒度**的结论。**
臂 2 全对，代价是 32 轮阻塞 31 次，等于退回"host 等设备"。
**但本臂的 event 记在整次 `replay()` ＋ 输出复制之后**（`probe_h2_staging_update_protocol.py` 的 arm 2
把 `done.record(stream)` 放在 `observed[i].copy_(...)` 之后），所以它等的是**整张图加尾部复制**跑完。
源缓冲实际需要的保护范围小得多——**只到图内那次 DMA 把源读完为止**，那在图里发生得早得多。
因此不能推广成"任何 host-source fence 都会抵消流水"：**更细粒度的源读取 fence（例如在源读之后立刻 record 的 event）本探针没有验。**

## 一条附带的硬约束

**捕获节点的源地址是烧死的。** 所以"轮换 K 份源"在**一张图**里做不到——只能靠
**K 张图**（capture 内存 ×K）或 **`TaskUpdate`**。这也说明臂 2 的 host 栅栏不是"以后可以优化掉的实现细节"，
它是"单图、单份可变源"这个形状的固有代价。

## 读法与边界（探针自身的纪律）

- **臂 1 是负对照，只能单向读**：复现了就证伪"无栅栏安全"；**某次没复现不等于安全**，它是竞争。
  探针因此**只对臂 2–5 断言退出码**，臂 1 永不参与；没复现时打印一条明确的 NOTE 而不是当成 pass。
  （第一版探针无条件 `return 0`，正确臂失败也会显示成功——已修。）
- **`host_waits: 0` 只是"本探针没有发起显式 `Event.synchronize()`"**，
  **不证明** `replay()`／`copy_()`／分配器内部没有阻塞——这一层没有仪表化。
- 用的是 torch 的 `copy_` / `pin_memory` / `Event`，不是直调 `aclrtMemcpyAsync` / `aclrtMallocHost`。
- 只验了"一个 copy 节点 ＋ 一段计算"的形状，**没有**验真实的 AICPU/AICore 双 launch ＋ handshake。
- **AICPU 侧另有分类**：blob 里**按值内联**的字段 capture 后冻结，但经 `k_args->runtime_args` 这个稳定指针
  到达的数据（含 `orch_args_storage_`，在发布区内）**不冻结**——见设计文档 §0 第 16 行。本探针不覆盖那条路。
- a5 未跑（非目标 #12）。


---

# Probe K — 显式 ACL 的发布协议，以及一次对 memcpy kind 常量的复审

**结论：H2 的每一条都在"数据路径里没有 torch"的条件下复现，数字完全一样；
另外拿到三条新事实和一条负面发现。** a2a3 onboard，CANN 9.0.0，`TASK_QUEUE_ENABLE=0` 与 `=1` 结果逐字相同，各跑两轮。
复跑：`task-submit --device auto --device-num 1 --run "TASK_QUEUE_ENABLE=0 python3 probes/probe_k_explicit_acl_publication.py"`
日志：`explicit-acl-results/{tq0,tq1}.log`，机读报告 `explicit-acl-results/{tq0,tq1}.json`。

## 为什么要有这个探针

**其一，设计文档的未验 ①**：H2 用的是 torch 的 `copy_` / `pin_memory` / `Event`，
而 simpler 侧落地必须直调 `aclrtMemcpyAsync` / `aclrtMallocHost`。本探针的数据路径里**没有 torch**
（torch 只用来建 device context），捕获与重放走裸 `aclmdlRICaptureBegin/End` ＋ `aclmdlRIExecuteAsync`。

**其二，一个我自己种下的坑。** `acl_rt.h:123-131` 是

```c
ACL_MEMCPY_HOST_TO_HOST = 0, ACL_MEMCPY_HOST_TO_DEVICE = 1,
ACL_MEMCPY_DEVICE_TO_HOST = 2, ACL_MEMCPY_DEVICE_TO_DEVICE = 3, ACL_MEMCPY_DEFAULT = 4,
```

而 `probe_c:27` 与 `probe_b2:17` 都写成 `H2D, D2H, D2D = 0, 1, 2`（**整体错一位**），
`probe_i:143` 给一个 host→device 复制传了字面量 `0`。这不只是标签问题，因为**全家族最吃重的那次测量**就出自这样一次调用：

```
probe_c:155   aclrtMemcpy(host, N, src, N, ACL_MEMCPY_D2H=1)   -> 107030
```

指针是 device→host，声明的 kind（1）却是 HOST_TO_DEVICE。设计文档第 10 行把这个 107030 读成
"同步复制在捕获中被拒，但捕获仍 ACTIVE、`CaptureEnd` 仍返 0，所以必须检查 rc"。
**万一 CANN 拒的是方向不符，那 107030 就跟捕获无关，这条规则底下就没有证据。**

## Part 1 — `kind` 是强制的还是推断的

捕获之外，每个组合看 rc **并且**用正确 kind 的回读确认字节真的搬了：

| 指针 | 声明 kind | rc | 数据真的搬了 | 谁这么写过 |
| --- | --- | --- | --- | --- |
| dev ← pinned | H2D | 0 | 是 | 正确对照 |
| dev ← pinned | **H2H** | 0 | **是** | `probe_i:143`、`probe_b2:96` |
| dev ← pinned | DEFAULT | 0 | 是 | — |
| pinned ← dev | D2H | 0 | 是 | 正确对照 |
| pinned ← dev | **H2D** | 0 | **是** | **`probe_c:155`，即 107030 那次** |
| dev ← dev | D2D | 0 | 是 | 正确对照 |
| dev ← dev | **D2H** | 0 | **是** | `probe_c:134`、`probe_b2:78` |
| dev ← unpinned | H2D | 0 | 是 | 正确、非 pinned 主机内存 |

**4/4 个错位 kind 全部 rc=0 且数据正确到位；没有一例"rc=0 但没搬"。**
=> **CANN 从指针属性推断方向，`kind` 是建议性的。**

两个后果，要分开说：

- **先前所有结论都站得住。** 错位的常量没有污染任何一次测量——B′ 的 seed／回读真的搬了数据，
  C 的异步复制真的是 D2D。**错的只是 RESULTS.md 里的方向标签。**
- **107030 与方向无关。** Part 2 直接证实：用**正确** kind 的同步 `aclrtMemcpy`，**三个方向全部 107030**。
  所以第 10 行的规则成立，而且现在是建立在一次规格正确的调用上。

常量已在 `probe_c` / `probe_b2` 修正；按上表，**不需要重跑**。

## Part 2 — 捕获之中，用正确的 kind

每个用例一条新流、一次独立捕获；看 op 的 rc、之后的捕获状态、`CaptureEnd`、有没有出图：

| 操作 | op rc | 捕获状态 | End | 出图 |
| --- | --- | --- | --- | --- |
| `aclrtMemcpy` **H2D** 同步 | **107030** | ACTIVE | 0 | 是 |
| `aclrtMemcpy` **D2H** 同步 | **107030** | ACTIVE | 0 | 是 |
| `aclrtMemcpy` **D2D** 同步 | **107030** | ACTIVE | 0 | 是 |
| `aclrtMemset` 同步 | **107030** | ACTIVE | 0 | 是 |
| `aclrtMemcpyAsync` H2D，捕获流上 | 0 | ACTIVE | 0 | 是 |
| `aclrtMemcpyAsync` D2D，捕获流上 | 0 | ACTIVE | 0 | 是 |
| **`aclrtMemsetAsync`，捕获流上** | **0** | ACTIVE | 0 | 是 |
| **`aclrtMallocHost` ＋ `aclrtFreeHost`，捕获中** | **0** | **ACTIVE** | 0 | 是 |

三条新事实：

1. **同步复制被拒与方向无关**——H2D／D2H／D2D 一律 107030。先前只测过一个（且标错的）方向。
2. **`aclrtMemsetAsync` 在捕获流上可用（rc=0，能进图）。** 这一条此前没人测过，而 #2176 的
   `KernelLaunchOps::memset_handshake(context, stream)` 正依赖它 —— 我在那条 review 里说这个签名
   "correct and load-bearing"，**当时并没有测量支撑**。现在有了（再看 Part 4）。
3. **`aclrtMallocHost` / `aclrtFreeHost` 在捕获中不被拒，捕获保持 ACTIVE。**
   第 9 行／§9 的"捕获内零分配"追踪 shim 今天只覆盖**设备**侧 malloc，
   **主机 pinned 分配是个缺口**，shim 应当一并追踪这两个符号。

## Part 3 — 被拒的 op 是不是就"不在图里"

先前这是从（rc≠0、ACTIVE、end=0）**推**出来的；这次直接测。
一次捕获里放两个：一个会被拒的同步 H2D，一个合法的异步 D2D（源填 `0x22`，同步源填 `0x11`，目的地清零）。

```
sync rc = 107030   async rc = 0   status = ACTIVE   CaptureEnd = 0   graph = True
把目的地清零 -> aclmdlRIExecuteAsync -> 同步 -> 读回：全 0x22
```

**证实：被拒的 op 不在图里，`CaptureEnd` 照样成功，重放出来的图缺了那次发布。**
这就是"忽略 rc ⇒ 静默交付一张缺少参数发布的图"，从推论升级为实测。

## Part 4 — 捕获下来的 `aclrtMemsetAsync` 每次重放都执行吗

图里只有 `memsetAsync(hs, 0, N)`。每轮：**图外**在同一条 run 流上把 `hs` 涂成该轮独有的 poison
（第 i 轮 = i+1），重放，再把 `hs` 快照进**该轮专属**的设备缓冲；全程不做逐轮主机同步，最后只同步一次。

| 臂 | 判据 | 结果 |
| --- | --- | --- |
| 图里 memset `hs` | 每轮 `hs == 0` | **32/32 对** |
| 图里 memset 另一块，不碰 `hs` | 每轮 `hs ==` 该轮 poison | **32/32 对** |

两个臂判据**不同**，这才是有区分力的地方：正臂证明"捕获的 memset 每次都跑了"，
对照臂证明"poison 确实落了地"——**光看正臂的零值，无法区分"memset 跑了"和"poison 根本没写进去"。**

> **探针自身的一处错（已修）**：第一版把对照臂的门限写成"必须每轮都错"，
> 而它的 `want` 本来就是 poison，于是正确的对照臂报成失败。判据本身没问题，**是断言的极性反了**。

## Part 5 — 两种发布形状，显式 ACL，逐轮核对（未验 ①）

图＝\[192 MiB 的 D2D（把流塞住，让载荷复制在主机返回时真的还悬着）\] ＋ \[4 KiB 载荷复制\]。
逐轮快照进专属缓冲，最后只同步一次。

| 臂 | 形状 | 错 | 主机阻塞 |
| --- | --- | --- | --- |
| **5a** | pinned host 映像 → H2D，每轮在上一次重放的 event 上栅栏 | **0/32** | **31/32** |
| **5c** | 同上，去掉栅栏（负对照） | **31/32** | 0 |
| **5b** | 稳定 device slot → D2D，版本在图外、同一条流上推进去 | **0/32** | **0** |

**未验 ① 关闭。** 三个数字与 H2 的 torch 版**逐一相同**（危险臂 31/32、host 栅栏 31 次阻塞、device slot 零阻塞），
所以 H2 的结论不是 torch 的产物，**显式 ACL 下成立**：

- 设计选的形状（**稳定设备 slot ＋ 图外流内 D2D 推版本**）在裸 ACL 下正确且主机零阻塞；
- host staging ＋ 栅栏也正确，代价就是那 31/32 次阻塞；
- 无栅栏的 host staging 是真竞争，32 轮错 31 轮。

> **又一处探针错（已修，且它差点让 5a 变成假证据）**：第一版用
> `aclrtQueryEventStatus(...) != 0` 当"未完成"。但 `aclrtEventRecordedStatus` 是
> **`NOT_READY = 0`／`COMPLETE = 1`**，**与 `aclrtEventWaitStatus` 的 `COMPLETE = 0` 极性相反**。
> 于是栅栏在"已完成"时才等、在"还悬着"时跳过 —— 5a 悄悄变成了 5c 的副本，
> 报出 `wrong 31/32, host_waits=0`。**`host_waits=0` 是那个 bug 的指纹**：
> 前面挂着 192 MiB 的 DMA，正确的查询不可能一次都不等。
> 结论：**凡是"没等"的计数，都要先怀疑判据的极性，再怀疑设备。**

## 影响到的地方

| 处 | 原文 | 改成 |
| --- | --- | --- |
| 第 10 行／§16.4 | 107030 出自一次方向标错的调用 | 三个方向都 107030，与方向无关；规则不变，证据变实 |
| 第 9 行／§9 捕获内零分配 | 只追踪设备 malloc | **`aclrtMallocHost`/`aclrtFreeHost` 在捕获中不被拒**，shim 要一并追踪 |
| 未验 ① | 未验 | **关闭**：显式 ACL 下 H2 三个数字全复现 |
| #2176 的 `memset_handshake(context, stream)` | 我说"correct and load-bearing"，无测量 | 现在有：`aclrtMemsetAsync` 可捕获，且 32/32 次重放都执行 |
| Probe C/B′/I 的方向标签 | H2D/D2H/D2D 错位一格 | 标签已改；**测量结论不受影响**（Part 1） |

## 观测边界

单进程、单设备、CANN 9.0.0、a2a3 onboard、纯 memcpy/memset 型负载。
**没有**真实的 AICPU/AICore 双 launch ＋ handshake —— Part 4 验的是"`aclrtMemsetAsync` 能被捕获并每次重放"，
**不是**"handshake 的最小失效区"（那仍是未跑的 Probe E），也不是 AICPU 侧的 cache 可见性。
5a 的 31/32 次阻塞是"本探针这种粗粒度栅栏"的代价，**更细粒度的源读取 fence 仍未验**（同 H2）。
a5 未跑（非目标 #12）。


---

# Probe L — 队列饱和时 enqueue 是阻塞还是返错

**结论：阻塞，不返错。每条流 1792 个任务槽，第 1793 次 enqueue 不返回；1792 以内全部可恢复；捕获中没有这个限制。**
a2a3 onboard、CANN 9.0.0。`TASK_QUEUE_ENABLE=0` 与 `=1` 下裸 ACL 三臂完全一致，
**只有走 torch 的那臂在 TQ=1 下变了**（见下）。
复跑：`task-submit --device auto --device-num 1 --run "TASK_QUEUE_ENABLE=0 PROBE_L_ARM_TIMEOUT=40 python3 -u probes/probe_l_queue_saturation.py"`
日志 `queue-saturation-results/{tq0,tq1}.log`，机读 `{tq0,tq1}.json`。

## 问的是什么

这是 program 自然排队那条轨的**承重未验项**。`run-pipeline-redesign.md` §12 原文：

> 实际 launch 遇队列压力是阻塞还是返回哪类错误 —— "靠 CANN 自动排队承接背压"的可行性**全压在它上面**，本轮无设备证据

**Probe F′ 答不了这一问**：它灌 4000 个 op 只验**保序**，从未驱动到拒绝，
而且那个 4000 是 **enqueued 深度**，不是在飞 run 数。

## 方法：闸住流头，否则灌多快就排空多快

不让任何任务退役，队列才会涨。所以用 `ACL_EVENT_EXTERNAL`（允许 record 之前先 wait，
这是能做出闸门的前提；Probe H 用 torch 的 `ExternalEvent` 是同一个理由）闸住流头，
在第二条流上 record 来释放。

**闸门自检先于任何结论**：在被闸住的流上 record 一个 event，它必须读到 `NOT_READY`。
读到 `COMPLETE` 就说明闸门没闸住 —— 那种情况下"排空的流"和"无限队列"长得一模一样，
所以报 INCONCLUSIVE 而不是"没找到上限"。

**阻塞怎么判**：不是用标志位，而是**每次 enqueue 计时** —— 返回 0 但耗时很久＝背压，
返回非 0＝拒绝，这是对第一问的两个不同答案。

## 结果

| 臂 | 任务类型 | 闸住 | 结论 |
| --- | --- | --- | --- |
| under_limit | `aclrtMemcpyAsync` D2D，**停在 1792** | ✔ | **1792 个全部接受、不阻塞**；释放后 `queued_work_ran=True`、`stream_reusable=True` |
| memcpy | `aclrtMemcpyAsync` D2D，灌到底 | ✔ | **第 1793 次 enqueue 不返回**（40 s 未归） |
| memset | `aclrtMemsetAsync`，灌到底 | ✔ | **同样是第 1793 次** |
| torch | `torch.add`（kernel 任务） | ✔ | TQ=0：**第 1793 次**；TQ=1：**第 5889 次** |
| capture | `aclrtMemcpyAsync` D2D，**捕获中** | 不需要 | **20 万个也没有上限**，中位 4.5 µs，捕获保持 ACTIVE、正常出图 |

### 四个读数

**① 答案是"阻塞"，不是"返错"。** 没有 `WouldBlock`、没有任何 rc ——
调用线程直接停在 `aclrtMemcpyAsync` 里不回来。**这意味着实现侧无法靠检查返回值来发现队列满**，
它只会把调用线程拖住。

**② 每条流 1792 个任务槽，与任务种类无关。** DMA 复制、DMA memset、kernel launch
三种任务**同一个数字**，所以这是流的槽位数，不是某类任务的配额。

**③ 1792 以内完全可恢复。** under_limit 臂释放闸门后：排队的工作**全部执行**（目的地读到 `0x5A`），
流**继续可用**（再发一次新的 copy＋同步，结果正确）。所以饱和不是损坏。

**④ TQ=1 把 torch 那臂推到 5889 —— 正好 `1792 + 4096 + 1`。**
`TASK_QUEUE_ENABLE=1` 时 torch 自己先在**主机侧**缓冲（4096 条），再下发到流；
裸 ACL 三臂不经过它，所以 TQ=0／1 完全一致。**框架的队列能加长吸收段，但终点行为不变**——
仍然是同一个阻塞。

**⑤ 捕获中没有这个限制。** 捕获时任务是**记进图**而不是排进流，所以不存在队列压力；
20 万个节点、每个 4.5 µs、`CaptureEnd` 正常出图。第五问答完：**这条限制只作用于 eager 提交。**

## 对设计的影响

| 处 | 原文 | 改成 |
| --- | --- | --- |
| 第 13 行"约束在飞数的预算" | "耗尽时**背压或返回 `WouldBlock`**，不是拒绝" | **实测是阻塞，没有 `WouldBlock` 这个选项**；数字是每流 1792 |
| "靠 CANN 自动排队承接背压" | 可行性未知 | **可行，但背压形态是阻塞调用线程** —— 任何"提交后立刻返回"的契约（如第 14b 行 `submit` 必须已入队）在队列满时会**卡住调用者**，这一点必须写进契约而不是留给实现发现 |

## 探针自身的三次返工（都记下来）

1. **第一版没有看门狗 → 自锁**：闸门保证队列永不排空，于是阻塞的 enqueue 永远等不到空位，
   进程被 harness 在 300 s 杀掉、**stdout 还在缓冲里，一个字都没留下**。
   **一次挂死不是"阻塞"的证据，是"实验设计坏了"的证据。**
2. **第二版把看门狗放进进程内 → 仍然挂死**：释放闸门要走同一个 runtime，
   而调用线程正卡在里面。**超时必须在进程外**，所以改成每臂一个子进程加硬超时。
3. **第三版能报"HANGS"了，但分不清是第 1 次还是第 N 次挂的**，
   而这正是第三问。改成**把深度写进磁盘文件**（`note()`，带 `fsync`），
   子进程被杀后驱动仍能读到"卡在第几次"。
   同时补上归因门：**闸门没闸住、或没跨过前几次 enqueue 的挂死，一律判失败而不是当答案**——
   torch 臂第一次跑就是这样被抓住的（它当时没写进度文件）。

> 还修了一处**又一次差点变成结论的**报告 bug：三个臂挂死时，汇总仍打印
> "PASS — every gate held and every saturated stream stayed usable"。
> 挂死的臂是被杀掉的，**关于第四问它什么都没证明**。现在 PASS 只声明"每臂都给出了可归因的判定"，
> 并单独列出哪些臂挂死、以及它们不覆盖第四问。

## 观测边界

单进程、单设备、一条流。**没测**：多条流是否共享这 1792（很可能不是，但没测）、
a5、多线程并发 enqueue 同一条流、以及队列满时**其他线程**的 ACL 调用是否也被拖住。
1792 是这台机器这个 CANN 版本的观测值，**不是 ABI 承诺**。


---

# Probe M — op-execute 超时预算被钳制在哪里，朝哪个方向

**结论：有下限，就是 4.294967 s（＝2³² ns）。低于它的请求一律被抬上去。**
**而且 CI 今天的配置本身已经失序**：五个 workflow 都是 `scheduler=2000ms / op_execute=3s / stream_sync=4000ms`，校验通过，生效后 **op_execute=4.295 s > stream_sync=4 s** —— 不需要构造反例，仓里就有一个。
全部 12 行数据吻合同一个模型：

```
effective = min(254, ceil(requested / 4.294967 s)) × 4.294967 s
```

也就是一个**以 2³² ns 为单位计数的 32 位纳秒寄存器，有效单位数 1..254**。
a2a3 onboard、CANN 9.0.0。复跑：`task-submit --device auto --device-num 1 --run "python3 -u probes/probe_m_op_timeout_clamp.py"`，
机读 `queue-saturation-results/probe_m.json`。

## 先记自己的一次错判 —— 这次是探针在发表前抓住的

`run-pipeline-redesign.md` §12 写「三臂都回报 `actual = UINT32_MAX µs`……**若钳制是下限，
则低于约 4.29 s 的 `SIMPLER_OP_EXECUTE_TIMEOUT_US` 静默失效，含 CI 的 2 s**」。
（原文里的"CI 的 2 s"不准确，**CI 实际请求 3 s** —— 五个 workflow 均为 `3000000`；我第一版把这个数字照抄了，没有核对，已改。结论不变：3 s 仍在下限之下，被抬到 4.295 s。）

我写这个探针时认为**它有单位错误**：`aclrtSetOpExecuteTimeOutV2` 的两个参数都是微秒
（`acl_rt.h:2629-2630`），所以 `UINT32_MAX µs` ＝ 4 294 967 295 µs ＝ **4294.97 秒**，不是 4.29 s；
而且那么高的下限会让全树预算（含 45 s 默认值）统统失效，与它自己臂 B（请求 3 s、3.93 s 被收割）矛盾。

**实测：下限真实存在，就是 4 294 967 µs ＝ 4.294967 s** —— 正是 UINT32_MAX **纳秒**换算成 API 的微秒。
**所以它的数字是对的，只有标签错了一个单位；而我的结论（"不能读成 4.29 s 的下限"）是错的。**
记在这里而不是悄悄改掉，因为这是"结论跑在证据前面"的又一次，只不过这次探针在发出去之前抓住了。

## 数据

| 请求 | rc | 实际生效 | 说明 |
| --- | :-: | --- | --- |
| 1 µs | 0 | 4 294 967 | **抬高 ×4.29e6** |
| 1 ms | 0 | 4 294 967 | 抬高 ×4294 |
| 100 ms | 0 | 4 294 967 | 抬高 ×42.9 |
| 500 ms | 0 | 4 294 967 | 抬高 ×8.59 |
| 1 s | 0 | 4 294 967 | 抬高 ×4.29 |
| 2 s | 0 | **4 294 967** | **抬高 ×2.15** |
| 3 s ← 那份文档臂 B | 0 | 4 294 967 | 抬高 ×1.43 |
| 4.294967 s | 0 | 4 294 967 | **恰好被采纳** |
| 5 s | 0 | 8 589 934 | ＝ 2 个单位（不是 5 s） |
| 45 s ← 全树默认 | 0 | 47 244 640 | ＝ 11 个单位 |
| 4294.97 s（UINT32_MAX µs） | 0 | 1 090 921 693 | **压低**到 254 个单位 |
| UINT64_MAX | 0 | 1 090 921 693 | 同上 —— **上限也存在**（≈1090.9 s ≈ 18.2 min） |

12/12 全被接受（没有一个返错），所以**唯一的信号是 `actual` 这个回读值**。

## 真正的后果：三层次序的校验校的是"请求值"，不是"生效值"

`validate_runtime_timeout_order`（`runtime_timeout_config.h:177-192`）校验的是 `cfg.op_execute_timeout_us`，
即**请求值**；钳制发生在之后的 `configure_aicore_op_timeout`（`device_runner_base.cpp:680-694`），
而那里只有一行 **`LOG_INFO`** 记录 `requested=… actual=…`。

于是 `scheduler < op_execute < stream_sync` 这条不变式**可以在"生效"层面被静默破坏**。构造很容易：

```
scheduler = 1000 ms
op_execute = 2 000 000 µs (2 s)      ← 校验通过：1 s < 2 s < 3 s
stream_sync = 3000 ms                  且 3000 > 1000 + 1500（guard）
```

生效后 `op_execute = 4.295 s > stream_sync = 3 s` —— **中层跑到了上层之上**，
而这条次序存在的目的正是"算子看门狗必须早于主机放弃 stream sync 触发"。
校验通过、日志只有 INFO、行为已经反了。

**所以这条不是"CI 的预算偏保守"那么轻 —— 它现在就是错的**：

| | 请求 | 生效 |
| --- | --- | --- |
| scheduler | 2.000 s | 2.000 s |
| op_execute | 3.000 s | **4.295 s** |
| stream_sync | 4.000 s | 4.000 s |

五个 workflow（`_st-npu-a2a3`／`_st-npu-a5`／`_st-network1`／`_ut-npu-a2a3`／`_ut-npu-a5`）全是这一组。**主机比算子看门狗早约 295 ms 放弃** —— 任何表现为 stream-sync 超时而非算子收割的 CI 失败，读到的都是错的那一层。deepseek 那个 workflow 侥幸没倒（10 s／45 s→47.24 s／50 s），但余量只有 2.76 s 而不是数字看起来的 5 s。

任何把 `op_execute` 设在 4.295 s 以下的配置，其三层次序都只是名义上的；**而且钳制不止一个下限**——量化在下限之上同样咬人（5 s→8.59 s），所以只认下限的校验会放过 `op_execute=5s / stream_sync=6s`（生效 8.59 s vs 6 s，又倒了）。**只有回读 `actualTimeout` 能抓住这一类。**要么校验改成校验**生效值**（先问 ACL 再校验），要么把下限写进校验并拒绝。

## 仍未答的一问

那份文档臂 B 请求 3 s，**在 3.93 s 被收割**，而 3.93 s **低于** 4.295 s 的下限。
两者不能同时成立于"算子看门狗按生效值收割"这一个解释下。可能的解释（本探针都没验）：
那次收割来自**另一层**（`stream_sync` 或 scheduler 层），或者设备侧的实际行为与 `actualTimeout` 的回报不一致。
**本探针只测了"运行时接受什么"，没测"何时真的收割"** —— 后者要一个跑得比所有预算都长的算子并扫描请求值。

## 观测边界

单进程单卡、a2a3、CANN 9.0.0。`aclrtSetOpExecuteTimeOutV2` 是**进程级**设置，本探针按顺序改了 12 次，
没有验证"后一次是否覆盖前一次"（假定覆盖）。a5 未测。4.294967 s 与 254 是这台机器这个版本的观测值，
**模型拟合得很干净但不是 ABI 承诺**。
