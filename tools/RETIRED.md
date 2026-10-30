# 已退役脚本登记（2026-10-25）

本文件登记因复现纪律缺陷而退役的历史脚本。退役脚本保留原文与提交历史（证据绑定其当时源码），
不得再用于生成新结果。缺口由新入口收口（见 `07_Eval_Integration/RESULTS_BM_ACCEPTANCE_GAPS_20261025.md`）。

## 一、运行时改写被跟踪源码（证据纪律违规，残留已清除）

- `tools/p1_diff.sh`（2026-10-23）：原第 6 行用 `sed -i` 就地改写
  `07_Eval_Integration/diagnostics/gate_bm_diff.cpp` 的 include。该源文件后来已按改后形态提交，
  故此行成为无操作残留；2026-10-25 已删除该行。历史测量仍绑定提交时源码，不受影响。

## 二、依赖未入库助手，干净检出无法重跑

- `tools/st2_apply.sh`（2026-10-23）：第一步执行 `tools/_st2d.py` 并随即删除该文件；
  该助手从未提交，故脚本无法从干净检出复现。其开创的故障注入覆盖已由
  `diagnostics/gate_bm_followup.cpp`（经 `tools/run_bm_followup.py`）取代。
  现已加退役头并 `exit 9`。

## 三、复用旧构建目录，重跑会覆盖工作副本

- `tools/p5_run.sh`（2026-10-23）：写入 `builds/p4_20261024/` 并截断该目录下的
  `cells/*.jsonl`，重跑会再次覆盖 P4/P5 的工作副本。**已入库证据不受影响**：
  `results/bm_p4_perf_20261024/`（提交 6b24f7a）与 `results/bm_p5_final_20261024/`
  均完好。后续运行一律使用全新输出目录。现已加退役头并 `exit 9`。

## 四、已废弃的负载生成驱动

- `diagnostics/run_bm_compliant.py`（2026-10-23）：三处登记缺陷——合成替换键
  （`~r/`/`~s/` 后缀而非保留真实键）、hotspot 格 find 80% 偏向热区导致两种
  locality 命中率不可比、二进制身份用占位串 `"acceptance"` 而非真实源码闭包
  SHA-256。被 `prepare_bm_audited.py` / `prepare_bm_audited_v2.py` 取代；文件头已
  加 DEPRECATED 说明，仅作历史记录。

## 五、生成器就地改写的清理

- `tools/s4_100k_v3.sh`（2026-10-24）：原内嵌 Python 就地改写
  `diagnostics/prepare_bm_audited_v2.py` 的 DATASETS 与输出目录，导致 100k v3 轨迹
  依赖未提交状态。2026-10-25 起 `prepare_bm_audited_v2.py` 支持
  `--dataset NAME=PATH --out DIR` 参数（默认行为不变）。
- `tools/s4_100k_v3.sh` 同日进一步**整体退役**：即使参数化后，重跑它仍会
  `rm -rf` 已提交的 `results/bm_audited_100k_v3_20261025/`、截断
  `builds/s2_20261025/` 内旧计时记录、并复用未经身份核对的旧 `pure` 二进制。
  轨迹复现性已由只读脚本 `tools/verify_bm_100k_repro.py` 独立验证
  （8/8 格 rows/operations SHA-256 与已提交 manifest 一致，
  `results/bm_audited_100k_v3_repro_20261025/verification.json`）；后续 100k 计时
  如需重跑，按 README §4 的 runner 纪律另立新输出目录。

