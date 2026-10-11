# TRUNCDIR 入口设计（冻结版，2026-10-11）

来源与预登记：`07_Eval_Integration/RESULTS_DEPTH_DIR_20261011.md` §6 的原话是——
“下一轮若继续这个候选，预先登记的触发条件是：**只做「截断分隔符」**，并且必须先解决截断下的
正确比较语义——那是一个设计问题（范围不变量或前向编码 + 解压），不是参数搜索；在它通过与本轮
同一套正确性门之前，不接任何计时。”

本文件就是那一步的设计冻结：规则、证明、接口、验收与停止条件。实现、门、A/B 都以本文件为准；
任何与本文件不同的形状都算换了候选，要另立一轮。

## 1. 候选与命名

- 候选：`ModelBackend::EqualDepthDirTrunc`（臂名 `DDirT`），是 `EqualDepthDir`（臂名 `DDir`）的
  **截断分隔符形式**，其余几何、组数推导、容量、指纹、查询、更新路径逐位不变。
- 模型文件仍是 `model/equal_depth_dir.hpp`，用 `fit(..., groups, bool truncate_separators)` 的
  布尔参数区分两种形态；默认值 `false` 时与旧调用点逐位一致。
- 开关表不新增字段：`model_backend` 已是 `require_only_switches_differ` 里按枚举整体比较的字段。

## 2. 截断规则（唯一规则，不做参数搜索）

区域内键升序 `k_0 ≤ … ≤ k_{n-1}`，区域前缀 `p`（`prefix_len_ = |p|`），后缀 `x_i = k_i[|p|:]`。
阈值 `j = 1..G-1`，排名 `r_j = min(n-1, floor(j*n/G))`，完整分隔符 `s_j = x_{r_j}`。

存储的分隔符 `t_j` 定义如下：

1. `r_j == 0` 或 `truncate_separators == false`：`t_j = s_j`（原样）。
2. 否则令 `c = lcp(x_{r_j-1}, s_j)`：
   - 若 `c < |s_j|`：`t_j = s_j[0 .. c+1)`（最短前缀，恰好在首个不同字节处胜出）；
   - 否则（`s_j` 是 `x_{r_j-1}` 的前缀，含完全相等的重复键）：`t_j = s_j`，不能再短。

即 **“比紧邻的左键大的最短前缀”**；只有重复键使该前缀不存在时才退回 `s_j`（不是参数，是规则的
边界情形，门里如实计数）。

## 3. 正确性论证（门要逐条检查）

记号：`count_T(q) = #{ j : t_j ≤ q }`（`predict_group` 的二分就是在算它）；`count_S(q)` 为未截断
版本的同一计数，也就是上一轮的参考。

- **引理 A（逐键路由等价）**：对每个存储键 `q = x_i`，`count_T(q) = count_S(q)`。
  证明：只需逐 j 证明 `t_j ≤ x_i ⟺ s_j ≤ x_i`。
  `⟸` 因 `t_j ≤ s_j`（t 是 s 的前缀）。
  `⟹` 反设 `x_i < s_j`。此时 `t_j ≠ s_j`，故 `t_j` 是 `s_j` 的真前缀，且按规则
  `t_j > x_{r_j-1}`（`c < |s_j|` 时第 `c` 字节严格更大；`c = |x_{r_j-1}|` 时 `t_j` 比左键长一字节）。
  于是 `x_{r_j-1} < t_j ≤ x_i < s_j = x_{r_j}`，而在有序数组里不可能存在排名严格介于
  `r_j-1` 与 `r_j` 之间的键。矛盾。
- **引理 B（单调）**：`t_1 ≤ t_2 ≤ … ≤ t_{G-1}`。二分按“第一个 `> q`”找边界需要这个性质；
  规则给出的 `t_j` 要么是 `s_j`，要么 `> x_{r_j-1} ≥ s_{j-1} ≥ t_{j-1}`（重复排名时两者相等）。
- **引理 C（任意查询的区间不变量）**：对任意探针 `q`（不必是存储键），令 `g = count_T(q)`，则
  `g` 之前所有组的键都 `< q`，`g` 之后所有组的键都 `> q`。
  证明：`j > g` 时 `t_j > q`，而组 `j` 起点的键 `≥ s_j ≥ t_j > q`；`j < g` 时 `t_j ≤ q`，
  组 `j` 内的键要么 `< t_{j+1} ≤ q`，要么等于 `t_{j+1}`——后者只可能在“退回 `s_j`”的重复键
  情形，此时该键字节等于 `q` 即 `q` 是存储键，落入引理 A。
  这条不变量正是查找（命中必须落在含等键的组）、下界扫描（跳过前面的组是安全的）与插入
  （组间有序性不被破坏）所依赖的“范围不变量”。

因此：截断不改变任何存储键的路由答案；对未命中探针提供合法的区间括号；`predict_group` 与放置
调用同一个函数，所以“测量用一张图、放置用另一张图”的错误不可能出现。

