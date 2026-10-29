# U3 两阶段切分：`extract_split` + `apply_drop` 通过单元测试

日期：2026-10-29。按 [接线阻塞记录](RESULTS_BM_U3_WIRE_BLOCKED_20261029.md) 的方案重构。

## 实现

把原 `split_at_block`（就地修改）拆成两阶段，解决"`Page` 拷贝被删除"与"强异常保证"的冲突：

| 阶段 | 签名 | 可抛 | 行为 |
|---|---|---|---|
| 1 提取 | `static SplitResult extract_split(const Page& src, b)` | **可抛** | 从 **const 源页**构造上半页：压实 arena、复制块、重建 region/fence/digest、拟合 `bm_`；**源页分毫未动**；同时返回**下半页预算好的 `BlockModel`** |
| 2 应用 | `void apply_drop(b, BlockModel&&) noexcept` | 不抛 | 删掉源页 `[b, count)` 的块与 fence、调整 `regions_[0].count`/`size_`、**移动赋值**预计算的模型 |

关键点：所有可能抛出的工作（arena 增长、模型拟合）都在阶段 1 完成；
阶段 2 只有 `vector::erase`（平凡类型）与 `BlockModel` 的移动赋值，因此可标 `noexcept`。

## 单元测试（三个切分点，全零失配）

| 切分块号 | 下半行数 | 上半行数 | 失配 |
|---:|---:|---:|---:|
| 70 | 1,050 | 998 | **0** |
| 35 | 525 | 1,523 | **0** |
| 100 | 1,500 | 548 | **0** |

检查：两半 scan 拼接与原始逐行相同；每个键恰好一边命中原值。

## 既有路径回归

三语料语义门：**263,469 项、零失配**（索引仍走原 `rebuild`，未接线）。

## 尚未做：接入 `rebuild`

接线顺序已确定（写在上一份记录里）：

```cpp
const auto b = page->block_count()/2;                  // 先固定切分点
auto split = Page::extract_split(*page, b);            // 可抛；源页完好
// 把新键插进所属的一半（compare(key, split.upper->fence_keys()[0])）
auto next_root = prepare_root(target, pages_.end(), staged, added);   // 可抛；两页都是候选
page->apply_drop(b, std::move(split.lower_model));     // noexcept
pages_.emplace(upper_key, Node{std::move(split.upper), ++generation_});
root_.swap(next_root);
```

上次接线失败是因为 `staged` 里放了空 `Node`；正确做法是**两页都先构造好**，
把 `staged` 填成两个真实节点后交给 `prepare_root`，再 `apply_drop` 与登记。

## 状态

| 项 | 状态 |
|---|---|
| 两阶段切分 API | ✅ 单元测试三点零失配 |
| 既有路径 | ✅ 语义门零失配 |
| 接线 | ⬜ 待做（下一步，顺序已定） |
| 成本收益（微基准） | ✅ −60.7% / −73.5% |
| Context16 默认 | ✅ 不变 |

## 产物

- 实现：[bm_n1_page_core.hpp](diagnostics/bm_n1_page_core.hpp)（`SplitResult`/`extract_split`/`apply_drop`）
- 测试：[check_bm_split_block.cpp](diagnostics/check_bm_split_block.cpp)（已改用两阶段 API）
- 运行：[tools/u3_fix.sh](../tools/u3_fix.sh)
