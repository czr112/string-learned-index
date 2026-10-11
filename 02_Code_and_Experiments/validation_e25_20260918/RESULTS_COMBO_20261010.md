# WIDE × 紧凑区域导航：有限组合轮结果

日期：2026-10-10。轮次 run_id `combo_20261010a`。
计划：`docs/NEXT_ACTION_PLAN_20261010.md`。
装置源码：`bench_combo.cpp`、`check_combo.cpp`、`run_combo.py`、`analyze_combo_verdicts.py`、
`run_combo_gate.sh`、`run_combo_san.sh`、`run_combo_diag.sh`、`run_combo_scancheck.sh`。

---

## 0. 结论

四臂 B／W／C／WC 在 2 后端 × 2 模式 × 3 语料 × 5 检查点上跑完，正确性与证据检查全过。
组合在维护、点查、空间三项都保留并叠加了收益，但**扫描在 grow/tranco 出现可分辨且可复现的回归**
（scan256 2.9–3.6 倍）。按计划第 4 节的采用规则：

- **W 与 WC 保留为场景候选，不升为默认**；扫描取舍登记在第 5 节。
- C 仍是默认关闭的短键导航候选；本轮补充了替换负载下对点查的正增量证据（tranco）。
- 本轮**未改动任何默认值**，未加外部臂（ART/HOT），未改计时协议。
可以直接引用的量级（200% 替换检查点，Context16 / same / mixed，中位数）：

| 指标 | B | W | C | WC |
|---|---:|---:|---:|---:|
| 维护累计 (cum_ns) | 482.0 ms | 159.1 ms | 461.0 ms | 158.0 ms |
| 命中点查 (hit_ns) | 51.5 ms | 25.8 ms | 49.7 ms | 25.0 ms |
| 索引空间 | 43.12 MB | 23.97 MB | 43.13 MB | 23.97 MB |
| 溢出记录数 | 128,533 | 748 | 128,533 | 748 |
| 导航元数据 | 0 | 0 | 512 B | 512 B |

四臂内容完全一致：4800 条记录、60 组证据检查全部 `ok`（每个检查点命中 4096/4096、
未命中 0、扫描与全量内容逐行零不符）。

---

## 1. 装置与验收

四臂定义（每后端分别运行，后端之间不计算候选收益比值）：

| 臂 | subblock_reclaim | reclaim_widest | compact_region_routing |
|---|---:|---:|---:|
| B（发布配置） | 0 | 0 | 0 |
| W | 1 | 1 | 0 |
| C | 0 | 0 | 1 |
| WC | 1 | 1 | 1 |

六对臂差异断言（`require_only_switches_differ`）全部落在装置里：B/C 只差导航、B/W 只差
WIDE 两开关、W/WC 只差导航、C/WC 只差 WIDE 两开关、B/WC 与 W/C 为三开关之并。
共同固定：发布开关（含 `compact_plan_commit`）、模型、Mode、容量计划、主块与回收预算；
`overflow_stable_store`、`overflow_inline_tag`、`overflow_migrate`、`reclaim_low_activity` 均为 false。
**正确性门 `check_combo`**：逐键对照两条外层搜索（scanned/routed）、空键／NUL／高位字节／
4096 长键、摘要碰撞集（64 字节共享前缀、强制多区域）、删除重插、重复键插入，以及
**惰性目录重建的观察**（构建后目录为空 → 首次查询填充 → 分裂后保持陈旧 → 下一次查询重建）。
四臂的全量内容逐行对照 `std::map` 参考表与彼此。

- `logs/combo_gate_20261010_v2.log`：same 模式 6 格全 `rc=0`（mixed/skew/tranco × 两后端）。
- `logs/combo_gate_20261010_grow2.log`：grow 模式 6 格全 `rc=0`。
- 门规模：每格 786,994 项检查、0 失败；`subblock_reclaims` 5 万–50 万，`reclaim_widened`
  682–1,248（tranco）。mixed/skew 在首版装置下未触发（装置缺陷，见第 7 节修正记录）。

**Sanitizer**（`run_combo_san.sh`，ASan+UBSan，`detect_leaks=0:halt_on_error=1`）：
`logs/san_combo_20261010.log` 两后端 `rc=0`、无任何报告；`reclaim_widened` 382（Context16）／
212（LitsHpt）证明机制在 sanitizer 覆盖下执行过。

