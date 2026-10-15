# Native eager caller ordering

实现初始基线：main `5f4f9525d`（#2561）。已 rebase 到 `c5cdd1b37`，提交 `f4ed8068`，分支 `feat/kernel-caller-order`；[PR #2567](https://github.com/hw-native-sys/simpler/pull/2567) 已合入 main `852f6ff169f8b8a32e327c3c2ad0c105e1b5f9e3`，CI 全绿。下方测试仍对应原 head，不改挂 merge commit。

## 范围

共同 native run 增加显式 caller stream launch，沿用原 prepare/launch/poll/wait/finalize。保留原双侧完成事件，用三个 slot 事件连接 caller 入口/出口；caller wait 持有 CpuDone 引用，Tail 完成后才解除。partial enqueue 保留事件与资源，未知完成进入隔离。借用 context 不做 device-wide sync/reset。prepare 在 capture 外；此 entry 明确拒绝 active capture。

A2/A3 保留 RunStreamPair 的代码代际规则；A5 从借用 context 选择双流。正常 program 未传 caller，不分配新增事件。未新增公开 PreparedCall、参数结构或第二套 executor。

## 已验证

- 构建四个 onboard runtime 与模拟器成功。
- 315 个无硬件 C++ 测试目标通过，含 caller 生命周期和现有 completion/wait 引用测试。
- 12 组真实 runner + 桩测试通过（两架构 × 两 runtime × 三探针）。新探针检验完整事件/stream 链与每个 wait 的事件身份、11 个必需提交点失败均 Partial、capture 在 metadata 发布前拒绝。
- task-submit `task_20261013_190034_13457835619` / device 1：TMR/HBG 两测试通过，各四次 invocation 轮换 caller streams；caller D2H 后仅同步 caller 即读到正确数值，发生在 simpler wait/finalize 前。错误 current context 拒绝后同一调用可重试。
- A2/A3 普通模拟器 sweep 通过。

- 完整 Python：3070 passed / 75 skipped；A5 普通模拟器 sweep 通过。
- A2/A3 program 普通全量上板：task-submit `task_20261013_190446_15189353803`，devices 3–6，退出 0（636.56 s），无 AIV。按 CI 排除 level 4、manual、sdma 和 sdma_fault；未把隔离 SDMA/fault 用例混入普通 sweep。
- 生命周期与 caller 实际调用：初轮 30 passed / 1 failed；失败是旧 fatal_device 断言仍要求一次全设备 drain，和 #2557 主线的 borrowed 禁止 drain 合同冲突。修正为 recover 及两次 close 后均须为 0，task-submit `task_20261013_191006_185929322889` / device 1：31 passed / 19 deselected。原失败日志独立保留为 lifecycle-before-fix.log。
- 删除 caller entry 或 exit 的负对照，四组合共八臂均触发断言失败，见 negative-controls.json。

最终核查另补关闭失败保护：共同 native event 释放失败后，即使 cold-path owners 都已关闭，adopted device 仍保留 context，禁止 destroy 抹掉重试入口。真实 runner 新测试先在旧判定下失败（caller-close-red.log），修订后四个 onboard runtime 重建成功、12 组真实 runner 桩再次全部通过；最终 task-submit `task_20261013_191814_270436326283` / device 1 的生命周期和 caller 顺序测试 31 passed / 19 deselected（92.44 s）。pre-commit 全部通过。上述 sweep 对应此最后 guard 补充之前；该 guard 仅影响 borrowed context 的销毁检查，program 在该检查前直接返回 false。

## 边界

native eager 冒烟不等于 K2 交付。Worker 公共入口、外部 Buffer/TaskArgs 接入与 context generation 的实际消费者尚未实现。旧 capability 位维持 0。metadata 仍同步，未实现 capture 的地址/源/图生命周期。A5 仅完成构建与 runner 桩；本机 A3，不能冒充 A5 silicon 验证。

AIV 不在本轮归因范围：沿用主线 DFX，日志在独立 ascend 目录，另有 occupancy 采样。规划与现场材料保持仓库外。

## 最终状态

代码已以单个 commit `f4ed8068` push 到个人 fork 并创建 PR #2567。规划文档、HTML 与实验材料均留在仓库外，未包含在 PR。

下一步是统一 Worker/Buffer/TaskArgs 接入，而非继续扩展另一套 kernel 参数 API。完整 eager 的业务与 generation/lifetime 验收完成后再进入 capture/replay。

## PR 提交验证

rebase 同时保留 #2563 的 artifact_identity 与 caller_stream。发现测试 ctypes NativeRunDescriptor 未跟随主线 provenance：C 头尺寸 88 bytes，旧 ctypes 56 bytes；新增尺寸/偏移核对测试先失败，补齐后通过。

最终 head 重建后：Python 3134 passed / 75 skipped；C++ 315/315；两个架构/两个 runtime 共 12 runner 桩通过；lint 通过。task-submit `task_20261013_194032_36376322189` / device 10：32 passed / 19 deselected，93.25 s。完整 scene sweeps 保留原测试基线，没有声称在新 head 重跑。

中间一次补测被 amend HEAD 后的构建版本校验拒绝：Python 四个子进程和 lifecycle 十五项均 ImportError，构建 hash 为 c297c61f8f83 而 HEAD 为 f4ed8068c3cb。重建后全部重跑通过，原失败日志 lifecycle-rebase.log 和 rebase-py.log 保留，不视为设备故障。
