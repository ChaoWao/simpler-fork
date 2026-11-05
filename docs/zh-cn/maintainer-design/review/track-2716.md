# #2716：范围、代码证据与维护动作

> **发布快照：2026-11-04。** 设计、实现状态与历史调查须按正文各自基线阅读；不代表当前 main 全量验收。参见[阅读入口与已知更正](../README.md)。
>
> 2026-11-04：main `63ffeb8a8bd9b6527912fee87f6a8defdf82d5e0`。只登记目标 main 的 PR；GitHub 保持只读。合同状态只在 [work-items](../work-items.md)，本文是来源、证据与下一动作索引。历史全量代码审计仍为 2026-10-18；本轮没有运行产品或硬件测试。

## 1. 完整范围

 | 方向 | 原生子树 | 合同索引 |
 | ---- | -------- | -------- |
 | #2721 公共基础 | #2460、#1931、#2720 | RA 与公共 EX；资源 identity / view / access / lifetime |
 | #2722 通信与后端 | #2594、#824、#2621、#2657、#2708、#2738、#2739、#2783、#2775、#2787、#2800、#2823、#2824、#2833、#2855、#2854、#2860 | DS 与 RA 退休/EX completion 合同 |
 | #2723 单 L2 Run 常驻 | 原生子列表为空；阳性对照 #2721 为 3、#2722 为 17 | EX-2；已有消息队列示例不是完整公共方案 |
 | #2724 多级多机部署 | #2659 | DS-1/DS-6，会话/部署/清理 |

递归还包括 #2657 → #2730/#2731、#2708 → #2673/#2674，共 30 个节点、29 条边。节点集合及逐父子集合均核对，关闭子项保留交付证据。相对 10 月 26 日，根与四方向正文更新时间未变而原生树增长；本轮重读树仍为 30/29，但 #2657/#2730/#2731 正文已更新。父子关系与正文必须分别核对。

完整场景由原 issue 承载，可分多个 PR；RA/DS/EX 是横切合同索引，不按代码目录重复拆业务 owner。相关引用、原生父子与硬依赖是三种关系。必要的通信 kernel、队列及完成桥接属于 runtime；应用布局、消息协议、queue 算法和模型策略在应用/样例。方向编号不是串行实施顺序。

## 2. 具名主线证据

以下均从 main `63ffeb8a8bd9` 读取；源码与 CI 结果分开，不因合入自动关闭父目标。

 | 对象 | 可复核证据 | 判定边界 |
 | ---- | ---------- | -------- |
 | #2594 / #2668，RA-12/13 与 DS-6 | [Worker._comm_domain / _enroll_domain_uses](https://github.com/hw-native-sys/simpler/blob/63ffeb8a8bd9b6527912fee87f6a8defdf82d5e0/python/simpler/worker.py#L10500)：Worker 表持有、精确配置复用、Buffer identity 登记 run；#2668 merge `abd52d666c` | #2594 已 CLOSED；#824 仍 OPEN，应对照原行为结项，不再规划 detach；backend 回收另按 #2787/#2823 |
 | #2120 | CLOSED 未合入；[关闭说明](https://github.com/hw-native-sys/simpler/pull/2120) 确认公共 API 替代；worker.py 的 detach_persistent_domain 0 命中，retain_after_run 24 次文本命中为对照 | API 不同不是 ownership 未交付；上游消费者迁移描述属于关闭说明，不记成本轮跨仓代码复核 |
 | #1931 / RA-15 | [child control](https://github.com/hw-native-sys/simpler/blob/63ffeb8a8bd9b6527912fee87f6a8defdf82d5e0/python/simpler/worker.py#L3553)：FREE 仍裸 Q → cw.free；相邻 COPY 经 ImportRegistry.materialize | 父 API 验证不能替代 child frame 身份验证 |
 | #2779 / #2775 | [URMA request completion](https://github.com/hw-native-sys/simpler/blob/63ffeb8a8bd9b6527912fee87f6a8defdf82d5e0/src/a5/runtime/tensormap_and_ringbuffer/runtime/backend/urma/urma_completion_kernel.h)：逐队列独立 BB/CQE 目标与分片请求完成；merge `42803935df` | 不等于 #2673 的稳定发送者/SHARED_POOL、多域与 Host fetch |
 | #2790 / #2739 | [A5 domain workspace](https://github.com/hw-native-sys/simpler/blob/63ffeb8a8bd9b6527912fee87f6a8defdf82d5e0/src/a5/platform/onboard/host/comm_hccl.cpp#L1520)：SDMA/URMA/RDMA 分别寻址；merge `9609da8a8b` | #2739 CLOSED；RDMA 真机及跨域资格归 #2824，真实注册/channel 生命周期归 #2787/#2823 |
 | #2825 / #2674 | [checked context](https://github.com/hw-native-sys/simpler/blob/63ffeb8a8bd9b6527912fee87f6a8defdf82d5e0/python/simpler/task_interface.py#L1228)、[host domain validation](https://github.com/hw-native-sys/simpler/blob/63ffeb8a8bd9b6527912fee87f6a8defdf82d5e0/src/a5/platform/onboard/host/comm_hccl.cpp#L1765)；merge `3338ca33fa` | 默认编译 ≠ 初始化成功；当前 A5 TMR L3 dense-prefix 限定。无需补 query/enable_urma，单 rank provider 限制不是硬件永久不可能 |
 | #2741 / #2792，DS-1 | [MPI progress](https://github.com/hw-native-sys/simpler/blob/63ffeb8a8bd9b6527912fee87f6a8defdf82d5e0/python/simpler/mpi_direct_runtime.py#L171) 独立 health timeout、shutdown echo 与 heartbeat drain | [supervisor 主等待](https://github.com/hw-native-sys/simpler/blob/63ffeb8a8bd9b6527912fee87f6a8defdf82d5e0/python/simpler/mpi_direct_supervisor.py#L332) 仍无关闭 deadline；异常终止函数有 timeout wait 作对照。持久化/远端回收/Group 迁移归 #2659 |
 | #2829 / #2866，EX-7 / RA-16 | [native backlog](https://github.com/hw-native-sys/simpler/blob/63ffeb8a8bd9b6527912fee87f6a8defdf82d5e0/src/common/worker/chip_worker.cpp#L2135) 计数 REAPED 且 destruction 非 unknown，包含正在 finalization 持有的 carrier；峰值按读/prepare 采样 | PR 正文曾写排除 finalization-owned，与最终代码不同。执行 credit、可观察结果、资源退休不等价；没有新增 backlog 上限 |

