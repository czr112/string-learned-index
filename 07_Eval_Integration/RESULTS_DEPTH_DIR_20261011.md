# EqualDepthDir：等深阈值目录的接入、正确性与同状态 A/B（P1–P3）

日期：2026-10-11。计划：`docs/NEXT_ACTION_PLAN_20261011_BLOCK_MODEL.md` §3（P1 隔离后端 → P2 正确性 →
P3 净收益 A/B），候选与编码由 [RESULTS_THRESHOLD_ENTRY_20261011.md](RESULTS_THRESHOLD_ENTRY_20261011.md)
的入口诊断预先选定（分隔符形式；63-bit 坐标形式已在该轮被实测否决）。

## 0. 判定

**正确性成立、机制成立，但收益是分裂的：这不是一个可以设为默认的通用后端。**
按计划 §6 的采用规则（主目标 mixed、比值 ≤0.90、≥4/5 进程同向），mixed 在静态点查上
**1.142（0/5 更快）**，不通过；因此 **默认仍为 Context16**，新后端以默认关闭的候选身份收口，
并带走一条明确的场景结论：**skew 上它是净收益**（静态 0.903、更新后 0.779，均 5/5）。
构建与维护在三个语料上都是它更快（0.87–0.95 / 0.43–0.71，5/5）。

## 1. 身份

| 文件 | sha256 |
| --- | --- |
| `validation_e25_20260918/index.hpp` | `8688225e23bae7aa0d54daafc4c19ceab61f7d250fb66431c2be832b2a983f82` |
| `validation_e25_20260918/switches.hpp` | `12a497a283cd25b0e6d67d74bfdfbc9815037cbaf192fa6ef244b00ca54278ee` |
| `validation_e25_20260918/model/equal_depth_dir.hpp`（新） | `07495f52e422868f5403efc2fdc99270a3d5150f7899c4b7ca5da9d74a8b75bc` |
| `validation_e25_20260918/check_depth_dir.cpp`（新门） | `7ad1636ecbf4a63ce57dff53eab2c70ad7726cec16d2a2ed3cb3224b7f274099` |
| `validation_e25_20260918/bench_depth_dir.cpp`（新 A/B） | `2fd6ea820a6ccc665dd63cb4d7d0551899a5cbf4e23b962f8a276d28c69d416b` |
| 四个适配包 manifest + `diagnostics/run_owner_lifecycle.py` | 已同步到上表 index/switches 哈希 |

## 2. 实现（P1）

`model/equal_depth_dir.hpp`：区域内按**排名等深**放 G−1 个阈值（第 j 个在 rank ⌊j·n/G⌋），
键的组号 = 后缀比较中「不大于它的阈值个数」；阈值以**后缀**存进一个 arena（自持、不借用训练键），
二分查找用索引自己的无符号字节序。三种边界状态（n=0 / n=1 / G=1）都定义答案为组 0。

`index.hpp`：第四个 `ModelBackend::EqualDepthDir`，`calibrate_depth_dir`、四处分发
（`canonical_group` / `predict_for` / demand 拟合 / demand 计数）、`mapping_identity` 混入目录身份、
`memory_breakdown` 计入 arena、`DepthDirStats`（含「阈值重复的区域数」）。组合拒绝与另两个后端同形
（`ModelKind != Context16`、`canonical != Full`）。默认关闭。

## 3. 正确性（P2）

| 检查 | 结果 |
| --- | --- |
| 门 `check_depth_dir`，mixed / skew / tranco（131072 键） | **131,626 项检查，0 失败**（三语料各自） |
| ASan/UBSan 三格（8192 / 2048 / 4096） | 8,626 / 2,476 / 4,526 项，0 失败，stderr 空 |
| 独立重算 | 门自带一份按契约写的参考实现（阈值排名 + 计数），在**真语料区域切片**上逐键比对，全等 |
| 等深性质 | 合成区域 n=100、G=8 的组大小**精确**为 12/13/12/13/12/13/12/13 |
| 全域 | 空键、内嵌 NUL、≥0x80、4096 字节键、重复键（重复阈值→空组，如实计数）、n=0/1、G=1 |
| 负对照 | 旋转组向量必须被抓到（合成数据 + 每个真语料区域） |
| 动态 | 1500 插入 / 700 擦除后重查与重单调；分裂与前缀重建路径经 Region 构造重拟合 |
| 回归 | 既有五个门/基准（`check_g1_contract`、`check_sround`、`check_compact_routing`、`check_hole_reuse`、`bench_lits2x2`）全部重建通过 |

