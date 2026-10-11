# Stage A 扫描起点诊断：独立核验与计数结论（2026-10-10）

本文是**独立核验件**，不替代主结果文档。它记录：本轮新增的核验装置、在 0 号机上跑到的门/网格状态，
以及从逐起点计数里读出的机制结论。所有数字都是**计数**，本文不把任何计数换算成时间或加速比。

## 1. 本轮新增的核验装置（本轮落盘）

| 文件 | 作用 |
|---|---|
| `check_scan_entry.cpp`（目标 `check_scan_entry` / `san_check_scan_entry`） | 12 键小案例的手算核验门。用**内容**（子块计数、后缀序列、溢出顺序，经 `diag_*` 访问器）独立重算 `adv_p_cmps/adv_p_skipped/adv_p_subblocks/adv_o_cmps/adv_o_skipped/rows/out_primary/out_overflow`，与计数器逐项比较；并把 6 行手算期望值冻结成表 |
| `analyze_scan_entry.py` | 逐次扫描分析器：网格完整性、逐行契约、按次增量与主文件累计增量的交叉核对（残差=维护期扫描工作），逐起点/逐探针表，计数比值 |
| `run_scan_entry.sh` | 一轮驱动（重建 → 门 → sanitizer → 三格 → 分析），对已存在的日志与输出名一律拒绝 |

手算门结果：`41 checks, 0 failures`；分支覆盖 `sub-block steps=44, overflow skips=12,
stopped-by-cmp=56, stopped-at-boundary=16` —— 四类推进路径都真实走到，不是"非零即通过"。

**装置自身的一处缺陷（已修，记录在案）**：分析器第一版把推进不变量写成了
`skipped <= cmps <= skipped + 1`。该式只在**单区域**成立；`Index::scan` 每进入一个区域就再调用一次
`Region::scan`，每个区域各自可能多出一次"失败的比较"。LitsHpt/tranco 格每次调用都跨两个区域，
于是两格诚实的网格被误拒（1682 条）。修正后的上界是
`skipped <= cmps <= skipped + scan_regions`（主块）与 `skipped <= cmps <= skipped + scan_ov_cursor_starts`
（溢出）。修正后 5 格全部通过。装置的错误被记下来，因为它一度把"规则写错"显示成了"实现有问题"。

## 2. 门与 sanitizer（本轮源码身份在运行当时）

```
check_scan_entry                                   rc=0  (41/0)
check_combo tranco 262144 ctx16 grow               rc=0  1573426/1573426 checks; subblock_reclaims=500238 reclaim_widened=8946
check_combo tranco 262144 lits  grow               rc=0  1573426/1573426 checks; subblock_reclaims=324604 reclaim_widened=5826
check_combo mixed 131072 lits  same                rc=0   786994/786994  checks; subblock_reclaims=259468 reclaim_widened=4772
san_check_scan_entry                               rc=0
san_check_combo mixed 8192 ctx16 same              rc=0   49714/49714 checks
san_check_combo mixed 8192 lits  same              rc=0   49714/49714 checks
```

日志：`logs/run_scan_entry_20261010.log`（本轮），以及 `logs/combo_gate_20261010_se*.log`、
`logs/san_combo_20261010_se.log`（并行执行进程在同一身份下所跑的门）。三次 WIDE 分支计数在
两种后端、三个语料上都非零，`NOT EXERCISED` 没有出现。

## 3. 网格与独立核验

| 网格 | 路径 | 主行 / 逐次行 | 严格分析器 |
|---|---|---:|---|
| Context16 grow/tranco 262144 | `results/main/combo_se_ctx16_growtranco*.jsonl` | 80 / 13200 | 通过 |
| LitsHpt grow/tranco 262144 | `results/main/combo_se_lits_growtranco*.jsonl` | 80 / 13200 | 通过 |
| Context16 grow/tranco 262144 | `results/scanentry_20261010/se_Context16_grow_tranco*.jsonl` | 80 / 13200 | 通过 |
| LitsHpt grow/tranco 262144 | `results/scanentry_20261010/se_LitsHpt_grow_tranco*.jsonl` | 80 / 13200 | 通过 |
| LitsHpt same/mixed 131072 | `results/scanentry_20261010/se_LitsHpt_same_mixed*.jsonl` | 80 / 13200 | 通过 |

通过的含义（逐行、逐格）：`rows == expect_rows`；`out_primary + out_overflow == rows`；
校验相 `bytes_mismatch == 0`；`scan_cursor_starts == scan_groups`；推进计数满足第 1 节的区间；
按次增量之和不超过主文件对应检查点的累计增量（差额即维护期扫描工作，逐格打印）；
17 个起点 × 2 计时遍 × 1 校验遍 + 4 个边界探针在每个 (rep, 臂, 检查点, limit) 都在。
两套网格分别是两份独立进程集（不同输出名、不同时间），并非同一次运行改名。

## 4. 计数读出的机制结论

### 4.1 中位数掩盖了全部现象，"极端起点"才是代价所在

LitsHpt / same / mixed，`frac=1.0`，`limit=16`，逐起点 `adv_p_cmps / adv_p_skipped / adv_p_subblocks`：

