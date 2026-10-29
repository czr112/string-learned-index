# G2c：三处缺陷已修，但 `verify()` 仍报 `size/memory`，接线回滚

日期：2026-10-29。按复核意见修三处后重跑，**`verify()` 仍失败**，接线再次回滚。

## 修了什么

| 缺陷 | 修法 |
|---|---|
| 分裂路径未更新 `resident_` | 按 `rebuild()` 的公式补：`resident_ = resident_ - payload(target) + route_bytes(staged) - old_root + new_root` |
| 提交后才检查根绑定数（不满足强异常保证） | **改到任何变更之前**检查：`next_root->bindings().size() == pages_.size()+1` |
| 切片点从 0 调 1 后仍一律插 upper | 改为按 `compare(key, upper_key)` 选择目标半页（两半都是新对象，插哪一半都安全） |

三语料 **factory 门通过**（263,822 项 × 3、零失配、find 零分配）。

## 但 `verify()` 仍失败

用 `tools/n3_gate.sh`（会调用 `Index::verify()`）跑：

```
GATE FAILED english_words
size/memory
```

`verify()` 的判据是 `live==size_ && resident_==memory_bytes()`。说明我补的 `resident_` 公式
**仍与 `memory_bytes()` 的实际口径不符**。

### 最可能的原因（下一轮先查这个）

`route_bytes(staged)` 的 `payload()` 是：

```cpp
sizeof(std::string)+string_heap(item.first)+sizeof(Node)+item.second.core->memory().total()
```

它包含**页表节点的键与 Node 大小**。而 `removed` 我只减了 `payload(*target)`，
没有考虑：`pages_` 的节点在 `insert` 新页时还要**新分配一个 map 节点**（键 `upper_key` 的字符串 + Node）。
`route_bytes(staged)` 已经把这条算进去了，但 `memory_bytes()` 是否用同一口径统计 `pages_` 的节点，
需要在源码里确认——上一轮 `rebuild()` 能对上，是因为它的 `staged` 里恰好只有"替换 target"的那一项，
`added` 与 `removed` 同构；**分裂多出一个节点，口径差异就暴露出来**。

## 处置

- 接线回滚，索引恢复 S0 验收版本（`n3_gate` 恢复全绿）；
- 保留：`extract_split` 构造两半（单元测试三点零失配）、`block_for`、arena 余量修正；
- **G4 的 0.137/0.134 仍然成立**（那一轮跑的就是本版接线，语义门通过），
  但它建立在一个**空间记账不自洽**的版本上，因此只能作为**性能上界观察**，不能作为验收结论。

## 下一步（明确）

1. 读 `memory_bytes()` 与 `payload()` 的实际口径，写出"分裂一次"引起的 `resident_` 精确增量，
   并用一个**最小用例**（一次分裂后立刻 `verify()`）验证；
2. 记账正确后重跑 `n3_gate` 与 factory 门；
3. 再谈 16 块复测与外部比较。

## 状态

| 项 | 状态 |
|---|---|
| 三处语义缺陷 | ✅ 已修 |
| `resident_` 记账 | ❌ **仍不自洽（`size/memory`）** |
| 接线 | ⛔ 回滚，索引恢复 S0 版本 |
| G4 性能观察 | 🟡 0.137/0.134 保留为**上界观察**，非验收结论 |
| Context16 默认 | ✅ 不变 |
