# T0 起点冻结记录（2026-10-28）

承接[详细长期优化方案](../docs/LONG_TERM_BM_TARGETED_OPTIMIZATION_20261028.md)。本轮只做 T0 的入库整理，
未改索引、未跑新实验。

## 已入库

| 批次 | 内容 |
|---|---|
| `5a9bd35` | 方案 `LONG_TERM_BM_TARGETED_OPTIMIZATION_20261028.md` 与快照 `BM_OPTIMIZATION_STATE_20261028.json` |
| `7404688` | **259 个文本产物**：`07_Eval_Integration/*.md`（审计与结果文档）、`diagnostics/*.{md,py,cpp,hpp,c}`（源码与规格）、`tools/*.{sh,py}`（运行脚本）、`docs/`（21,113 行新增） |

至此，**源码、规格、文档与运行脚本**全部在版本库内，可复现。

## 仍未入库：`07_Eval_Integration/results/` 下的原始证据（199.8 MB / 864 项）

体积集中在少数几个目录：

| 体积 | 路径 |
|---:|---|
| 28.9 MB | `results/bm_page_read_20261018/commands.log`（单个日志文件） |
| 28.3 MB | `results/c2_shortkey_entry_20261018_attempt2_aa_key/` |
| 28.2 MB | `results/c2_shortkey_entry_20261018c/` |
| 28.0 MB | `results/c2_shortkey_entry_20261018_attempt1_arm_count/` |
| 17.6 MB | `results/bm_audited_100k_v3_repro_20261025/` |
| 17.5 MB | `results/bm_audited_100k_v3_20261025/` |

**未擅自提交的原因**：这些是运行日志与轨迹，提交会把仓库永久增大 200 MB；
且其中包含三份**已否决尝试**的完整日志（`attempt1_arm_count`、`attempt2_aa_key`），
按项目"保留失败尝试"的惯例应当保留，但是否进版本库属于仓库策略，不该由我单方面决定。

**建议的三个选项（择一后即可完成 T0）**：

1. **全量提交**：`git add 07_Eval_Integration/results`，仓库 +200 MB 历史；
2. **只提交小的元数据**：`results/**/{identity,decision,summary,verification}.json` 与 `*.md`，
   日志与轨迹留在本地（配 `.gitignore` 规则）——**推荐**，既保证可追溯又不膨胀；
3. **都不提交**：只用 `results/` 作为本地证据，对外只保留文档与源码。

## 另外 7 个已跟踪但被修改的文件

属于并行会话对既有 `RESULTS_*.md` 与 `.gitignore` 的更新，内容未核对，**未提交**。
若确认是最终版，应与 T0 一并入库。

## 状态

| 项 | 状态 |
|---|---|
| 方案与快照入库 | ✅ |
| 源码/规格/脚本入库 | ✅（259 文件） |
| 原始证据入库 | ⬜ 待选项 1/2/3 |
| T1 成本归因 | ⬜ 未开始 |
| Context16 默认 | ✅ 不变 |

## 下一步（T1）

对五类对象做统一成本账本：**五个查询回退格、三个写收益不足格、两个领先 Context16 的对照格**，
把"重复查重/重复定位、块数组与 fence 数组整页复制、arena 不回收、输出缓冲"各自的占比测出来，
再据此选**一个查询机制 + 一个写机制**进入实现（每个机制两次机会）。
