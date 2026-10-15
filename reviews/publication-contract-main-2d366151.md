# Descriptor 发布合同收口：本地实现

- 基线：main `2d3661517`，含 #2358 和 #2360。
- 本地分支：`local/descriptor-publication-contract`。
- 提交：`0cc1cf4f427fdcfb4b0b548b655bbe9c36ab18a2`；6 文件，+139/-16。
- 交付：[PR #2367](https://github.com/hw-native-sys/simpler/pull/2367)，head `403b1808cdf288ce1e203352e2cb393828dcc149`，rebase 到 `31da0560e` 后 patch range-diff 未变。已推送代码分支，待评审。
- 当前 head 复验：运行库重建、22 个 helper 用例、四组合真实 host launch 拒绝探针与 pre-commit 均通过。下文完整回归是原提交上的结果，未在 rebase 后重复整套 sweep。
- 代码、接口文档和 UT 在提交中；本报告、规划、HTML 和探针仅留本地。

## 行为

`KernelArgsHelper` 保存 Empty / Prepared / Published 三种 host 状态。发布失败归入 Empty，原始复制错误通过 rc 返回，不引入新的 run 结果或设备健康状态。

| 操作 | 结果 |
| --- | --- |
| prepare 成功 | 保存本次 descriptor，Prepared；目的指针非空不意味着可 launch |
| Prepared 时再次 prepare | INVALID_STATE，旧源、目的、allocator 不变，连新 slot 也不分配 |
| publish 成功 | 同步复制结束后才 Published |
| publish 失败 | 消费源、清 run pointer，保留 slot allocation |
| 重复 publish | 拒绝且不再复制；已成功的发布状态保留 |
| release / abandon | 撤销状态和 run view，slot 的物理 owner 不变 |
| move | 源失去发布状态，目标继承 |
| fresh prepare | 撤销旧成功状态，本次必须重新 publish |

两架构的 `launch_execution` 在创建 stream、启动 DFX 和提交 kernel 前检查 descriptor 状态。拒绝返回 INVALID_STATE / NotStarted，保留 prepared owner，不创建 ActiveExecution、不 poison device。C API 既有路径接回 prepared 并进入失败清理。

## 范围

这是 #2358 的后续保护，原正常生产路径 prepare/publish 仍相邻；没有当前 mutation race 的修复或性能收益宣称。同步复制的位置、HBG prepare 重叠、slot owner、准入和 device ABI 不变。

状态只证明 Runtime descriptor 已发布。HBG Definition/scheduler/image publication 与 late DFX 仍各自检查 rc，不能将这个布尔查询当作“全部 metadata 已就绪”。异步源保活、独立调度窗口、capture/replay 和 Pipeline A 多 launched 尚未由此实现。

## 验证

| 验证 | 结果 / 范围 |
| --- | --- |
| 旧代码负对照 | 两 runtime 各 2 个用例失败：重复 prepare 把 7 换成 19；换 slot 还额外分配 |
| 生产 helper UT | TMR/HBG 各 11，共 22 用例通过 |
| C++ 回归 | 163/163 executable 通过 |
| ASan + leak detection | 两 helper targets 通过 |
| 真实 host launch 入口探针 | A2/A3、A5 × TMR/HBG；每种 4 个拒绝场景通过，NotStarted、owner 保留、零 stream/event 创建 |
| launch 探针负对照 | 只在本地覆盖生产 launch_execution 定义、删除新 guard；四种组合均触发断言失败 |
| a2a3sim sweep | exit 0 |
| a5sim sweep | exit 0 |
| A2/A3 onboard | task_20260919_013858_20516537539，设备 0–3，普通非 SDMA、level<4 sweep，exit 0 |
| pre-commit | clang-format/tidy、cpplint、markdownlint 等全部通过 |

探针链接刚重建的真实 host runtime，设备 allocation/copy/stream/event 边界由 host stub 拦截；它验证 host 拒绝路径，不是 A5 真机执行。局部负对照的 A5 dep_gen 查询采用 false stub（这些场景未启用 DFX）。A5 真机、SDMA 专项、capture 未由本次验证。

运行库和 tests 在提交前由同一份工作树重建、执行，随后原样提交。完整日志、探针源、移除 guard 的对照源和补丁保存在 [evidence](../LOCAL-EVIDENCE.md#artifact-b4f6e70c4ee1)，同目录有 SHA256.json；无新未解决问题。

## PR #2367 审阅补充

- head `964d3a73`：新增仓库内 C++ host probe 与 pytest 驱动，接入已有 A2/A3、A5 onboard UT 作业；生产行为不变，补充 Empty-during-copy 注释。
- 两架构 × 两 runtime × 四种拒绝状态全部通过；A2/A3 经 pytest，A5 直接执行同一 host-only 测试函数。新版探针删除 gate 的四组合负对照均断言失败。22 个 helper 用例与 pre-commit 复验通过。
- 旧 head CI 的 A2/A3 onboard 作业在 Set up job 下载 action 时遇到 SSL 错误，未进入 checkout/build/tests；GitHub 拒绝单作业重试。新 head 的完整 CI 待返回，原因与链接已记入 PR。

## 合入状态

2026-09-19，PR #2367 已合入，merge `eeedcaa4a4afcd22e18f7c2d636b2f9a6ba8b5d7`；审阅 head 为 `964d3a73`。以上 pending 描述为当时状态。工作树停在合入后的 main 提交（detached HEAD），保留分支和原始验证材料。

审阅 head `964d3a73` 的 [CI](https://github.com/hw-native-sys/simpler/actions/runs/35434816191) 已全部通过（docs deploy 按配置跳过），含两架构 onboard UT、scene sweep、两平台 sim、packaging 与 pre-commit。旧 head 下载 action 的 SSL 失败未在新 head 重现。此结果属于审阅 head，不宣称合入后再次执行全套。
