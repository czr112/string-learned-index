# 交付物行引用重绑报告（2026-09-23）

对象：`MODEL_AND_MAPPING.md`（2026-09-20）与 `LITS_AUDIT.md`（2026-09-21）。
两文写作时的 `index.hpp` 约 4110 行；当前为 4530 行，末改 2026-09-22 12:51:49 UTC。

每条旧行号映射到**同一符号在当前位置**的行号，引用含义不变。

## MODEL_AND_MAPPING.md

- `index.hpp:NNN` 未映射：无
- 变更行数：165

## LITS_AUDIT.md

- `index.hpp:NNN` 未映射：无
- 裸行号未映射：[508, 512, 848, 1432, 1457, 1796, 1830, 1984, 2307, 2332, 2475, 2662, 3070, 3840, 3987, 4012, 4062]
- 变更行数：442

## 映射表

| 文件 | 旧 | 新 | 符号 |
|---|---:|---:|---|
| MODEL_AND_MAPPING.md | 468 | 588 | enum class ModelKind |
| MODEL_AND_MAPPING.md | 476 | 605 | Config::model_kind 默认值 |
| MODEL_AND_MAPPING.md | 583 | 770 | class Model |
| MODEL_AND_MAPPING.md | 959 | 1450 | Region::canonical_group |
| MODEL_AND_MAPPING.md | 1007 | 1450 | canonical_group（组数上界） |
| MODEL_AND_MAPPING.md | 1017 | 1460 | canonical_group（组数上界） |
| MODEL_AND_MAPPING.md | 1125 | 1656 | "a group's sub-blocks" 注释 |
| MODEL_AND_MAPPING.md | 1242 | 1560 | Region::compute_prefix |
| MODEL_AND_MAPPING.md | 1248 | 2036 | 构建路径 model_.train |
| MODEL_AND_MAPPING.md | 2677 | 3811 | Region::scan |
| MODEL_AND_MAPPING.md | 2810 | 4173 | Index::locate |
| LITS_AUDIT.md | 747 | 770 | class Model |
| LITS_AUDIT.md | 776 | 794 | Model::train |
| LITS_AUDIT.md | 848 | 880 | Model::predict 内 fp.byte(ch) |
| LITS_AUDIT.md | 1347 | 1425 | Region::prefix_matches |
| LITS_AUDIT.md | 1356 | 1456 | canonical_group 内 predict<false> |
| LITS_AUDIT.md | 1836 | 2036 | Region 构造 model_.train |
| LITS_AUDIT.md | 2481 | 2715 | demand_profile_impl 的 model.train |
| LITS_AUDIT.md | 2501 | 2766 | 需求探针 predict<false> |
| LITS_AUDIT.md | 2558 | 3255 | place_with_ctx |
| LITS_AUDIT.md | 2655 | 1493 | predict_for 的 predict<Hash> |
| LITS_AUDIT.md | 2662 | 2930 | find_impl 的 fp.tail |
| LITS_AUDIT.md | 2676 | 2944 | result.steps = pred.consumed |
| LITS_AUDIT.md §2.3 | 1901 | 2191 | canonical_group 调用者 |
| LITS_AUDIT.md §2.3 | 1982 | 2110 | canonical_group 调用者 |
| LITS_AUDIT.md §2.3 | 2005 | 2214 | canonical_group 调用者 |
| LITS_AUDIT.md §2.3 | 2042 | 2269 | canonical_group 调用者 |
| LITS_AUDIT.md §2.3 | 3069 | 3337 | canonical_group 调用者 |
| LITS_AUDIT.md §2.3 | 3154 | 3432 | canonical_group 调用者 |
| LITS_AUDIT.md §2.3 | 3186 | 3464 | canonical_group 调用者 |
| LITS_AUDIT.md §2.3 | 3377 | 3655 | canonical_group 调用者 |
| LITS_AUDIT.md §2.3 | 3539 | 3817 | canonical_group 调用者 |

## 内容更正（不只是行号）

`LITS_AUDIT.md` 的「四张表」一节写的状态也已过时，已另作更正：E38 之后
`model_backend` 已进入全部四张表，白名单为 14 项，`switches()` 也打印它。
该节原文说「白名单没有 model_kind」「pin_frozen_switches 不含 model_kind」——
`model_kind` 与 `model_backend` 是两个不同的字段，前者今天仍是被要求相等的非开关字段。