**诊断双胞胎**（`diag_combo`，-DE25_DIAG，在计时负载形状上证明机制执行）：
12 个（后端×模式×语料）组合全 `rc=0`；每个组合在 200% 检查点上 `subblock_reclaims` 与
`reclaim_widened` 的 4 次重复最小值都大于 0（例如 Context16/same/mixed：124,363／2,017），
且 C 臂每行 `routing_regions == regions`。脚本：`analyze_diag_combo.py`。

**计时协议**：5 独立进程 × 4 重复，四臂在每次重复内轮转位置；比值在重复内配对形成后
按进程取中位数，方向计数基于这些中位数，噪声底取逐进程中位数的 max/min。
命中与未命中分开报告；扫描 16/256/4096 三档、同一起点集合；所有答案在计时外逐条验证。
装置顺序：命中阶段先计时（惰性目录重建因此计入被测量的字段，而不是被预热隐藏）。
---

## 2. 维护（替换负载，累计）

60 格方向统计（每格 5 个进程内配对中位数）：

| 配对 | FASTER | SLOWER | 未分辨 |
|---|---:|---:|---:|
| W/B（cum_ns） | 44 | 0 | 16 |
| C/B（cum_ns） | 3 | 4 | 53 |
| WC/W | 9 | 0 | 51 |
| WC/C | 43 | 0 | 17 |
| WC/B | 45 | 0 | 15 |

- **WIDE 是维护收益的主体**：Context16/same/mixed 的累计从 482 ms 降到 159 ms（0.35），
  skew 到 0.20，tranco 到 0.58；全程 5/5 同向。
- **导航单独几乎不动维护**（53/60 未分辨）。唯一可分辨的负格：LitsHpt/grow/skew
  上 C/B = 1.025–1.042（0/5），即目录重建随结构变动进入写路径的代价，量级 2.5%–4%。
- **组合 = WIDE 的维护收益 + 导航的小额增量**（WC/W 有 9 格 FASTER、0 格 SLOWER；
  tranco 上 WC/W ≈ 0.83–0.91）。
- `build_ns` 四臂无分辨差异（全部落在噪声底内）——构建一次，与开关无关，符合实现。

## 3. 点查

命中查询：

| 配对 | FASTER | SLOWER | 未分辨 |
|---|---:|---:|---:|
| W/B | 34 | 1 | 25 |
| C/B | 11 | 0 | 49 |
| WC/W | 17 | 0 | 43 |
| WC/C | 36 | 0 | 24 |
| WC/B | 47 | 0 | 13 |

未命中查询与命中同形（W/B 37 FASTER；WC/B 46 FASTER；无 SLOWER）。

- **WIDE 让替换后的点查快一倍上下**：mixed 0.50、skew 0.45、tranco 0.70（200%，
  5/5 同向），与溢出记录数降到 0.6%–24% 一致。
- **导航的增量在短键语料**：tranco 静态 C/B = 0.764（5/5），且 200% 时 WC/W = 0.755
  （4/5）、WC/C = 0.639——启用 WIDE 之后导航仍然值钱（回答计划里"回收之后导航是否还有价值"）。
- mixed/skew 上导航对点查中性（方向多为 1–3/5，量级 ±3%–7%，均没过噪声底）。
- 唯一 SLOWER 命中格：Context16/grow/skew/静态 1.021（噪声底 1.01）。该检查点上 W 与 B
  结构完全相同（还没有发生过一次插入回收），因此它不是机制结果，而是本装置在静态点的
  ±2% 级逐臂抖动，登记为装置注记。
## 4. 扫描：可分辨回归

扫描在混合语料上跟随 WIDE 变快（scan4096 mixed 0.85–0.88、skew 0.88，5/5），
但在 grow/tranco 上**跟随 WIDE 变慢**，且量级随倒换量增长、两份独立进程集都复现：

| 格 | 检查点 | 配对 | 中位数 | 方向 | 噪声底 |
|---|---|---:|---:|---:|---:|
| Context16 grow tranco scan256 | 200% | W/B | 2.951 | 0/5 | 1.82 |
| Context16 grow tranco scan256 | 200% | WC/B | 3.599 | 0/5 | 1.36 |
| LitsHpt grow tranco scan256 | 200% | WC/C | 3.269 | 0/5 | 1.35 |
| Context16 grow tranco scan4096 | 200% | W/B | 1.492 | 0/5 | 1.27 |
| LitsHpt grow tranco scan4096 | 200% | WC/C | 1.398 | 0/5 | 1.09 |
| LitsHpt same mixed scan16 | 25% | W/B | 7.300 | 0/5 | 1.33 |
| Context16 grow tranco scan16 | 200% | WC/B | 2.304 | 0/5 | 1.10 |

