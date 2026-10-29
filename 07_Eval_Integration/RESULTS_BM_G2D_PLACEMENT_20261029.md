# G2d：`resident_` 记账已修好，但插入落位仍有错——接线回滚

日期：2026-10-29。连续性排查，两个问题一个解决、一个暴露。

## 1. 记账问题：已定位并修好

用最小用例打印四个数（一次分裂后立刻读）：

```
after build: size=65536 live=65536 resident=1944569 memory=1944569 pages=32 bindings=32
after split 0: size=65537 live=65537 resident=1944577 memory=1985516 pages=33 bindings=33
```

→ `size_/live` 与 `pages_/bindings` 都对，**只有 `resident_` 少 40,939 B**。

原因：`8a94f76` 版本的接线**根本没有更新 `resident_`**（+8 B 只是 dirty 记账的零头），
而我上一轮试图用 `rebuild()` 的手写公式补，公式虽对，但 `payload()` 的口径（含页表节点的
字符串与 `Node`）与实际分裂后的足迹差一页的 slack。

**修法（已验证）**：提交后直接 `resident_=memory_bytes();`——按构造精确，代价 O(pages)≈33 次。
修复后四数完全一致：

```
after split 0: size=65537 live=65537 resident=1985516 memory=1985516 pages=33 bindings=33
```

## 2. 但插入落位仍有错

用会调用 `verify()` 的门（`n3_gate`）：

```
GATE FAILED english_words
FAIL insert read-back
page range
```

`page range` 是 `Page::verify()` 的槽范围断言——说明新键被插进了**不属于它的半页**，
导致块内后缀落在该块的 fence 区间之外。

根因（与复核第三条一致）：`b = block_for(key)` 给出"包含该键的块"，而
`upper_key = 第 b 块的首键`；随后按 `compare(key, upper_key) >= 0` 选上半页。
当 `key` 恰好**小于**第 `b` 块首键时（`directory_block` 的前驱语义会返回 `b`，
但键实际小于该块 fence），判断会把键放进 upper，而它其实属于 lower。
**两种情况必须用同一个判据**：要么全程用 `block_for` 的结果选半页，要么全程用 fence 比较。

**正确做法**：把"新键该进哪一半"完全交给 `block_for` 的结果——若 `block_for(key) < b`
则进下半页，否则进上半页；`b` 本身应取 `block_for(key)`（或 `b = block_for+1` 以保证上半页非空），
不要再用 `compare(key, upper_key)` 二次判断。

## 3. 处置

- 接线已回滚，`n3_gate` 恢复全绿（263,469 项零失配 × 3）；
- **`resident_` 的修法保留在文档里**（提交后重算），下一轮接线直接采用；
- 页核心保留两半 `extract_split`、`block_for`、arena 余量——单元测试三点零失配。

## 4. 状态

| 项 | 状态 |
|---|---|
| `resident_` 记账 | ✅ **已定位并验证修法**（提交后 `resident_=memory_bytes()`） |
| 插入落位判据 | ❌ `block_for` 与 `compare(upper_key)` 两套判据混用，已定位 |
| 接线 | ⛔ 回滚；两项修法都已写清 |
| G4 性能上界观察 | 🟡 0.137/0.134 保留，仍非验收结论 |
| Context16 默认 | ✅ 不变 |

## 5. 下一轮动作（两项合起来即可过门）

```cpp
const auto b=core->block_for(key);            // 唯一判据
const auto split_point=(b==0?1:b);            // 保证上半页非空
auto split=Page::extract_split(*core,split_point);
Page* dest=(b<split_point)?split.lower.get():split.upper.get();   // 与 block_for 同源
...
root_.swap(next_root);
resident_=memory_bytes();
```
