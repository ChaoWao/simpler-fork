> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# PR #2568 review — 46d43741c

## 0. 结论

**当前静态审查未发现明确的代码阻塞项；CI尚未闭合，暂不标Ready或建议立即合入。** 该PR交付固定步数、单A3 HBG端点的Qwen连续DEVICE反馈验证，不改runtime容量，不代表通用动态请求、四后端或DFX已经完成。

审查head：`46d43741c1e32f30897af56587682da28b63653c`；真实merge-base：`c5cdd1b37a2e1907feeeceb82b4e70939c9c96d0`。新拉取main为`852f6ff169f8b8a32e327c3c2ad0c105e1b5f9e3`，已包含#2567；审查使用merge-base→PR精确head，不混入main后续改动。未修改作者分支、运行测试或代发评论。

## 1. 背景与问题

先前Qwen验证主要覆盖独立请求，或把采样token读回host后才提交下一轮。后者难以证明真实自回归依赖下的提前入队。本PR关联#2429，明确只完成其同请求DEVICE反馈阶段；没有把整个Issue收口。用户既有要求以普通stream异步演进为主，vLLM/capture不是本PR前置或交付。

例：A产生token给B；A完成后读取A的logits，在等待B完成前提交C，C直接引用B的DEVICE token。设备执行仍是A→B→C串行。该行为需要同时保持A/B/C结果归属和内存寿命，不能用host预知的golden token代替实际反馈。

## 2. 方案与理由

- `_run_device_chain`最多保留两个pending handle。满额先等最早handle、验证其HOST logits，再prepare/submit后继；提交顺序单线程，设备排队由既有Worker/runtime保证。
- 元数据、HOST logits、DEVICE next_hidden各两帧轮换；只有所属run完成并读完logits后才重写。每步sampled_ids独立分配并预置-1，前驱输出直接绑定为后继输入，直到全部消费者结束和最终host审计才释放。保留全部小token缓冲符合固定步数审计目的，空间随steps增长，不能称作无限连续服务的常量内存方案。
- `compile(seed_outputs=("out",))`仅在该验证显式使用时将OUT改INOUT，把NaN写入HOST logits对应设备存储；缺失写入不能被旧输出掩盖。原签名不原地修改，缓存key区分seeded模式。
- 原生trace核对dispatch/slot/epoch、joined观测及设备边界。同设备/频率、正时间戳、前驱结束不晚于后继开始才接受。允许真实串行边但明确记录fallback；缺失证据、全串行、没有初始pair之外的joined refill都会失败。
- 接受、prepare-only、enqueue、成功completion和retirement分别记账；失败仍保留已接受run IDs，未证明enqueue记未知；成功Worker.close后才记retired。失败先尝试排干已有handle，再走Worker.close；close失败不主动关闭HOST buffers。Worker本身仍持有create_buffer登记，未发现新增提前释放路径。
- 没有runtime/C ABI/IPC布局变更，没有新增版本协商。`result.json`是该资格验证脚本的审计产物，不是正常Worker.submit依赖的控制文件。

## 3. 实现与声明核对

| 声明 | 实现 | 位置 | 结论 |
| --- | --- | --- | --- |
| 真实前驱DEVICE token反馈 | previous sampled_ids直接入参 | device_feedback_worker_submit.py:57,320 | 符合 |
| 先读最早结果再补队 | 两pending循环与finish | 同文件:67–99 | 符合 |
| 两帧复用、token保留 | 分开frame与per-step存储 | 同文件:119–166 | 符合限定范围 |
| 缺失写入被发现 | -1与NaN哨兵、seeded签名 | 同文件:143,320；callable_bridge.py:31 | 符合 |
| 每条边具备设备串行证据 | epoch+clock+boundary核验 | clone_worker_submit.py:139–262 | 符合 |
| 不把所有回退算成功 | joined及refill计数要求 | device_feedback_worker_submit.py:357–362 | 符合，不要求每边都joined |
| 失败关闭与报告 | 排干、保留首错、close后退休 | 同文件:89–99,380–438 | 静态路径与CPU用例覆盖 |
| 127步真实硬件通过 | PR/Issue给出任务和结果路径 | 作者报告 | 未独立复跑或读取远端原始artifact[1] |

[1] 作者报告127步、126条joined边、125条补队边、0fallback，以及tokens/logits/KV均通过。代码支持这些检查，但本次review没有该硬件artifact的独立本地副本；不能将作者报告写成manager实测。CPU新增用例覆盖1/2/3/127步、失败/超时、缺失写入、重复token、frame地址空间隔离及trace缺失；mock不能替代设备验证。

## 4. 剩余问题与CI

**Must-fix代码问题：本次未发现可由源码确认的新增缺陷。** 不把限定的固定步数方案要求成通用动态资源实现；该目标由另一条原作者工作流负责。

合入前仍需完成CI处置，当前汇总（含CodeRabbit）为17成功、1跳过、1失败、1取消：

- **A3 UT job115666309382**：日志首个失败为device_memory_info的TMR L2初始化，在device10 BootstrapDispatcher同步返回507018，后续kernel lifecycle测试也有同设备初始化失败。至少有一组日志汇总16失败、10通过；这不是本PR Qwen反馈断言失败。此次7文件差异未改相关runtime或这些硬件UT执行路径，未找到直接因果联系；507018根因仍未确认，不能据此断言坏卡或偶发。
- **A5 UT job115666309451**：任务04:22:30提交设备队列，约04:46:27才开始测试；04:52:08被取消。最后一组kernel UT已到44%，已显示部分通过，cann-examples跳过。工作流限制30分钟，job从04:21:34到取消约30分34秒，排队耗掉约24分钟；这与job总时限耗尽相符（推断），不是全部测试通过。原作者应让该job取得完整结论。
- Ubuntu/macOS UT与A3/A5 onboard等当前成功。没有以它们代替上述缺口。本次无CI重试、无设备测试。

**后续边界**：动态请求准入、逐消费者更早回收token、资源预算、DFX、其他后端和capture未交付；PR正文已明确限定，因此不是隐瞒功能缺失。新main #2567涉及native caller-stream，后续集成时仍应以实际CI基线核对，不自动要求无冲突PR为追main重跑。

## 5. 合入建议

问题真实 → 两帧补队与真实DEVICE反馈方法成立 → 代码与固定范围声明一致 → 静态审查可接受。**CI验证链尚未闭合，先由原作者处置A3初始化失败和A5未完成，再确认合入。** 不需要为了这次review拆分成小功能PR。

## 附录A：改动规模

| 分类 | 文件 | 新增 | 删除 | 总改动 |
| --- | ---: | ---: | ---: | ---: |
| Core | 0 | 0 | 0 | 0 |
| Build | 0 | 0 | 0 | 0 |
| Test/Examples | 6 | 1168 | 39 | 1207 |
| Docs | 1 | 38 | 0 | 38 |
| Uncategorized | 0 | 0 | 0 | 0 |
| 合计 | 7 | 1206 | 39 | 1245 |

总改动超过1000行，审查与冲突负担较大，但全部为一项资格验证及回归，符合用户一个完整功能一个PR的要求。建议阅读顺序：补队脚本 → bridge签名 → trace证据 → CPU负例 → README。未要求按容量或单个断言拆PR。

## 附录B：pto-isa

固定pin：`418de18dde8fa33773dc5c384fdfe5506463b628`。本PR不改变pin或pto头文件引用，无需据此要求升级；仅记录现有固定依赖。

## 附录C：审查方法

仅manager静态审查及远端CI日志核对。未启用外部CLI复审、未新增重复作者、未跑本地测试、未发表GitHub评论。
