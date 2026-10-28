# F4M compact只处理需回收页：回归门通过

日期：2026-10-18。修复F3已知维护欠账；F3/F4源码、计时和失败判定不变。
**新独立类型通过普通与ASan/UBSan各8,626,797项检查；干净页/纯值更新compact零重建，
单页删除只重建该页2047活行，其他页版本不变。没有新性能或动态采用结论。**

[预登记](diagnostics/F4M_DIRTY_COMPACT_SPEC_20261018.md)、[新类型](diagnostics/dirty_compact_index.hpp)、
[可复现生成器](diagnostics/generate_dirty_compact.py)、[验收驱动](diagnostics/check_dirty_compact.cpp)。

## 实现与异常安全

叶页仍是原冻结F3 Page，Node字段、树路由、find、update和scan函数保持原样。
新增写侧dirty集合，成员是稳定std::map迭代器，按页界排序。
原地insert/erase产生需回收页；纯值update、重复insert、缺席写不标记，新建/重建页干净。
compact从dirty集合取页，允许按既有活键预算吸收一个邻页，不再遍历全部页。

首次标记可能分配，发生在页内容修改前。替代页/路由先准备完成，发布前移除将被销毁的dirty迭代器，
再交换/删除旧节点。标记失败、构建失败或部分分裂失败后，原map、页版本和dirty集合均保持。
这增加了写侧有序集合操作和可能的分配，不声称维护免费或只有O(1)写成本。
原地insert/erase为判断是否需要标记，存在额外存在性查询；尚未测写入吞吐。

首次普通门在“全删后应剩单空哨兵页”断言失败：成对吸收后可能留下两个干净空页。
原失败目录[保留](results/f4m_dirty_compact_20261018/failure.json)，源码/输入已归档。
b轮改为优先吸收前面的可合并页，使按键序消费dirty时空页可继续收敛；完整重新执行正确性与sanitizer，未新增任何计时。

## 实际检查

| 门 | 每种构建检查项 |
|---|---:|
| 定向维护/失败、继承F3域门、随机写与增长 | 1,647,140 |
| tranco/64k | 1,374,239 |
| mixed/131k | 2,774,447 |
| skew/131k | 2,830,971 |
| 合计 | **8,626,797** |

- 连续10次无写compact及1024次纯值update后compact，处理目标页和重建均为0。
- 8192键四页中只删一页的一条记录，compact只处理1目标、重建2047行；其余三页版本不变。再次compact幂等。
- 五个新增失败场景：dirty标记在erase前失败、在槽内insert前失败、已有dirty的compact失败、相邻两dirty页合并失败、部分分裂失败后回滚新标记。
- 继承F3三种失败、空/NUL/80/ff/长键/前缀、块满重建、连续分裂、全删至单空页及跨页owning scan。
- 普通与sanitizer每份JSON一致、rc=0、stderr=0。全部活键/scan/页窗口与独立std::map相符，dirty成员及常驻账本一致。

真实语料先做1024次纯值更新，再1600次混合写，最后compact：

| 输入 | compact前需回收页 | 处理目标页 | 单事件最多源页 |
|---|---:|---:|---:|
| tranco/64k | 60 | 60 | 1 |
| mixed/131k | 110 | 110 | 1 |
| skew/131k | 110 | 110 | 1 |

本负载的脏页已很密，不能用这三格夸大稀疏写收益；稀疏性由上述8192键单页定向实验直接证明。
合成相邻合并门最多两个源页；干净邻页可能因合并被吸收，所以不能声称每个被重建的源页都曾被写。
自报字节只计集合对象及迭代器载荷，不含map/set节点链接、分配器元数据和完整builder暂存，仍不是完整堆口径。

## 判定

[独立复核](results/f4m_dirty_compact_20261018b/independent_verification.json)重查登记/归档源码、输入、
二进制、日志及所有门记录，确认原Node和读/路由/update/scan函数保留，结果verified。
产物：[结果](results/f4m_dirty_compact_20261018b/verification.json)、[原始门](results/f4m_dirty_compact_20261018b/gates.json)。
仅通过语义与维护工作量门；没有测新类型的点查/写入吞吐、暂停或完整峰值，不把F4联合收益套给它。
F3的通用读预算失败、F4的单因素未通过和生产默认继续保持。
