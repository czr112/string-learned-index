# Eval Integration（本轮）：统一接口接入与读性能基线

## 当前阅读入口（2026-10-11）

- **最新一轮（10月18日）**：[E2 全库无放回泛化与空间账本](RESULTS_E2_GENERALIZATION_20261018.md)、
  [E1 正式只读接口外部复测](RESULTS_REFINED_EXTERNAL_20261018.md)、
  [当前状态与采用决策](STRUCTURE_OPTIMIZATION_STATE.md)。
- [下一阶段详细计划](../docs/NEXT_ACTION_PLAN_20261011_POST_TRUNCDIR.md)：目录字节序修复、有限读路径诊断及 M4；
  前置发现见 [最小反例核查](AUDIT_DEPTH_DIR_ORDER_20261011.md)。
- [读路径入口诊断（P1）](RESULTS_DEPTH_DIR_READ_PATH_ENTRY_20261011.md)：目录额外工作来源；目录读路径收口、转 M4。
- [M4 归并入口](RESULTS_MERGE_ENTRY_20261011.md)：有序段归并的部署性工作减少未通过；机制收口、转 M5。
- [简版性能对比与已做优化](BASELINE_COMPARISON_SIMPLE.md)：接入新模型前的外部 ART/HOT 对照。
- [结构优化状态](STRUCTURE_OPTIMIZATION_STATE.md)：当前已完成项、关闭项与默认配置。
- [完整外部模型对照](RESULTS_BLOCKLEARN_MATRIX4_20261011.md) 与
  [模型级接入结果](RESULTS_BLOCK_RANK_ENTRY_20261011.md)。
- [等深目录结果](RESULTS_DEPTH_DIR_20261011.md)：场景收益与主目标未通过的结论。
- [截断分隔符等深目录](RESULTS_DEPTH_DIR_TRUNC_20261011.md)：截断机制成立（mixed/skew 更快、
  arena −57%…−89%），但对 C16 的主目标仍未通过的结论。

本目录报告与逐轮记录已纳入主仓库；评测器及 HOT 以固定版本 submodule 引用。
首次克隆请先按根目录 README 获取依赖。以下身份和阶段表保留的是 10 月 8 日接入轮历史记录，
当前源码身份以适配 manifest 和相应最新结果文档为准。

日期：2026-10-08。上游实现：`02_Code_and_Experiments/validation_e25_20260918`。
评测仓库：`zsh20070718/learned-index` @ **`403de526a9104147d5d2659b6882c126c1323b5f`**，
克隆于 `learned-index/`，**未改动其任何一行**。

---

## 目录

```
07_Eval_Integration/
  PRIOR_WORK_AUDIT.md              阶段 0 交付：旧两轮任务书的核查与三个源码身份
  README.md                        本文件
  learned-index/                   评测仓库 @403de52（不改）
  adapters/
    common/contract_acceptance.cpp 三方共用的接口验收程序
    our_index/                     Our-Context16 包 ＋ 共享实现 ＋ CMake ＋ ADAPTER_NOTES
    our_index_lits/                Our-LITS 包（复用上面的实现，只差一个编译定义）
    art/                           libart 包
    hot/                           HOT 包
    BASELINE_ADAPTER_NOTES.md      ART／HOT 的适配说明与 ISA 决策
  datasets/                        mixed／skew／tranco 键文件（raw-lines）
  results/                         评测输出（report.md／report.json／summary.csv／samples.csv）
  build/                           本轮自建的 cmake 构建目录（不在评测链路里）
```

---

## 三个源码身份

| 身份 | 值 |
|---|---|
| 本索引 | `index.hpp` 5,423 行，SHA-256 `4fb0a25fba8f16a339d87f67ead6e3501ce1bb6c9d4fccb8a0dddfe9180c40f7` |
| 默认配置 | `baseline_config()` ＋ `pin_frozen_switches()`；ON 六个（含 `compact_plan_commit`）、候选 OFF 八个；后端 `Context16` |
| 评测仓库 | `403de52`，契约 `docs/INDEX_CONTRACT.md` ＋ `src/index_contract.h` |
| ART | `armon/libart` @ `301046804a…`，逐文件 SHA-256 |
| HOT | `speedskater/hot` @ `96bf6fb7…`，ISC 许可 |

**每个适配包的 `benchmark-adapter.json` 都带 `source_identity`**——评测方**不递归哈希外部源码树**，
所以那是外部实现身份唯一的落点。

---

## 怎么跑

```bash
cd 07_Eval_Integration/learned-index

# 四方静态点查
python3 -B scripts/benchmark.py \
  --dataset ../datasets/mixed.keys \
  --implementation ../adapters/our_index \
  --implementation ../adapters/our_index_lits \
  --implementation ../adapters/art \
  --implementation ../adapters/hot \
  --output ../results/<新目录名>      # 输出目录必须尚不存在

# 接口验收（评测入口只验 bulk_load+find，可选操作要单独跑）
for A in our_index our_index_lits art hot; do
  cmake -S ../adapters/$A -B ../build/acc-$A -DSTRING_INDEX_CONTRACT_INCLUDE_DIR=$PWD/src \
        -DOUR_INDEX_BUILD_ACCEPTANCE=ON -DART_BUILD_ACCEPTANCE=ON -DHOT_BUILD_ACCEPTANCE=ON
  cmake --build ../build/acc-$A -j4
  $(find ../build/acc-$A -name '*_adapter_acceptance' -type f | head -1)
done
```

---

## 环境前提（本轮补齐，**必须记下**）

1. **WSL 里原本没有 `cmake`**，评测入口因此报 `cmake executable is unavailable`。
   已装**用户级** cmake 3.30.5 于 `~/.local/opt`，软链到 `~/.local/bin`（该目录已在 `~/.profile` 的 PATH 里）。
   **不需要 root，不动系统包。**
2. **必须用 WSL 的 g++**：本索引在 Windows MinGW 下编不过（`lits_model.hpp` 的 `1UL << 52`）。
   所以评测必须整体在 WSL 里跑，**不能借用 Windows 侧的 cmake**（那会带出 Windows 编译器）。
3. **ISA 统一在 `-march=x86-64-v3`**，因为 HOT 需要 AVX2+BMI2、v2 编不出来。
   本机已核实有 avx2／bmi1／bmi2。

---

## 各阶段状态

| 阶段 | 状态 | 交付 |
|---|---|---|
| 0 核查历史工作 | **完成** | `PRIOR_WORK_AUDIT.md` |
| 1 本索引接入 v1 | **完成** | `adapters/our_index*` ＋ `ADAPTER_NOTES.md`；验收 100/100 与 101/101 |
| 2 接入 ART／HOT | **完成** | `adapters/art`、`adapters/hot`；同一验收 99/99 与 99/99 |
| 3 读性能基线 | **进行中** | `READ_BASELINE.md` |

**尚未做**：`BENCHMARK_PROTOCOL.md` 的正式计时协议（多进程、绑核、轮转平衡、逐操作延迟分开）——
当前矩阵用的是评测入口的**默认协议**（单进程、3 重复），**只能当量级参考**，
不能作为论文级排名，这与 `docs/UNIFIED_BENCHMARK.md` 自己的告诫一致。
