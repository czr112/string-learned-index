# 新模型接入对照：BlockLearn × 我方索引（matrix4 / matrix4s）

日期：2026-10-11。来源：队友仓库 `https://github.com/Zephyr761/Learned-Index-Model`
（main @ `60837a886b7040828e6f0bb5382d7950d8eed4fa`，克隆于 `02_Code_and_Experiments/Learned-Index-Model`）。
入口：评测仓库 `403de52`（未改动）的 `learned-index/scripts/benchmark.py`；语料：`datasets/{mixed,skew,tranco}.keys`。
产物：`results/matrix4_{mixed,skew,tranco}/`、`results/matrix4s_mixed/`、`results/matrix4_rollup.json`、
`results/blocklearn_identity_20261011.json`；脚本：`run_matrix4.sh`、`run_matrix4s.sh`、`analyze_matrix4.py`；
失败冒烟记录（第一次调用被沙箱改写路径，dataset 读取失败）保留在 `results/matrix4_smoke_invalid_input/`。
身份记录：他们的 manifest 自称 `blocklearn-prototype-v1-source-uncommitted`，本轮按 **commit + 逐文件哈希**自行钉住
（见 identity JSON）；libart/HOT 用的都是本项目已钉值的源码副本（301046804a… / 96bf6fb7…）。

## 0. 判定

**接入成功，对照完成**：他们的四个 BlockLearn 消融臂在我们统一入口上全部 `passed`，其自有 ASan 测试套件 4/4；
在加强协议的 mixed 轮（15 重复，同轮八臂）里，他们的最优臂 `block-residual-delta` 的 1 万次点查中位耗时
是我们的 `ours-context16` 的 1.36×、ART 的 1.80×；我们的 `ours-lits` 介于两者之间。**本轮证据不支持
"他们的模型已超过我方索引/ART/HOT"**——这和他们 README 的自述一致（"尚未证明在真实数据上超过 ART/HOT"）。

## 1. 接入动作与身份

1. 克隆其仓库并钉住 commit；按他们的 CMake 约定把 `related_works/libart`、`related_works/hot` 用**本项目已钉值**的
   libart（301046804a…）与 HOT（96bf6fb7…）源码副本补齐（原 gitlink 无 `.gitmodules`，目录为空）。
2. 正确的接入验收：Debug + ASan 构建并 `ctest` → `blocklearn_semantics`、`baseline_contract`、
   `model_certificates`、`experiment_contract` **4/4 通过**。
3. Release 构建（`-DBLOCKLEARN_REQUIRE_HOT=ON`）成功，HOT 原生可用；构建期唯一告警来自上游 HOT 头文件
   （`MultiMaskPartialKeyMapping.hpp` 的 `-Wstringop-overflow` 提示，GCC 13 下的上游现象，非本轮代码引入）。
4. 八臂（每语料一次调用，保证同轮同进程块）：`ours-context16`、`ours-lits`、`art-libart`、`hot`
   ＋他们的 `block-plain`、`block-hash`、`block-residual`、`block-residual-delta`。

## 2. matrix4：入口默认协议（3 重复 × 10k 均匀点查），三语料 × 八臂

中位点查耗时（ms，1 万次查询；×ART 越小越好）：

| 语料 | ours-c16 | ours-lits | art | hot | block-plain | block-hash | block-residual | block-residual-delta |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| mixed | 10.28* | 5.77 | 3.67 | 5.02 | 9.33 | 6.39 | 6.69 | 6.61 |
| skew | 6.77 | 6.92 | 10.45 | 5.99 | 10.10 | 7.34 | 7.40 | 6.57 |
| tranco | 6.03 | 4.10 | 2.97 | 7.36 | 6.90 | 4.67 | 5.69 | 6.28 |

（*mixed 的 ours-c16 单轮离群：`matrix4s` 同臂 15 重复复测为 5.12 ms，见 §3；这是入口默认协议
"3 重复不足以排名"的直接例证。）

构建中位（ms）与内存（矩阵轮）：我方 49–76 ms / index 36.8–40.6 MB（自持，retained=0）；
ART 23–55 ms；HOT 37–108 ms；他们 **580–1053 ms** / index 自有 0.89–2.09 MB ＋ retained input 12.4–25.8 MB。

## 3. matrix4s：mixed 确认轮（15 重复，同轮八臂）

