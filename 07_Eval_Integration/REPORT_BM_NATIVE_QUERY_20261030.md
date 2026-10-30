# Windows原生查询校准（2026-10-30）

算法保持TX10e0494；原生工具链MinGW GCC15.2.0，固定自身测试线程到最低允许CPU（本机CPU0，mask1），没有调整其他进程、全局电源或优先级。已校验真实链接工具和实际加载的本地运行库。公共contract99/99与32格原生语义检查均通过。这不是Windows sanitizer验收，相关算法sanitizer证据仍来自WSL。

## 预登记校准结果

| 批次下限 | 格 | 同时通过两个A/A检查的状态 | 结论 |
|---|---|---:|---|
| 250ms | words_95_5_uniform_growth | 1/4 | 未合格 |
| 250ms | urls_80_20_hotspot_replacement | 0/4 | 未合格 |
| 1000ms | words_95_5_uniform_growth | 3/4 | 未合格 |
| 1000ms | urls_80_20_hotspot_replacement | 3/4 | 未合格 |

四个状态均需过奇偶与前后半段A/A，且两个固定格均通过。两组都未达到这一条件，selected_ms=null。原生1000ms虽明显比旧WSL更接近门，但不能把3/4写成通过。因此没有正式16进程32格确认，没有计算校准阶段候选/对照性能比，也没有追加样本。

所有512条测量批次、循环数校准与预热记录均保留。线程CPU时间与周期只作为诊断；周期未转换为耗时。不把缺失的Windows调度切换统计写成零。旧WSL失败记录独立保存，没有混样。

## 后续

本机的已登记两环境尚未取得完整查询计时资格。继续确定性工作量/空间优化，下一项固定16段模型诊断只改变模型预算，保持物理布局和其他参数；若计数和空间过门，也只能登记为候选机会，仍不能声称耗时收益或采用。Context16继续默认。

规模标签需和真实基数一起读：原32格记录包含source_pool_rows、initial_rows与final_rows；words_100k是来源池规模标签，不等于重放时索引始终包含100k条键。

[协议](../docs/PROTOCOL_BM_NATIVE_QUERY_20261030.md) · [审查](../docs/REVIEW_BM_NATIVE_QUERY_20261030.md) · [原始证据](results/bm_native_query_calibration_20261030/report.json)。同目录raw_records.tar.gz包含原始文本和生成contract源码，members.json逐项绑定SHA；不含可执行文件、运行库DLL或数据集。精确命令和工具链哈希保存在commands/identity/environment。
