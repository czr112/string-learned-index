# 优先级 1 完成：候选 factory 真实身份 + 有界构建 + 正式验收（2026-10-23）

按[更正与修订计划](CORRECTION_BM_ACCEPTANCE_20261023.md)的第 1 项执行。

## 修的三件事

| 问题（复核指出） | 修正 |
|---|---|
| `bulk_load` 先复制全部输入行，声明 `bounded-per-page` 不成立 | 新增 `bm_split_index.hpp`（有界索引 + 再平衡页），`bulk_load` 直接把 `sib::Row` 交给按页构造的构造函数，**不再materialise全量副本** |
| 构建宏传字面量 `"acceptance"` | 改为**真实哈希**：`core` = `bm_n1_page_core.hpp` 的 SHA、`bundle` = 其再哈希、`adapter` = `bm_split_adapter.hpp` 的 SHA、`model` = 上游 commit |
| 候选未走公共 contract | 用 `contract_acceptance.cpp` + `-DACCEPTANCE_FACTORY_SYMBOL=make_bm_split_experiment_index` 重跑 |

## 结果

**身份**（本轮实测）：

```
model_commit = 3894c767338a865d2920ba5e21283827fea1d803
core         = bb45fa46cd2cd872bbaac854fd0318d8f09162ba5894c96f808b88bcc992d982
bundle       = c19086b3513e329bae228c9c5f9d147c0cc64175a00048bb09ddd48b4f1721d4
adapter      = db529b4ba047130bbf9f408ba93d61a54498abd5ed5255eb376cdb32cddb87f9
```

**正式验收**：`99/99 checks passed`（通过候选 factory 的公共 contract；基线 factory 为 100/100，
差异来自候选 factory 不接受参数，验收程序少一项参数检查）。

**语料 + 零分配门**（经同一 factory）：

| 语料 | 检查项 | 失配 | insert/update/erase | find 分配 | 终态 |
|---|---:|---:|---|---:|---:|
| english_words | 263,822 | 0 | 600 / 200 / 200 | **0** | 65,936 |
| memetracker_urls | 263,822 | 0 | 600 / 200 / 200 | **0** | 65,936 |
| memetracker_phrases | 263,822 | 0 | 600 / 200 / 200 | **0** | 65,936 |

覆盖：全行、全查询、缺席探针、边界字节（空/单 NUL/0xFF/内嵌 NUL/4KB）、
全量 scan、scan 跨索引销毁、增删改、`find` 零分配。

## 仍未闭合（第 1 项的剩余部分）

- **差分门**：候选 factory 与 `bm_page`/`bm_bounded_page` 的页边界、fences、模型输入与参数、
  查询答案、维护计数**逐字段**对比——本轮只做了答案级对比；
- **异常路径**：失败注入需改在 `bm_split_page`（有界版）上重跑，上一轮跑的是非有界版；
- **正式 manifest**：把上述身份与门禁结果写成候选的正式 list。

## 下一项（优先级 2）

修正轨迹生成器并加**执行前审计**：增长无重复；**真实语料键**替换的成功操作 2:1:1；
活跃数守恒；冷/热两组命中率与成功操作量匹配；生成前检查热区可用新键数。

## 证据

- 候选 factory：[bm_split_adapter.hpp](diagnostics/bm_split_adapter.hpp)
- 有界候选索引：[bm_split_index.hpp](diagnostics/bm_split_index.hpp)
- 验收驱动：[gate_bm_split_factory.cpp](diagnostics/gate_bm_split_factory.cpp)、[tools/st_p1.sh](../tools/st_p1.sh)
- 产物：`builds/p1_factory_20261023/{acceptance,gate}`（SHA 见上）