| 臂 | 中位点查 ms | ×ART | 构建 ms | p50 ns | p99 ns | index B | retained B |
|---|---:|---:|---:|---:|---:|---:|---:|
| art-libart | 3.88 | 1.00 | 23.2 | 826 | 2214 | — | — |
| **ours-context16** | **5.12** | **1.32** | 42.4 | 907 | 3323 | 39,321,200 | 0 |
| ours-lits | 5.67 | 1.46 | 101.0 | 864 | 3029 | 36,759,089 | 0 |
| hot | 6.19 | 1.60 | 37.7 | 823 | 1772 | — | — |
| block-residual | 7.36 | 1.90 | 601.4 | 1066 | 2621 | 1,149,688 | 20,884,099 |
| block-hash | 7.59 | 1.96 | 650.5 | 1263 | 3075 | 1,149,688 | 20,884,099 |
| **block-residual-delta** | **6.97** | **1.80** | 623.0 | 1274 | 3321 | 1,149,688 | 20,884,099 |
| block-plain | 9.67 | 2.49 | 631.0 | 1599 | 5238 | 887,544 | 20,884,099 |

## 4. 观测（对这轮证据负责的读法）

1. **消融梯子**：E1→E2（普通哈希标签）在三个语料上都是大胜（mixed 9.33→6.39、skew 10.10→7.34、tranco 6.90→4.67）。
   **E3（残差感知标签）没有稳定超过 E2**：mixed 6.69 vs 6.39（差）、skew 7.40 vs 7.34（平）、tranco 5.69 vs 4.67（明显更差）。
   这正是他们方案的核心研究点，本轮语料/协议下**未获支持**，应作为给队友的首要反馈。
2. **E4（+局部 delta/合并/分裂）**在 skew（6.57）与 mixed 的加强轮（matrix4s 6.97）里是他们四臂最优；
   但 mixed 默认轮里 E2 比 E4 略快（6.39 vs 6.61），tranco 里 E2 明显更快（4.67 vs 6.28）。
   静态路径 E3/E4 本应相同，tranco 的差异提示协议噪声与实现细节都要再核。
3. **相对我们的位置**：mixed（15 重复）排序 ART < ours-c16 < ours-lits < HOT < E4 < E3 < E2 < E1；
   skew（3 重复）E4/HOT 与我方两臂同级、ART 最慢；tranco（3 重复）ART < ours-lits < E2 < E3 < ours-c16 < E4 < E1 < HOT。
4. **构建**：他们 0.58–1.05 s，是我们（49–76 ms）的 8–14×；**内存**：按"索引 + 保留输入"口径，
   他们为 mixed 22.0 MB / skew 27.1 MB / tranco 14.5 MB（index 自有 0.89–2.09 MB ＋ borrowed input 12.4–25.8 MB），
   我们 36.8–40.6 MB，即他们的合计约为我们的 38%–67%。
5. 尾延迟上他们有竞争力：skew 的 E3/E4 p50 890/965 ns 优于我方 979/1004 ns；tranco 的 E3/E4 p99 2274/2280 ns 与我方 2419 ns 同级。

## 5. 限制（不得越读）

- `matrix4` 用入口默认协议（3 重复），项目早已判定它**不能用于排名**；`matrix4s` 只覆盖 mixed。
  skew/tranco 的名次只能当方向性参考；绝对数字不可跨轮比较（机器状态）。
- 只评了 `bulk_load + find`（静态点查）。E1–E3 明确不支持 insert；混合读写需要另立同轮实验（他们自带 mixed_runner，
  但与本入口不同装置，不能把两边的数字拼成结论）。
- 内存是分项逻辑估计（他们含 borrowed input；我们自持拷贝），本报告不做单一"总内存"排名。

## 6. 下一步建议

1. 把 §4.1/§4.2 作为**设计反馈**给队友：E3 未胜 E2（tranco 尤甚）、E4 的 delta 价值要混合负载证明、构建成本高。
2. 若目标是**用他们的模型替换 LITS 占位**（即进入我们 `Index` 的 region-model 位置），下一步应做
   "模型级接入"：把 `block_model.*`（分段线性块预测 + 候选窗口）与 `residual_tags.*` 接到我们的 `RegionModel`
   接口上做新 `ModelBackend`，再走一次"正确性门 → 同轮 A/B（Context16 / LitsHpt / BlockLinear）"。这是独立一轮工作。
3. 若要更硬的对外表：对 skew/tranco 也跑 15 重复（matrix4s 扩展），并把混合读写轮单独立项。