绝对值（Context16 grow tranco 200%）：scan256 B 1.241 ms → W 4.024 ms；scan4096
B 15.42 ms → W 23.97 ms。重复扫描（`run_combo_scancheck.sh`，全新 5 进程、新输出名，
`logs/combo_scancheck_verdicts.log`）在同格给出 2.95–3.60 倍与 1.20–1.55 倍，量级与方向一致。

装置自身的定标：静态检查点上 W 与 B（以及 WC 与 C）结构完全相同，扫描读数仍可出现
**最高约 1.29 倍**的逐臂差（LitsHpt/same/mixed 静态 scan16，WC/C = 1.286，0/5，两轮都出现）。
因此小倍率（≈1.1–1.3）的扫描格保留在"本装置分辨能力边缘"，而 2 倍以上的 grow/tranco 格
超出该边缘、且随倒换量单调增长，登记为**真实回归**（机制未定，见第 7 节）。

四臂在这些格上返回的行数与内容完全一致（272／4352／69632 行，逐行零不符）。
**扫描计数器（`analyze_scandiag.py`，`results/main/combo_scandiag_*.jsonl`）把原因再收窄一层**：
两格对照（Context16 grow tranco、LitsHpt same mixed）在 200% 上，B 与 W 的
`scan_regions`、`scan_groups`、`scan_groups_empty`、`scan_blocks_skipped`、
`scan_result_allocs`、`scan_start_locates`、`scan_cursor_starts`、`scan_canonical`
全部相同（比值 1.000），`scan_ov_cursor_starts` 因溢出减少而降到 0.57／0.09，
`scan_merge_cmps` 为 1.29–1.32（tranco）／0.93（mixed）。
**也就是说：同样的行数、同样的组与子块、同样的结果分配、同样的定位次数，扫描的被计数
工作量几乎不变，而挂钟时间差到 3 倍。** 因此这不是"扫描算法做了更多工作"，
而是 WIDE 索引状态下扫描路径的实测减速；机制仍未定（未计入的候选是组内起始游标的
推进比较——它没有计数器，且它在小 limit 下占比最大，与"limit 越小相对回归越大"一致）。
## 5. 空间与溢出

- **WIDE 同时是空间收益**：index_bytes 在 48 个非静态格上全部变小（0.49–0.84），
  静态点四臂相等（1.000，IDENTICAL）。mixed/same 200%：43.12 MB → 23.97 MB。
- **溢出记录**：静态四臂相同（390）；200% 时 B 128,533 → W 748（mixed/same），
  tranco/grow B 195,278 → W 46,787（0.24），其余语料 0.6%–11% 不等。
- **导航元数据**：8 字节/区域，C 与 WC 臂在 60 格里都是"略增"（x B 略大于 1，
  因为分母是几十 MB）；绝对量很小：mixed/same 64 区域 → 512 B。它已计入
  `memory_breakdown()` 的 tables 项，不是旁列。

## 6. 采用规则与判定

按计划第 4 节逐条对照：

- 正确性、固定预算、身份核验：全部通过（第 1 节）。
- **WC 相对 W/C 有增量收益**：维护 WC/W 9 格 FASTER / 0 SLOWER；点查 WC/W 17 格
  FASTER / 0 SLOWER；tranco 上 WC/B 静态 0.782、200% 0.517（Context16/same）。组合不是"重复计数"。
- **但扫描出现可分辨回归**（grow/tranco 2–3.6 倍，两轮复现）：
  → **登记取舍，保留场景候选，不升为通用默认**。
- 结论：本轮产出的是"完成的组合判断"而不是"完成的组合候选"：
  W 与 WC 在维护／点查／空间上有真实、可复现的收益；导航与 WIDE 在短键点查上可叠加；
  扫描的代价必须随收益一起引用。**默认值不变。**
- C 单独的对外可比性仍成立（静态短键导航），且本轮补上了替换负载下的正增量证据。

## 7. 装置缺陷与修正记录（必须在引用时知情）

1. **首版替换负载是批量的**（先全部 erase 再全部 insert）。批量删除先制造大量空洞，
   使回收路径在 mixed/skew 上从不触发（`reclaim_widened = 0`，门 rc=3 不含糊地拒绝）。
   改为与 `check_widest`/`bench_widest` 一致的逐键交错后，六格门全过。
   旧日志 `combo_gate_20261010.log` 保留，记录的是旧装置的行为。
2. **分析器曾把配对方向写反**（表头 W/B 实际打印 B/W），由 routing_bytes 列出现
   0.000 而发现；修正后 60 组证据检查仍全过。
