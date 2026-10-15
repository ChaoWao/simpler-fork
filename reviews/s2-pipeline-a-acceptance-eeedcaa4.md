# S2 同步发布与 Pipeline A 接收门槛

核查基线：main `eeedcaa4a4afcd22e18f7c2d636b2f9a6ba8b5d7`，2026-09-19。
原则仍以 [runtime-design-principles](../runtime-design-principles.md) 为准；本文是代码核查和验证记录。
本地工作分支 `local/s2-hbg-publication-acceptance`；仅补测试，不修改生产代码，不 push、不创建 PR。

## 结论

S2 的交付边界是**单次准备结果的同步发布合同**。#2342/#2353/#2358/#2367 已交付生产机制；本次补齐 A5 resident 与两种 legacy 回退之间的 host 发布验证。

原路线把异步 source 保活也列成 S2 退出门，随后又要求 K1 依赖 S2、C1 依赖 kernel eager，形成倒置。修正为：S2 保活到同步复制返回；K1 使用已有准备／发布接口接入共同执行；C1 才将它替换为带完成寿命的异步命令。生产 descriptor prepare/publish 仍相邻，仍不宣称该 seam 已带来重叠或性能收益。

A5 legacy 的设备终态是另一条合同：它影响移除正常 drain 的整流同步，归 Pipeline A/P2。host 发布测试不能代替它。

## 逐项代码核查

| 验收项 | 代码主键 | 结论 |
| --- | --- | --- |
| HBG 构图与 metadata copy 分开 | 两架构 `runtime_maker.cpp`：`bind_callable_to_runtime_impl` / `publish_run_image_impl` | bind 保存本次源；publish 逐项检查返回码并消费 pending record |
| image 发布失败不进入执行准备 | common onboard `c_api_shared.cpp`：`simpler_prepare_run` / `cleanup_failed_prepare` | publish 失败立即清理；不会继续 `prepare_execution` |
| descriptor 不是“非空即就绪” | `KernelArgsHelper::publish_runtime_args` / 两架构 `DeviceRunner::launch_execution` | #2367 的 Published 状态必须属于本次调用；真实入口已有 CI 拒绝测试 |
| 正常 drain 仍依赖整流同步 | `DeviceRunnerBase::wait_run_fence` | 两 event 完成后仍调用 `sync_stream_pair`；P2 尚未收口 |
| native 仍单 launched | `DeviceRunnerBase::try_acquire_native_run` | `active_native_run_` 从 null CAS 成本次 owner；P4 未交付 |
| Python 仍单 active run | `Orchestrator::activate_fifo_head` | `active_run_id_ != INVALID_RUN_ID` 时不激活后继；P5 未交付 |
| 设备健康通知只是证据 | `consume_device_fault_notices` / `DeviceHealthState` / `try_acquire_native_run` | #2354 已合入记录与代际隔离；尚未把 suspect 接成停止准入、恢复消费者 |
| kernel 入口仍拒绝 | `simpler_kernel_mode_launch` | 参数检查后返回 INVALID_STATE；K1/K2 未交付 |

引用采用固定 commit 与符号名；不把其他分支的实现当作 main。

## A5 HBG 发布矩阵

使用真实 `bind_callable_to_runtime_impl`、`publish_run_image_impl` 和 `release_run_bindings_impl`，HostApi 的分配／复制是可检查的 host 内存后端。普通任务提供合法的 callable 元数据，实际经过 scheduler 构建；不是只测空图。

| 构图结果 | A2/A3 发布区域 | A5 发布区域 | A5 worker 的 scheduler 引用 |
| --- | --- | --- | --- |
| 普通单 AIV 任务 | image | resident scheduler → image | 指向本次 scheduler region 内的 context |
| GRAPH 任务 | Definition → image | Definition → image | legacy，task 地址为 0 |
| MIX 任务 | image | image | unsupported-shape legacy，task 地址为 0 |

