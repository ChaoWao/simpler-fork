# Kernel 参数快照：本地实现与验证

> 2026-09-24：下文为旧方案历史证据。当前单枚举合同已替代双视图模块，见 [1761d130 验证](argument-storage-contract-1761d130.md)。

日期：2026-09-23。基线：main `9ca91c522e8a80483f6a7cf8ad094f994fdcff5f`。
实现时本地分支：`local/kernel-call-arguments`；worktree：simpler 的 `.claude/worktrees/kernel-call-arguments`。

按用户后续指示，已更名为 `feat/snapshot-kernel-call-arguments`，提交 `96f868fb` 并创建 [PR #2444](https://github.com/hw-native-sys/simpler/pull/2444)，目标 main。仅三个代码/测试文件，规划材料未提交；下文保留该提交的本地验证范围。

## 交付范围

- `src/common/task_interface/kernel_call_args.h`：内部 host-only 参数模块，接受现有 ChipCallable / ChipStorageTaskArgs；不增加 public C ABI、wire 类型、环境变量或设备分配。
- `tests/ut/cpp/common/task_interface/test_kernel_call_args.cpp`：11 个测试，通过正常 CMake/CTest 注册；CMake 为 tensor factory 链接真实 host assert 实现。
- 参考旧 kernel 分支 `58180f7897e257e0fc3f0f27061c5ab6012df580` 的 `normalize_invocation_tensor` 数值校验；没有迁移缓存、dispatch packet 或独立 runner。

## 确定的参数合同

| 内容 | 实现 |
| --- | --- |
| 签名与数量 | 检查计数范围、每个 ArgDirection，以及 tensor/scalar 的准确数量 |
| DeviceTensor | 地址、dtype、shape、stride、offset 和 backing 大小保留；host 从不读取 storage，最后使用者保活由执行集成负责 |
| HostTensor | 仅 IN 控制输入；先校验所有参数再保存视图覆盖区间，起点归零、shape/stride 保留；不复制整块 backing 的无关首尾 |
| Scalar | 逐槽保存 64 位；现有 signature 只有方向，没有 scalar dtype；类型解释来自 callable 编译合同，不声称新增运行时 dtype 校验 |
| 容量 | 调用者明确提供 host 控制载荷容量，包含每段的 8 字节对齐；descriptor/scalar 数量另受现有上限约束，不是 device workspace 预算 |
| 错误 | invalid_argument / length_error / bad_alloc，不产生设备命令，不交出半构造结果；失败赋值保留旧值 |
| 寿命 | 内部用 offset 关联 host 存储，move 后仍有效；原 host 容器可以在 snapshot 返回后修改或销毁。返回的 HOST descriptor 借用 snapshot 寿命 |
| 空 tensor | 保留合法 rank/dtype/地址空间及空 shape，不读取内容、不要求实际 backing；沿用旧 kernel 几何校验的空视图语义 |

调用期间源 host 字节须可读且不被并发修改；结构校验不能证明任意 host 指针已映射。Device 地址的 context、分配者与最后消费者同样不是纯参数模块能证明的。

## 验证

- CMake 构建和 CTest：`test_kernel_call_args`、`test_callable_scalar_count`、`test_chip_max_tensor_args`、`test_kernel_invocation_header`，4/4 targets 通过。
- 新模块：11/11 用例通过。包含原控制容器销毁、strided/offset 值、混合 Host/Device/scalar、两次独立快照、失败替换、move、容量和几何越界。
- 独立 GCC 构建：`-fsanitize=address,undefined -fno-omit-frame-pointer`，11/11 通过，leak detection 开启。系统默认链接路径未包含 libubsan；显式使用已安装的 Conda GCC sanitizer 库完成，无系统修改。
- 对三个改动文件运行 pre-commit，适用检查全部通过。
- 负对照在 `/tmp` 的独立副本执行，没有修改工作分支源文件：

| 故意破坏 | 结果 |
| --- | --- |
| 省掉 host 字节复制 | 内容断言失败，exit 1 |
| scalar 截成 32 位 | 位模式断言失败，exit 1 |
| host 解引用 DeviceTensor | 隔离测试 SIGSEGV，returncode -11 |
| 全部预检之前读取第一个 host 参数 | 隔离测试 SIGSEGV，returncode -11 |

原始日志：`/tmp/kernel-call-args-build.log`、`/tmp/kernel-call-args-sanitized.log`、`/tmp/kernel-call-args-negative.json`；负对照源码和逐臂日志在 `/tmp/kernel-call-args-negative/`。

## 集成边界

本次没有 production caller，也没有改主线 `simpler_kernel_mode_launch` 拒绝桩；没有运行 onboard 或 program 场景测试。该模块完成不等于 K1/K2 或完整 kernel eager 已完成。

后续 K1 接入共同行为时调用此模块，并完成实际消费者映射：HBG 只能在 host 访问控制值，DeviceTensor get/set 在访问处拒绝；TMR 不得把返回的 HOST 地址直接交给 AICPU。后端不支持的输入须在设备 publication 前拒绝或按确定合同完成 lowering。#2433 的 Device-only Tensor / scalar-only host 值方案不能直接替代这里的 HostTensor 合同。

Pipeline A 的 admission、顺序、完成、退休以及 workspace owner 本次均未修改。规划文档与本记录仅保存在仓库外，不纳入代码提交。

## PR #2444 评审修订：显式参数角色

修订 head `ddd37236`。新增 `KernelCallPolicy`：每个 tensor 必须显式声明 `DeviceOperand` 或 `HostControl`，分别校验 DEVICE storage、HOST + IN。缺少、额外、未知或空间不匹配的角色均拒绝；不能把默认 HOST 的 program 数据输入推定成控制值。Adapter 根据 callable 合同提供角色和 `host_control_capacity_bytes`；当前尚未接入生产预算源，预算不是 launch envelope 或 device workspace 上限。

补充空视图可有 null backing / zero stride、容量按每段 8 字节对齐扣减的合同；scalar 数量复用 `count_scalar_args`。Callable 源生命周期测试改为作用域析构，替代不释放存储的 `clear()`。

`host_copy_tensor_count` 继续必须为零。本模块的 host 控制字节是 host 准备输入，不是 wire 上追加的 host-copy tensor suffix；后端必须先消费、按确定合同转换或拒绝，不能把本地副本数量直接写进该字段。未修改 wire ABI。

验证：13 个新模块用例、4 个相关 CTest targets、ASan/UBSan/leak、pre-commit 通过。普通 HOST 输入隐式变成控制值的回归在旧 head 上先确认失败；恢复 address-space-only 推断的负对照也以断言失败被抓住。原来的四项负对照重跑均失败。

原 head 的 A5 CI 在 `test_result_reads_while_successor_runs_hbg` 未捕获到 overlap window：5 次均 `completed-across-read`，前驱结果均有效。源码与日志不足以区分读取等待后继与后继恰在读期间完成；已请求原 job 重跑。新 head 的 CI 仍须单独验收，不能把本地参数测试或其他 PR 的通过当作此失败已关闭。独立的 program strided HOST staging 尺寸问题记在该 worktree 的本地 `KNOWN_ISSUES.md`，没有混入 PR。


## 实施更新：显式绑定与同参数双侧视图

2026-09-23，#2444 修订 `a596cbee`（基线 main `a9e6a42b6`）：移除逐调用角色表。`KernelCallBindings.add_tensor` 绑定 DEVICE 引用，`add_host_value` 显式绑定 HOST 按值输入，`bind_host_value` 为已有 device 参数关联 host 初值；两侧保持同一逻辑参数索引。沿用 `ChipTensor.address_space`，没有新增 HostTensor/DeviceTensor 结构。关联要求 dtype/shape/stride 相同，可有不同 base/offset，由调用方保证值版本一致。host-only 限 IN，双侧允许 IN/INOUT；绑定不读内容，snapshot 前由用户保证就绪。整次预检后复制，scalar 保持原始位模式。

验证：16 个模块用例、4 个相关 CTest targets、ASan/UBSan/leak、pre-commit 通过。五个负对照抓住遗漏复制、scalar 截断、device 解引用、提前读取及丢失关联。前一 head `ddd37236` CI 已全绿；本次修订 CI 单独等待。仍无 production caller，未交付 kernel eager 或设备 lowering。仅代码/测试纳入 PR。
