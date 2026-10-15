# Open kernel PR 去留核查（2026-09-19）

## 结论

建议关闭 10 个向 main 提交的旧栈 PR：#2173、#2174、#2175、#2180、#2185、#2187、#2189、#2190、#2193、#2199。关闭含义是撤下旧交付形状、保留贡献和仍需交付的能力，不是“都已进入 main”。

#2253/#2280/#2289 暂保留在集成线作为测试、真实修复和 HBG 实验来源；#2306 当前 STOP policy 不采纳，须重写，若不改则关闭。后经用户明确授权，已关闭上述 10 个 PR 并逐条说明替代路线和保留能力；未删除分支。其余 PR 未修改。

## 范围与证据限制

- GitHub 当前 open 列表 58 项；扫查全部 title/body/base，重点读 14 个 kernel PR 的完整 diff 文件列表、相关实现和人工评论。不是对 58 个 PR 的逐行正确性审批，没有跑构建/onboard，也未独立复跑旧实验。
- main 基线 `2d3661517`，#2360 合入 `c0d2c52b`；kernel 集成线 `6cde5929`。PR heads 见下表。原始 API/差异证据保存在 `/tmp/kernel-pr-triage/`（临时缓存）。
- 按 [设计原则](../runtime-design-principles.md) 和 [ROADMAP](../ROADMAP.md)：Pipeline A → kernel eager → capture/replay；共享执行与退休；每次 eager 准备并提交一次。并不因为一个符号名含 prepare/cache 就判它是被撤回的 PreparedCall。
- GitHub 的 merged 必须连同 base 解读：#2216/#2247/#2250 等进入 integration branch，不代表进入 main。

## 关键源码与沿革