## 4. 接口冻结（实现/门/A-B 三方共用）

`model/equal_depth_dir.hpp`：

```cpp
static RegionModel fit(const std::string_view* keys, std::size_t n,
                       std::size_t prefix_len, std::uint32_t groups,
                       bool truncate_separators = false);   // 默认 false = 现状逐位不变
bool truncated() const;                                     // 是否按 §2 截断
std::string_view separator(std::size_t idx) const;          // idx 0..G-2 对应 j=idx+1
std::size_t separator_bytes() const;                        // arena_ 实际字节数
// identity(): 混入 truncate_ 标志 + arena + offsets（不同形态身份必须不同）
```

`index.hpp`：

- `enum class ModelBackend { Context16, LitsHpt, BlockLinearRank, EqualDepthDir, EqualDepthDirTrunc };`
- 新增 `inline bool is_depth_dir_backend(ModelBackend)`（或等价局部谓词），把所有现有
  `== ModelBackend::EqualDepthDir` 的接线点改为“两值都算”：`calibrate_depth_dir`（截断标志由
  backend 决定）、`mapping_identity`、`calibrate/构造分发`、demand 拟合（§4033-4073）、
  `Region::uses_depth_dir()`。`memory_breakdown()` 已计入 `depth_dir_.owned_bytes()`，无需改。
- `DepthDirStats` 增加两个计数（只加不删，旧字段语义不变）：
  `threshold_bytes_total`（各区域 arena 实际字节和）、`regions_truncated`。
- 组合拒绝规则与 DDir 相同：仅 `ModelKind::Context16` + `Route::Full`。

`switches.hpp`：`backend_name` 增加 `EqualDepthDirTrunc -> "EqualDepthDirTrunc"`；
`require_only_switches_differ` 的 `model_backend` 行已是枚举整体比较，不加新字段。
`baseline_config()` 不需要改（默认仍是 Context16）。

`tools_build.py`：注册 `check_depth_dir_trunc`（`-DE12_COUNT`）与 `san_check_depth_dir_trunc`（进
`SAN_TARGETS`）；`bench_depth_dir` 目标不变（源码里加运行时可选的臂列表）。

## 5. 门（check_depth_dir_trunc.cpp）

沿用 `check_depth_dir.cpp` 的全部检查形状（合成等深、重复键、域与拒绝、动态、三语料 131072、
独立重算），并新增：

1. **独立参考**：门内自带一份按 §2 写的截断参考实现（不看模型内部），对每个真语料区域逐键对比
   `predict_group`；并报“截断与未截断路由不同的键数”（预期 0）。
2. **不变量检查**：每个区域逐条验证引理 B（相邻 `t_j` 非降序）与 §2 的构造性质
   （`t_j` 是 `s_j` 前缀、`t_j > x_{r_j-1}`，重复键退回情形单独计入）；再对三类探针
   （每区域若干真实键、若干分隔符前缀、若干变异键）验证引理 C 的括号性质。
3. **负对照**：人为破坏一个区域的分隔符（换成更短的公共前缀 / 交换两个阈值）后，检查器必须报失败；
   两个破坏各跑一次，证明门不是空转。
4. **规模统计**：每语料打印 `separator_bytes`（原样/截断）、每区域 P50/P95/max 分隔符字节、
   `regions_truncated`、退回 `s_j` 的重复阈值数。
5. ASan/UBSan 三格（沿用 8192/2048/4096 的规模），无诊断才算过。

## 6. A/B（不改装置形状）

- 装置：`bench_depth_dir.cpp` + `run_depth_dir_ab.py`，5 独立进程 × 4 重复、重复内配对、位置轮转、
  噪声底 = 逐进程中位数的 max/min、拒绝覆盖已有 stem。臂列表由运行参数给出，默认仍是 `C16,DDir`。
- 主对照：`C16 vs DDirT`（三语料）；机制对照：`DDir vs DDirT`（三语料，独立 stem）。
- 采用标准沿用计划 §6：主目标 `mixed` 静态点查 `DDirT/C16 ≤ 0.90` 且 ≥4/5 进程同向、收益超过噪声底；
  风险格可分辨变慢 >5% 则不作通用读配置；构建增量 ≤10%、常驻增量 ≤5%（峰值单列）；
  只具空间价值时按空间取舍单独评估（常驻下降 ≥20% 且读/扫描/更新无可分辨 >5% 回归）。
- 计时前必须先跑同一套门；门未过不接任何计时。

## 7. 身份与交付

`RESULTS_DEPTH_DIR_TRUNC_20261011.md`（新建）记录：门、sanitizer、A/B、内存、机制读数与判定；
`STRUCTURE_OPTIMIZATION_STATE.md` 增补本轮的“已执行/判定/下一步”；适配 manifest 与
`diagnostics/run_owner_lifecycle.py` 里的 index.hpp 哈希同步重钉；快照后单独提交。
