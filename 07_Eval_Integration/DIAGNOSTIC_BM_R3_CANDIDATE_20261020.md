# R3 候选登记：分裂时不再重建字符串（直接搬 arena 字节）

日期：2026-10-20。依据[计划第 6 节](../docs/LONG_TERM_BM_PLAN_20261020.md)与
[R2 归因](RESULTS_BM_R2_20261020.md)。**本篇只登记候选与判据，不含实现**——实现需要改动页编码核心，
必须在能完整跑语义门与 sanitizer 的环境里做。

## 现状（已核实到代码行）

`bm_page_index.hpp` 的重建路径：

```cpp
std::vector<Row> part(rows.begin()+begin,rows.begin()+end);   // 拷贝整页 Row（含 std::string key）
auto core=std::make_unique<Page>(part,occupancy_);            // 再把前缀后缀写进 arena_（memcpy）
```

`Page` 的构造（`bm_page_core.hpp`）**不保留 `std::string` 行**：它把每行去掉公共前缀后的后缀
拷进连续的 `arena_`，并按 15/16 槽切块、算 tag/CRC 与 fences。

因此对一次满页分裂，字符串被搬运**三次**：`rows` 拼接、`part` 拷贝、arena 追加。
前两次是纯临时开销——页只需要"有序键值序列"来追加后缀。

## 候选

新增 `Page::split_from(const Page& source, std::size_t half_boundary, ...)`：

- 两个半页直接以源页的 `arena_` 字节区间为输入：按源页的 region/block 边界切分，
  无跨界的块整块搬（`memcpy` 一段后缀字节 + 复制 16 个 entry 元数据）；
- 只有跨界的那一个块需要重新切槽（≤16 行），以及新插入键所在的块需要重建；
- fences/digests 由块首键重算（`fence = region_prefix + first_suffix`，需保留 region 前缀），
  或直接复用源页的 `fences_`（半页边界处的 fence 用新页首键替换）。

这样一次分裂的临时分配从「整页 `Row` × 2」降到「跨界块 + 新键」，预计页构建时间显著下降。

## 判据（登记）

1. **语义**：`find_base`/`find`/`find_exact`/`scan` 在四份真实语料上与现实现逐一相同；
   分裂前后全键读回与全量 scan 一致；sanitizer（ASan/UBSan/LSan）零报告；
   失败路径（`fault()` 注入）仍不留悬空页、旧根不被破坏。
2. **性能门**：R1 的四条增长轨迹上，增长插入 ≥15%（words 14.4→≤12.2µs、URLs 32.7→≤27.8µs），
   且查询不回退（`find_base` 在 AA 带内）。
3. **空间门**：自计常驻不变或更低（页 arena 布局不变，预期逐字节相同）。
4. **两次机会**：若两轮合理实现都无法达到 15%，按计划停止该支线，保留已验证的空间与构建成果。

## 为什么不是别的方向

- 根复制：R2 已量化 3%–8%，即使归零也跨不过 15%。
- 槽满/arena 不足：R2 显示触发几乎都是页满分裂（26→51 页），不是槽或 arena。
- 缓存/预取类改动：属性能调优，不改变复制量，无法解释 6–13 倍差距。

## 实现前需补齐的两件事

1. R2 记录的两个缺口：撤销重建决策点上的**直接触发计数**（页满/槽满/arena），
   以及把 `page_ns` 拆成「复制/拟合/分配」三段——否则无法证明 15% 全部来自复制消除。
2. `Page` 的 `stable_fences_`、`digests_`、`region.prefix` 在"整块搬运"路径上的不变量，
   必须先在纸面推导并写成断言，再动代码。

## 边界

- 本候选只影响 `bulk_load` 之外的重建路径；不影响构建、查询与空间布局。
- 不改模型、页大小、槽位与根结构；默认仍为 Context16。
