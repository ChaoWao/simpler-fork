# TMR 调用参数进入 launch：可审批合同

设计归档快照：状态与阅读顺序见[索引](../README.md)。设计约束、历史合同和当前实现须分别阅读。

状态：用户已批准完整载体与发布边界合同，授权原 dynamic 实施并交付一个 PR。原5个短前缀WIP保留并按本合同整合；不得单独发布旧方案。

## 目的与本次交付

交付一个 PR：a2a3/a5 TMR program 普通调用，把本次 tensor **描述信息**（设备地址、shape、stride、type）与 scalar 按实际数量打包到 AICPU launch 参数，由 RTS 复制。这里不复制 tensor 数据，不改变 tensor 数据 owner。设备将参数原始字节复制到既有、已对齐的 entry 存储，再执行 orchestration；派生 task 的元数据继续按既有路径物化。

工作区、握手、注册期代码/函数表、结果和 DFX 仍按各自寿命管理。仍有一份192 B描述符上传用于本 run 的执行配置与资源引用，不能宣称已经删除全部独立 H2D 或固定 descriptor 分配。

## 明确的 prepare / launch 合同

A3 的实际 run stream 在 launch 持有执行权后才由 ensure_run_streams 准备；不能提前到 prepare，否则可能替换前驱仍使用的 stream。slot 预约也不等于 stream 执行权。

本 PR 接受以下边界变更；这替代原作者“只移动一次函数调用、其他状态不变”的表述：

1. **Prepare**：继续分配/核对 slot 的设备范围，捕获本 run 的独立主机快照和全部实际入口值。保存 fallback 需要的字节；首次初始化还保存原 initialized-prefix 的完整已初始化范围。预留 launch 包的主机容量。TMR 此时状态是 Prepared，设备描述符尚未 Published；不查询或替换尚未取得执行权的 run stream。
2. **Launch**：取得既有执行许可、准备实际 stream 后，使用这次 launch 将使用的同一个 AICPU stream 查询 aclmdlRICaptureGetInfo。仅查询成功且 NONE 使用 launch 包；ACTIVE、INVALIDATED、未知或查询失败均选 descriptor 路径。查询不导致隐式重试。stream 准备失败仍是启动失败，不是可以继续 launch 的 fallback。
3. **确定本次发布**：从 prepare 捕获的 owner 中构造两个路径之一；允许补写该 owner 的 source/count 控制字段和选择长度，不能回读可能已变化的调用方 Runtime 来重新快照。参数、计数、上传标记和 launch 包必须来自同一 run。描述符路径每次都发布当次实际参数，不能靠首次初始化代替。
4. **单次发布后提交**：在 activate_launch_shape、DFX arming、任何本 run 的 kernel/event 提交之前完成唯一一次描述符 H2D。首次初始化仍发送完整 initialized-prefix；复用时 envelope 路径192 B、descriptor 路径1280 + 128×T B。只有复制成功才标 Published、提交 workers 初始化事实；然后按原顺序 launch AICore/AICPU。HBG 保持原 prepare-time 发布。
5. **状态与失败**：TMR 的 launch 入口必须接受有效 Prepared，而不能被现有 runtime_args_published 前置检查永久拒绝；真正提交 kernel 前必须是 Published。快照消费一次，禁止重复 publish。发布失败保留原错误和 NotStarted，禁止 kernel 提交，走现有未启动资源收尾；不能返回默认空结果掩盖复制错误，也不能把失败快照当可再次发布。沿用既有 slot/执行许可/退休规则，无新并发准入。

调用方不得在内部 stream 的一次 launch 序列中并发改变 capture 状态。查询不赋予 graph 参数寿命保证；保留已有 kernel/persistent 与捕获路径行为，不新增 capture 支持或参数更新能力。

**代价**：TMR 的一次描述符上传进入 launch 路径，失去它与前驱执行重叠的机会；prepare 的构图、分配、快照仍可进行。新增 capture 查询和状态转换工作。这不是无成本的函数移动，不承诺延迟或吞吐收益。P4 提前入队/租约机制不改，但此边界是与主线接手方相邻的接口，发布时需写清兼容条件。

## 布局、来源和设备错误处理