1. [集成记录（固定 tip）](https://github.com/hw-native-sys/simpler/blob/6cde5929/INTEGRATION-LOG.md)：D2 记录吸收 HBG/K4/K5/K3 等原始贡献；D6 记录 K4/K5 叠栈处理；D20 记录 #2185 加固；D22 记录最新 #2190。此文件包含撤回决策，必须读后续修订，不能把 D9 的旧拓扑当现行结论。
2. [主线 KernelEventKind](https://github.com/hw-native-sys/simpler/blob/2d3661517/src/common/platform/include/host/kernel_execution_state.h)：Start/AicoreStart/AicoreDone/AicpuDone/SerialTail；明确不允许 prepare 发布被 captured launch 等待的 tail。
3. [#2187 enqueue_kernel_launch_sequence](https://github.com/hw-native-sys/simpler/blob/136e9712d22c495ac921a6900c8f24fa9b8ebcf3/src/common/platform/onboard/host/kernel_launch_sequence.h)：consume_prepare_tail 及 caller 双分支仍在。
4. [主线 setup_static_arena](https://github.com/hw-native-sys/simpler/blob/2d3661517/src/common/platform/onboard/host/device_runner_base.cpp)：整个 kernel arena 变更预检先于第一次提交；拒绝直接 return，避开统一 rollback。
5. [主线 RetainedTempBump::begin](https://github.com/hw-native-sys/simpler/blob/2d3661517/src/common/utils/retained_temp_bump.h)：仍有增长释放旧地址的路径，不能把 #2193 的全部目标标成完成。
6. [当前集成线 enqueue_kernel_launch_sequence](https://github.com/hw-native-sys/simpler/blob/6cde5929/src/common/platform/onboard/host/kernel_launch_sequence.h)：clear 用 h.aicpu、cancel 用 h.caller，#2280 修复的对象仍存在。
7. [#2306 create_hidden_stream](https://github.com/hw-native-sys/simpler/blob/a11883c552f647ae62423a1f0dd5263ec154de22/src/common/platform/onboard/host/kernel_platform_ops.cpp)：新增 STOP 设置。其 Context 级影响与关闭验证缺口由 [PR 自己披露](https://github.com/hw-native-sys/simpler/pull/2306)，本轮未独立测试平台语义。

## 逐项 disposition

### [#2173 — Add: [Context Prepare] prepare and freeze kernel resources](https://github.com/hw-native-sys/simpler/pull/2173)

- 核查 head：`15741ac05b24908c71703c72f2549f5fe0e9fa53`；base：`main`。
- **建议：关闭旧 PR；按 K1 迁移需要的资源能力。**
- 依据：HBG prepare/freeze 是累计旧栈，含旧 K1/资源合同；集成线 #2216 已吸收。固定 workspace 的能力仍需要，但要接当前 owner、capacity 和 retirement。
- 保留/迁移：保留 resource plan、对齐/容量预检、失败保有资源测试。

### [#2174 — Add: [H2] serialize immutable HBG packets for HostArgs](https://github.com/hw-native-sys/simpler/pull/2174)

- 核查 head：`1fe6bf53f0af9b6a02eeb2edff080a8bc7e7467a`；base：`main`。
- **建议：关闭旧 PR；将机制归入 C1/C3。**
- 依据：累计 H2 包含前置栈；旧 packet 按 frozen capacity 携带非 heap image，集成线 #2289 已改 live spans/version 3。当前主线 HBG pending publication 已由 #2342/#2353 交付，不能把旧上传路径再铺回来。
- 保留/迁移：保留相对地址序列化、HostArgs 传输实验、pristine restore 需求；同步 pending publication 不等于 replay restore 已完成。

### [#2175 — Add: [H3] seal and validate HBG execution slots](https://github.com/hw-native-sys/simpler/pull/2175)

- 核查 head：`89b00a9cad3bdc32dee3ec2ab1a65d21a8a23c20`；base：`main`。
- **建议：关闭旧 PR；迁移 admission 校验。**
- 依据：累计 H3 基于旧 packet/slot ABI；独立可信 registry 的判断仍正确，不能随整栈重放旧布局。
- 保留/迁移：保留 packet 不能自证写地址、bounds/generation 校验和拒绝不修改目的区的测试。

### [#2180 — Add: TMR kernel invocation snapshots and native transport adapter（K4）](https://github.com/hw-native-sys/simpler/pull/2180)

- 核查 head：`b0943525dd8eaeb486bca92bbf5480a37eb61fcb`；base：`main`。
- **建议：关闭旧 PR；迁移单次参数快照。**
- 依据：K4 连同旧 K1/K2 整栈提交；集成线 INTEGRATION-LOG D6 已记录采用其 K4 链。单次值快照仍需要接共同准备/提交层。
- 保留/迁移：保留 tensor/scalar 编码、host 生命周期、bounded decoder 和传输探针。TmrEncodingCache 不等于已撤回的 PreparedCall store，关闭理由不是名字里有 cache。

### [#2185 — Add: kernel-mode entry layer — give the four C entries callers (⑩a)](https://github.com/hw-native-sys/simpler/pull/2185)

- 核查 head：`12600b87b1e858708fd0952f582305452308aafc`；base：`main`。
- **建议：关闭旧 PR；K1/K2 采用已修订入口能力。**
- 依据：入口加固已经在集成线 #2247 接替，Worker 接入又由 #2248 扩展。原 PR 的平台 stub/UNSUPPORTED 测试前提也不能代表当前 main。
- 保留/迁移：保留带 code 的异常、借用设备拒绝路径、close 失败保有 owner、Worker/PyTorch 接入验收。

### [#2187 — Add: [Binder] standalone three-stream kernel submission](https://github.com/hw-native-sys/simpler/pull/2187)

- 核查 head：`136e9712d22c495ac921a6900c8f24fa9b8ebcf3`；base：`main`。
- **建议：关闭旧 PR；旧协议不能接入。**
- 依据：head 136e9712 的 enqueue_kernel_launch_sequence 仍消费 PrepareTail，并由 caller 同时 fork/join 两侧；main KernelEventKind 已固定 caller↔AICPU↔AICore 链。集成线已有修订实现，且仍有 #2280 修复要吸收。
- 保留/迁移：保留逐步失败注入及 partial enqueue/compensation 测试，按新链和 Pipeline A 状态重写。

### [#2189 — Add: TMR kernel resident arguments and invocation isolation（K5）](https://github.com/hw-native-sys/simpler/pull/2189)

- 核查 head：`b6435e4858b1e0443966d664cca478276baefa55`；base：`main`。
- **建议：关闭旧 PR；迁移 resident/invocation 分离。**
- 依据：K5 连同 K4/K2 累计提交；集成线 #2216 已吸收，之后注册、init 和 DFX 继续演化。原 PR 明确 DFX unsupported，不能当作最终 kernel 能力。
- 保留/迁移：保留稳定配置与逐调用值分离、executor-owned 参数存储、最后读者释放。

### [#2190 — Add: bounded kernel callable registration](https://github.com/hw-native-sys/simpler/pull/2190)

- 核查 head：`a8b19e8a0a707797ab24d2832c48d078b971686b`；base：`main`。
- **建议：关闭旧整包 PR；另拆当前 main 的常驻注册能力。**
- 依据：稳定地址、预算内增长、无淘汰的注册方向符合原则；但当前 head 还引入 capture 内 prepare、RELAXED 同步上传及独立 dispatch ABI。集成线 D22/#2290 已吸收并继续修改。不能原样作为当前路线下一步。
- 保留/迁移：保留有界代码池、稳定 callable handle、容量失败保护测试；注册放 capture 外，错误/保活接共同执行层。8192/2GiB 是旧具体取值，不自动成为新方案硬约束。

### [#2193 — Fix: kernel-mode capacity refusals no longer free what they protect (⑤K3)](https://github.com/hw-native-sys/simpler/pull/2193)

- 核查 head：`b8e739d9d8143ca6f35bf2177241976f900e2ab7`；base：`main`。
- **建议：关闭旧 PR；剩余缺口单独跟进。**
- 依据：main setup_static_arena 已在任何 commit_region 前整体预检并拒绝，旧 arena 破坏性拒绝已解决。旧补丁修改两份 runtime_maker.cpp 的 RetainedTempBump，如今实现已在 src/common/utils/retained_temp_bump.h。
- 保留/迁移：当前 RetainedTempBump::begin 仍可 free+malloc 增长；未来 graph/有效引用下的地址保有必须另做。当前 kernel launch 仍 disabled，不能把它报告成已复现的 main replay 回归。

### [#2199 — Add: TMR kernel-mode execution coordination and teardown（K7）](https://github.com/hw-native-sys/simpler/pull/2199)

- 核查 head：`2d02e0a82ab62b8a0bc9f5df1ef620b863521598`；base：`main`。
- **建议：关闭旧 PR；按共同生命周期迁移 device 协议。**
- 依据：TMR K7 已在集成线 #2250 接替，之后 #2290/#2294 继续修正注册/驻留。旧栈公开 launch 仍 fail-closed，不能和后续集成版本各维护一份。
- 保留/迁移：保留统一 init verdict、cancel、双侧退出、最后读者、fault-before-Done；对齐 Pipeline A 的 result/health/retirement，不盲目丢掉 device 协调。

### [#2253 — Add: strengthen TMR kernel-mode eager and ACLGraph scene tests](https://github.com/hw-native-sys/simpler/pull/2253)

- 核查 head：`8d6240b790c9c7a25d8ce929c53570c28ab8a19a`；base：`feat/kernel-mode-integration-test`。
- **建议：暂保留：验收测试来源，目标仍为集成线。**
- 依据：这是测试增量，mixed operator、A/B/A、参数几何、多图寿命等仍符合目标，不能因为依赖旧入口就丢掉场景。
- 保留/迁移：K2/C3/C4 迁移测试与数值 oracle；旧分支通过不冒充 current main 通过。迁移完成后关闭旧测试 PR。

### [#2280 — Fix: keep the host cancel ordered after the handshake clear](https://github.com/hw-native-sys/simpler/pull/2280)

- 核查 head：`84c44df59a6a7e43780ba39ddea2f49e0ac2e4ab`；base：`feat/kernel-mode-integration-test`。
- **建议：保留：集成线真实协议修复。**
- 依据：当前集成线 clear 在 h.aicpu、compensation cancel 在 h.caller；PR 将 cancel 改为 h.aicpu，消除同一字的跨流无序。main 尚无该 kernel_launch_sequence 接线。
- 保留/迁移：继承 binder 时必须带修复及负对照；这不是需要向 main 空搬的独立功能。

### [#2289 — Add: [HBG kernel] connect graph execution](https://github.com/hw-native-sys/simpler/pull/2289)

- 核查 head：`917f5c68f7bd72793f81fba715f88411027f8dcf`；base：`feat/kernel-mode-integration-test`。
- **建议：暂保留为 HBG 实验/迁移来源；不整包并 main。**
- 依据：实际 HBG eager/replay、内部 intermediate 和 live image 改进有明确价值，且已有独立 reviewer 的局部真机复验评论。代码仍接集成线 kernel owner/binder，非当前共同 program 执行链。
- 保留/迁移：K1 取显式 tensor 合同；C1/C3 取 pristine restore、重定位/独立校验和 live-span；保留 A3 数值测试。不能据此跳过 Pipeline A、PyTorch 入口或 A5 验收。

### [#2306 — Remove extra TMR result-check task and use RTS fail-stop errors (K7)](https://github.com/hw-native-sys/simpler/pull/2306)

- 核查 head：`a11883c552f647ae62423a1f0dd5263ec154de22`；base：`feat/kernel-mode-integration-test`。
- **建议：当前方案拒绝；重写错误传播后再保留为实现 PR。**
- 依据：diff 在 stream 创建时调用 aclrtSetStreamFailureMode(...ACL_STOP_ON_FAILURE)。PR 自己明确 A2/A3 上作用可扩至整个借用 Context，且 STOP 后同进程 close 未验证。与不擅改 caller 全局策略的原则不符。
- 保留/迁移：保留 eager event 不等于错误传递的 A/B 实验。不要简单删 STOP 又宣称成功；需完成共同 result/health/retirement 和 caller 可见错误合同。若维持当前政策不改，建议关闭为设计不采纳。

## 邻近工作不随旧 kernel 栈清理

- #2354 是当前 Pipeline A 健康证据工作，保留；它目前仅记录证据，不停准入、不删除 sync，不能据此宣称 P2/P4 已通过。
- #2355 的 run identity/DFX 也保留其自身评审流程；profile 的 capture 不是 ACLGraph capture。
- #2028 是旧 program queue 实现，与 Pipeline A 有重叠，交给 A 路线做独立去留对照。本次不把它列入 kernel 关闭批次，也不依据旧 CI 直接推荐合入。
- #1987/#1967 同属 program 生命周期/多 rank 顺序，应由 A 路线核对，不自动打包关闭。
- #2120 是 CommDomain retention 公共接口，不能因 persistent 一词误判为旧 kernel PreparedCall。
- 其余 open PR 本轮未发现需要按“旧 kernel 路线”关闭的依据；这不构成正确性批准。

## 执行顺序建议

1. 关闭第一批 10 个旧 main PR 时，逐条说明仍需保留的能力、集成参考及迁移目的，使用 superseded/implementation route replaced，不写 merged into main，不删除贡献分支。
2. 将 #2193 retained-temp 的剩余保活合同和 #2190 的有界常驻注册纳入 K1/C2 的具名验收项；不能只关掉票就算完成。
3. #2253/#2280/#2289 标明实验/迁移角色；不将 integration 整包反向合并 main。
4. #2306 必须改变错误策略，或关闭当前实现并保留它证明的错误传播缺口。
5. 生产接线继续遵循 A → K1/K2 → C1–C4，从现有实现选择具体可复用机制，不重新发明已验证部分。

## 58 项 open PR 清单

以下是本轮查询快照，不承诺后续状态不变。

| PR | 标题 | base | 本轮分类 |
| --- | --- | --- | --- |
| [#2363](https://github.com/hw-native-sys/simpler/pull/2363) | Refactor: retire the unread ready_queue_shards field | `main` | 未列入旧 kernel 关闭批次 |
| [#2355](https://github.com/hw-native-sys/simpler/pull/2355) | Add: carry run identity through the swimlane export, join and trace | `main` | 未列入旧 kernel 关闭批次 |
| [#2354](https://github.com/hw-native-sys/simpler/pull/2354) | Record the device fault channel's evidence per device generation | `main` | 未列入旧 kernel 关闭批次 |
| [#2323](https://github.com/hw-native-sys/simpler/pull/2323) | Add: expose the device nocache-alias offset to incore kernels | `main` | 未列入旧 kernel 关闭批次 |
| [#2310](https://github.com/hw-native-sys/simpler/pull/2310) | Update: ground weekly-changelog entries in the tree, not the PR body | `main` | 未列入旧 kernel 关闭批次 |
| [#2306](https://github.com/hw-native-sys/simpler/pull/2306) | Remove extra TMR result-check task and use RTS fail-stop errors (K7) | `feat/kernel-mode-integration-test` | 当前方案拒绝；重写错误传播后再保留为实现 PR |
| [#2304](https://github.com/hw-native-sys/simpler/pull/2304) | Add: correlate remote task frames across hosts | `main` | 未列入旧 kernel 关闭批次 |
| [#2302](https://github.com/hw-native-sys/simpler/pull/2302) | Support: pass CPU-sim target through kernel compilation | `main` | 未列入旧 kernel 关闭批次 |
| [#2300](https://github.com/hw-native-sys/simpler/pull/2300) | Add: record why A5 does not port the full a2a3 retirement | `main` | 未列入旧 kernel 关闭批次 |
| [#2299](https://github.com/hw-native-sys/simpler/pull/2299) | Refactor: rename in-graph tasks to sub-tasks and reshape their ids | `main` | 未列入旧 kernel 关闭批次 |
| [#2289](https://github.com/hw-native-sys/simpler/pull/2289) | Add: [HBG kernel] connect graph execution | `feat/kernel-mode-integration-test` | 暂保留为 HBG 实验/迁移来源；不整包并 main |
| [#2288](https://github.com/hw-native-sys/simpler/pull/2288) | Add: broadcast AICore EXIT to the owned set before waiting | `main` | 未列入旧 kernel 关闭批次 |
| [#2287](https://github.com/hw-native-sys/simpler/pull/2287) | Add: stamp the A5 AICore exit handshake as a device phase | `main` | 未列入旧 kernel 关闭批次 |
| [#2284](https://github.com/hw-native-sys/simpler/pull/2284) | Fix: run graph control work before the sync_start drain check | `main` | 未列入旧 kernel 关闭批次 |
| [#2282](https://github.com/hw-native-sys/simpler/pull/2282) | fix: Configure tensor wait budgets through the Host timeout path | `main` | 未列入旧 kernel 关闭批次 |
| [#2280](https://github.com/hw-native-sys/simpler/pull/2280) | Fix: keep the host cancel ordered after the handshake clear | `feat/kernel-mode-integration-test` | 保留：集成线真实协议修复 |
| [#2253](https://github.com/hw-native-sys/simpler/pull/2253) | Add: strengthen TMR kernel-mode eager and ACLGraph scene tests | `feat/kernel-mode-integration-test` | 暂保留：验收测试来源，目标仍为集成线 |
| [#2252](https://github.com/hw-native-sys/simpler/pull/2252) | Fix: align L2 Chip swimlanes with Host runner windows | `main` | 未列入旧 kernel 关闭批次 |
| [#2244](https://github.com/hw-native-sys/simpler/pull/2244) | Fix: adapt host build graph swimlane clock alignment | `main` | 未列入旧 kernel 关闭批次 |
| [#2218](https://github.com/hw-native-sys/simpler/pull/2218) | Update: remove host and device clock anchors | `main` | 未列入旧 kernel 关闭批次 |
| [#2217](https://github.com/hw-native-sys/simpler/pull/2217) | Place a peer host's work by the frame that dispatched to it | `main` | 未列入旧 kernel 关闭批次 |
| [#2199](https://github.com/hw-native-sys/simpler/pull/2199) | Add: TMR kernel-mode execution coordination and teardown（K7） | `main` | 关闭旧 PR；按共同生命周期迁移 device 协议 |
| [#2198](https://github.com/hw-native-sys/simpler/pull/2198) | Refactor: Region adopts canonical Buffer identity | `main` | 未列入旧 kernel 关闭批次 |
| [#2193](https://github.com/hw-native-sys/simpler/pull/2193) | Fix: kernel-mode capacity refusals no longer free what they protect (⑤K3) | `main` | 关闭旧 PR；剩余缺口单独跟进 |
| [#2190](https://github.com/hw-native-sys/simpler/pull/2190) | Add: bounded kernel callable registration | `main` | 关闭旧整包 PR；另拆当前 main 的常驻注册能力 |
| [#2189](https://github.com/hw-native-sys/simpler/pull/2189) | Add: TMR kernel resident arguments and invocation isolation（K5） | `main` | 关闭旧 PR；迁移 resident/invocation 分离 |
| [#2187](https://github.com/hw-native-sys/simpler/pull/2187) | Add: [Binder] standalone three-stream kernel submission | `main` | 关闭旧 PR；旧协议不能接入 |
| [#2186](https://github.com/hw-native-sys/simpler/pull/2186) | Fix: connect upstream PTO ISA FFTS to simulator state | `main` | 未列入旧 kernel 关闭批次 |
| [#2185](https://github.com/hw-native-sys/simpler/pull/2185) | Add: kernel-mode entry layer — give the four C entries callers (⑩a) | `main` | 关闭旧 PR；K1/K2 采用已修订入口能力 |
| [#2183](https://github.com/hw-native-sys/simpler/pull/2183) | Update: select A5 cluster ownership from trusted topology | `main` | 未列入旧 kernel 关闭批次 |
| [#2180](https://github.com/hw-native-sys/simpler/pull/2180) | Add: TMR kernel invocation snapshots and native transport adapter（K4） | `main` | 关闭旧 PR；迁移单次参数快照 |
| [#2179](https://github.com/hw-native-sys/simpler/pull/2179) | Refactor: make host_build_graph AICPU threads symmetric | `main` | 未列入旧 kernel 关闭批次 |
| [#2175](https://github.com/hw-native-sys/simpler/pull/2175) | Add: [H3] seal and validate HBG execution slots | `main` | 关闭旧 PR；迁移 admission 校验 |
| [#2174](https://github.com/hw-native-sys/simpler/pull/2174) | Add: [H2] serialize immutable HBG packets for HostArgs | `main` | 关闭旧 PR；将机制归入 C1/C3 |
| [#2173](https://github.com/hw-native-sys/simpler/pull/2173) | Add: [Context Prepare] prepare and freeze kernel resources | `main` | 关闭旧 PR；按 K1 迁移需要的资源能力 |
| [#2165](https://github.com/hw-native-sys/simpler/pull/2165) | Fix: memset TMR payload/slab buffers before orch starts | `main` | 未列入旧 kernel 关闭批次 |
| [#2164](https://github.com/hw-native-sys/simpler/pull/2164) | Fix: size the ring task window for host_build_graph PA Case1/Case2 | `main` | 未列入旧 kernel 关闭批次 |
| [#2157](https://github.com/hw-native-sys/simpler/pull/2157) | feat(a5): add RDMA deferred completion backend for tensormap_and_ringbuffer | `main` | 未列入旧 kernel 关闭批次 |
| [#2146](https://github.com/hw-native-sys/simpler/pull/2146) | Fix: 修正单元素维 stride 的连续性判定 | `main` | 未列入旧 kernel 关闭批次 |
| [#2138](https://github.com/hw-native-sys/simpler/pull/2138) | Update: prewarm each chip with one internal TMR task | `main` | 未列入旧 kernel 关闭批次 |
| [#2120](https://github.com/hw-native-sys/simpler/pull/2120) | Add: Worker.detach_persistent_domain public retention API | `main` | 未列入旧 kernel 关闭批次 |
| [#2116](https://github.com/hw-native-sys/simpler/pull/2116) | feat(worker): add extensible chip-child control dispatch | `main` | 未列入旧 kernel 关闭批次 |
| [#2114](https://github.com/hw-native-sys/simpler/pull/2114) | Add: SIMPLER_TENSOR_DATA_TIMEOUT_MS to override the tensor-data wait | `main` | 未列入旧 kernel 关闭批次 |
| [#2097](https://github.com/hw-native-sys/simpler/pull/2097) | Add: A5 RTT die-preflight scheduler placement | `main` | 未列入旧 kernel 关闭批次 |
| [#2096](https://github.com/hw-native-sys/simpler/pull/2096) | Add: comm region template and SPSC message queue  | `main` | 未列入旧 kernel 关闭批次 |
| [#2076](https://github.com/hw-native-sys/simpler/pull/2076) | Perf: align A5 TMR hot function entries | `main` | 未列入旧 kernel 关闭批次 |
| [#2067](https://github.com/hw-native-sys/simpler/pull/2067) | Reduce redundant host_build_graph fanin edges | `main` | 未列入旧 kernel 关闭批次 |
| [#2066](https://github.com/hw-native-sys/simpler/pull/2066) | Add: an AUTO-scope twin of the qwen3_14b_decode scene test | `main` | 未列入旧 kernel 关闭批次 |
| [#2059](https://github.com/hw-native-sys/simpler/pull/2059) | Adapt simpler to unified PTO-ISA async workspace interface | `main` | 未列入旧 kernel 关闭批次 |
| [#2028](https://github.com/hw-native-sys/simpler/pull/2028) | Add: queue consecutive runs on shared streams | `main` | 未列入旧 kernel 关闭批次 |
| [#2026](https://github.com/hw-native-sys/simpler/pull/2026) | Feature: enable SDMA and URMA in the default A5 runtime | `main` | 未列入旧 kernel 关闭批次 |
| [#1997](https://github.com/hw-native-sys/simpler/pull/1997) | Add: sustained four-chip step jitter reproducer | `main` | 未列入旧 kernel 关闭批次 |
| [#1987](https://github.com/hw-native-sys/simpler/pull/1987) | Fix: stabilize depth-two chip run lifecycle | `main` | 未列入旧 kernel 关闭批次 |
| [#1967](https://github.com/hw-native-sys/simpler/pull/1967) | Fix: align full-rank chip run activation | `main` | 未列入旧 kernel 关闭批次 |
| [#1938](https://github.com/hw-native-sys/simpler/pull/1938) | test(st): L3 dispatch runs the callable it names | `main` | 未列入旧 kernel 关闭批次 |
| [#1903](https://github.com/hw-native-sys/simpler/pull/1903) | Support: add l2-orchestrator-standalone bench for the L2 submit path | `perf/hbg-orch` | 未列入旧 kernel 关闭批次 |
| [#1888](https://github.com/hw-native-sys/simpler/pull/1888) | Add MPI direct L3 transport, runtime, and vector-add example. | `main` | 未列入旧 kernel 关闭批次 |
| [#1810](https://github.com/hw-native-sys/simpler/pull/1810) | Fix: track reader anti-dependencies in TensorMap | `main` | 未列入旧 kernel 关闭批次 |

## 关闭执行记录

用户明确授权后，逐项复核 head 未变，关闭并重新查询确认 10/10 为 CLOSED，且各有对应说明。

- [#2173 关闭说明](https://github.com/hw-native-sys/simpler/pull/2173#issuecomment-5740488331)
- [#2174 关闭说明](https://github.com/hw-native-sys/simpler/pull/2174#issuecomment-5740488714)
- [#2175 关闭说明](https://github.com/hw-native-sys/simpler/pull/2175#issuecomment-5740489017)
- [#2180 关闭说明](https://github.com/hw-native-sys/simpler/pull/2180#issuecomment-5740489402)
- [#2185 关闭说明](https://github.com/hw-native-sys/simpler/pull/2185#issuecomment-5740489713)
- [#2187 关闭说明](https://github.com/hw-native-sys/simpler/pull/2187#issuecomment-5740490024)
- [#2189 关闭说明](https://github.com/hw-native-sys/simpler/pull/2189#issuecomment-5740490341)
- [#2190 关闭说明](https://github.com/hw-native-sys/simpler/pull/2190#issuecomment-5740490697)
- [#2193 关闭说明](https://github.com/hw-native-sys/simpler/pull/2193#issuecomment-5740491040)
- [#2199 关闭说明](https://github.com/hw-native-sys/simpler/pull/2199#issuecomment-5740491312)
