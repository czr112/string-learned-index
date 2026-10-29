# BM TX 最终证据（2026-10-30）

实现/测量版本：10e0494（此前实现 12933d8）。补充零分配诊断单独保存源码；算法没有再次修改。初次零分配驱动未绑定自身散列，仅作有限证据；修正驱动后的 v2 完整重跑作为该门正式记录。
本轮所有正式运行、失败发现记录和 A/A 未定结果都保留。生产默认继续 Context16。

- [汇总 JSON](summary.json)：32 格判定、空间、历史身份与点流 SHA。
- 各运行的 report.json、identity.json、commands.json 直接可读；raw_records.tar.gz 包含全部该轮文本记录（含原始计时、校验、编译输出与元数据），members.json 逐项给出 SHA 和字节数。
- [发现阶段索引](../bm_tx_acceptance_20261030/evidence_index.json)：包括首次红灯、修复后绿灯、初轮普通/消毒验收和失败的编译驱动。
- input_identity/ 保存既有轨迹 manifest 和历史查询身份；fault 语料散列是在全部测试完成后记录，不能冒充运行前的绑定。
- supplement_sources/ 保存本轮补充诊断、干净构建与汇总工具源码；其他实现及驱动均在 10e0494。

归档不含可执行文件、目标文件、原始数据集或派生查询二进制。查询二进制由提交中的查询生成器和既有输入生成；两轮 query/final SHA 均与历史 32 格相符。所有输入仍需本地既有固定轨迹及上游快照，不能把这份归档单独当成完整数据集分发。

## 各轮记录

- [bm_tx_final_sanitizer_20261030](bm_tx_final_sanitizer_20261030/report.json)：80 个文本记录，压缩 10,880 B。
- [bm_tx_performance_20261030](bm_tx_performance_20261030/report.json)：529 个文本记录，压缩 1,023,490 B。
- [bm_tx_query_bm0_20261030](bm_tx_query_bm0_20261030/report.json)：619 个文本记录，压缩 88,306 B。
- [bm_tx_query_bm1_20261030](bm_tx_query_bm1_20261030/report.json)：619 个文本记录，压缩 87,697 B。
- [bm_tx_point_20261030](bm_tx_point_20261030/report.json)：81 个文本记录，压缩 133,325 B。
- [bm_tx_clean_acceptance_20261030](bm_tx_clean_acceptance_20261030/report.json)：80 个文本记录，压缩 10,798 B。
- 初次有限记录 [bm_tx_read_allocations_20261030](bm_tx_read_allocations_20261030/report.json)：73 个文本记录，压缩 10,306 B。

- [正式零分配 v2](bm_tx_read_allocations_v2_20261030/report.json)：73 个文本记录，压缩 10,334 B；identity 包含包装 runner 自身哈希。

## 复核与复现

先校验 evidence_manifest.json 的文件 SHA，再按各目录 members.json 校验压缩包内文本。解包到对应 builds/ 目录后，tools/summarize_bm_tx.py 可校验记录完整性与输入身份并重新汇总；输出路径必须是新文件。无需重新计时才能复核汇总。

重新执行验收/性能时使用新的输出目录，入口为 tools/run_bm_tx.py、run_bm_tx_performance.py、run_bm_tx_query.py。补充零分配入口 run_bm_tx_reads.py 使用已经生成的历史查询 cases；clean 入口 check_bm_tx_clean.py 从 10e0494 导出源码，复用已有且由 runner 核对身份的依赖/输入。完整命令在 commands.json。

干净重建只确认固定源码在本机可重新构建验收，不代表另一台机器复现或全部外部依赖重新编译。查询 A/A 质量失败阻止默认采用与无条件规模扩张；本轮未继续追加样本。