- common KernelArgs 增加4个 uint32_t：entry offset/T/S/source。A3 从120增到136 B，A5从112增到128 B；envelope统一192 B头，随后128×T原始tensor字节、8×S scalar字节。T≤256、S≤128；host窄化/算术检查在上传前完成。
- TMR descriptor 将 prebuilt metadata 提前，source/count位于124/128/132，entry区域起点192；workers、teardown_gates、extent和首次初始化边界保持当前 main 的数值。EntryArgsStorage 改为TMR私有值布局，不改HBG共享TaskArgsTpl。三端必须一致重建，不支持新旧布局混用。
- RTS的base不假定64 B对齐。只按现有KernelArgs基础对齐读取头；验证source一致、offset、count边界及副本count一致后，才形成payload地址，并 memcpy 到既有64 B对齐目标。不得将RTS原始payload转为Tensor*解引用。
- 仅orchestrator线程在第一次create_from_entry_storage/config读取前解码。失败时使用实际 pre-runtime 错误路径：发布runtime_init_ready，返回非零thread rc，scheduler看到rt为空跳过dispatch，全部线程仍走正常核退出与completion gate，终态使用既有ThreadRc。不能在rt/SM尚未wire时调用orch_mark_fatal，不能直接从platform入口逃过核收尾。
- 计数比对只发现不一致，不证明同count数据的新鲜性；不新增payload完整性保证。
- sim与kernel/persistent保留descriptor来源。HBG语义不变，但共享KernelArgs实际增加16 B，新增字段默认0，平台setter使用HBG空实现；要覆盖其构建/链接兼容性，不能声称其二进制未改变。

## 字节与内存成本

以下是复用run的原始传输字节，非耗时、非分配器实际保留容量：

| 场景 | 旧A3 / A5 | 新envelope路径 |
| ---- | --------- | -------------- |
| 4 tensor + 2 scalar | 34168 / 34160 B | 912 B（192 B描述符 + 720 B launch包） |
| 满容量256 tensor +128 scalar | 34168 / 34160 B | 34176 B（分别多8 /16 B） |
| 稳态描述符本身 | 34048 B | 192 B |

首次初始化仍完整上传原语义范围，不能套用稳态数字。descriptor fallback为1280+128×T B，再加136/128 B KernelArgs。

- 每个slot新增grow-only主机参数buffer，原始请求最多33984 B，受slot独占预约保护；新增主机打包和AICPU解包复制。prepare快照也须保留fallback所需字节，不宣称它缩成192 B。
- simpler原设备descriptor分配仍保留，不能计为设备内存节省。RTS同时拥有另一份参数副本；最大原始请求增长33864 B（A3）/33872 B（A5）。池的实际保留容量可能更大。
- v9.0.0源码的PCIe/UMA存在4096/6208/15360/47168容量级别及动态fallback；UB池到16384 B，超过后会额外分配device、host及tseg元数据并在回收时释放。不能承诺底层分配次数不增加或H2D调用耗时更低。
- 固定源码版本3542a202的复制/回收证据已审：950/CHIP_DAVID强制同步策略；UB先复制到RTS主机owner；v100另有提交内复制完成等待。这是版本源码证据，不是安装binary哈希或硬件实测证明。

## 验收与授权限制

manager 在实现后按精确head审查，至少覆盖：prepare后源Runtime变化仍发布原run值；Prepared→Published门禁可达；双路径均单次上传；首次初始化/大→小复用；query未知fallback；发布失败无kernel提交、原错误不丢、无二次publish；不对齐源字节解码；解码错误的真实正常退出路径；HBG默认字段与kernel/sim旧路径。可新增有意义的精确case首次执行；既有或已执行用例/CI重试仍需单独授权。无硬件探测、无自动merge。

细节依据：[原作者完整源证据与布局](../README.md#archive-boundary)（路径由工作区详情打开）、[manager复核](../README.md#archive-boundary)。本页上述快照、状态与错误合同优先于原报告“仅移调用”的不完整说法。

## 可选决定

- 批准本合同：原dynamic交付一个PR，包含launch载体、延后单次发布以及相应状态/快照调整。
- 仅恢复已批准短前缀：沿用[原合同](../README.md#archive-boundary)，4 tensor描述符1728 B，无capture查询、无KernelArgs扩大、无发布边界迁移；它是有界过渡，后续RTS载体需再次布局重建。
- 暂不实施：保留WIP，等待新的具体方向。