这些实际路径并不总是同时具有 Definition 和 resident scheduler。GRAPH 使 A5 选择 legacy；不能以三段“统一顺序”的抽象表代替分支验收。

新增两项测试，两架构都运行：

- `SchedulerModeChangesPublishOnlyThisRunsSources`：同一个 Runtime 依次普通→GRAPH→MIX→普通；bind 不做 metadata copy；本次源返回后仍有效；publish 的目的、顺序、长度与全部字节匹配；A5 模式标记区分三种路径，legacy 不携带旧 scheduler 地址；release 清空 run-owned allocation 和 worker 引用。
- `SchedulerPublicationFailureAllowsFreshModeSelection`：对三种形状的每个实际发布位置注入 copy 错误；后续写入停止、record 被消费、重试拒绝、释放无残留；随后同一 Runtime 以另一种模式重新 bind/publish/release 成功。

这不验证设备执行、cache 可见性、A5 legacy 终态、提前排队或 replay。

## 负对照

只在本地临时修改 production runtime_maker，构建并运行新增测试，随后原样还原源码。

| 临时删除 | 预期与观测 |
| --- | --- |
| resident scheduler 的 `add_pending_metadata` | 两项新增测试在明确的区域数断言失败；不会动态缩小预期后误过 |
| `select_legacy_scheduler` 中写模式标记 | 模式切换测试在非零／不同模式断言失败 |

负对照没有进入提交，生产源无差异。正常版本与 sanitizer 的最终结果见下表。

## 与并行工作衔接

- Pipeline A 原文档仍是其阶段归属来源；本次不修改该工作树。#2354 在 main 只是健康证据记录，不能把它当 E1/E2 已验收。
- open #2370（核查 head `6984242c`）处理 descriptor teardown gates 的上传边界，会修改已有 byte-count 测试；它不是本次普通／legacy scheduler publication 覆盖的替代。未在本任务审阅其正确性或合入。
- 本次本地测试交付后，S2 同步合同可标记验收；K1 的生产接线仍等 Pipeline A 的 P2/P4/P5 门槛。完整内存归一化 M1–M4 属并行工作，不偷偷加入 A 的新增前置。

## 本地交付与验证结果

提交 `42a4d5f14162a56001de98e7bab863eaef67085c`，分支 `local/s2-hbg-publication-acceptance`；单文件增加 132 行测试。工作树干净，未 push、未创建 PR。

| 验证 | 结果 |
| --- | --- |
| main 基线加本地测试的 C++ 回归 | 166/166 executable targets 通过 |
| 两架构 HBG bind suites | 各 24，共 48 用例通过 |
| ASan + leak detection | 两套 HBG suite 通过；补齐编译器自带 libasan 的动态库路径后运行，首次加载失败未执行测试 |
| 两种源码负对照 | 删除 scheduler 发布源使两项新增测试失败；删除 legacy 模式标记使模式切换测试失败 |
| pre-commit | clang-format、clang-tidy、cpplint 等通过 |
| 文档 | 路线 Markdown、两份 HTML 重新生成并通过引用／归档校验 |

本次只增加 host 测试，未重跑 device/sim 场景；主线已有的 #2367 CI 结果保留独立归属。同步内存后端不证明真实 DMA、cache 可见性、双流执行或 A5 legacy 终态。

