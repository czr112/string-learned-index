# U3 实现规格（修订）：`Page` 的"按后缀构造"入口与 `rebuild()` 去 scan

日期：2026-10-29。依据 [U3 第 0b 步](RESULTS_BM_U3_STEP0B_20261029.md)：`scan` 占重建成本 **64%–77%**。
本文把实现写成可直接照做的形态，**尚未实现**。

## 为什么必须新增构造入口

`Page` 现有的构造是模板 `<class InputRow> Page(const std::vector<InputRow>& rows, size_t occupancy)`，
内部逐行读 `rows[i].key`（`size()` / `substr()` / `compare()`）。

重建路径现在的做法是：

```cpp
auto rows = it->second.core->scan("", SIZE_MAX);   // ← 2,048 个 std::string，每个都分配
rows.insert(at, Row{std::string(key), value});     // ← 又一次拷贝
rebuild(it, std::move(rows), false);               // ← 内部走上面的模板构造
```

`scan` 之所以贵，是因为**完整 key = region 前缀 + 后缀**，要连续存储就必须物化成 `std::string`。
所以不能靠"给 scan 换成 string_view"绕过去——**必须让构造入口同时拿到前缀与后缀**。

## 新增构造（约 45 行，逻辑与现有构造逐段对应）

```cpp
// 与现有构造相同的编码流程，但输入是 (后缀, 值) 且共享同一个 region 前缀，
// 因此不需要为任何一行物化完整 key。
template<class SuffixRow>   // SuffixRow 需有 .suffix (std::string_view) 与 .value (uint64_t)
explicit Page(std::string_view region_prefix,
              const std::vector<SuffixRow>& rows,
              std::size_t occupancy=15);
```

内部逐段照抄现有构造，唯一的差别：

| 现构造 | 新构造 |
|---|---|
| `rows[i].key` | `rows[i].suffix` |
| `common_prefix(rows[first].key, rows[end-1].key)` | 直接把 `region_prefix` 作为 region 前缀（不再计算） |
| `fences_.push_back(rows[first].key)` | `fences_.push_back(std::string(region_prefix)+std::string(suffix))`（每个 region 一次，不是每行） |
| 训练用 `boundary()` | 不变 |

**注意**：新构造要求所有行的后缀确实以 `region_prefix` 开头（即它们来自同一页）。
若重建涉及跨 region 合并，**回落到整页扫描路径**（受控降级）。

## `Page` 侧取后缀的入口

```cpp
// 按块顺序输出 (后缀, 值)，不分配：后缀就是 arena 里的既有字节区间。
std::vector<std::pair<std::string_view,std::uint64_t>> suffixes() const;
```

同时暴露 `region_prefix()`（`regions_[0].prefix`）。

## `rebuild()` 的改法

```cpp
// pages_.erase / staged 逻辑不变，只把"取行"换成"取后缀"
const auto prefix = it->second.core->region_prefix();
auto pairs = it->second.core->suffixes();          // 无分配
// 按 key 找到插入位置：比较 prefix+suffix 与目标 key（用 compare 的复合比较，不物化）
auto pos = lower_bound(pairs.begin(), pairs.end(), key, [&](const auto& p, std::string_view k){
    return compare_suffix(prefix, p.first, k) < 0; });
pairs.insert(pos, {std::string_view(key).substr(prefix.size()), value});
auto next = std::make_unique<Page>(prefix, pairs, occupancy_);
```

要求：`key` 必须以 `prefix` 开头（`route()` 已保证），否则走回落路径。

## 门（与 U3 原门一致）

| 门 | 判据 |
|---|---|
| 语义 | 三语料全行/全查询/缺席/边界/跨销毁 scan/增删改，零失配 |
| 不变量 | 每次插入后 `verify()`；块序、槽范围、tag、fence 数 |
| 异常 | 三类分配失败注入后旧键完整、计数守恒、可继续 |
| 收益 | 重建单价 **−25% 以上**（预期 −60%～−75%），总写耗时 −19%～−24%（预期 −45%～−70%） |
| 空间 | 常驻请求字节 ≤1.05× |
| 回退 | 完整查询相对原 BM ≤5% |

## 停止线

- 第一次实现若在**语义门**失败且两轮内无法修复 → 停止，保留"scan 占 64%–77%"这条成本结论；
- 收益若低于 25%（例如因为 fence/digest 重算吃掉大部分）→ 停止，不再追加机制。

## 状态

| 项 | 状态 |
|---|---|
| 成本证据（scan 64%–77%、拟合 3.7%） | ✅ |
| 本规格（构造入口 + rebuild 改法） | ✅ |
| 实现 | ⬜ **待做**（两次机会） |
| Context16 默认 | ✅ 不变 |
