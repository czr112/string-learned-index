# 外部复测：把紧凑区域导航放进统一评测入口（matrix3）

日期：2026-10-10。入口：评测仓库 `403de52` 的 `learned-index/scripts/benchmark.py`（未改动评测仓库）。
这是计划里"完成组合判断后，只对保留下来的配置做必要的外部复测"的那一步：保留配置里
唯一能被静态点查矩阵评价的是**紧凑区域导航（C）**——WIDE 的收益要替换负载才出现，
本入口只验收 `bulk_load` + `find`。

## 1. 六臂与单变量纪律

| 臂 | 包 | 导航开关 |
|---|---|---|
| ours-context16 | `adapters/our_index` | off |
| **ours-context16-routing** | `adapters/our_index_routing`（新） | **on** |
| ours-lits | `adapters/our_index_lits` | off |
| **ours-lits-routing** | `adapters/our_index_lits_routing`（新） | **on** |
| art-libart | `adapters/art` | — |
| hot | `adapters/hot` | — |

新包与旧包共用同一份 `our_index_adapter.cpp` 与同一套构建规则；唯一的差别是包级 CMake
变量 `OUR_INDEX_ROUTING=on`，它把开关翻在适配器的唯一 Config 构造点。基线包不定义该宏，
**并且断言开关确实为 off**——以后若有人改 `baseline_config()`／`pin_frozen_switches()`，
失败会出现在这里而不是悄悄把候选当成基线测。开关不由运行时参数选择：评测入口传空参数，
"参数选择的开关"永远不会被选中。

运行时验证：`results/matrix3s_tranco/report.json` 中 2 个基线臂的 `adaptation_notes`
声明 `compact_region_routing off (baseline arm)`，2 个候选臂声明
`compact_region_routing ENABLED (candidate arm)`——接线生效是查过的，不是假定的。

## 2. matrix3：三语料 × 六臂，入口默认协议（单进程 × 3 重复）

全部 18 个臂运行 `passed`。中位查表时间（ns）与相对 ART：

| 语料 | ours-c16 | ours-c16-C | ours-lits | ours-lits-C | art | hot |
|---|---:|---:|---:|---:|---:|---:|
| mixed | 4,750,397 | 5,447,906 | 4,810,585 | 4,614,877 | 4,025,336 | 6,122,486 |
| skew | 6,580,459 | 6,546,330 | 6,942,061 | 5,947,029 | 8,243,398 | 6,663,509 |
| tranco | 5,743,651 | 4,822,708 | 4,588,957 | 5,249,308 | 3,414,858 | 6,853,720 |

候选自身的效应（同轮、同进程块、同重复）：

| 语料 | Context16 on/off | LitsHpt on/off |
|---|---:|---:|
| mixed | 1.147（慢） | 0.959（快） |
| skew | 0.995（平） | 0.857（快） |
| tranco | 0.840（快） | 1.144（慢） |

**方向在语料与后端之间翻转，这一轮的 3 重复不足以分辨**：同一臂的逐重复样本自身
散布 ±20% 以上（tranco 的 ours-context16 三次为 6.15M／5.74M／4.68M ns），
比候选效应本身还大。这正是 `READ_BASELINE.md` 早就写下的"该协议不能据此排名"。

**绝对数字不能跨轮比较**：与 matrix2（2026-10-08）相比，本轮的 ART/HOT 中位数
高了 9%–62%（tranco：ART 2.61M→3.41M，HOT 4.22M→6.85M），机器状态不同。
matrix3 的价值是"当前身份下的一次同轮记录"，不是与 matrix2 的对照。
## 3. matrix3s：加强协议的 tranco（单进程 × 15 重复，同重复配对）

用入口自己的 `--workload` 覆盖（`workload_repeats15.json`，其余不变）把重复数提到 15，
然后按本项目的方法配对：比值在**同一次重复内**形成（同种子、同负载、同进程），
报告 15 个比值的中位数与方向计数。