| 起点 | B | W |
|---|---|---|
| 7 | 2/2/1 | **26/26/1** |
| 8 | 2/2/1 | **18/18/1** |
| 14 | 0/0/10 | **360/360/10** |
| 15 | 0/0/10 | **340/340/10** |
| 16 | 0/0/4 | **136/136/4** |

同一格的中位数（17 个起点）是 B 1.0 / W 1.0 次比较。也就是说：**只看中位数会得出"两边一样"的结论，
而实际代价集中在少数起点上**，且全部发生在主块侧（`adv_p_*`）。

### 4.2 方向：WIDE 把记录从溢出搬进主块，线性推进于是变长

上表里 B 的 `0/0/10` 表示：这些起点所在组的主块**是空的**（10 个子块整块跳过、0 次比较），
记录全在溢出容器里，而溢出容器的头部恰好已经满足起点。W 侧同样是 10 个子块，
但记录已经在主块里，于是同一段线性推进要逐条比较 340–360 次。

跨检查点的总计数比值（17 起点 × 4 重复求和）与这个读法一致：

| 格 | 比值 | frac=0.5 | frac=1.0 | frac=2.0 |
|---|---|---|---|---|
| mixed/same/LitsHpt | W/B `p_cmps` | 3.22 | **89.10** | — |
| mixed/same/LitsHpt | W/B `o_cmps` | 0.154 | 0.019 | — |
| tranco/grow/Context16 | W/B `p_cmps` | 1.000 | 1.000 | 1.78 |
| tranco/grow/Context16 | W/B `o_cmps` | 0.833 | 0.857 | 0.545 |

主块推进比较在 W 侧上升、溢出推进比较在 W 侧下降，两者互为镜像：**这不是"扫描变慢了"这一句空话，
而是"同一次线性起点推进落在了更多、更满的主块记录上"**。

### 4.3 边界探针：一次零结果扫描要走完整组

mixed/same/LitsHpt，探针 `after_last`（排在末键之后，返回 0 行）：

| 检查点 | 臂 | p_cmps | p_skip | o_cmps | o_skip |
|---|---|---:|---:|---:|---:|
| frac=0.0 | B / W | 492 / 492 | 492 / 492 | 0 / 0 | 0 / 0 |
| frac=2.0 | B | 0 | 0 | **492** | 492 |
| frac=2.0 | W | **492** | 492 | 0 | 0 |

即：**返回零行的一次扫描，在两种臂下都要线性走过整组 492 条记录**，只是 WIDE 换到了主块侧支付。
其余三个探针（`absent_mid`、`first_key`、`before_first`）都只花 1–2 次比较。

## 5. 与计划判据的对照（`docs/NEXT_ACTION_PLAN_20261010_SCAN.md` §3.3）

1. **正确性通过、WIDE 分支实际执行**：满足（第 2、3 节）。
2. **分相位计数揭示可省的起点定位工作，并能指出具体主块侧或溢出侧**：满足，且落在**主块侧**：
   起点推进是逐条线性走，最坏一次零结果扫描 492 次比较；W 侧在同一批起点上是 0→136/340/360。
3. **起点/状态的分布与回归相符**：方向相符（回归最大的 mixed 格比值最大），但本文**不**把计数换算成
   时间占比；时间侧仍须阶段 B/C 的同状态、同队列干净 A/B 才能定论。

因此计数支持**实现一次可检验的候选 S（缩短扫描起点定位，主块侧优先）**，
不支持"计数减少即性能成功"这种说法。

## 6. 身份与时效性告警（必须随本文件一起读）

* 本目录的 `index.hpp` 在本轮进行中至少变动过两次（`f44fd8b3…` → `f7857efd…` → `cad9673b…`，
  最后一次 19:47:09）；`bench_combo.cpp`、`tools_build.py` 同步变动（诊断消费模式 `-DE25_SCAN_CONSUME`
  与目标 `bench_scanconsume` 加入）。
* 第 3 节各网格只对**其自身构建记录**里的身份成立：两套网格分别由
  `build/diag_combo.built.json` 在各自时刻记录（`logs/archive/build_diag_combo.*.json` 可查）。
  **源码再次变化后，这些网格不能当作新身份的复测结果**；按计划第 8 节，须先重跑受影响的门、
  再用新输出名重测。
* 第 3 节最后一行第三格（mixed/same/lits）由 `tools_build.py` 以 rc=2 拒绝执行：
  运行前校验发现源码已被并行改动，因此**没有**产出误导性的半份数据；分析器随后同样 rc=2 拒绝。
  这是装置按设计工作，不是丢失证据。

## 7. 复现命令

```bash
# 手算核验门（只读计数，不产出结果文件）
python3 -B tools_build.py check_scan_entry
python3 -B tools_build.py san_check_scan_entry

# 严格分析器：自带拒绝路径自检
python3 -B analyze_scan_entry.py --selftest
python3 -B analyze_scan_entry.py combo_se_ctx16_growtranco combo_se_lits_growtranco
python3 -B analyze_scan_entry.py --dir results/scanentry_20261010 \
    se_Context16_grow_tranco se_LitsHpt_grow_tranco se_LitsHpt_same_mixed
```

分析器对缺文件、缺格、重复格、`rows != expect_rows`、输出拆分不符、计数越界、
按次增量超过主文件增量一律拒绝（`--selftest` 会逐条演示这些拒绝）。