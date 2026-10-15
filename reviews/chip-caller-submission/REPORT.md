# ChipWorker caller-stream submission

基线：main `852f6ff169f8b8a32e327c3c2ad0c105e1b5f9e3`（#2567 已合入且 CI 全绿）。分支 `feat/chip-caller-submission`。

## 交付边界

将 #2567 的共同 caller-stream native launch 接进现有 ChipWorker/run lane。直接提交可带 caller stream：准备前校验、可 join 则入队、不可 join 则先 drain 前驱再普通 launch；成功返回意味着双侧 kernel 与 caller exit wait 均已提交。调用者随后追加 D2H 才有顺序保证。返回前遇到 launch 错误会抛出，不交付一个只是 prepared 的 handle。

普通未传 caller 的语义不变，现有 poll/wait/finalize、slot generation、argument borrow/retirement 不另建副本。Host copy-back 仍须 wait/finalize。新增 C ABI 仅暴露已有 runner preflight；sim 返回 UNSUPPORTED；旧 runtime 缺少 caller API 时仍可普通提交。

本步只开放内部 ChipWorker direct 提交参数；上板测试使用 owned ChipWorker。公开 Worker 借用 context、统一 Buffer/TaskArgs 的外部 context 消费、generation 与完整业务保活尚未交付。没有把 internal POD ChipTensor 测试入口变成新的公开参数设计。

## 验证（提交前，同一份暂存代码）

- C++：317/317 无硬件测试目标通过；其中真实 ChipWorker/lane 加 native 桩的 joined-launch suite 27/27，新增六项覆盖 caller 入队、depth=1、join refusal、校验拒绝、失败同步抛错及缺少 API 的兼容行为。`/tmp/chip-caller-cpp.log`、`/tmp/chip-caller-ut.log`。
- Python：3134 passed / 83 skipped，`/tmp/chip-caller-py.log`。
- A2/A3、A5 × TMR/HBG：实际 runner + RTS 桩四组通过，校验 capture 预检不发布 metadata，原有 eleven partial enqueue failure 测试仍通过。`/tmp/chip-caller-probes.log`。
- A2/A3 sim：55 resource cases + 两 runtime L2 分组通过；A5 sim：51 resource cases + 两 runtime L2 分组通过。对应 `/tmp/chip-caller-sim-a3.log`、`/tmp/chip-caller-sim-a5.log`。
- task-submit `task_20261013_232958_5534414661`，device 1：36 passed / 23 deselected（136.10s），包括新 ChipWorker 测试的 TMR/HBG × depth1/2，既有 caller 和 lifecycle 测试。每个新测试四次调用交替两条 caller，独立 input/output allocation；只同步 caller 就验证全部输出，随后才 run.wait。见 smoke/run.log。
- 单独编译移除 caller 返回前入队/抛错 block 的负对照；四个相关测试 4/4 失败，正常 27/27 通过。见 negative.log / without-enqueue-guarantee.cpp。未修改被测 sweep 的 tracked 源码或二进制。
- pre-commit、diff check 通过。`/tmp/chip-caller-lint-final.log`。
- Program A2/A3 普通常规 sweep 通过（exit=0，617.98 s）：task-submit `task_20261013_232959_5558329179`，devices 2–5；按 CI 排除 level4、manual、sdma、sdma_fault。121 resource cases 通过，L2 分组及收尾通过；本轮没有复现 AIV。见 sweep/run.log。

两次板测都有独立 ascend 目录和持续 occupancy.log。使用主线 DFX，无 overlay。A5 本机没有 silicon，A5 stub/sim 不作为 A5 上板验证。AIV 原问题仍未定因，本步不宣称修复。

## 后续

本步合入后接 ChipWorker 借用初始化与关闭，完整检查回滚、未知完成保留、错误 current context、close 不 reset；随后接公开 Worker/Buffer/TaskArgs，消费 context generation 并验证业务链。两个 runtime 完成 eager 验收后再接捕获。

路线图、HTML、日志和本报告只保留本地，不进代码 PR。

## 提交版本核验

单个 commit `43ff1f7f70fad5a1c8cbfc2b6c697bbc85f3f442`，基线仍为 `852f6ff16`，rebase 无代码变化。提交后重建成功；Python C API 非硬件测试 13 passed / 34 skipped，lane 27/27。提交前 full suite 使用同一份代码，未冒称在提交后重跑全量。

首次提交后核验误用了系统 pytest（venv 没有 pytest 可执行文件），收集前报 No module named simpler；改为显式 `.venv/bin/python -m pytest` 后通过。两份日志均保留。这是运行命令选择错误，没有修改代码。

[PR #2572](https://github.com/hw-native-sys/simpler/pull/2572) 已创建，head `43ff1f7f`，待 review/CI。12 个文件 +383/−24；规划与日志未提交。