| 后端 | off 中位 ns（范围） | on 中位 ns（范围) | 配对比值中位 | 方向 | 比值范围 |
|---|---:|---:|---:|---:|---:|
| Context16 | 5,776,573（3.74M–7.27M） | 4,999,402（3.80M–5.69M） | **0.913** | 12/15 快 | 0.539–1.209 |
| LitsHpt | 4,785,220（3.47M–6.50M） | 4,230,452（3.00M–5.14M） | **0.919** | 10/15 快 | 0.527–1.389 |

也就是说：在入口协议下、把样本量加到 5 倍之后，tranco 上的方向是**偏快、量级约 8–9%**，
但 15 个重复里仍有 3–5 个重复方向相反，且单重复比值可到 0.53 或 1.39。

## 4. 判定

- **对外可比的一格现在有了**：当前实现身份（`index.hpp` `f573480d…`）下的六臂矩阵、
  两组候选包、以及加强协议的 tranco 配对结果都已落盘，全部通过入口验收。
- **入口协议无法复核项目内测到的 24%**：项目自己的装置（5 进程 × 4 重复、臂位置轮转、
  同重复配对、另加噪声底判定）在 tranco 静态给出 0.76–0.80、方向 5/5；
  入口给 0.913/0.919（12/15、10/15）。两者**方向一致、量级不一致**。
  这不是矛盾判决，而是两台尺子的分辨能力不同：入口没有跨臂轮转与绑核，
  逐重复散布 ±20%–50%，而它的报告自己写着"小规模结果不用于论文级排名"。
- 因此：**C 维持"默认关闭的短键静态导航场景候选"**，其对外证据记为
  "方向为正、量级 8–9%（入口加强协议）；项目内装置 0.76–0.80"。
  不升默认——提升默认需要的是项目装置给出可分辨收益，而这一点在上一轮的
  mixed/skew 上并未成立。
- WIDE（subblock_reclaim + reclaim_widest）不经这个矩阵评价；它的证据在
  `../02_Code_and_Experiments/validation_e25_20260918/RESULTS_COMBO_20261010.md`。

## 5. 顺带修掉的一处身份漂移

适配器 C++ 里的 `implementation_version` 字符串一直写着旧身份
`index.hpp sha256 4fb0a25f…`（2026-10-08 的值），而两份 manifest 早已更新到 `f573480d…`。
matrix 运行会把这句话原样记进结果文件，等于让新结果携带一个它并不是的身份声明。
本轮在适配器里改正为 `f573480d…`，并在源码注释里写明该字符串必须随 manifest 同步。
（这是第二次同型漂移：第一次是 manifest 与磁盘；这一次是代码标签与 manifest。）

## 6. 文件与身份

- 报告与原始结果：`results/matrix3_{mixed,skew,tranco}/`、`results/matrix3s_tranco/`
  （`report.md`、`report.json`、`summary.csv`、`samples.csv`、`runs/`、`builds/`）。
- 运行日志：`logs/matrix3_{mixed,skew,tranco}.log`、`logs/matrix3s_tranco.log`。
- 驱动：`run_matrix3.sh`、`run_matrix3s.sh`、`workload_repeats15.json`、
  `analyze_matrix3.py`、`analyze_matrix3s.py`。
- 新增适配包：`adapters/our_index_routing/`、`adapters/our_index_lits_routing/`；
  共享适配器与 CMake 的改动：`OUR_INDEX_ROUTING` 变量（默认 off，基线包行为与产物不变）、
  修正后的版本标签、以及基线包"开关必须为 off"的断言。
- 实现身份：`index.hpp` 5,831 行（wc -l）SHA-256
  `f573480d5ab602cb29de68ffc8e1ff480f4ad9b89c4a5049cea0bf69d0687f0a`（未改动）；
  `switches.hpp` `e75c7fad…`（未改动）。本轮只动了适配层与装置。