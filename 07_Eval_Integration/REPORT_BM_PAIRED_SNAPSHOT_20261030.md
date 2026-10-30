# 成对相对快照估计：已知真值资格校准未通过

该记录收口 `deb9d2b` 登记、`0b0181b` 审查后的已有运行。本轮只读取并归档现有结果，没有启动或追加测量。

`builds/bm_snapshot_paired_calibration_20261030/report.json` 已为 `complete/calibrate`，`selection.json` 为 `qualified=false`。四个实例全部使用 TX8 快照；这不是 Model16 与 TX8 的性能比较。

| 固定场景 | 已知真值 B/A 中心与 95% CI | 其他失败门 | 判定 |
|---|---|---|---|
| words_95_5_uniform_growth | 1.001512 [0.982280,1.021121]，真值门通过 | replica B、相对奇偶、前后半段 | unqualified |
| urls_80_20_hotspot_replacement | 1.006155 [0.987602,1.025056]，真值门未过 | 相对奇偶、前后半段 | unqualified |

每场景四个独立进程，合计八份样本。原绝对门没有被追认为通过。按协议 64–65 行停止该估计器：不追加进程/批次、换排序/下限或放宽目标，不运行候选确认。校准失败没有证明 Model16 无收益，也没有证明机器波动的具体原因。

公共契约记录 TX8 99/99、Model16 127/127；全部 32 格语义检查通过，五项合成分析检查通过。它们不能替代资格门。

现有 68 份文本记录已归档，包含原始样本、命令、身份、编译/契约日志及生成源码；排除 `.exe/.dll` 和数据集。八份样本 SHA 与 selection 一致，report SHA 为 `feaf1cb130430f2bebba0aa59ed264a8db73d379eb148eeaff6cea91e4be96e9`。归档逐成员 SHA 解包流式复核通过。

证据：[selection](results/bm_snapshot_paired_calibration_20261030/selection.json)、[report](results/bm_snapshot_paired_calibration_20261030/report.json)、[归档复核](results/bm_snapshot_paired_calibration_20261030/archive_verification.json)、[协议](../docs/PROTOCOL_BM_PAIRED_SNAPSHOT_20261030.md)。

下一步按 [长期结构计划](../docs/STRUCTURE_PERFORMANCE_PLAN_20261030.md)：先修复并验收本地 HOT 基线有效性，再用工作量和匹配布局诊断已确认回退。Context16 默认；动态16和固定8维护仍停止，Model16只读延迟未知。
