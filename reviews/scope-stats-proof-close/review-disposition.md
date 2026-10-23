# PR2684 review disposition

最终候选 `5a1c5c089`；完整代码复审在 `77aba9a8a` 为 **approve**，所有先前 Must/Should 已关闭。首轮 `25d31c653` 的 Request changes 原报告独立保留。

| Finding | 处理与证据 |
| --- | --- |
| Must1 旧publication重启混入新run | admitted ordinary epoch过滤（required=false仍检查），pending sticky host error在records lock外报告；真实loaded-queue successor回归先红后绿。限定内部collector协议；不外推硬件poison后的公共run可重试。 |
| Should2 空unknown残留旧JSONL | 精确移除本prefix scope_stats.jsonl，无关sibling保留；实际boundary先红后绿。 |
| Should3 背景专属schema旧描述 | 同笔修正文档四字段适用范围、unknown stop来源及held恢复前置。 |
| Should4 CutUnknown注释暗示device free | 明确artifact vocabulary与owner handshake/termination证明分离，无新token或释放路径。 |
| Consider1 onboardstop证明 | PR正文声明onboard仍无独立stopproof；正控来自sim join和真实CPU seam。 |
| Consider5 容量粒度 | 明确四字段一起由optional值提供或一起null。 |
| Consider6 scene成功schema | docstring明确successful output；未改变成功场景断言。 |
| Consider7 export rc | 保留原best-effort DFX错误优先级；不借本笔改变计算错误/公共状态传播。 |
| 父级发现 fatal与跨epoch配对 | 已有真实HTML red/green，旧normal HTML兼容；保留positive fatal且不虚构pair。 |

复审仅余 A5 scene-test docstring 与 A3 同步的一词修正，已集入最终 `5a1c5c089`；两 head 的实现、测试体及构建输入相同。actual Claude 已核对最终一词差异、五个目录 tree hash 和两架构 docstring 一致，最终 **approve**，没有剩余 Must/Should。最终 exact-head CI39889814912 attempt1 completed success：18 checks success、1 expected deploy skip，CodeRabbit status success。GitHub 无未解决 review threads；CodeRabbit comment 的代码审阅仍止于旧25d31，最终独立审阅来自 actual Claude。PR 待用户审核，未合入。
