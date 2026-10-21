# N1 插桩规格：在重建决策点直接计数（待应用）

日期：2026-10-21。依据[计划](../docs/LONG_TERM_BM_PLAN_20261020_AFTER_R.md) N1 与
[N1 首测](RESULTS_BM_N1_REASONS_20261020.md)。**本文件是可直接套用的补丁规格**：
按下面三步做完即可得到"槽满 vs arena 不足"的实测分布，无需再设计。

## 为什么必须复制

`bm_page_core.hpp` 的哈希被多轮 `identity.json` 与 R0 manifest 的 `core_bundle_sha256` 冻结。
直接改会让这些身份全部漂移。因此按既有惯例（`bm_probe_page.hpp`、`bm_shortdir_page.hpp`）
复制后再改，并以新身份发布本轮的驱动与结果。

## 第一步：复制并改命名空间

```bash
cp 07_Eval_Integration/diagnostics/bm_page_core.hpp 07_Eval_Integration/diagnostics/bm_n1_page_core.hpp
cp 07_Eval_Integration/diagnostics/bm_page_root.hpp 07_Eval_Integration/diagnostics/bm_n1_page_root.hpp
cp 07_Eval_Integration/diagnostics/bm_page_index.hpp 07_Eval_Integration/diagnostics/bm_n1_index.hpp
```

- `bm_n1_page_core.hpp`：`namespace bm_leaf` → `namespace bm_n1_leaf`，`#include "bm_page_root.hpp"` 若有则改指向 `bm_n1_page_root.hpp`。
- `bm_n1_page_root.hpp`：`namespace bm_page` → `namespace bm_n1_page`，`using bm_leaf::…` → `using bm_n1_leaf::…`，`#include "bm_page_core.hpp"` → `"bm_n1_page_core.hpp"`。
- `bm_n1_index.hpp`：`namespace bm_page` → `namespace bm_n1_page`（与 root 一致即可），`#include "bm_page_root.hpp"` → `"bm_n1_page_root.hpp"`。

## 第二步：在 `try_insert` 的四个出口加计数

在 `bm_n1_page_core.hpp` 的 `bm_n1_leaf` 命名空间里加入：

```cpp
struct RebuildReasons {           // 进程内累计，跨全部页
    std::uint64_t page_full=0, prefix_change=0, slot_full=0, arena_short=0, duplicates=0, inserted=0;
};
inline RebuildReasons& reasons() { static RebuildReasons r; return r; }
```

`try_insert` 改为（只改返回语句，判据不动）：

```cpp
    Insert try_insert(std::string_view key,std::uint64_t value) {
        if(key.size()>UINT32_MAX)throw std::length_error("key exceeds page limit");
        if(find(key)){++reasons().duplicates;return Insert::duplicate;}
        if(size_>=2048||regions_.empty()){++reasons().page_full;return Insert::rebuild;}
        const auto& r=regions_[0];
        if(key.size()<r.prefix.size()||compare(key.substr(0,r.prefix.size()),r.prefix)!=0)
            {++reasons().prefix_change;return Insert::rebuild;}
        const auto s=key.substr(r.prefix.size());
        const auto b=directory_block(r,s,nullptr);auto& block=blocks_[b];
        if(block.count==16){++reasons().slot_full;return Insert::rebuild;}
        if(s.size()>arena_.capacity()-arena_.size()){++reasons().arena_short;return Insert::rebuild;}
        …原实现…
        ++reasons().inserted;return Insert::inserted;
    }
```

注意：原判据 3 与 4 在**同一条 `if`** 里（`block.count==16||s.size()>arena_.capacity()-arena_.size()`），
必须拆成上面两条，否则无法区分——**这是唯一有语义风险的一处改动**（顺序保持与原代码一致：先槽满后 arena）。

## 第三步：驱动与判据

用 `bm_n1_page::Index` 重放 `bm_p3_dynamic_20261020/traces` 的四条增长轨迹，输出：

```
{"label":…,"page_full":n,"prefix_change":n,"slot_full":n,"arena_short":n,
 "inserted":n,"duplicates":n,"mismatches":0}
```

判据：
- `mismatches == 0`，且 `inserted + duplicates == 插入次数`（重复由 `find` 先短路，与原实现一致）；
- `page_full + prefix_change + slot_full + arena_short` 必须等于本轮 N1 已测的单页重建数（77/356）；
- 分裂（25 次）也走 rebuild，但页数增加，需在驱动里用 `page_count()` 增量区分，不计入四个出口的合计。

## 预期（可先验的推断）

- `page_full` 应接近 0：单页重建时页行数只有约 1,045，远未到 2,048。
- `prefix_change` 应为 0：插入键是源键加后缀，前缀必然匹配。
- 因此 **`slot_full` + `arena_short` ≈ 77/356**，二者谁占多数决定 N2 登记哪个机制：
  - 若 `slot_full` 占多数 → 机制方向是"块内槽位再分配/更细的块切分"；
  - 若 `arena_short` 占多数 → 机制方向是"后缀区容量策略"。

## 边界

- 只加计数，不改任何判据顺序与语义；sanitizer 与四语料门必须在应用后重跑。
- 新页核心副本会产生新的 `core_bundle_sha256`，须随本轮结果一起发布新身份，不回写旧哈希。