3. **`san_check_combo` 曾缺 `-DE25_DIAG`**（源内 `#error` 拦下），补进 `EXTRA` 后通过。
4. 扫描减速的机制（为什么减少溢出反而变慢）本轮**未定**；有界诊断已把范围收窄到"不是遍历工作量"（见第 4 节末），剩余疑点是未计入的起始游标推进与主块逐项读取的成本。
## 8. 身份与交付

- `index.hpp` 5,831 行（wc -l），SHA-256
  `f573480d5ab602cb29de68ffc8e1ff480f4ad9b89c4a5049cea0bf69d0687f0a`（与计划钉值逐位一致；
  本轮的实现源码**未改动**，只新增装置）。
- `switches.hpp` 165 行，SHA-256
  `e75c7fad79a1944af4b1ecc48761b8d8180568aa473d7f0a0f0872ef16d0880f`（未改动）。
- 装置（本轮结束后重算的最终值；`bench_combo.cpp` 在计时轮之后又加过扫描计数器，
  所以它与构建计时轮的记录 `logs/build_bench_combo.json` 里的哈希不同——计时数据绑定的是
  加计数器之前的版本，扫描计数器只影响 `diag_combo` 的输出字段）：
  `bench_combo.cpp` `664b69feae890976ed5881c6882a5f4f5d25266e410204505735af010edcab1e`（终版）／
  `fd6ee7121a664fe7731a11240987b9c52180ef957d42fceb5adbe209d7f06eb8`（计时轮版本）；
  `check_combo.cpp` `0f9833d1d94f90b8b8784939a953bedb2e8c0ce140d83914a5d856eff48adeb9`；
  `run_combo.py` `05a189a53545c45ec60ec23d46445b61368d4c8ec93aaf1ab10a6dd242645b2c`；
  `analyze_combo_verdicts.py` `85f2e5631585b0d08b1009229a834ad297c8bd8253323aa56c5aec3c9e96fe3c`；
  `analyze_scandiag.py` `aa44cba56d94770f5a49c432db2606b9c1b877ded02498179708acccd6b3b18a`。
- 构建记录：`logs/build_bench_combo.json`、`logs/build_check_combo.json`、
  `logs/build_diag_combo.json`、`logs/build_san_check_combo.json`。
- 原始数据：`results/main/combo_combo_20261010a_{ctx16,lits}_{same,grow}.jsonl`（4 × 1200 行）；
  复现扫描 `results/main/combo_scancheck_*.jsonl`。
- 分析日志：`logs/combo_analyze_20261010b.log`（全表 + 噪声底）、
  `logs/combo_verdicts_20261010b.log`（方向统计 + 全部回归格）、
  `logs/combo_gate_20261010_v2.log`、`logs/combo_gate_20261010_grow2.log`、
  `logs/san_combo_20261010.log`、`logs/diag_combo_20261010.log`、
  `logs/combo_scancheck_verdicts.log`。
- 适配包 manifest 未受影响（`index.hpp` 未变）；如需把 C 或 WC 提为默认，才需要重同步与四方复测。

## 9. 未决问题与下一步

1. **扫描回归的机制未定，但已被收窄**。本轮的定向诊断（`run_combo_scandiag.sh` +
   `analyze_scandiag.py`）证明扫描的被计数工作量在 B/W 间相同，所以问题不在组／块／
   结果分配／定位。下一个有界问题是"起始游标的推进比较"：它是扫描里唯一没有计数器的
   环节，且在小 limit 下占比最大（与观察到的"limit 越小、相对回归越大"一致）。
   要测它需要给 `index.hpp` 增加一个诊断计数器——那会改变实现身份，必须先明确收益，
   并且按项目纪律重跑门、sanitizer、manifest 与状态文档。
2. 若扫描机制被解释且可修：WC 才有资格进入"完成的组合候选"；届时按计划做必要的
   外部复测（静态四方点查只能评价导航部分）。
3. 若扫描代价被认定结构性：WC 的场景定位是"更新密集、扫描次要"的负载；
   报告与状态文档按此引用，不再尝试把它推成通用默认。
4. 计划第 4 节的后备方向（残余主块检索/放置成本）保持待命，本轮不动。

一句话给外部读者：**WIDE 把更新负载的维护时间压到 1/3、把替换后的点查压到 1/2、
把空间和溢出压到 1/2 以下，并在短键语料上与紧凑导航叠加；代价是 grow/tranco 的扫描
慢 1.4–3.6 倍，因此两者都保持默认关闭的场景候选。**