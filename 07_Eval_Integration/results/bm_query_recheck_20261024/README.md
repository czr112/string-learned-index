# 正确分组查询复验证据

正式结果来自第二轮完整运行，32 个原场景与 1 个独立控制，每格 16 进程。

- report.json / primary_summary.json：终态查询、A/A 与配对区间。
- qa.json：独立数据完整性核验通过，不代表性能门通过。
- raw_logs.tar.gz：全部正式计时、语义与编译日志，以及源输入清单。
- previous_mixed_raw.tar.gz：旧 32 格原始记录，用于既有数据重分析。
- preliminary_run1.tar.gz：校准中止轮的全部原始数据及原版源码，不与正式轮合并。
- cases.json：源池、源排名热区、初始/终态与查询文件哈希；输入二进制仍在本地冻结运行目录。
- evidence_index.json：文件及归档成员 SHA256、二进制哈希和源码绑定。

完整说明见 ../../RESULTS_BM_QUERY_RECHECK_20261024.md。