## 4. 同状态 A/B（P3）

装置：`bench_depth_dir.cpp` + `run_depth_dir_ab.py`。5 个独立进程 × 4 重复，两臂在每次重复内
轮转位置，比值在**同一次重复内**配对，报告为逐进程中位数的中位数；噪声底 = 逐进程中位数的
max/min。20 对/语料，**0 个完整性问题**（两臂 checksum、命中数、探针哈希全部相等，mismatch 为 0）。

比值 = DDir / C16，<1 表示目录更快：

| 语料 | 静态点查 | 更新后点查 | 构建 | 替换/维护 | 判定（对噪声底） |
| --- | ---: | ---: | ---: | ---: | --- |
| mixed | **1.142**（0/5） | 1.044（1/5） | 0.871（5/5） | 0.489（5/5） | 静态**可分辨回归**（噪声底 1.128） |
| skew | **0.903**（5/5） | 0.779（5/5） | 0.947（4/5） | 0.427（5/5） | 静态与更新后**均为可分辨收益**（噪声底 1.134 / 1.124） |
| tranco | 1.257（0/5） | 1.222（0/5） | 0.917（5/5） | 0.711（5/5） | 静态与更新后**未分辨**（噪声底 1.321 / 1.246） |

常驻（`memory_breakdown().model`，全索引）：mixed 1,056,768 → 3,263,329 B、
skew → 2,936,754 B、tranco 2,113,536 → 2,465,307 B。mixed 上 +2.21 MB ≈ 全索引 +5.6%，
**超出计划 §6 的 ≤5% 常驻预算**；tranco +0.35 MB 在预算内。这一项独立于速度，按预算判未通过
（除非另行采用 M3 式的「目录区域释放参考表」，那是另一个候选，本轮未做）。

## 5. 入口读数（P2 的机制面，来自同轮的门）

| 语料 | 主块访问 参考→目录 | 完整比较 | 溢出记录 | 组负载 max/avg（参考→目录） | 空组 |
| --- | ---: | ---: | ---: | --- | ---: |
| mixed | 1,467,679 → **131,213** | 170,119 → 132,762 | 2,547 → **0** | 1366/6.17 → **12/6.17** | 14278 → 0 |
| skew | 2,173,636 → **131,072** | 205,280 → 132,648 | 5,033 → **0** | 824/6.50 → **12/6.50** | 19178 → 0 |
| tranco | 327,879 → **131,072** | 132,475 → 131,343 | 0 → 0 | 546/11.98 → **12/11.98** | 5993 → 0 |

目录的主块访问恰好 ≈ 每次查询 1 块（131,072 = 查询数），溢出归零——这是 skew/tranco 上
更新路径也变快（0.43–0.71）的机制面解释。但它**不是**加速比：A/B 才是。

## 6. 机制假设（本轮未验，留给下一轮）

目录在三个语料上有一份**近似常数**的查找成本（7.5–8.3 步，比较 21–127 字节/查询），而
Context16 的预测对键敏感、且其 16,512 B/区域的表访问局部性好；本轮的目录是**未截断**的后缀
arena（mixed 33 KB/区域、skew 28 KB/区域、tranco 2 KB/区域），二分查找在其中的随机访问
是缓存不友好的。这与「tranco（arena 最小）的方向最接近平手、混合与 skew（arena 大）分化最大」
的模式一致，但**没有直接测量**，所以只作为假设登记。

下一轮若继续这个候选，预先登记的触发条件是：**只做「截断分隔符」（入口诊断已测其常驻为
3.4–5.0 KB/区域、tranco 0.6 KB/区域）**，并且必须先解决截断下的正确比较语义——那是一个
设计问题（范围不变量或前向编码 + 解压），不是参数搜索；在它通过与本轮同一套正确性门之前，
不接任何计时。

## 7. 复现

```bash
cd 02_Code_and_Experiments/validation_e25_20260918
python3 tools_build.py check_depth_dir && python3 tools_build.py san_check_depth_dir
python3 tools_build.py run check_depth_dir mixed 131072 ''
python3 tools_build.py bench_depth_dir && python3 run_depth_dir_ab.py --run --stem depth_ab
python3 run_depth_dir_ab.py --analyze --stem depth_ab
```
