# S0 快照：当前结构的显式配置与实测形态

日期：2026-09-23。目录：`validation_e25_20260918`。

依据：总计划 §4.1「先建立当前结构 S0 的显式配置快照。记录主块预算、区域划分、逻辑组数、已启用维护策略、溢出策略和所有实验开关；**尤其核清 E31 局部整理与扫描定位缓存当前是否真的启用**」。

**为什么是程序而不是一段文字**：一段关于 `Config` 的文字会在加字段的当天失效——这棵树自上一份这样的文字以来加了九个字段。所以快照由 `diag_s0_snapshot.cpp` 生成，产物在 `results/main/s0_snapshot.jsonl`。

---

## 1. 配置（由 `pin_frozen_switches` 钉住，不是 `Config` 的裸默认）

```text
write_fast_locate=1, reuse_locate_ctx=1, compact_reuse_tag=1, compact_needs_garbage=1,
compact_repack_budget=0, subblock_reclaim=0, reclaim_hysteresis=0, reclaim_skip_repeat=0,
scan_cache_start=0, scan_reserve_result=1, overflow_stable_store=0, overflow_inline_tag=0,
overflow_migrate=0, model_backend=Context16
```

| 类别 | 字段 | 值 |
|---|---|---|
| **主块预算** | `block_bytes` / `slots` / `load` | 1024 B / 16 槽 / 0.75 |
| **区域划分** | `region_keys` / `fixed_groups` | 2048 / 0 |
| 寻址 | `offset_directory` | true |
| 多块组 | `multiblock` / `reuse_group_ids` | true / true |
| 写路径 | `write_simd` / `write_locate` | true / **false** |
| 溢出策略 | `ov_threshold`（Hybrid 分界） | 32 |
| 映射 | `model_kind` / `model_backend` | `Context16` / `Context16` |
| **放置 route** | `canonical` | `Route::Full` |
| **查询 route** | `query_mode`（全局，非 Config 字段） | **`Route::Interval`** |

### 三条必须随快照一起读的口径

1. **`Config{}` 的裸默认不是发布配置**——它的 `multiblock=false`、`reuse_group_ids=false` 是 E5–E8 的单块路径。发布配置由 `pin_frozen_switches()` 钉住。
2. **放置与查询走的 route 不同**（`Full` vs `Interval`）。这不是笔误，是发布配置的既有事实；`check_g1_contract` 的 C 部分就是为这条不对称而写的（6,144 个缺席键、两条 route、零处不符）。
3. **三个 bool 不在任何断言里**：`offset_directory`、`write_simd`、`write_locate` 既不在 `require_only_switches_differ` 的白名单、也不在它的"非开关字段必须相等"检查里。目前没有实验翻转它们，所以没有已发生的错误——但**两条臂若只差这三者，断言会静默通过**。G2 之后的任何实验若动它们，必须同时补进白名单。

### 候选开关的当前状态（全部默认 false）

| 开关 | 来源 | 为什么没进默认 |
|---|---|---|
| `compact_repack_budget` | E29 C3 | 负结果：1.45×–6.02× 更慢 |
| `subblock_reclaim` | E31 | 空间大降，但 `grow` 混合上一致慢 4%–8% |
| `reclaim_hysteresis` / `reclaim_skip_repeat` | E31H / E31P | 负结果，明确不再盲调 |
| `scan_cache_start` | E27 S2 / E32 | 小收益，仅百万域名方向稳定 |
| `overflow_stable_store` | O1 / E34 | 读路径受损 1.02–1.53 |
| `overflow_inline_tag` | T8 / E36 | 场景候选，二分负载仍慢 |
| `overflow_migrate` | E37 | 归因未分离、尾延迟六组全变差 |

**回答 §4.1 的那个点名问题**：**E31 的局部整理（`subblock_reclaim`）与扫描定位缓存（`scan_cache_start`）当前都没有启用**，两者都是默认关闭的候选。

---

## 2. 实测形态（初始装载后，未做任何更新）

| | mixed | skew | tranco |
|---|---:|---:|---:|
| 键数 | 131,072 | 131,072 | **262,144** |
| 区域 | 64 | 64 | 128 |
| 逻辑组 | 21,251 | 20,163 | 21,888 |
| 子块 | 33,529 | 34,775 | 33,669 |
| 每组区域数（min–max） | 171–613 | 171–493 | **171–171** |
| 主块记录 | 128,525 | 126,039 | **262,144** |
| **溢出记录** | 2,547 | 5,033 | **0** |
| 最挤组（entries） | 1,366 | 824 | 687 |
| 垃圾字节 | 0 | 0 | 0 |

**三条读数**：

1. **tranco 初始溢出为 0，且每组区域数恒为 171**——它的容量规划是均匀的，初始状态没有任何溢出压力。所以"溢出成本"这个问题在 tranco 上**只在动态负载下才出现**。
2. **mixed 与 skew 的每组区域数差异很大**（171–613）——同一个 `region_keys=2048` 对不同分布给出很不一样的分组。
3. **初始状态垃圾为 0**——所有垃圾都是删除的产物。

### 常驻空间分解（字节）

| 分量 | mixed | skew | tranco |
|---|---:|---:|---:|
| `blocks` | 34,333,696 | 35,609,600 | 34,477,056 |
| `model` | 1,056,768 | 1,056,768 | 2,113,536 |
| `prefix` | 2,624 | 4,906 | 2,048 |
| `overflow` | 476,363 | 844,255 | 0 |
| `tables` | 298,026 | 282,794 | 307,456 |
| **`directory`** | **3,114,229** | **2,746,678** | **1,619,573** |
| `self` | 2,704 | 2,704 | 5,264 |
| **合计** | 39,320,640 | 40,586,425 | 38,584,445 |

**读数**：**主块占 ~87%–89%**，是压倒性的一项；**边界目录占 4%–8%（1.6–3.1 MB）**，比模型那 1–2 MB 还大——这是 E33 讨论过的那个目录，它在空间账上不是零头；`model` 随区域数线性增长（每个区域一份表）。

---

## 3. 这份快照怎么用

- **它是 S0/S1 对照的"必须相等"清单**：任何结构实验都要先证明区域数、逻辑组数、主块预算与逻辑映射与这里一致（总计划 §7.1：「不能只依赖同一参数名称：检查逻辑组数是否由槽数等物理参数间接推导」）。
- **它绑在 `index.hpp` 的当前哈希上**：`diag_s0_snapshot` 的构建记录在 `logs/build_diag_s0_snapshot.json`。`index.hpp` 再改，这份快照就要重出。
- **它不含性能**。S0 的计时基线必须在**同轮**产出（总计划 §7.4 的协议），不能引用 E25–E38 的历史绝对值——那些绑在已被取代的源码上（G1 更正了 `index.hpp`，此后所有计时记录都漂移了）。

---

## 4. 边界

- 这是**初始装载**后的形态，不是任何动态检查点的形态。E28/E37 已测过长期替换下溢出接近活跃数的动态形态，那些数字绑在各自轮次的源码上。
- **只跑了发布配置**（M0=Context16）。LITS 后端的形态见 `RESULTS_E38_P3.md` §5.1。
- `garbage_bytes` 是**减法派生值**（`bb - bottom - bytes`），没有"区间排序 + 间隙合并"的独立账本——这一点在 G0 账本 A-4-19 与 `CODE_AUDIT.md` §6 都已登记。
