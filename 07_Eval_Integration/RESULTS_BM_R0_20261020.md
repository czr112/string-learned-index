# R0：统一 BM 正式实验入口（有界构建 + base 定位）

日期：2026-10-20；执行入口[新详细长期计划](../docs/LONG_TERM_BM_PLAN_20261020.md)第 3 节。
**结论：新 factory `make_bm_experiment_index` 已建立并通过公共 contract 验收（100/100）、
三类真实语料的全行/缺席/owning scan/边界/增删改门（各 263,121 项、零失配）以及 words 格的
ASan/UBSan 复验。旧 `make_bm_page_index` 的历史身份未被覆盖。**

## 入口定义

`bm_experiment_adapter.hpp`：

| 项 | 值 |
|---|---|
| factory | `make_bm_experiment_index(params)`，参数 `lookup=base|window|directory`，默认 `base` |
| 构建 | 直接接收 contract rows，按页（≤2048 行）转换，无全量输入副本 |
| 定位 | 默认 `find_base`；`window`/`directory` 仅作对照 |
| 结构 | 原 BM 模型（`3894c767…`）、2048 行/页、15/16 槽、FlatRoot |
| Descriptor | 同时记录 `model_commit`、`build_mode=bounded-per-page`、`lookup_mode`、`root_mode=flat` 与能力 |
| 版本字段 | `implementation_version` = 核心 bundle SHA；`adapter_version` = adapter SHA；**无占位符** |

身份（本轮实测）：`model_commit=3894c767338a865d2920ba5e21283827fea1d803`、
`core=2cb28beb…82d0`、`adapter=0428cacd…afff`、`bundle=2b1d18c4…2f7`。

## 通过的验收

| 验收 | 结果 |
|---|---|
| 公共 contract（`contract_acceptance.cpp`，`lookup` 参数） | **100/100** |
| 语料门：words / urls / phrases，全行 + 全查询 + 缺席 + 边界字节 + owning scan（索引销毁后仍有效）+ 200 insert / 100 update / 100 erase | 各 **263,121 项、失配 0** |
| sanitizer：words 格 ASan/UBSan/LSan | 同上计数、零失配、rc=0 |
| 能力声明 | `bulk_load/find/insert/update/erase/scan` 均为 native；`range_sum` 明确 unsupported |

自计常驻：words 1,960,578 B、urls 6,121,110 B、phrases 7,059,852 B（分别对应 0.43/0.67/0.73 ART 的历史口径）。

## 与 R0 全部要求的差距（必须说明）

计划第 3 节还要求两项，本轮**未完成**：

1. **差分门**：新 factory 与 `bm_page::Index`/`bm_bounded_page::Index` 的页边界、fences、模型输入/参数、
   维护计数逐项对比——本轮只在语料门里验证了答案，没有逐字段比对结构。
2. **分配账本**：`find` 零分配声明需要分配追踪器验证；本轮未接 `allocation_trace`，
   因此"查询零分配"仍只是历史轮的结论，不是本入口的结论。

另外，"包装是否引入 >5% 完整 find 回归"也未测——需要先用同一核心的三种 lookup 做配对，
而本轮没有计时。这两项（差分、分配）应在 R1 计时之前补齐，否则 R1 的差值可能混入包装成本。

## 交付与下一步

- 交付：`bm_experiment_adapter.hpp`（factory + manifest 字段）、`check_bm_experiment.cpp`（语料门）、
  `r0_gate.sh`（全新目录重建 + 全部门 + 退出码即失败）、本轮哈希记录。
- 下一步（R1）：4 格公平增长对照（words/URL × 95/5、80/20），BM 新 factory、ART、Context16
  用**完全相同的初始 map 与操作流**；先语义、再纯索引计时；同时补齐 R0 的差分与分配账本。

## 边界

- 单进程；语料门不覆盖并发与动态故障注入。
- 全新目录重建的是本入口实际链接的依赖（模型六份源码 + benchmark runner + contract 头），
  上游库以源码参与编译而非复用旧 `.a`。
- 未做计时，故不声称性能；旧包装成绩不得与本入口混用。
