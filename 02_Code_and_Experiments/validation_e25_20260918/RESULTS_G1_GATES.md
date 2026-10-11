# G1 证据：正确性门对**当前源码**的重建与复跑

日期：2026-09-23。目录：`validation_e25_20260918`。平台：WSL2 Ubuntu 24.04 + g++ 13.3.0，`-O3 -march=x86-64-v2 -static`。

---

## 0. 为什么做这件事

`index.hpp` 最后一次改动是 **2026-09-22 12:51:49 UTC**（`switches.hpp` 是 10:57）。构建记录因此分成两半（明细见 `results/main/g0_build_drift.txt` 与 `g0_evidence_timeline.txt`）：

| 类别 | 个数 | 含义 |
|---|---:|---|
| **LIVE** | 18 | 构建晚于 09-22 12:51，记录里的源哈希与当前磁盘**逐字节相同** |
| **DRIFT** | 51 | 构建早于该时刻，源哈希对不上 |

**漂移本身不是缺陷**——共享头文件改了就该漂移。错的是**把漂移记录当成当前源码的证据**。

`RESULTS_FREEZE.md` §4 那张表（「8/11 与 E24 逐字节相同，3 个不同」）的两个二进制**都建于 2026-09-20**，所以那句话当时成立、**现在不绑当前源码**。

项目自己的严格入口对此的反应是拒绝运行。实测：

```text
for t in check check_e8_hetero check_e15_plan check_e16_probe \
         check_e19_reuse check_e20_r2 check_e9_multiblock check_e11_locate; do
    python3 tools_build.py run "$t"
done
→ 八个全部 RUN REFUSED (rc=2)
```

这就是严格入口存在的理由。按原任务书 §8「共享头文件改变后，运行须与当前构建身份一致」，本轮把这批门**重建并复跑**，然后**逐字段**与它们自己的历史日志对照。

---

## 1. 方法

| 步骤 | 命令 |
|---|---|
| 重建 | `python3 tools_build.py <target>` |
| 复跑 | `python3 tools_build.py run <target> <KIND> <N> [CSV] [TAG] [MODE]` |
| 对照 | 继承门用 **E24 自己的运行日志** `validation_e24_20260918/logs/<gate>_run.log`；开关门用本周转目录里该门的运行日志 |
| 判据 | **逐字段比对**，不是「两边都 passed」 |

**每个门的参数按其记录过的调用形状给出**（`KIND ∈ {mixed, skew, tranco}`）：

- `check_e25_write`：`tranco` 用 `N = 32768`（与它自己的日志名 `*_tranco_32768_run.log` 一致）
- `check_e23_c3`／`check_e31`：`N = 131072`（mixed、skew）、`N = 262144`（tranco），末位 `MODE = same`
- `check_e26_c2`／`check_scan`／`check_migrate`：`N = 131072`（mixed、skew）、`N = 262144`（tranco）
- 三个 `check` 门与 `check_scan_contract` 无参数

产物：新二进制身份在 `logs/build_<target>.json`；运行输出在 `logs/g1_rerun/`；对照表在 `results/main/g0_inherited_gates_rerun.txt` 与 `results/main/g1_switch_gates_rerun.txt`。

---

## 2. 八个继承门：E24 的记录对当前源码

| 门 | E24 记录 | 当前源码 | 判定 |
|---|---|---|---|
| `check` | （E24 无运行日志；全工作区历史值唯一） | `operations 51000, scans 7777, … rebuilds 328, splits 98` | **逐字段相同** |
| `check_e8_hetero` | 8 字段 | 8 字段 | **逐字段相同** |
| `check_e15_plan` | 7 字段 | 7 字段 | **逐字段相同** |
| `check_e16_probe` | 6 字段 | 6 字段 | **逐字段相同** |
| `check_e19_reuse` | 8 字段 | 8 字段 | **逐字段相同** |
| `check_e20_r2` | 13 字段 | 13 字段 | **逐字段相同** |
| `check_e9_multiblock` | 17 字段 | 17 字段 | **仅 1 项不同** |
| `check_e11_locate` | 16 字段 | 16 字段 | **仅 1 项不同** |

两处不同的**全部内容**：

