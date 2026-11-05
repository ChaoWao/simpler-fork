# 资源与访问：GitHub 交接对照

> **发布快照：2026-11-05。** 本轮为资源设计与跟踪交接；不代表当前 main 全量验收。源码事实按各自 SHA 阅读，资源进度查 GitHub。参见[阅读入口](../README.md)。
>
> 本页是 2026-11-05 UTC 的一次性交接记录，不维护实施状态。旧 RA 编号仅用于解析历史引用；完整目标、负责人和进度查 GitHub。设计问题发布不等于方案确认，退役本地轨道不等于功能交付。

资源设计当前入口见 [work-items §2](../work-items.md#2-资源与访问github-入口)；共同原则见 [#2721](https://github.com/hw-native-sys/simpler/issues/2721)，待决问题见 [设计澄清评论](https://github.com/hw-native-sys/simpler/issues/2721#issuecomment-6651217998)。

| 旧编号（只读） | 设计主题 | 唯一跟踪入口 | 保留要求与边界 |
| -------------- | -------- | ------------ | -------------- |
| RA-1 | 设备标识与部署 | [#2721](https://github.com/hw-native-sys/simpler/issues/2721#issuecomment-6651217998) | 设计问题 1：逐接口核定逻辑／物理设备号；非恒等可见设备映射验收。不是已确认的通用转换层。 |
| RA-2 | A5 Host↔Device 映射 | [#2496](https://github.com/hw-native-sys/simpler/issues/2496) | 外部平台能力继续由 #2495 的子项跟踪；验证注册／注销、双向可见性与真实 consumer。 |
| RA-3 | 本地绑定与跨进程接入 | [#2460](https://github.com/hw-native-sys/simpler/issues/2460) | U2a/U2b 的本地绑定由本项跟踪；跨进程 SVM 能力另由 #2497 承担，不因本地 HOST 消费成立而算全部交付。 |
| RA-4 | 可见性与必要同步 | [#2720](https://github.com/hw-native-sys/simpler/issues/2720) | Region 方案入口；#2721 评论问题 5 要求逐路径说明可见性及应用协议边界，不预设统一 cache API。 |
| RA-5 | 显式搬运与调用请求 | [#2460](https://github.com/hw-native-sys/simpler/issues/2460) | U1a 的逐调用 transfer、身份和 IO 合同；公共模型不增加读写策略，既有 access/wire 的生产者与消费者成对迁移。 |
| RA-6 | 依赖与重叠可写别名 | [#2721](https://github.com/hw-native-sys/simpler/issues/2721#issuecomment-6651217998) | 设计问题 2：同任务 writable alias 必要性待决；#1388 独立跟踪任务间 WAR。#2450 的关闭不是 alias 已交付的证据。 |
| RA-7 | 临时／持久／异步使用寿命 | [#2721](https://github.com/hw-native-sys/simpler/issues/2721#issuecomment-6651217998) | 设计问题 3：保留寿命区分及回收验收；不承诺两套 Arena 或删除全部层级 heap。EX-5 只在机制必要性确认后承接迁移。 |
| RA-8 | 实际可访问性 | [#2721](https://github.com/hw-native-sys/simpler/issues/2721#issuecomment-6651217998) | 设计问题 6：部署、链路、OS／驱动接入和上下文共同决定访问；不预设公共 capability/grant 对象。 |
| RA-9 | 组合资源及部分失败 | [#2721](https://github.com/hw-native-sys/simpler/issues/2721#issuecomment-6651217998) | 设计问题 4：先以 Host 控制区＋Device 数据区＋通信资源检验内部组合，不预设公共 ResourceBundle；跨节点消费仍与 DS-10 交接。 |
| RA-10 | 映射／复制／完成的可观测结果 | [#2721](https://github.com/hw-native-sys/simpler/issues/2721#issuecomment-6651217998) | 设计问题 7：明确身份与完成事实，不预设公共 plan 类型或固定 event 数量。 |
| RA-11 | 共享区间与上层同步样例 | [#2720](https://github.com/hw-native-sys/simpler/issues/2720) | 公共区间及基础访问／同步由 Region 方案核定；queue 和发布协议属于应用。#2096 的具名历史证据保留于评审，不能据此关闭完整目标。 |
| RA-12 | 通信域跨 run 所有权 | [#824](https://github.com/hw-native-sys/simpler/issues/824) | 方向 2（#2722）的域寿命目标；公共 Buffer 的接纳和关闭合同与 #2460 协调，不全部迁入 #2721。 |
| RA-13 | 统一 Buffer／Tensor 与消费者 | [#2460](https://github.com/hw-native-sys/simpler/issues/2460) | U1b/U2b 的共同模型、绑定和 IO；#2146 的 ST-1 是具体消费者回归入口。 |
| RA-14 | 公共接口迁移 | [#2460](https://github.com/hw-native-sys/simpler/issues/2460) | 统一入口、兼容和 wire 消费者迁移；子项 #2888 清理公共 COW 数据路径。任务方向保留，同进程借用和启动继承保留。 |
| RA-15 | 逻辑关闭与物理释放 | [#2460](https://github.com/hw-native-sys/simpler/issues/2460) | U4 与 #1931 的身份校验衔接；覆盖接受使用、最后使用、失败保留和实际释放，不预设公共独立 grant 对象。 |
| RA-16 | 执行 workspace 所有权与预算 | [#2666](https://github.com/hw-native-sys/simpler/issues/2666) | 既有执行资源与 prepare/launch/retain/release 交接；#2883 跟踪资源分类，#2316 跟踪流水容量。范围含外部交接，不把通用 kernel 实现并入 #2716。 |

## 核对依据

本轮读取各入口正文与评论，核对 #2721 → #2460 → #2888、#2721 → #1931/#2720、#2722 → #824 及 #2495 → #2496/#2497 的原生关联。交接不修改远端正文、负责人或关系。

正文哈希及观察时间保存在 [目标来源清单](../goal-sources.json) 的 `resource_handoff`；这是范围核对，不是主线代码或硬件的重新验收。原有具名 SHA 证据保留在 [目标覆盖](../goal-coverage.md) 和历史评审中。

判据：每项可以由既有机制满足、纳入设计、转交关联任务或取消；取消须有明确理由。设备编号、别名、Arena、组合资源与公共 plan/capability 的具体形状仍按上述评论待决，不能由缺少某个候选类型名推导“必需功能未实现”。
