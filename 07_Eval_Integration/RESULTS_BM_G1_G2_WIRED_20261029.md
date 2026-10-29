# G1/G2 完成：页切分接线通过三语料语义门

日期：2026-10-29。按 G1 事务设计接线，**三语料 263,822 项检查、零失配**。

## 根因回顾（本轮修正的两处）

| 问题 | 修正 |
|---|---|
| `prepare_root` 排除 target，只 stage 上半页 → **根漏掉下半页** | **两个半页都作为新页 stage**，`prepare_root` 之后 target 采纳下半页、上半页插入页表 |
| 下半页若仍是原页对象，无法作为 `unique_ptr` 进 staged（`Page` 拷贝被删） | `extract_split` 现在**同时构造两个全新的半页**（各自压实 arena + 重拟合），旧页在提交后被替换掉 |

`apply_drop` 因此不再需要——旧页整体被两半取代，避免了"先改后提交"的顺序冲突。

## 事务顺序（实现即此）

```cpp
b = block_for(key) 夹到 [1, total-1]
split = Page::extract_split(*core, b)        // 可抛；两个新半页，源页不动
split.upper->try_insert(key, value)          // 键保证落在上半页（局部对象）
staged = {target->first: split.lower, upper_key: split.upper}
next_root = prepare_root(target, pages_.end(), staged, added)   // 可抛
swap(target->second, staged[target->first])  // 下半页就位（旧页销毁）
pages_.insert(staged[upper_key])             // 上半页入表
root_.swap(next_root)
```

并加了契约断言：`next_root->bindings().size() == pages_.size()`，否则抛错——
直接堵住上一轮"静默丢页"的路径。

## 门禁结果

| 语料 | 检查项 | 失配 | insert/update/erase | find 分配 | 终态行数 |
|---|---:|---:|---|---:|---:|
| english_words | 263,822 | **0** | 600 / 200 / 200 | **0** | 65,936 |
| memetracker_urls | 263,822 | **0** | 600 / 200 / 200 | **0** | 65,936 |
| memetracker_phrases | 263,822 | **0** | 600 / 200 / 200 | **0** | 65,936 |

（该门通过候选 factory 驱动，覆盖全行/全查询/缺席/边界/跨销毁 scan/增删改/零分配。）

## 尚未做

- **端到端写收益**：接线后尚未重测四臂；微基准给的 −60.7%/−73.5% 是组件级，
  且现在是"两次 extract"（两半各压实一次），成本高于单次切分，需实测。
- **G3 异常注入**：新路径的失败注入未做。
- **G3 空间**：连续分裂后的终态与峰值未测。
- **模型参数对齐**：`extract_split` 仍用 `fit(...,16,1,7,3)` 与块边界训练，
  与原页构造的参数/训练键来源不同——G1 的该项仍未完成。

## 状态

| 项 | 状态 |
|---|---|
| 页切分接线 | ✅ **三语料零失配** |
| 根/页表契约断言 | ✅ 已加（绑定数必须相等） |
| 端到端收益 | ⬜ 未测 |
| 异常注入 / 空间曲线 | ⬜ 未测 |
| 模型参数对齐 | ⬜ 未做 |
| Context16 默认 | ✅ 不变 |

## 产物

- 实现：[bm_n1_page_core.hpp](diagnostics/bm_n1_page_core.hpp)（`SplitResult{lower,upper}` + `extract_split`）、
  `bm_n1_index.hpp` / `bm_split_index.hpp`（`rebuild_by_split`）
- 运行：[tools/g1_both.sh](../tools/g1_both.sh)、[tools/g2_wire.sh](../tools/g2_wire.sh)
