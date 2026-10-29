# 更正与 G1 分裂事务设计（2026-10-29）

更正 [U3 哨兵记录](RESULTS_BM_U3_SENTINEL_BLOCKED_20261029.md) 中"只剩哨兵特例"的判断，
并按新方案 G1 给出分裂事务设计。**未改代码。**

## 1. 撤回：真正的缺陷是"新根漏掉下半页"

`prepare_root(target, neighbor, staged, extra)` 的语义是：**排除 `target` 的旧绑定**，
把 `staged` 里的绑定填进去。现有 `rebuild()` 正是靠这一点工作：

```cpp
staged.emplace(first->first, Node{...});            // 上半/新页，键仍是 target 自己的键
auto next_root = prepare_root(target, neighbor, staged, ...);
auto replacement = staged.begin();
std::swap(first->second, replacement->second);      // target 采纳 staged 的页
staged.erase(replacement);                          // 其余 staged 才真正插入页表
```

我上一轮的接线**只把 upper 放进 `staged`**，于是 `prepare_root` 排除了 target（下半页所在的页）
却没有把它补回——**根里少了整个下半页**。哨兵报错只是它最先暴露的症状，
普通页会表现为**路由丢失**（查询走到邻页或返回缺席）。

→ 正确做法：**两个半页都必须进 `staged`**（下半页沿用 `target->first`，上半页用新键），
`prepare_root` 之后再 `swap` 下半页进 target、把上半页插入页表。

## 2. G1：分裂事务的完整顺序与契约

```cpp
// 阶段 0（可抛，源页不变）
b          = block_for(key) 夹到 [1, total-1]；
upper      = extract_split(*core, b)；           // 压实 arena、重建 region/fence/digest、拟合 bm_
upper.try_insert(key,value)                     // 键保证落在上半页 → 局部对象，安全

// 阶段 1（可抛，页表与根都不变）
Pages staged;
staged.emplace(target->first, Node{lower_page, gen+1});   // ← 下半页：必须显式放回
staged.emplace(upper_key,     Node{upper,      gen+2});
added      = route_bytes(staged);
next_root  = prepare_root(target, pages_.end(), staged, added);

// 阶段 2（不抛）
core->apply_drop(b, std::move(split.lower_model));
swap(target->second, staged[target->first]);              // 下半页就位
pages_.insert(staged[upper_key]);                          // 上半页入表
root_.swap(next_root);
```

### 根与页表的逐绑定契约（必须写成断言）

1. `bindings_[0].lower` 为空，且对应 `pages_.begin()`；
2. `bindings_` 的键集合 == `pages_` 的键集合，**逐一相等、顺序相同**；
3. 每个绑定的 `page` 指针 == 该键在 `pages_` 中的 `core` 指针；
4. `pages_` 中每页的行数之和 == `size_`。

第 2、3 条现在**没有断言**，这正是上次能静默丢页的原因。G1 要求把它们加进 `verify()`
（或在 `prepare_root` 之后立刻检查）。

## 3. 另外三项事实（按复核意见登记）

| 项 | 事实 | 处理 |
|---|---|---|
| 模型参数 | `extract_split` 用 `fit(...,16,1,7,3)`，而原页构造用的是页内的 `block_entries/coordinate_symbols/certificate_target` 常量；**训练单位也不同**（我按块边界喂，原构造按 region fence） | G1 必须对齐：用同一组参数与同一批训练键，否则结构收益与模型变化混在一起 |
| 空间 | 下半页保留原 arena 与数组容量，上半页是新分配的紧凑 arena | 需测**连续分裂**后的终态与峰值，不能只看单次 |
| 微基准局限 | −60.7%/−73.5% 只含"构造+切分"，不含完整插入、根发布与记账 | G4 重测完整维护路径，并纳入**回落比例** |

## 4. G1 的三天交付（本轮只登记，不做）

1. **最小反例**：一个能稳定暴露"新根缺下半页"的用例（普通页 + 哨兵页各一），
   断言第 2 条契约失败；
2. **契约实现**：`verify()` 增加上面四条断言；
3. **参数对齐**：把 `extract_split` 的拟合参数与训练键来源改成与原构造一致，
   并**单独测量**参数对齐前后的差异（否则无法分离结构收益）；
4. **覆盖率**：在四条增长轨迹上统计"页满 / arena 不足 / 前缀变化"的比例，
   得出快速路径**实际能覆盖的写成本份额**与回落成本。

## 5. 状态

| 项 | 状态 |
|---|---|
| "只剩哨兵特例"的判断 | ❌ **撤回**，真因是 `prepare_root` 排除 target 而 staged 未补回下半页 |
| `extract_split`/`apply_drop`/`block_for` | ✅ 已实现，单元测试三点零失配（组件级） |
| 语义门（既有路径） | ✅ 263,469 项零失配 |
| 接线 | ⛔ 停止追加补丁，转入 G1 事务设计 |
| 微基准收益 | ✅ −60.7%/−73.5%（组件级，非端到端） |
| Context16 默认 | ✅ 不变 |

方案与快照已入库（`docs/LONG_TERM_BM_U3_INTEGRATION_PLAN_20261029.md`、`docs/BM_U3_STATE_20261029.json`）。
