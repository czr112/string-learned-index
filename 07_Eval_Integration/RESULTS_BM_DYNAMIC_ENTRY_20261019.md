# 阶段5入口：冻结动态轨迹与逐操作正确性（单进程观测）

日期：2026-10-19；起点`332f38f`工作树。依据[最新长期计划](../docs/LONG_TERM_BM_PLAN_20261018.md)阶段5第1–2条。
**结论：四份冻结轨迹（2数据×2读写比）在BM页上逐操作正确、最终map与全量scan一致，普通与两格ASan/UBSan完全相同；
本轮只作入口，不做配对计时、不声称吞吐。**

## 轨迹（用未修改的上游生成器冻结）

| 轨迹 | 初始行 | 操作 | find / insert | 命中 find | 插入新键/重复 |
|---|---:|---:|---:|---:|---:|
| english_words 95/5 | 52,428 | 20,000 | 19,000 / 1,000 | 17,100 | 999 / 1 |
| english_words 80/20 | 52,428 | 20,000 | 16,000 / 4,000 | 14,400 | 3,999 / 1 |
| memetracker_urls 95/5 | 52,428 | 20,000 | 19,000 / 1,000 | 17,100 | 999 / 1 |
| memetracker_urls 80/20 | 52,428 | 20,000 | 16,000 / 4,000 | 14,400 | 3,999 / 1 |

生成器：`MixedWorkloadConfig(seed=20261019, operations=20000, initial_load_fraction=4/5,
read_miss_ratio=1/10, weights={find:19/20 或 4/5, insert:1/20 或 1/5})`，经未修改的
`generate_mixed_operations` 产生；`SIBOPS1`/`SIBROW1` 由上游 `write_operations`/`write_rows` 写出，哈希已冻结。
本轮update/erase权重为0（计划允许两档各自固定，替换/增长语义留到下次扩格）。

## 逐操作正确性

每一条操作都对照`std::map`核对：find的presence/value、insert的`inserted/already_exists`、
update/erase的`found/not_found`，并每步核对`size()`；结束时对最终map做**全键读回**与**全量有序scan**比对。
四轨迹失配均为**0**；最终规模53,427/56,427与`初始行+新键`一致；普通与`english_words`、`urls 80/20`两格
ASan/UBSan的计数字段完全一致。

## 首次观测（单进程，非配对，不用于排名）

| 轨迹 | find µs/次 | insert µs/次 | 页内插入 | 页重建 | 根发布 | 重建行数 | 终态行数 |
|---|---:|---:|---:|---:|---:|---:|---:|
| english_words 95/5 | 0.77 | 17.2 | 897 | 102 | 103 | 131,655 | 53,427 |
| english_words 80/20 | 0.95 | 15.7 | 3,618 | 381 | 382 | 432,299 | 56,427 |
| urls 95/5 | 1.20 | 34.2 | 897 | 102 | 103 | 131,655 | 53,427 |
| urls 80/20 | 1.31 | 25.9 | 3,618 | 381 | 382 | 432,299 | 56,427 |

可读出的结构性事实（不计入任何判据）：
1. **多数插入是页内完成**：95/5为897/999、80/20为3,618/3,999，符合“16槽写余量”的预期；
   触发页重建的比例约10%。
2. **每次重建都伴随一次根发布**（102/103、381/382），说明换页仍走`O(页数)`复制根的路径；
   这正是计划阶段5第4条要单列并评估的维护成本，本轮只记录，不优化。
3. 插入远贵于查询（17–34µs对0.8–1.3µs），但这是**含重建的插入**，不是纯写入成本；
   下一轮才拆分“页内插入/重建/根发布”的分项。

## 判定与下一步

- 入口通过：轨迹可重放、逐操作与终态均正确、sanitizer一致。
- **未做**：配对计时（多进程×重复）、读预算对比（用最终map重建只读快照后测点查≤1.15）、
  集中写轨迹、根复制随页数增长的量化、暂停分位数。
- 下一步按计划：先登记配对协议（进程/重复/顺序/指标），再做**读预算**（最终map快照 vs 动态实例）
  与根成本两项，通过后才扩到24格并接回ART/Context16同能力对照。

## 边界与披露

- 单进程、每轨迹一次回放；`find µs`含计时器开销，且未配对，不能当作吞吐或加速比。
- 轨迹为均匀插入目标（上游生成器只支持均匀选键）；集中写需要另冻结完整轨迹。
- update/erase为0；替换/增长语义只覆盖“增长”。
- 初始行52,428来自65,536唯一键的4/5；另有1次重复插入（生成器允许）被正确拒绝。
- HOT继续缺席；本轮无外部排名、不改默认。

## 证据

- 轨迹制备：[prepare_bm_dynamic.py](diagnostics/prepare_bm_dynamic.py)，身份在
  [bm_dynamic_inputs_20261019/identity.json](results/bm_dynamic_inputs_20261019/identity.json)。
- 回放入口：[bm_dynamic_entry_20261019](results/bm_dynamic_entry_20261019/)——
  `records.json`、`records_sanitized.json`、`decision.json`、`verification.json`、`identity.json`、`commands.log.gz`。
- 复核：`python3 -B 07_Eval_Integration/diagnostics/verify_bm_dynamic_entry.py <目录>` → `verified`。