日志、负对照脚本、补丁与 SHA256 校验见 [本地证据](../LOCAL-EVIDENCE.md#artifact-0b7ad325bee9)。

## PR 交付

按用户要求已推送并创建 [#2372](https://github.com/hw-native-sys/simpler/pull/2372)，head `42a4d5f14162a56001de98e7bab863eaef67085c`，base `eeedcaa4`，一个提交、一个测试文件。以上“未 push”是本地验证完成时的历史状态；规划文档、HTML 和实验日志继续仅保存在本地。提交和基线未变化，无需重复已通过的验证。

## #2372 审阅更新

head `95f8c52bc7d3a9974279fa8103b61481e77f1de8`，无冲突 rebase 到 main `7660ab28`（含 #2370），仍只有一个 commit、一个测试文件（+145）。失败发布后显式验证 worker 的 task/mode 清零，再检查重新 bind 的 legacy 不携带旧 scheduler 地址；补充形状 trace，区域计数改为各项相加。

重建后 166/166 C++ targets、两架构 HBG 48 用例与 ASan/leak 全部通过，pre-commit 通过。新增负对照：删除 release-time worker clearing 时，旧失败路径测试 exit 0，新测试 exit 1，证明本轮断言补到了原缺口。生产临时改动已还原。原来的两个负对照保留其原 head 验证归属。


## #2372 replacement 状态评审修复

head `7da788ef4d5b9844de51cee14bb6702b3b6dd13f`，base 仍为 `7660ab28`，一个 commit、一个测试文件（+164）。发布失败后新 bind 的 prerequisite 数量与 phase 均受检查；A5 每个 worker 的 mode 非零且区别于失败调用，resident task 地址属于本次 scheduler 区域，legacy task 地址为零。

本轮重新通过 166/166 C++ targets、两架构 HBG ASan/leak 和 pre-commit。负对照将 resident worker task 置零：此前 failure-path 测试仍通过，本次新增地址断言失败。恢复生产代码后完成上述全量验证。证据留在 `pr2372-replacement-7da788ef/`；本地规划、HTML 与实验材料不进入 PR。


## #2372 已合入，S2 同步验收收口

GitHub 确认 #2372 于 `2026-09-20T00:37:37Z` 合入 main `767618b3e4620c16cfa2ba66a7fcb3e707e8cd3c`。相对本轮测试基线 `7660ab28`，主线仅增加 #2371 文档和 #2372 测试，没有改变 Pipeline A 生产执行路径。以上正常、ASan 与负对照仍归属于审阅 head `7da788ef`，没有声称重新测试 merge commit。

S2 同步合同验收完成；A/P2 的正常 drain 与安全退休、P4/P5 连续入队仍需原工作线交付。之后进入 K1/K2，异步 publication 与 replay 保留在 C1 之后。


## 2026-09-23 接收状态更新

源码基线 main `9ca91c522e8a80483f6a7cf8ad094f994fdcff5f`。本轮只读核查，不重跑测试。

- #2411 的 `wait_run_fence` 已在本 run 双边界完成、记录传输成功且本 epoch 为 Ok 时返回，无整流 sync；其他形状保留有界后备和已接受的错误通道限制。
- #2419 的 early authorization、joined launch、whole-operator boundary、wait 消费证明已贯通 parent/lane/native；#2426 修正时钟同 tick 的验收。批准且有板端验收的首步是 A2/A3 HBG、HOST tensor、本地 L3 单 endpoint、非诊断、无 live comm、显式 launch_depth=2。默认1；不能宣称 TMR/A5/Device tensor 已开放。
- #2406/#2399 构图和 host 访问合同已合入。#2440 已提供四个 workspace 区域的预算与消费者寿命所有者；kernel 接入应复用，不新建平行池。
- main `simpler_kernel_mode_supported` 仍返回0，`simpler_kernel_mode_launch` 仍拒绝；旧 kernel 分支 `58180f789` 与 main 相比独有24提交，main 独有140提交，不能整体搬入。#2433/#2442 目标均为该功能分支，不是 main。
- #2432 已改变 TMR program 参数承载与发布时点；#2438 仅复用相同192字节配置前缀。历史“所有参数在 descriptor、prepare/publish 相邻”的描述不可作为当前主线合同。

下一步：在 main 做 K1/K2 接入，复用当前完成/发布/workspace机制；caller Device tensor 寿命与借用设备失败处理仍需新证明。Pipeline A 的更广后端与真实 Qwen driver #2436 并行，不将其扩大为 kernel eager 的全部前置。
