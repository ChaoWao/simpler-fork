> **2026-10-21 接管快照**：保留原基线与历史结论；现状见[接管入口](../../../kernel-mode-cross-repo-handoff.md)，设计判断以[当前规范](../../../runtime-design-principles.md)为准。此页不授权恢复历史任务、timer 或自动化。

# A5 HBG 提前入队：一个完整 PR 的实施合同

状态：用户于 2026-09-29T07:09:27.708053+00:00 明确批准，交付一个 PR；实现与发布授权，不合并。
依据：原作者 `.docs/p4-after-2466-next-decision.md` revision 3，检查基线 fb76a54d5；当前 main 7b614764b。实施前复核最新主线，已有变动不直接照搬旧行号。

## 为什么做

当前 A5 HBG 已能在 A 执行时准备 B 的图与参数，但 B 必须等 A 结束才提交。此 PR 使显式 `launch_depth=2` 时 B 在 A 执行期间进入 RTS 队列；设备依旧先执行整个 A，再执行整个 B。A 的资源安全退休后让 C 补入，持续处理普通 Worker.submit。

## 范围与工作过程

- 本地 L3 根 Worker、单 chip endpoint、A5 onboard HBG program、每 run 一个任务；沿用 A5 现有两套 run 资源。遵守既有输入/调用方地址准入，不扩展不安全 DEVICE 指针的提前提交。
- 默认 launch_depth=1 不变。不增加资源深度或配置项；诊断独占、不支持路径的既有行为保持。
- 两条持久 RTS stream 使用现有一条 CrossRun 引用：core 为 `A → A_core_end → wait(A_whole) → B_wait_proof → B`；CPU 为 `A → wait(A_core_end) → A_whole → B → wait(B_core_end) → B_whole`。CPU FIFO 提供第二条顺序约束；设备无跨 run 并行。
- 只有成功提交整算子边界后才允许后继引用。等待消费证明位于 B 核函数之前，使 A 正常退休不用等待整个 B。
- poll/drain 状态按资源集及 run 身份区分，不能因为 A 已排干就把 B 报成完成。调试读回使用各 run 自己的 core 数，允许不同形状。

## 失败与资源寿命

- 等待提交一旦可能对设备可见就登记引用并标记部分提交，不能回滚为未启动。通用 API 失败向上传播；只有既有明确 unsupported/decline 分支可退回普通顺序提交，NotStarted 本身不代表自动重试许可。
- 正常 drain 先证明事件引用已消费，再结束对应资源使用。证明失败时走有界排干；仍无法证明则停止该 runner 的新提交并向调用方报错。
- poison 只是禁止继续提交，不是设备已停止。仍可能被设备或后继引用的工作区、调用方借用和 event 必须保留/隔离；不能凭 fence 的日志拒绝来声称整个 lease 已保留。已独立证明结束的资源不需一概保留到 reset。
- cleanup 在 discharge 后再读取 poison；错误经 completion_rc、原 mailbox 和 RunHandle 返回。只有实际确认停止/重置才能按现有协议结束未知引用；故障可能影响同一 runner 的其他 run。

## 代价与边界

- 最多新增两个 ACL 等待消费证明 event，保留到 runner 关闭；若使用时间戳验收，再增加最多两个 timestamp event。增加按 run 的状态、事件调用及引用维护。
- 不增 mailbox、arena bank 或单块容量上限。两套资源同时使用更久，内存为各资源集实际形状的容量高水位之和，不承诺任意负载峰值不变。
- 未证明引用消费的失败分支可能同步整个 stream 并等后继。不承诺性能收益。
- 不开放 TMR、sim、kernel、remote/MPI、多 endpoint、capture/replay；不宣称已实现进程级统一逻辑 stream。

## 交付与验收

一个 PR 同时完成能力接线、run 状态、引用退休和测试。核心改动为 A5 runner/HBG runtime，以及公共 print_handshake_results 参数和 A3 调用点；若发现其他共享接口必须变更，应先交具体证据。

真实 Worker.submit 验证 A 未结束而 B 已 accepted、设备整算子串行、结果正确、16 次连续补队、不同 core 数、失败后引用保留、默认和诊断行为。不以 host 收尾重叠或单次时间戳相等推断提交因果。

不是 #2473 的前置或阻塞项。新架构合同须批准后才实施。