| 门 | 字段 | E24 | 当前 | 原因 |
|---|---|---:|---:|---|
| `check_e9_multiblock` | `compactions` | 106 | **79** | E26 C2 让一部分无收益压缩不再跑 |
| `check_e11_locate` | `queries_answered_from_overflow` | 13893 | **13894** | 同上：一条记录留在溢出 |

**这不是回归**：两门都仍 `passed`；变动的量正是 C2 的预期后果；其余 31 个字段全部相同。

> **`check` 的历史值来源要说清**：E24 目录里没有它的运行日志。它的一致性依据是**全工作区范围内的历史值唯一**（`grep` 出的 JSON 前缀在整个工作区只出现一个不同的取值），与当前运行逐字段相同。这是**比前七个弱一档**的证据——前七个有 E24 同轮的日志直接对照。

---

## 3. 六个开关门：历史日志对当前源码

| 门 | 数据 | 历史日志 | 当前源码 |
|---|---|---|---|
| `check_e25_write` | mixed / skew / tranco | 10 字段 | **10 个字段全同**，0 项不同 |
| `check_c3` | mixed / skew | 11 字段 | 仅 2 项不同，**都只有字节记账** |
| `check_c3` | tranco | 11 字段 | 同上 |
| `check_e31` | mixed / skew / tranco | 12 字段 | 仅 2 项不同，**都只有字节记账** |
| `check_e26_c2` | mixed / skew / tranco | **无运行日志** | 全部通过，数值与 `RESULTS_E26.md` 报过的一致 |
| `check_scan` | mixed / skew / tranco | **无运行日志** | 全部通过 |
| `check_migrate` | mixed / skew / tranco | **无运行日志** | 全部通过（`"result":"pass"`） |

**`check_e25_write` 的 10 个字段**（三数据集全部相同）：

```text
status, keys, operations, comparison_batches, records_compared,
variants, final_size, final_overflow, compactions, seed
```

**当前运行还多出 6 个字段**（`cfg_v0` / `cfg_v1` / `cfg_v2` / `branch_notlocated_fallback` / `branch_prefix_rebuild` / `branch_threshold_split`）——那是 E25 之后给这个门加的**开关串打印与分支非空性断言**，也就是说**门本身变强了**，不是被测对象变了。

---

## 4. 字节记账的差可以精确分解

`check_c3` 与 `check_e31` 唯一不同的字段是 `*_memory`。差值**不是常数，而是随区域数线性增长**：

| 数据 | N | 区域数 | `frozen_memory` 增量 | 每区域 + 固定项 |
|---|---:|---:|---:|---|
| mixed | 131072 | 64 | **+8,728** | 136 × 64 + 24 |
| skew | 131072 | 64 | **+8,728** | 136 × 64 + 24 |
| tranco | 262144 | 128 | **+17,432** | 136 × 128 + 24 |

（区域数由 `region_keys = 2048` 与 N 直接决定：131072 / 2048 = 64，262144 / 2048 = 128。）

**读数**：E38 把第二个模型后端接进 `Region` / `Index`，给**每个区域加了 136 字节、Index 级再加 24 字节**。这是模型接入的**真实常驻代价**，不是测量噪声，也不是回归——两臂（frozen 与候选）承受同一个偏移。

**这条必须随数字一起读**：`RESULTS_FREEZE.md` §6 的空间行、以及各轮 `memory` 列，都是**接线之前**的二进制测的；当前源码的常驻空间比那些记录**多 136 B/区域 + 24 B**。

---

## 5. 复跑改变了什么、没改变什么

**没改变**：所有门的 `status` 与**内容类不变量**（记录数、键序、与参考表的逐条比对、溢出计数、压缩计数、目录复核组数、扫描比对次数）。

**改变**：只有两处**事件计数**（`compactions` 106→79、`queries_answered_from_overflow` 13893→13894）与**字节记账**（+136 B/区域 + 24 B）。

**这正是项目纪律说的那种情况**：合理改造后的内部计数不同，不应被强行修到与旧日志逐字一致——需要的是**能解释**。

---

## 6. 本轮顺手修掉的一个洞：构建记录被覆盖

`logs/build_<target>.json` 是**固定路径**，而 `tools_build.py` 用 `open(..., "w")` 写它。于是**同一个目标重建第二次就地把上一条身份覆盖掉**：`build_id`、可执行哈希、源哈希全没了。

