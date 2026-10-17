# Pipeline A 设计资料

本分支保存重要设计资料的可阅读快照，来源为 manager 管理的本地设计文件。
最近同步的主线基线：`aa91a9d60e0e7ce22cdae3cf70533ece46875546`。文档导出日期：2026-10-17。
HTML 可下载后用浏览器打开；Markdown 可直接在 GitHub 阅读。

## 先读什么

1. [运行时原则](runtime-design-principles.md)：串行设备执行、parent FIFO、最后消费者退休、资源背压。
2. [路线与阶段表](run-pipeline-track-a.md) / [HTML 看板](run-pipeline-track-a.html)：交付、依赖与剩余工作。
3. [内存状态报告](memory-current-state.html)：内存类型、所有权与生命周期。
4. [DFX 完整设计](review/dfx-joined-launch-complete-design.md)及其[实施合同](review/dfx-joined-launch-complete-decision.md)。

## 当前状态边界

- 步骤一有界验收，步骤二 #2447/#2456，步骤三 #2471/#2511/#2568/#2592，以及可配置资源容量 #2571 已合入。
- 默认自动资源准入首版 [#2596](https://github.com/hw-native-sys/simpler/pull/2596) 已发布，未Ready：原作者正在补齐分配压力下闲置资源组回收、可退休前驱等待和实际分配边界回归，并适配已合入#2595；CI格式检查失败后测试跳过。不要求调用者配置多个depth，自动模式Qwen组合仍需另验。
- 旧固定4/2与N+1 Qwen方案已撤回。[现行要求](review/p4-qwen-grant-composition-decision.md)替代该方案。[具体实施合同](review/p4-auto-resource-admission-decision.md)和[收口评审](review/p4-auto-resource-admission-manager-review-r21.md)已经归档且获用户批准，原作者正在实施。
- #2588 当前da7460e15在main e994上保留PMU初始化/结束独立观察，修正泳道扩展发布与预算边界，单提交。A3 PMU全零根因仍未解决，新CI在途；证据推断和超时尾项由原作者继续处理，未Ready。旧b2a7的A5通过不是新head验收；A5 HBG ArgsDump仍延期。
- [#2592](https://github.com/hw-native-sys/simpler/pull/2592)已合入：可配置容量下六请求636步Qwen DEVICE组合，合入补丁与已审head一致，最终CI18成功、1跳过。限定单A3 HBG、固定形状、关闭诊断；自动准入模式与该workload的组合仍需另验。
- 历史合同中“是否批准”“待合入”等文字属于当时决策记录，不是当前用户待办；以本索引和路线表辨别阶段。固定形状vLLM eager、动态请求范围、capture/replay及最终性能报告不是当前已交付能力。

## 文档目录

### 当前原则与进度

- [Simpler 运行时总体原则](runtime-design-principles.md)
- [Pipeline A：当前路线、阶段状态与交接](run-pipeline-track-a.md)
- [run-pipeline-track-a.html](run-pipeline-track-a.html)
- [memory-current-state.html](memory-current-state.html)

### 设计基线（历史，按文内基线阅读）

- [Run-pipeline 重构设计 v7 — 分两轨：program 自然排队 / kernel 接入](run-pipeline-redesign.md)
- [run-pipeline-redesign.html](run-pipeline-redesign.html)
- [Workspace：按位置、写入方与最后使用者分类](workspace-memory-principles.md)
- [§2.4 连续 DFX 采集：五 collector 代码核查矩阵与会话设计](dfx-continuous-collection-matrix.md)

### 普通异步合同

- [Stream 架构：用户确认的设计前提](review/stream-architecture-user-contract.md)
- [P4 重启：DEVICE tensor 连续异步调用](review/p4-device-chain-decision.md)
- [普通异步：解除固定 2/3 套资源限制](review/p4-dynamic-run-resources-decision.md)
- [普通异步：默认按资源准入与背压](review/p4-qwen-grant-composition-decision.md)

### 默认自动准入（已合入，组合验收另列）

- [完整实施合同](review/p4-auto-resource-admission-decision.md)
- [设计收口评审及勘误](review/p4-auto-resource-admission-manager-review-r21.md)

### DFX完整接线（已批准，PR尚未完成）

- [DFX 与普通提前入队配合：完整功能实施合同](review/dfx-joined-launch-complete-decision.md)
- [Joined-launch DFX: implementation design and decision](review/dfx-joined-launch-complete-design.md)
- [A5 HBG逐任务参数采集补充设计（用户已延期）](review/dfx-a5-task-args-capture-decision.md)

### 上传与内存设计合同

- [注册时建立函数表，run 仅引用 — USER APPROVED](review/dynamic-registration-once-decision.md)
- [TMR 调用参数进入 launch：可审批合同](review/dynamic-invocation-launch-decision.md)
- [上传支线下一步：相同执行配置不重复上传](review/dynamic-warm-prefix-cache-decision.md)
- [上传支线下一步：复用 A5 HBG 调度状态存储](review/dynamic-hbg-scheduler-retention-decision.md)
- [默认工作区管理：一个 PR 的实施合同](review/memory-default-management-decision.md)
- [本地 L3：默认管理四类工作区 — 用户已批准](review/memory-local-l3-default-management-decision.md)
- [HBG 图定义随 KernelArgs 传入：实施合同](review/memory-graph-kernelargs-decision.md)

### 验收与成本分析（按文内证据基线阅读）

- [Ordinary async: real Worker.submit acceptance inventory and warm-prepare cost map](review/ordinary-async-acceptance-and-cost-map.md)
- [Ordinary async acceptance/cost map — manager review](review/ordinary-async-acceptance-manager-review.md)

## archive-boundary

## 归档边界

只发布设计、合同、选定分析和进度快照。不发布自动化脚本、会话记录、进程ID、CI原始日志、调度账本、测试输出或本机环境配置。
原文指向未归档的本地审查/操作证据时，链接转到本节；其名称仍作为原始引用保留，不意味着这些证据已上传。
本机绝对路径已换成通用占位符；代码链接固定到各文件导出时的基线；未变化文件保留既有快照与链接。设计内容与历史证据不因导出而成为新的支持或性能承诺。

[导出清单与原文/副本指纹](manifest.json)记录本次选取范围。后续设计或进度有实质更新时同步本分支，检查状态、脱敏和链接后提交备份；无变化不创建提交。
