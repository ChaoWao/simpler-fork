# P4 重启：DEVICE tensor 连续异步调用

设计归档快照：状态与阅读顺序见[索引](../README.md)。设计约束、历史合同和当前实现须分别阅读。

状态：用户明确要求“重启，按照你的设计开展”，批准以下范围实现并交付一个完整 PR；复用 pipeline-a-p4-submission 原作者。覆盖此前对本项开发的暂停，不接管外部 Qwen/workload 集成。

## 交付行为

同设备/context、同 endpoint、本地 L3、A3 HBG program、布局已知、无诊断的有界路径，支持 `A(x) -> device y -> B(y) -> device z -> C(z)`。调用方可预先提供输出存储；后继直接使用设备地址，无需先wait前驱或中间D2H/H2D。设备仍完整算子FIFO串行，host允许提前准备/提交，连续超过两个run可持续补位。

## 一起兑现的合同

- 沿用用户的进程级逻辑串行stream方向，明确本PR实际覆盖的process/device/context/endpoint owner；不能用另建每run stream绕过串行顺序，也不能把单runner验收宣传成全部跨入口共享已完成。A3两个物理stream保持完整边界join。
- 支持具有可证明真实owner或借用期限的device buffer。框架/原分配者保留分配释放权，Simpler只保留必要使用引用直到最后设备/回拷/映射消费者结束；跨父子进程必须使用正确device地址域及导入机制。无法证明有效的裸指针仍拒绝或按既有安全路径执行，不能仅删除joinable_shape检查。
- 内容尚未产生不妨碍已知地址/布局的设备后继排队；若host构图必须读取前驱结果，仍须等待。保持调用方显式依赖合同，不新增全局tensor冲突检测或伪造动态shape。
- 区分逻辑受理、完整RTS提交、设备完成、host可读和资源退休。资源不足可背压，进度不能依赖用户逐个wait。有限launch depth为首步实现边界，不扩大为无界队列。
- Partial提交失败保留原始错误和所有可能仍被设备引用的资源，停止不安全的新准入。异常、提前释放引用、close/reset、代码重新注册均不得提前释放或覆盖在用地址。不保证设备故障只影响一个run。
- 默认行为和既有诊断独占保持；TMR/A5、跨endpoint、复杂group/SUB/通信、kernel caller-stream统一与capture实现不在本PR。

## 并行职责

P4负责设备输入输出引用、顺序、提交/完成/退休生产接线及真实入口验收；memory原作者负责内部四类workspace与capture/replay设计，dynamic负责A5 HBG调度存储，temporal负责ORCH_PHASES设计。共享文件若发生实际变更重叠，先记录接口和基线，分别原作者提交，禁止修改他人worktree或覆盖他人改动。外部负责人继续workload/依赖分析，尚未收到其对文件所有权分工的确认，不擅自代发评论或声称已协调完毕。

## 验收与成本

交付应包含真实Worker.submit的A→B→C数值与身份验证、前驱未完成后继已实际入队、FIFO、超过双槽的持续补位、提前释放/Partial/close资源安全、等待前驱不无故等待整队列。增加引用/代际追踪、可能延长buffer占用，资源不足会背压；不承诺延迟收益。

只允许真正新增且未执行的定向测试首次执行，已执行测试及既有测试不得重跑；onboard首次执行遵守硬件precheck与项目测试工作流。CI重试仍逐项授权，当前无新retry许可。git-commit/github-pr/fix-pr已获本PR发布维护授权，不合并，不新增PTO_/PTO2命名。

范围内正常接口细化由原作者完成并留可审查记录，不重新申请已批准范围。若真实代码证明必须新增跨进程资源转移/reset权或扩大模式等超范围架构，先交具体冲突与方案，不以删保护绕过。完成一个能力PR，不拆成额外报告/工具前置PR。
