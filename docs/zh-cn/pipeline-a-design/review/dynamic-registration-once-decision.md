# 注册时建立函数表，run 仅引用 — USER APPROVED

设计归档快照：状态与阅读顺序见[索引](../README.md)。设计约束、历史合同和当前实现须分别阅读。

状态：用户于 2026-09-21T08:36:09Z 明确同意“注册一次，后续查表”。以下完整合同已批准实施并交付一个 PR；git-commit/github-pr/fix-pr 可用，不合并。
依据：dynamic `.docs/registered-function-table-decision.md`，基线 `a21e561a2`。
本合同取代旧的“每 run 比较快照”方案，并合并作者原来拆开的 A/B 两张表。

## 用户要解决的问题

RegisterCallable 已上传 callable 代码并保留注册资源。但每次 prepare 仍重新构建并上传函数索引表。
应当在注册资源中建立表，后续 run 引用它，而不是再次比较、填表和上传。

这里有两种地址，不能混用：

| 消费者 | 表项含义 | 现状 |
| ------ | -------- | ---- |
| 四种 runtime 的 AICPU 调度路径 | func_id 对应的 CoreCallable 对象地址 | 每 run 随 descriptor 上传 1024×8 B |
| a5 HBG 原生 AICore scheduler | func_id 对应的执行入口地址 | 另在每 run scheduler state 中构建并上传 1024×8 B |

onboard 的执行入口是对象地址加 `CoreCallable::binary_data_offset()`；a5sim 从对象的
`resolved_addr()` 取得主机函数地址。只迁移第一张表不能完成 a5 HBG 的注册一次目标。

## 一个 PR 的具体范围

1. 表由已有 pooled ChipCallableBuffer 拥有，与代码使用同一去重与引用计数寿命。
   以实际 `max_func_id + 1` 建立只读对象地址表；缺项为 0，注册时校验域、长度和尺寸计算。
2. a5 HBG 同时建立执行入口地址表。只为需要此视图的路径建立它；同一个 pooled owner
   对不同注册调用者必须提供一致、已完成的视图，不允许 run 引用后再原地补写。
   若当前去重键不能保证这一点，作者必须在实现前报告，不能静默增加注册写入/同步。
3. onboard 两张表并入已有注册分配块的对齐尾部，由同一次注册拷贝上传；保留原代码布局与
   内容去重依据。注册部分失败不发布；sim 使用注册对象和主机表，不执行 H2D。
4. 四种 descriptor 的嵌入数组改为对象表设备地址及长度；cold path 绑定，访问前检查空表、
   func_id 域、实际长度和缺项。主机验证使用 owner 的主机视图，不能解引用设备地址。
5. a5 HBG 的 `SchedulerWorkerContext` 由 state-relative table offset 改为注册入口表的
   绝对设备地址及长度；lookup 同时检查实际长度。删除每 run scheduler layout 对这张表的
   reserve、填充和上传；修改相关 layout 定义、初始化与测试 fixture。调度策略不变。
6. 运行期只绑定引用。A→B→A 使用各自 callable 的表；注销、重注册、设备重建仍使用现有
   注册/预约约束。host-only lookup 接口与 sim 必须一起适配。

精确生产位置：`device_runner_base.cpp` 注册与 bind；四种 runtime descriptor/cold path；
a5 HBG `runtime_maker.cpp:1221-1229,1275`、`scheduler_layout.h:34`、
`scheduler_types.h:1080,1401`、`scheduler_ready.h:275-281,1062-1066`。
后者的修改是此次明确申请的 wire/layout 边界，不是重写 scheduler。

## 代价与限制

- 原始表负载每 run：普通路径 8 KiB，a5 HBG 两表合计 16 KiB。迁移后不再逐 run 上传这些表；
  净 descriptor/state 字节减少须扣除地址、长度、对齐和每 worker context 元数据，以实际布局验收。
- 注册池常驻空间增加：每张表 `8×实际长度` 加对齐；a5 HBG 两张表上限合计约 16 KiB/池块。
  主机也持有构造/验证所需数据；相对原 per-callable 稀疏向量，净内存变化取决于稀疏度和去重数量。
- 不增加注册设备分配次数或拷贝次数；不承诺延迟收益。保留同步、单次设备执行、准入和异常隔离。
- prepared 未 launch 也是消费者。正常路径现有 reservation 阻止注销；attach/drain 异常路径的
  claim 释放不是完整退休证明，本 PR 不扩大后续注册/释放许可，也不宣称修复了该已有寿命问题。
- kernel-mode callable dispatch 目前为桩；只保持零地址/长度不解引用，不宣称 capture 已验证。
- 不包含内存扩容、多代保留、全局 func_id 注册表或新错误协议。不新增 PTO_/PTO2 标识符。

## 验收

检查全部生产消费者，没有残留每 run 表填充/上传。核对各布局与总拷贝字节；新场景覆盖注册失败
不发布、去重、稀疏/越界/缺项、A→B→A、两种地址语义、sim、prepared 时注销拒绝及零表保护。
现有测试 fixture 可随接口适配，但不得重跑已执行测试；新案例也只执行一次，CI 重试另行逐项授权。
当前 head manager 复核、阻塞反馈处理与相关 CI 完成后才能 Ready。交付一个 PR，不合并。
