# #2444 对照参数整体方案的复核

核查日期：2026-09-23。PR head `ddd372366fda990012799fa28f589188174e09d1`，差异基线 `9ca91c522e8a80483f6a7cf8ad094f994fdcff5f`。PR 仍 open；查询时该 head 的检查全部完成、无失败（docs deploy skipped）。没有修改或推送代码，没有发表 GitHub 评论。

## 结论

需要修正接收接口，不能直接合入当前角色方案，也不能仅删除 enum 后恢复 HOST 自动推断。

### 方案符合性：两项 P2

1. `src/common/task_interface/kernel_call_args.h:26-35,55-78`：每次调用必须填写 `KernelTensorRole` 平行数组。与最新方案不保留逐调用角色表的决定冲突；所谓 adapter callable contract 没有真实生产入口提供和校验。两个角色分别只接受 DEVICE 与 HOST，随后复制和 getter 又按 address_space 分支，形成独立填写但重复约束的状态。
2. 同文件 `:63-102,128-140`：一个参数槽只保留一个视图，DEVICE 引用与 HOST 值快照互斥。不能表达同一 HBG 逻辑输入的 device view 及明确关联的 host 值。增加枚举成员不能代替真实的关联、读取时机和寿命合同。

当前无生产消费者，因此以上是新接口与目标设计不匹配，不是已发生的 program 回归。13 项 helper 测试及 CI 通过不能证明尚未实现的接收合同。

### 代码规范

未发现需要阻止合入的明确注释、命名或格式规范问题。平行角色表属于设计冗余判断，不能冒充编译错误。PR 并没有新增 HostTensor/DeviceTensor 两个结构体；此前文档命名不严谨不应转化成不存在的 PR 缺陷。

## 修正边界

- 先确定真实入口的本次绑定及合法读取点：device 借用、显式 host 按值源，以及同一参数需要双侧访问时的对应关系。
- 快照模块消费明确选定的值源，不决定 HOST tensor 的用途。保持 L3 Tensor.buffer.address_space 与 L2 ChipTensor.address_space 的既有表示，不给 PTOBufferHandle 添加重复字段。
- 保留两阶段校验、几何检查、scalar 位模式、offset 所有权和源销毁后的独立性测试。按新的真实绑定合同替换角色数组测试。
- 不把整套静态消费分析、Buffer 所有权重构、capture 或 MTP 实现塞入这个 PR；它们不是删除角色 enum 的附带工作。
- 若先独立交付复制机制，范围必须收敛为通用几何校验和显式值快照原语，不将其宣称为完整 KernelCallArgs 接收接口。最终参数接入仍需真实 adapter 验证。

两种防误修约束：不能新增 HostTensor/DeviceTensor 类型来隐藏角色分类；不能改回 `if HOST then control snapshot` 把 program staging 存储悄悄解释成控制值。

本轮完成设计复核，没有在真实绑定接口尚未具体化时再次猜一个替代 API。当前 head 保持不变。


## 实施更新：显式绑定与同参数双侧视图

2026-09-23，#2444 修订 `a596cbee`（基线 main `a9e6a42b6`）：移除逐调用角色表。`KernelCallBindings.add_tensor` 绑定 DEVICE 引用，`add_host_value` 显式绑定 HOST 按值输入，`bind_host_value` 为已有 device 参数关联 host 初值；两侧保持同一逻辑参数索引。沿用 `ChipTensor.address_space`，没有新增 HostTensor/DeviceTensor 结构。关联要求 dtype/shape/stride 相同，可有不同 base/offset，由调用方保证值版本一致。host-only 限 IN，双侧允许 IN/INOUT；绑定不读内容，snapshot 前由用户保证就绪。整次预检后复制，scalar 保持原始位模式。

验证：16 个模块用例、4 个相关 CTest targets、ASan/UBSan/leak、pre-commit 通过。五个负对照抓住遗漏复制、scalar 截断、device 解引用、提前读取及丢失关联。前一 head `ddd37236` CI 已全绿；本次修订 CI 单独等待。仍无 production caller，未交付 kernel eager 或设备 lowering。仅代码/测试纳入 PR。