## 3. CI：配置、实际执行、缺口分别记录

当前主线 [_st-npu-a5.yml](https://github.com/hw-native-sys/simpler/blob/63ffeb8a8bd9b6527912fee87f6a8defdf82d5e0/.github/workflows/_st-npu-a5.yml#L117) 扫描 examples/tests/st 的 A5 非 L4 场景；URMA 构建开关已删除，域仍显式选择 engine。测试 [ChunkedFetch](https://github.com/hw-native-sys/simpler/blob/63ffeb8a8bd9b6527912fee87f6a8defdf82d5e0/tests/st/a5/tensormap_and_ringbuffer/urma_deferred_completion/test_urma_deferred_completion.py#L155) 因 #2860 临时 skip，其他五个 ST 为阳性对照。

实读 [#2825 A5 job 126372009297](https://github.com/hw-native-sys/simpler/actions/runs/41625463411/job/126372009297) 日志：PR head `ad3d41c5ee21`，实际 checkout `6462eb37f2e02fed962c4a1ca9060380354a29e8`（基于 `70499ab1a0ab` 的 merge ref）。普通 GET/PUT、重复 GET/PUT、ChunkedPut 五个 ST 均实际 PASS，URMA demo、SDMA/URMA 同 kernel 与混合 DAG 也有 PASS；ChunkedFetch 明确 SKIPPED。成绩属于该 merge ref，不冒记为当前 main 硬件 PASS。

\#2854/#2860 的 PUT/GET S3 停滞仍 OPEN；一次后续 PASS 不证明间歇故障根因已修复。#2858 诊断采集不等于修复；后续 release timeout 不自动解释首次执行卡住。#2824 的 RDMA 测试环境/传输/CI 仍独立，SDMA/URMA 成功不能替代它。

## 4. 维护者动作表

 | 对象 | 当前证据 | 下一动作 / 责任角色 | 触发 |
 | ---- | -------- | ------------------- | ---- |
 | #2787 / #2835 | PR DRAFT head `e895be4babb9`；main reset_domain_workspaces 仍析构 manager，真实注册/channel 依赖需要完整路径核验 | 通信 owner 明确初始化/正常 release/异常 finalize 的同一账本；维护者优先审真实 manager + 有状态 HCCL 故障覆盖 | 新 head、生命周期合同或测试证据 |
 | #2854/#2860 / #2858 | 现有分块传输停滞；GET 暂停看护；诊断 PR head `ad8b55668c6b` | backend / CI owner 恢复可定位证据、同 revision 设备对照，再讨论恢复测试；不先宣布 #2835 修复 S3 | 诊断 job、首个未完成请求证据 |
 | #2673 / #2833 / #2855 | 三个独立目标：AIV 队列映射、重叠子域、batch/flush | 各原 owner 按当前 pin 分别设计；#2833 消费 #2787 的实际生命周期产物，避免环依赖；batch 首版不等多 AIV | 具名设计/PR/硬件结果 |
 | #2650/#2675 / #2847 | 旧候选仍 OPEN，#2779/#2790/#2825 已合入；#2847 head `799e938449fc` 适配 caller-owned workspace | 维护者与通信 owner 对比当前 main 收窄/去重；不是因旧 PR 仍开而重复实现 ABI/完成协议 | 重新基线或范围决定 |
 | #824 / #2120 | #2120 CLOSED，统一 ownership 已交付；#824 原始四条行为仍挂 OPEN | 域 owner 对照 #2639/#2668 的范围/测试回填结项；剩余 backend cleanup 分清归属 | 结项核对，无需等待新 detach PR |
 | #2659 / #2598 / #2791 | #2598 CLOSED；#2656/#2665/#2741/#2792 已在 main；#2659 表仍泛写待核定 | 部署 owner 回填已交付子集，收窄下一项到外部账本/关闭期限；#2791 `096efb9757a5` 按其实际子集核验 | 新交付、关闭期限设计及真实 MPI 故障证据 |
 | #2730/#2731 / #2714 | 11 月 4 日正文约定显式 mapped view，L3 全员同一完整 Host Buffer，PTO URMA 与独立 Host workspace | 按用户面 §5.2 审接口与生命周期；#2714 最新 head 见 README，旧评审不能外推；Stage 2 必须真实非 self PTO 读取与有用 overlap | head / 接口 / 支持矩阵变化 |
 | #2685/#2796/#2818/#2820 | A3 L4 / L2 / L3 Host 候选并存；#2668 前置已解除 | Host owner 对齐公共 Buffer/map/retirement，按层级分交付并去重 | 当前 main 重基与测试证据 |
 | #2720 / #2635 | 新 head `05a058f9c85c`；方案仍待分析，旧评审仅针对 `850a36bd9` | Region owner 明确公共机制/应用协议；维护者重审当前 diff，不自动继承旧缺陷或批准 SPSC ABI | 方案和 head |
 | #2723 | 无原生子项，有单 L2 Run DATA/STOP/drain 示例 | 常驻 owner 提炼完成关联、槽位复用、背压、停止准入与异常退出的完整合同 | 方案或具体交付 |
 | #2429 / #2666 / #2658 | #2429 已收窄为 vLLM 消费者；#2605 head `591f003be553` | 外部消费者 owner 按五项当前验收推进；普通 Async 不重复开发，系统组合交 #2658，capture 交 #2666 | 数值 / 生命周期 / 集成证据 |

以上是本地观察与建议，未向 GitHub 发评论、修改 issue、关闭旧 PR 或启动实现。没有把 CI 成功自动判为 Ready。

### Host DDR 的当前设计依据

[#2657](https://github.com/hw-native-sys/simpler/issues/2657)、[#2730](https://github.com/hw-native-sys/simpler/issues/2730)、[#2731](https://github.com/hw-native-sys/simpler/issues/2731) 的 2026-11-04 正文为当前合同。L2 local mapping 与 L3 Host URMA 是两个完整交付；workspace 接入在 Stage 2 内。普通 Host Buffer 不加 allocation-wide 消费开关；映射返回新访问描述。L3 首版全子 L2 注册同一完整 backing，既不自动分表，也不强制配对 table-sized Device window。地址相关 setup 在协调 map preparation；实际 workspace 的 post-init 发布、per-task 注入和 completion ownership 必须一起交付。native Host/H2D 不受影响，首版额外独立 Host registration 明确拒绝。

设计落点为 [L3+ §4](../l3plus-design.md#4-四方向如何承载完整设计)、[用户面 §5.2](../worker-memory-model/user-surface.md#52-api-契约与已交付边界) 与 [退休契约](../worker-memory-model/access-retirement-contract.md)。这些是目标，不是对 main 的 map_view 或 Host URMA 实现声明。

## 5. 目标覆盖口径

work-items §6 / goal-sources 保存所有原生节点完整范围、逐条 checkbox、具名提案及两条决定评论。#2673 的旧 R3 batch 由 #2855 承接；#2674 历史 R1/R2 API 备选被 10 月 29 日确认方案替代；#2429 历史编号与五项当前标准同时保留，不静默换义。#2657/#2730/#2731 当前正文新增 32 个 V2 索引，原始编号继续保留溯源并明确替代/限制范围；当前与历史共 231 个 #2716 来源索引。关闭 issue 仍保留目标/验收来源，不能靠从清单删除来制造覆盖率。

所有本地编号重新从源文档提取，全部文档载体核对 owner；编号有家、GitHub 有完整业务 owner、代码已实现、硬件已验收分别判断。通用 kernel/eager-capture、HBG 调度、DFX 与跨仓模型仍在既定外部轨，必要 L2 通信接入没有被排除。