本轮重建十四个门时**真的发生了这件事**：`RESULTS_FREEZE.md` §7 引用的 `check_e8_hetero` 的 `build_id 1789836331828454586` 与 exe 哈希 `92a621ccae067c7b…`，**现在只作为文字存在于那份报告里**，记录本身已经不存在。这与项目记过的「固定输出路径 + `open(w)` = 跨轮覆盖已交付证据的身份字段」是**同一类**事故。

**修法**：`build()` 在写记录之前调用 `archive_stale_record()`，把**将被替换**的那条记录按 `build_id` 归档到 `logs/archive/build_<target>.<old_build_id>.json`。性质：

- **按 `build_id` 命名**，所以同一目标重建多次保留**每一条**身份，而不是只留最后一条；
- **幂等**：已归档的跳过；记录未变的目标完全不动；
- **不删除任何东西**；
- `tools_build.py` **不是任何构建记录的源**（已核：14 个记录的 `sources` 里都没有它），所以改它不作废任何记录。

**验证**（不是"改了脚本就算"）：重建 `check`，旧记录 `1790100264350818560` 出现在 `logs/archive/build_check.1790100264350818560.json`，新记录 `1790101806024214737`；且**新旧 exe 哈希相同**（`8df44f76cc8d081d…`）——同源同参数重建出同一个二进制，这本身是一次确定性检查。

---

## 7. 边界

- **这一轮没有改任何算法、开关默认值或被测程序。** 只重建、复跑、比对，加上一处构建工具的身份归档。
- **复跑不等于重新验收性能**。「门通过」只支持「**在门的覆盖范围内未发现功能变化**」，**不是**点查／更新性能未受影响的证据。
- **三个门（`check_e26_c2`、`check_scan`、`check_migrate`）没有可对照的历史运行日志**，所以它们的结论只是"当前源码通过"——**没有**"与历史逐字段相同"这一层。它们的历史数值只能从 `RESULTS_E26.md` / `RESULTS_E27.md` / `RESULTS_E37.md` 的正文引用里部分核对（`check_e26_c2` 的 mixed 数值与 `RESULTS_E26.md` 报过的一致）。
- **没有重跑的早期门**：`check_freeze_prefix`、`check_scan_preS2`、`check_e31_prehardening` 是**故意另存的"改动前"记录**，重建它们会毁掉存档，**保持原样**。
- **sanitizer 覆盖不受本轮影响**：`san_check_e9_multiblock`、`san_check_e11_locate`、`san_check_e31`、`san_check_freeze` 等 8 个 `san_*` 目标在 09-22 13:01–13:03 已经是 LIVE 的，它们代表的是**当前源码**在 ASan/UBSan 下的运行。
- **整数计数与字节记账之外没有别的差异**——本轮没有对 `check_c3` / `check_e31` 的 `subblock_reclaims`、`subblock_reclaim_helped`、`dir_groups_checked` 等做过任何断言之外的判断。

---

## 附：新的构建身份

| 目标 | `build_id` | exe SHA-256（前 16） |
|---|---|---|
| `check` | 1790101806024214737 | `8df44f76cc8d081d` |
| `check_e8_hetero` | 1790100277728805133 | `86189a5d6e63be34` |
| `check_e15_plan` | 1790100289576148048 | `cd603031f3259458` |
| `check_e16_probe` | 1790100302087281009 | `c12c3c53c311526e` |
| `check_e19_reuse` | 1790100314378863918 | `6df6e5b86cc1c55f` |
| `check_e20_r2` | 1790100326666978433 | `68c2e2b58fb6c14f` |
| `check_e9_multiblock` | 1790100339767288704 | `3df2e81f1cf41c77` |
| `check_e11_locate` | 1790100352088047240 | `61f99beb4310a10d` |
| `check_e25_write` | 1790100929739844157 | `76d287431248250d` |
| `check_e26_c2` | 1790100945802985916 | `0ba036016d521450` |
| `check_scan` | 1790100960894306970 | `f34b667087327751` |
| `check_c3` | 1790100976475690003 | `51e7458f6658d436` |
| `check_e31` | 1790100992403877604 | `711e898bb3f425e0` |
| `check_migrate` | 1790101007253949365 | `e0d38d3145e74fc5` |

（`index.hpp` 在这些构建里是 `587172d2a6c970b9…`，与 `check_freeze` 等 LIVE 记录一致。复跑命令与原始输出在 `logs/g1_rerun/`。）
