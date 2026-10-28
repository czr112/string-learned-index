# T2：单次定位插入（去掉重复查重）——通过语义门，写收益 +1～+6 个百分点

日期：2026-10-29。实施 [T1 账本](RESULTS_BM_T1_COST_20261028.md) 中"合并两次串行查找"这一项。

## 改动

原来成功插入要查两次同一个键：

```cpp
if(it->second.core->find(key))return false;                 // 第一次
const auto result=it->second.core->try_insert(key,value);   // try_insert 内部又 find 一次
```

改为把 `try_insert` 拆成两个入口：

- `try_insert(key,value)`：保持原有行为（自己查重），供 other 调用方；
- **`try_insert_known_absent(key,value)`**：调用方已确认该页不含此键，跳过第二次查找；
- 二者共用 `try_insert_impl(key,value,bool check_duplicate)`，只在是否执行 `find` 上分叉；
- `Index::insert` 改调 `try_insert_known_absent`（`bm_n1_index.hpp` 与 `bm_split_index.hpp` 各一处）。

新增计数 `reasons().duplicates_skipped`，可核对跳过次数。

## 结果

**语义门（三语料 + 零分配）**：263,469 项检查、**零失配**，与改动前逐项相同。

**写路径**（同一 v3 配对轨迹，4 块，配对均值比值，越小越好）：

| 格 | 改前比值 | 改后比值 | 改前降幅 | 改后降幅 | 增量 |
|---|---:|---:|---:|---:|---:|
| words 95/5 均匀 增长 | 0.447 | **0.388** | 55.3% | **61.2%** | **+5.9 pp** |
| words 80/20 均匀 增长 | 0.432 | **0.421** | 56.8% | **57.9%** | +1.1 pp |

方向与 T1 的估计一致（重复查重占插入成本 12%–24%，而插入只是写时间的一部分，
所以折算到写请求耗时为个位数百分点）。95/5 格增益更大，因为它插入占比低、每次插入的相对权重高。

## 判定

- **通过语义门**，无回退；
- 收益 **+1.1～+5.9 个百分点**，属于方案允许的"普通写成本"改进；
- 尚未跑：16 块的正式配对、跨数据集（urls/100k/独立样本）、查询子集复测。

## 边界

- 只有 4 块、单格两次抽样，**不足以作为正式结论**；需要按 16 块协议在四套数据上复测；
- 收益来自"少一次全键查找"，不含任何结构改动；
- `Page::try_insert` 的旧入口保留，避免影响其它调用方。

## 产物

- 改动：`bm_n1_page_core.hpp`（`try_insert_impl` + 两个入口 + 跳过计数）、
  `bm_n1_index.hpp` / `bm_split_index.hpp`（改调新入口）
- 运行：[tools/t2_single_lookup.sh](../tools/t2_single_lookup.sh)（门）、
  [tools/t2_measure.sh](../tools/t2_measure.sh)（写路径抽样）
