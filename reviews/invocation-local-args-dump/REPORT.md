# 逐调用 args-dump 选择：C1 交付与验证

日期：2026-10-21。PR [#2660](https://github.com/hw-native-sys/simpler/pull/2660) 已合入 `ef17033ba`。生产改动基于 Simpler main `3b58364657c15c14c753bea44824ebe04fb8f23c`；功能验证 head `f24d0a7367685492f9b65cea1a7217aacabfcc92`，最终单提交 `b59f6fdec`（仅两条 UT 的字节快照保存方式不同；src/python/docs 树相同）。这是共同执行路径的增量，不是 capture/replay 或全路线验收。

## 本笔做什么

`ArgsDumpCollector::begin_run` 和 retained `run_begin` 原来通过 `DumpDataHeader::dump_args_level` 发布本次选择，retained admission 还同步读回这个共享字。AICPU `dump_args_init` 从该字选择 PARTIAL/HYBRID/FULL。host admission 可以先于本次设备初始化；共享池配置不是一次调用的稳定输入。

本笔将 `dump_args_level` 放进 `DeviceRuntimeLaunchDesc`，在 onboard/sim 的 `prepare_launch_shape` 按本次 CallConfig 设置。六个 TMR/HBG scheduler init 调用点显式传入 descriptor 的值。Onboard 使用已有值快照、pinned source、异步 publication 和入口 fork；发布失败保持既有 NotStarted/Partial 与资源保留合同，不能继续执行。Sim 读取同一 Runtime 字段。

Host admission 只冻结本次导出的选择，不再写/读回共享设备级别。旧 pool 字改为 reserved，避免留下一个看似可供设备读取的过期字段。

**没有把整个 admission 改成 host-only。** 普通单 run 的 payload counter reset、retained 单调游标、队列、terminal banks、payload acknowledgement 和收集/退休顺序保持原合同。没有新增公开 API、版本协商、PreparedCall 或第二个执行器。A5 HBG 的 args-dump 能力限制不放宽。

## 布局与费用

两架构独立编译 base/head 布局，数值相同。TMR 使用 entry storage 前的对齐空隙；HBG 从 `graph_section_reserved[10]` 取一个字，余九字。仅增加已有 descriptor 中一个 32-bit 本次值，不增加独立上传或分配。

| 组合 | sizeof(DeviceRuntimeLaunchDesc) | 后继字段 offset | workers offset | sizeof(Runtime) |
| --- | --- | --- | --- | --- |
| A2/A3 TMR | 43264 | orch_args_storage_ = 192 | 34048 | 43456 |
| A5 TMR | 47872 | orch_args_storage_ = 192 | 34048 | 48064 |
| A2/A3 HBG | 9472 | sm_image_bytes = 224 | 256 | 43520 |
| A5 HBG | 14080 | sm_image_bytes = 224 | 256 | 48128 |

这些是此基线的数值，不沿用早期 HBG descriptor 尺寸。HBG handshake 边界与 TMR 字段/entry-storage 边界均有提交的回归断言。

## 回归怎样证明

- 修改生产代码前，普通和 retained admission 的新用例都在 main 上失败：改变级别会改变 pool header 的字节。
- 新 producer 测试把旧 pool 字所在 reserved 写成 poison，切换 HYBRID/FULL/PARTIAL；检查真实选择谓词、payload arena 增量和导出的级别。测试使用已有 fixture 的 host free-queue 归还步骤，不把池耗尽当选择失败。
- Retained 测试在 A 的 device init 前先 admit 不同级别的 B，再分别执行 A/B，检查选择和各自 manifest，避免只验证 host metadata 冻结。
- 真实库 RTS 探针冻结 descriptor 后改 host Runtime 为 99，证明上传的是本次旧快照；另含只有 dump 级别变化的序列，覆盖 prefix-cache 失效。
- 独立临时对象负对照改回从 pool reserved 取级别；真实 producer 的新测试失败于选择/payload 断言。负对照不修改生产 worktree 或运行中的设备二进制。
- public borrowed Worker 使用同一 callable/buffer，执行 FULL → PARTIAL → OFF → HYBRID → FULL。未标记的 vector callable 在 PARTIAL/OFF 不生成 manifest，HYBRID 有 metadata 而无 payload，FULL 有实际 payload；每次正常结果仍正确。

## 验证记录

| 验证 | 结果与范围 |
| --- | --- |
| C++ 全量 | 378/378；UT 快照改成显式 memcpy 后，八个受影响程序另通过 |
| Python 全量 | 3525 passed / 181 skipped |
| A3sim | 65 个 resource sessions 无失败；HBG L2 14 passed / 7 skipped，TMR L2 31 passed / 1 skipped |
| A5sim | 61 个 resource sessions 无失败；HBG/TMR L2 16/30 passed |
| SDK 真实库 RTS 桩 | 两架构 × 两 runtime × 三探针，12/12；不初始化/占用设备 |
| A3 public borrowed Worker | 2 passed；同 Worker 的五次模式切换与正常结果 |
| A3 SDK 标记探针 | 6 passed；由任务中的板测入口启动，但探针本身仍为 host-backed |
| A3 标记参数场景 | PARTIAL/HYBRID/FULL 各一次，通过 |
| A3 普通 sweep | 154 个 resource sessions 无失败；HBG L2 43 passed / 1 skipped、TMR L2 60 passed；leftover 1 passed；未跑 SDMA/SDMA fault/level 4 |
| DFX-off | A3sim/A5sim 各两 runtime 构建通过，未以此宣称完整 off-mode 冒烟 |
| 其他 | 全部 pre-commit、C++ axis 检查、strict docs 构建通过；独立 base/head 布局对照通过 |

全量功能验证固定在 `f24d0a736`。之后仅将两条 C++ UT 的 pool-header 快照保存改为 vector + memcpy，避免默认成员复制对 padding 的处理差异；八个受影响程序另通过。最终 `b59f6fdec9ed90381301dbdf8d9ea9481e25a001` 的生产/Python/产品文档与功能验证版本相同，不将测试执行的 SHA 改挂。最终 extension 重建，提交戳与最终 head 一致；重建后原六份 backend 二进制 SHA256 仍全部相同。无本地 A5 silicon 主张。

最终 task `task_20261021_194301_417749223115` 持 devices 1/2/3/4，exit=0，锁已释放。设备日志和 basetemp 分开保存；后半程另有每约十秒的设备/队列占用快照，该采样不能排除采样间隙的旁路占用。

首次普通 board sweep 在提交变更前已有部分资源用例通过；我在运行中提交导致后续子进程被 extension/source SHA 检查拒绝。该 task 已终止，不记为完整 sweep 或设备故障。最终 head 重建后重新开始完整验证，期间固定 HEAD。旧任务 `task_20261021_193130_36346642582`，最终任务 `task_20261021_194301_417749223115`。

原始日志以 `kernel-next-dump-*` 文件名仅在本地保留；最终任务独立保存 device log、pytest basetemp、设备锁和二进制 SHA256。首次 PMU 单发 AIV 仍未分类，本笔不是其根因修复。

## 与其他工作关系

TMR #2624/#2625、shared #2626、HBG #2646/#2647 已有各自候选。本轮读了固定 head 的 PR 范围和 capture/launch owner 关键代码，选取它们尚未交付的共同 args-dump 配置闭包；没有完成五笔候选的全量源码审查，也没有替其 dedicated API/长期 owner 设计签收。I1 的真实跨仓入口映射与 I2/I3 请求验收继续由 ROADMAP 维护。
