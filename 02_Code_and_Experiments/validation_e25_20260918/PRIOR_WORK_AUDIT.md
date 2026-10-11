# PRIOR_WORK_AUDIT：原任务书逐项核查账本（G0）

日期：2026-09-23。目录：`validation_e25_20260918`。

依据：四份原任务书的原文，以及当前源码、构建记录、原始记录与分析脚本。

**本文不是完成度总结，是逐条核查账本。** 每一条原任务条目都给出：它要求什么、当前

代码在哪、证据绑在哪份源码上、覆盖了什么、缺什么、状态、以及关闭标准。

---

## 0. 方法是怎样的，以及一个必须随数字一起读的身份事实

### 0.1 核查怎么做的

| 步骤 | 做法 |
|---|---|
| 建清单 | 把四份原任务书按**它们自己的编号条目**拆成 253 条，逐条建行 |
| 对源码 | 每条找出实现的 file:line 与开关，**核实该开关真的被代码读取**（字段声明不算） |
| 对证据 | 每条找出门、原始记录、分析入口，并给出**构建身份** |
| 反驳 | 每条"已完成"主张交给第二个独立代理去**推翻**：打开 file:line、查构建身份、查门是否真的进入被测分支、查原始数据是否存在 |
| 定稿 | 改判结果落到状态栏，`could_not_verify` 栏保留没核实到的部分 |

**253 条里有 126 条主张"已完成"。经独立反驳，61 条站不住**（59 条降级、2 条判错）。这个比例本身就是本文最重要的读数。

### 0.2 身份事实：`index.hpp` 的最后一次改动

`index.hpp` 最后改动于 **2026-09-22 12:51:49 UTC**（`switches.hpp` 为 10:57）。据此，69 条构建记录分成两半：

| 类别 | 个数 | 含义 |
|---|---:|---|
| **LIVE** | 18 | 构建晚于该时刻，记录里的源哈希与当前磁盘**逐字节相同** |
| **DRIFT** | 51 | 构建早于该时刻，源哈希对不上 |

**LIVE 的 18 个恰好集中在两处**：LITS 适配线（`check_lits_model`／`check_lits_index`／`check_lits_fingerprint`／`bench_lits_adapter`／`diag_lits_adapter*`）与四个 M0 复核门（`check_freeze`／`check_overflow_store`／`check_tag_store`／`check_scan_contract`），加上 8 个 `san_*` 插桩目标。

**这意味着：E25–E38 的计时与多数早期门的"通过"，证明的是各自那一期的源码，不是当前源码。** 漂移本身不是缺陷——共享头文件改了就该漂移；错的是**把漂移记录当成当前源码的证据**。

用项目自己的严格入口验证这一点，结论是它正确地拒绝运行：

```text
python3 tools_build.py run check          -> RUN REFUSED (rc=2)
（八个继承门全部如此）
```

### 0.3 本次核查期间完成的补测

按原任务书 §8「共享头文件改变后，运行须与当前构建身份一致」，已把这批门**重建并复跑**，产出 `RESULTS_G1_GATES.md`。要点：

- 8 个继承门 + 6 个开关门重建复跑，**全部 rc=0**；
- 与各自的历史日志**逐字段**比对：`check_e25_write` 10 个字段三数据集全同；`check_e8_hetero`／`check_e15_plan`／`check_e16_probe`／`check_e19_reuse`／`check_e20_r2` 逐字段相同；
- 只有两处事件计数不同（`compactions` 106→79、`queries_answered_from_overflow` 13893→13894，正是 E26 C2 的预期后果）与**字节记账**不同；
- 字节记账的差可以精确分解：**每个区域 +136 字节、Index 级 +24 字节**（8,728 = 136×64+24 于 64 区域；17,432 = 136×128+24 于 128 区域）——这是 E38 接入第二模型后端的真实常驻代价。

### 0.4 状态怎么读

| 状态 | 含义 |
|---|---|
| 已完成并采用 | 实现、验证、结论齐全，且**证据绑在当前或明确标注的源码上** |
| 已完成，负结果或条件性候选 | 有符合任务要求的实测与停止理由；保留证据，不重做同一试验 |
| 条件未满足，已按任务书关闭 | 有进入条件的诊断及关闭依据 |
| 部分完成 | 有实现无测量、有诊断无原型、缺交付形态、或证据绑在已被取代的源码上 |
| 尚未完成 | 原任务要求仍成立且无闭环证据 |
| 客观阻塞 | 精确说明缺失的输入、能力或记录 |

**"没进默认"不等于"未完成"。** 本文按上面六格分别记，很多候选机制（E31 子块回收、O1、T8、E37 迁回）的状态是"已完成，负结果或条件性候选"，不是"没做"。

---

## 1. 汇总

### 1.1 改判前后的状态分布

| 状态 | 核查代理初判 | 经独立反驳后 |
|---|---:|---:|
| 部分完成 | 100 | **140** |
| 已完成并采用 | 107 | **80** |
| 尚未完成 | 20 | **15** |
| 已完成，负结果或条件性候选 | 19 | **13** |
| 条件未满足，已按任务书关闭 | 7 | **5** |
| 客观阻塞 | 0 | **0** |
| **合计** | **253** | **253** |

### 1.2 改判去向

| 从 | 到 | 条数 |
|---|---|---:|
| 已完成并采用 | 部分完成 | 26 |
| 部分完成 | 部分完成（维持，但反驳补了缺口） | 20 |
| 已完成，负结果或条件性候选 | 部分完成 | 7 |
| 尚未完成 | 部分完成（反驳认为有实现，只是缺证据） | 5 |
| 条件未满足已关闭 | 部分完成 | 2 |
| 已完成并采用 | 已完成，负结果或条件性候选 | 1 |

### 1.3 四条主导发现

**① 证据身份是本文最大的一类缺口。** 61 条改判里，绝大多数不是因为"没做"，而是因为**引用的证据绑在已被取代的源码上**：`bench_e25`／`bench_e26`／`bench_scan*`／`bench_long*`／`bench_tag`／`bench_migrate`／bench_prefix`／`diag_*` 全部 DRIFT。E38 恰好改动了**默认查询路径**（`find_impl` 改走 `predict_for`，`Model::Prediction` 新增 `fp_consumed`），所以那些点查与扫描数字确实不是当前源码跑出来的。

**② 交付物本身的行号也漂了。** `MODEL_AND_MAPPING.md`（09-20）与 `LITS_AUDIT.md`（09-21，自述 `index.hpp` 4110 行）的 file:line 全部对不上当前的 4530 行版（抽查五处全不匹配）。内容实质没错，但**引用不可复现**。

**③ 有几处"内容齐、形式缺"。** 原任务书要求的是**一张表**或**一份文件**，项目把内容分散在多轮报告里，于是形式要件没有交付：`§2.3` 的"现象—对应代码—计数—选择"四列表、`§12.4` 的同轮最终表、`INVARIANTS.md`、`CODE_AUDIT.md`、`§9` 的三行决策表、`§9.4` 的 ART 八条核对表、`§11 SA0` 的 I1–I12 清单。

**④ 命名与分类存在跨轮混用，必须逐条对齐才能引用。** 已确认的几处：任务书要的 `compact_subblock(group, block)` 实际叫 `repack_subblock(g, blk)`；`MODEL_AND_MAPPING.md:150` 把 §5 的 A0–A5 六层整体标为"已做（名为 F0–F3）"，而 F0–F3 只有四层且缺 A2、A5 两层、A4 对应的 F3 未实现；§4 的分支 A/B/C 从未按 A/B/C 落地；`M0` 在任务书指维护变体 M0/M1/M2，在项目里指"现有维护规则"；`R1` 在 E37 里又是另一件事（迁回臂名）。

---

## 2. 逐项账本

每一小节对应原任务书的一节。条目按原书编号给出，状态为**反驳改判后**的状态。



---

## A-0-2  —— 架构线 §0–§2：执行决定、优先级、模型与分组来源、审查对象（12 条）

### [A-0-2-00] §0 优先级表 P0 第 1 行：审查当前版本、模型来源、接口与正确性不变量；进入条件＝能说明真实调用链和实际配置、关键不变量成立

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：模型枚举 index.hpp:588 ModelKind、:597 ModelBackend；模型类 index.hpp:770 class Model；映射汇聚点 index.hpp:1450 canonical_group、:1609 derive_groups、:1560 compute_prefix、train 调用 index.hpp:2036。开关面 Config index.hpp:600–745，四张登记表 switches.hpp:29–144。交付模型调用面审计 LITS_AUDIT.md §2（训练点 2、预测点 3、canonical_group 调用者 8）与 MODEL_AND_MAPPING.md §1、§3（八问）

**证据**：源码身份：index.hpp 现 4530 行，末改 2026-09-22 12:51:49（results/main/g0_evidence_timeline.txt）。构建身份：check_freeze LIVE 14/14（build 09-22 12:59:46）、check_scan_contract LIVE 14/14、check_lits_model/index/fingerprint LIVE 14/14；diag_trace DRIFT（build 09-20 11:35:38）。不变量：I1 由 diag_trace 512 键/数据集 i1_result:pass（logs/diag_trace_{mixed,tranco}.log），扫描保序由 check_scan_contract 全量 0 违例＋负对照（logs/check_scan_contract_mixed.log: monotone_pairs 131008 / monotone_violations 0 / result pass；负对照 start_mismatches 35）。交付物：MODEL_AND_MAPPING.md（2026-09-20）、LITS_AUDIT.md（2026-09-21）

**覆盖与缺口**：已覆盖 M0=Context16 全链路（训练/预测/指纹/扫描依赖）与 LITS-HPT 接口审计；I1（放置组=查询组）已验，扫描保序已由门看守。尚未覆盖：两个交付物的 file:line 绑在 09-20/09-21 版 index.hpp（LITS_AUDIT 自述 4110 行），对当前 4530 行已漂移（抽查 ModelKind 文档 :468→现 :588、canonical_group :959→现 :1450、derive_groups :1007–1017→现 :1609、scan :2677→现不在 2600 段）；LITS 后端分派 index.hpp:2072（calibrate_lits）与 LITS 臂调用链不在 MODEL_AND_MAPPING 覆盖内；方案 §3 其余不变量（超出本单元行范围）未核

**关闭标准**：把 MODEL_AND_MAPPING/LITS_AUDIT 的行引用重绑到当前 index.hpp 哈希，并把 ModelBackend::LitsHpt 分派纳入调用链图；关闭标准＝交付物每条 file:line 对当前源码可复现

**未能核实**：未逐条核对 MODEL_AND_MAPPING 全部 30+ 处 file:line（抽查 5 处均漂移）；未读方案 §3 不变量表全文，故未验证 I2–I5 是否成立

### [A-0-2-01] §0 优先级表 P0 第 2 行：冻结版与 ART 的同轮基准、插入失败分层；进入条件＝数据来源一致、知道压力发生在哪些组与子块

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：ART 同轮：bench_long_art.cpp（由 bench_long.cpp 派生）五臂同进程同重复配对（臂序轮换），RESULTS_E31.md §4。失败分层：diag_transfer.cpp、diag_overflow.cpp，只读试放 Region::trial_place（index.hpp:3511 区，F0–F3 注释）；四层 F0/F1/F2/F3，F3 未实现

**证据**：源码身份：index.hpp 09-22 12:51。构建身份：bench_long_art DRIFT（build 09-20 07:44:28，changed index.hpp,switches.hpp）、diag_transfer/diag_overflow DRIFT、bench_long DRIFT（results/main/g0_build_drift.txt）。门：check_e31 DRIFT（改动前版保留为 logs/build_check_e31_prehardening.json）。原始数据：results/main/long_e31art_{same,grow}.jsonl（各 1500 条）、overflow_diag.jsonl（24 条）、transfer_diag.jsonl（6 条）；分析入口 analyze_e31art.py→e31art_analysis.txt、analyze_e31.py→e31_analysis.txt

**覆盖与缺口**：已覆盖 3 数据集 × same/grow；失败分层给出候选级“卡字节 vs 卡槽位”与 F1/F2 可行性，ART 与四 Config 臂同轮配对（维护耗时 ÷ART 2.8×–9.6×）。尚未覆盖：分层是候选级计数、两列可同时为正、不是互斥分类（RESULTS_E28.md §2 口径），“压力发生在哪些组与子块”只有候选计数没有组级分布；ART 无结构列（libart 不暴露溢出/压缩/字节核算）；且 bench_long_art 为 DRIFT，比值绑在已被取代的源码上

**关闭标准**：若需组级压力分布需补诊断；ART 结构列见 §2.2 行。关闭标准＝该基准与分层已被 P1（E31）与 P1 条件分支（E37）作为触发依据使用

**未能核实**：未找到任何“哪些组/子块”的组级压力分布记录（只有 diag_overflow 候选级计数）；未核实 ART 侧路径深度/节点类型/分配行为是否在任一结果文件留有实测

> **反驳改判**：`已完成并采用` → **`部分完成`**（downgraded）
>
> ①实现归属错：审计把 diag_transfer.cpp 说成也做「只读试放 Region::trial_place」。全目录 grep trial_place 只有 diag_overflow.cpp 命中（diag_overflow.cpp:195-199 读 trial.f1_fits / trial.f2_budget_suffices / trial.refused_by_*）；diag_transfer.cpp:1-25 自述回答的是另一个问题（替换键是否换逻辑组，E26 诊断的「第一步」）。trial_place 本体在 index.hpp:3540，F0/F1/F2 注释块在 3511-3517（且 F3 全代码只在注释里被提到，没有实现）。②证据计数错：审计写 results/main/transfer_diag.jsonl「6 条」，文件实为 24 行（3 语料 × same/grow × 4 个 frac 检查点，cat 可见 frac 0.25/0.5/1/2）。RESULTS_E28.md §6 也是「6 条」，属陈旧数。③本行自己的「进入下一步条件」（知道压力发生在哪些组与子块）被审计的 coverage 栏自认未满足——「只有候选计数没有组级分布」。按任务书 §0 的口径，该行不能记为「已完成」。④2.8×–9.6× 的维护比出自 bench_long_art，构建于 2026-09-20 07:44:28 UTC（g0_evidence_timeline.txt:60），源哈希对不上（g0_build_drift.txt:34：changed …

### [A-0-2-02] §0 优先级表 P1 主线：单子块后缀整理；进入条件＝真实失败事件中存在较多仅靠本块回收就能成功的插入

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：Config::subblock_reclaim 默认 false（index.hpp:695）；Region::first_candidate index.hpp:1663、Region::repack_subblock index.hpp:1685、放置钩子 index.hpp:3347/:3264（insert/update 失败→回收→重试）；登记在 switches.hpp 四张表（baseline_config:46、pin_frozen_switches:69、switches():87、字段表:118）

**证据**：触发证据：E28 F1 同失败插入可放回 80%–95%（same 63%–92%）——RESULTS_E28.md 表、results/main/overflow_diag.jsonl。实现与对照：RESULTS_E31.md §0/§2/§3/§4；门 check_e31（DRIFT，logs/check_e31_{kind}_{same,grow}.log，断言 subblock_reclaims>0＋边界目录逐组复核）；计时 bench_long（DRIFT，long_e31_{same,grow}.jsonl，e31_analysis.txt）、含 ART 的 bench_long_art（DRIFT，long_e31art_*.jsonl，e31art_analysis.txt）。冻结未动：check_freeze LIVE 在改动后重跑（logs/check_freeze_{kind}_postE31.log，SHIPPED==CHOSEN 且印 subblock_reclaim=0）

**覆盖与缺口**：已覆盖同轮同进程四/五臂、3 数据集 × same/grow × 4 检查点；门为逐条全等（非校验和）且断言被测分支真跑过。尚未覆盖：grow 混合字符串 100% 一致退化 1.082/1.062（两程序同号 0/5）；无 sanitizer（E31 §8.5 自认）；offset_directory=false 定长布局不可达未覆盖；C2/C3 与回收交互无计数器；继承门只重跑两个

**关闭标准**：采用前提里写上 grow 混合 4%–8% 代价；补一次 ASan/UBSan 门与 offset_directory=false 路径核验。关闭标准＝由目标负载（是否变长键长期替换）决定采用

**未能核实**：未在当前源码上重跑 check_e31（现为 DRIFT，无 LIVE 复验）；未核实 E31 在 LITS 后端下的行为（无该臂）

> **反驳改判**：`已完成，负结果或条件性候选` → **`部分完成`**（downgraded）
>
> 实现与触发统计我核过、是对的：subblock_reclaim 在 index.hpp:3262、3345 被真实读取（字段声明在 695，非仅声明）；first_candidate index.hpp:1663、repack_subblock index.hpp:1685、switches.hpp 四张表 46/69/87/118 全部对得上；审计引的 E28 F1「80%–95%（same 63%–92%）」也成立——results/main/overflow_diag2.jsonl 里每一行 ov_F1_rank_fits 与 ov_F1_fits_after_tidy 完全相等，所以「按序选中子块」与「任一候选」在该数据上同值，口径没错。但支撑该状态的证据全部绑在已被取代的源码上：分支非空性断言门 check_e31 构建于 2026-09-21 11:28:55 UTC（g0_evidence_timeline.txt:36），源哈希对不上（g0_build_drift.txt:57）→ DRIFT；计时 bench_long（09-20 07:09:37）与 bench_long_art（09-20 07:44:28）同为 DRIFT。LIVE 的 check_freeze 只证明冻结组合未变（印 subblock_reclaim=0），它不执行该分支，因此当前源码上没有任何 LIVE 证据证明该分支被进入过。按本课题规则「DRIFT 的门的通过不能作为当前源码的证据」，『验证』这一环不成立，故降级。另：审计 coverage 写「无 sanitizer（E31 …

### [A-0-2-03] §0 优先级表 P1 条件分支：有界迁回既有溢出；进入条件＝本块整理后确有合法可容纳的溢出记录

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：Config::overflow_migrate 默认 false（index.hpp:745）；Region::drain_overflow_bounded index.hpp:1725、drain_all_bounded、OverflowList::erase_positions（单趟压缩）、kMigrateBudget=64；调用点 index.hpp:2526/:3287/:3367；只读诊断 Region::diag_migration_scan（三步扫描 NOW/AFTER/GREEDY）

**证据**：条件诊断：RESULTS_E37.md §1–§5、results/main/e37_feasibility.txt、migrate_diag_{same,grow}.jsonl（GREEDY 可迁 35.5%–95.2%，NOW 全 0 因 E31 回收是惰性的）。实现：R0/R1 两臂；门 check_migrate DRIFT 三数据集 pass（logs/check_migrate_*）；计时 bench_migrate DRIFT（migr_{same,grow}.jsonl 6240 条，e37_analysis.txt）；修复实验 repair_diag_*。构建身份：bench_migrate build 09-21 10:53、check_migrate 09-21 10:42，均 DRIFT

**覆盖与缺口**：已覆盖可行性诊断、从初始态运行（溢出 −46%～−91%、点查 R1/B 0.571–0.714 六格 5/5、常驻空间 0.657–0.924）、修复退化快照（失败，定位到 repack_subblock 对 <2 活跃条目直接返回）。尚未覆盖：归因未分离（R1=迁回＋额外子块回收，回收促成 +40,336～+163,352 为主，迁移仅 2,165–32,012）；尾延迟 p99 六格 1.055–4.469 全 0/5；修复路径不成立；无 sanitizer；无 LITS 后端臂

**关闭标准**：做“只扩大回收触发面、不迁回”的臂以分离归因（RESULTS_E37.md §10 自陈）。关闭标准＝该臂能否复现同量级效果；在此之前不把溢出降幅记在迁回头上

**未能核实**：未核实“跳过回收后是否永久不恢复”（E37 无恢复计数器）；unverified：百万域名 grow 35.5% 偏低格是否有单独复验

> **反驳改判**：`已完成，负结果或条件性候选` → **`部分完成`**（downgraded）
>
> 实现核对通过：overflow_migrate 在 index.hpp:3285、3365 被真实读取（声明 745）；drain_overflow_bounded index.hpp:1725、kMigrateBudget=64 index.hpp:1714、调用点 2526/3287/3367、OverflowList::erase_positions index.hpp:1202、只读诊断 diag_migration_scan index.hpp:2400 全部存在。但有三处不成立于该状态：①审计写的证据路径 logs/check_migrate_* 不存在——logs/ 下只有 build_check_migrate.json，没有任何 check_migrate 运行日志（同类文件只出现在审计之后才生成的 logs/g1_rerun/ 下），「三数据集 pass」没有给出可查的运行记录。②「NOW 全 0」与数据不符：results/main/e37_feasibility.txt §1 里 NOW可迁 在 tranco same B 为 547/1258/2334/928、tranco grow B 为 957/1621/2458/1139、skew grow B 为 5/6/7/10、mixed grow B 为 1/2/4；RESULTS_E37.md §2 自己把百万域名列为例外（NOW 500–3,300 条）。③门与计时都 DRIFT：check_migrate 2026-09-21 10:42:01、bench_migrate 2026-09-21 …

### [A-0-2-04] §0 优先级表 P2 条件分支：相邻子块重分配、区域内容量再分配、溢出容器调整；进入条件＝分别有对应的容量错配或搬移证据（原表一行，含三支）

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：(a) 相邻子块重分配：未实现（MODEL_AND_MAPPING.md §7.2 ‘未做’，需 A2 级证据）。(b) 区域内容量再分配：部分——E8 的按组需求容量规划（cross-round validation_e8_20260913，patch_e8.py/bench_e8.cpp）；当前构建路径的 derive_groups index.hpp:1609 与 demand_profile index.hpp:2673 是冻结 R1 的规划部分；跨组 live 重分配（F3）未实现。(c) 溢出容器调整：O1 Config::overflow_stable_store 默认 false（index.hpp:738）、T8 Config::overflow_inline_tag 默认 false（index.hpp:742）；O2/O3 未做

**证据**：(b) validation_e8_20260913/E8_REVISION_REPORT.md:143/:208、RESULTS_E8.md（溢出 −21.9%/−51.9%/−62.1%，等主块字节预算缺口 0B）；E28 F2 判定组预算不是约束 97%+（RESULTS_E28.md §0.3、§5.6）→ F3 无对象。(c) 前置诊断 E32 §4（results/main/ov_cost_diag.jsonl，溢出占维护 12.2%/37.3%/37.6%，代价在 erase 搬移）→ 选 O1；O1 门 check_overflow_store LIVE 三数据集 pass（logs/check_overflow_store_*.log，record_moves_total 0），计时 bench_long_o1 DRIFT（long_o1_*.jsonl）；T8 门 check_tag_store LIVE，计时 bench_tag DRIFT；E35 结论前缀内联不成立（RESULTS_E35.md）

**覆盖与缺口**：已覆盖 (c) 的 O1/T8 原型与 LIVE 差分门、O1/T8 的读路径代价（O1 点查 +2%～+53%、扫描 skew +6.5%～+13.6%）；(b) 只覆盖“预算够用”的必要条件与历史 E8 等预算需求再分配。尚未覆盖：(a) 无任何容量错配诊断或原型；(b) 跨组容量错配本身未被任何一层覆盖（MODEL_AND_MAPPING.md §7.4 原话）；O2/O3 未做；O1 不净收益故未进默认

**关闭标准**：(a) 先做相邻子块容量错配诊断再决定是否原型；(b) 补 F3 或跨组容量诊断；(c) O2/O3 未做且 O1 读路径回归未解决。关闭标准＝每一支各有对应证据，或明确写“无对象”并给判定依据

**未能核实**：未在当前源码上复现 E8（09-13 轮，index.hpp 已大幅变更，无 LIVE 证据）；未找到 (a) 相邻子块重分配的任何门或计数；未核实 E8 的“等预算”在当前构建上仍成立

### [A-0-2-05] §0 优先级表 P2 条件分支：简化查找路径与扫描游标；进入条件＝当前版本仍存在可消除的重复访问或导航成本

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：查找路径：write_fast_locate 默认 ON（index.hpp:638，写路径改用 find 的指纹/SIMD/边界目录机制）、reuse_locate_ctx 默认 ON（index.hpp:643，insert/upsert 复用同次 locate 的组号/标签，见注释 index.hpp:640–643）。扫描：scan_cache_start 默认 false（index.hpp:720，缓存 Index::scan 的 locate(start)，注释 :714）、scan_reserve_result 默认 ON（index.hpp:673）

**证据**：查找路径：RESULTS_E25.md §0（V0/V1/V2 与 ART 同轮，V2÷ART、V1/V0）、results/main/e25_time.jsonl；门 check_e25_write DRIFT（logs/check_e25_write_*）。扫描：RESULTS_E27.md、results/main/e27_scan_time.jsonl（analyze_scan.py）；E32 §1 扫描起点缓存（bench_scan_s2 派生，results/main/e27s2_scan_time.jsonl 2700 条，S2÷S1 0.92–1.02、百万域名 0.932 5/5）；门 check_scan DRIFT。构建身份：bench_scan_s2 DRIFT（build 09-20 10:55）

**覆盖与缺口**：已覆盖写路径重复访问消除（冻结 ON，E25 证明“只改写路径就把只改值拉到接近/优于 ART”，故慢不能笼统归因模型）与扫描起点缓存（候选，默认 off）＋ scan_reserve_result（冻结 ON）。尚未覆盖：扫描游标复用仍未做（MODEL_AND_MAPPING.md §4.6 ‘游标复用仍未做’）；查找路径其余导航成本无独立测量；S2 收益小、仅百万域名方向稳定（其余 1–3% 落在噪声底 1.03–1.34）

**关闭标准**：实现扫描游标复用或明确关闭；决定 S2 是否进默认。关闭标准＝游标复用有实现与门，或写清不做的依据

**未能核实**：未找到“查找路径简化”除 E25 locate 复用外的单列结论（全目录无此措辞记录）；未核实 E25 计时在当前源码上的可复现（DRIFT）

### [A-0-2-06] §0 后续独立线：接入开源参考模型或课题模型；进入条件＝模型接口契约明确、适配正确性通过

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **已完成，负结果或条件性候选** |
| **置信度** | high |

**实现**：model_backend=ModelBackend::LitsHpt（index.hpp:597），分派与区域校准 index.hpp:2072（calibrate_lits）；Region::demand_profile_ctx(…lits_shared) 承载共享模型；适配器 model/lits_hpt_adapter.hpp；固定源码留档与最小补丁 vendor/lits_model_original/、vendor/lits_model_safe/（3 文件）、patches/lits_model_safety.patch。回退策略：被数值准入界拒的区域改用 Context16

**证据**：契约与审计：LITS_AUDIT.md（P0 接口/数据域审计，§2 调用面、§3 键坐标、§11.3 full_key+prefix_length 约定）、RESULTS_E38.md（P1）、RESULTS_E38_P3.md（P2/P3）。门（全 LIVE，build 09-22 12:52–13:03）：check_lits_model（results/main/e38_lits_model_all.jsonl: PASS，518144 live+518144 absent、monotone_violations 0）、check_lits_fingerprint（PASS，524288 键）、check_lits_index（PASS，mixed ops_compared 350798 / tranco 612942，results/main/e38_lits_index_*.jsonl）；san_check_lits_* LIVE 全过。计时 bench_lits_adapter LIVE（results/main/lits_adapter_p3b_{same,grow}.jsonl、e38_p3_analysis.txt、analyze_lits.py）

**覆盖与缺口**：已覆盖 M0 与 LITS-HPT 两后端、3 语料、构建/静态点查/常驻空间/纯命中·纯未命中队列；适配器与源码位置算术逐条一致、系数逐比特同。尚未覆盖：方案 §8.1 的 MappingContract/PredictorAdapter 正式契约仍未起草（MODEL_AND_MAPPING.md §4.4）；许可未解决（fetch_lits_pinned.py 只记“未解决”）；字节≥128 与嵌入 NUL 一律拒绝（域限定）；mixed 3 个被拒区域未做校准后分布测量；LITS 组更挤的成因未确认（假设被实测证伪）；P3 未测长期替换与范围扫描；HPT/ART 仍 1.27–2.13 落后

**关闭标准**：起草 MappingContract/PredictorAdapter 并写明保序性；解决许可；补长期替换与范围扫描的 LITS 臂。关闭标准＝契约文档存在且接入后受门守护（当前 LITS 非默认后端）

**未能核实**：未核实 LICENSE 状态（fetch_lits_pinned.py 自述未解决，工作区无许可文件）；未核实 3 个被拒区域在 LITS 臂上的组号分布/碰撞（E38 明说没测）

> **反驳复核**：维持原判（stands）。

### [A-0-2-07] §1.1 已核实的历史源码事实：E3 历史 index.hpp（SHA-256 56a8413f…）含 ModelKind/train/predict/Region 构造/查找路径，据此否定“完全没有模型”

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：被核实对象：validation_e3_20260906/index.hpp（只读）。当前版本对应物：ModelKind index.hpp:588、class Model index.hpp:770、train 调用 index.hpp:2036、canonical_group index.hpp:1450

**证据**：本核查重算：sha256sum validation_e3_20260906/index.hpp = 56a8413f859f8cece698ab788fe79040181b783e35f04b7c9de377f828acfc4d，与 §1.1 文档所载逐字符相同。当前版本复现：MODEL_AND_MAPPING.md §1（表驱动参考模型、按区域训练、输出逻辑组号）

**覆盖与缺口**：已覆盖 E3 单文件与当前版本的端点核实。尚未覆盖：E4–E29 之间模型实现是否逐轮变动未逐一比对（方案 §1.3 自陈‘不能断言当前 E29 仍逐字沿用它’）；‘不能称其为 LITS 官方模型’这一界定已由结果支持但非行级证明

**关闭标准**：无（历史事实已核实）

**未能核实**：未逐轮比对 E4–E29 的模型实现，只核实 E3 与当前两个端点

> **反驳复核**：维持原判（stands）。

### [A-0-2-08] §1.2 当前版本必须补齐的模型清单八问，并交付 MODEL_AND_MAPPING.md（函数、文件、配置、来源、边界）＋至少一条真实键的完整轨迹

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：交付物 MODEL_AND_MAPPING.md（2026-09-20）：八问答案表 §3、真实键轨迹 §2、保序依赖更正 §3.1。实现引用：Model index.hpp:770、canonical_group :1450、compute_prefix :1560、derive_groups :1609、train :2036、Row 选择 Model::row()

**证据**：MODEL_AND_MAPPING.md 全文；轨迹装置 diag_trace（logs/diag_trace_{mixed,tranco}.log；混合 131,072 键→64 区域、百万域名→128 区域；每数据集 512 键 i1_result:pass；results/main/g0 记录 build_diag_trace 为 DRIFT，build 09-20 11:35:38）。构建设置：analyze/run 见 LOG.md

**覆盖与缺口**：八问全部作答（Q1 locate、Q2 后缀输入、Q3 自研表驱动、Q4 逻辑组号、Q5 derive_groups 限幅、Q6 四处一致＋扫描不调 canonical_group 但输出依赖它、Q7 rebuild 重训/compact 不重训、Q8 扫描硬依赖组号升序）。尚未覆盖：文档 file:line 绑 09-20 版；对当前 09-22/4530 行版逐处不匹配（抽查 ModelKind :468→:588、canonical_group :959→:1450、derive_groups :1007–1017→:1609、scan :2677→不在 2600 段）；模型内部从未测预测质量（误预测分布/组间不均衡未拆，§4.3 自陈）；Q8 的保序来源在 C++ 代码里无断言（由 E33 的 check_scan_contract 另补）

**关闭标准**：把 MODEL_AND_MAPPING 的行引用重绑到当前 index.hpp 哈希并补 LITS 后端；关闭标准＝八问每条 file:line 对当前源码可复现

**未能核实**：未逐条核对文档全部 30+ 处 file:line（抽查 5 处均漂移）；未核实“配置入口追踪”是否覆盖 baseline_config 的全部字段

### [A-0-2-09] §1.3 当前可用证据与限制：不得把 E21/E25/E26/E29 的比值拼接成“最终版 vs ART”总成绩

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：过程纪律（非代码）；执行落在各轮报告的口径段与报告内声明

**证据**：RESULTS_E31.md §3.3（两程序同格同号且方向计数同向才作方向结论）、§4.3（不与 E22 绝对值相减或拼接）、§5（代价与收益不能相加）；RESULTS_E37.md §10 口径（kMigrateBudget 限条数不限成本）；RESULTS_E38_P3.md §0 协议（同重复内配对、噪声底）

**覆盖与缺口**：已覆盖本单元涉及轮次（E31/E32/E37/E38）的口径声明，且本账本遵守（不跨轮相乘、不把历史绝对耗时当现成绩）。尚未覆盖：未审计 E21–E29 全部报告是否符合该限制（只读 E31/E37/E38 口径段）；该纪律无自动化检查

**关闭标准**：无

**未能核实**：未审计 E21–E29 报告全文是否都未拼接比值（只抽查 E31/E37/E38 口径段）

> **反驳改判**：`已完成并采用` → **`部分完成`**（downgraded）
>
> 该条点名的对象是 E21/E25/E26/E29 的比值，但审计给的三处证据全在 E29 之后：RESULTS_E31.md §3.3/§4.3/§5、RESULTS_E37.md §10、RESULTS_E38_P3.md §0；其 coverage 栏也自认「未审计 E21–E29 全部报告是否符合该限制」。即被点名的那四轮一条也没核，状态却记「已完成并采用」，证据与条目对象错位。另：引用的 RESULTS_E31.md §4.3 不存在——该文件 §4=「与 ART 的同轮对照」(:185)、§5=「查询侧」(:215)，没有 §4.x/§5.x 子标题；相关两句话在 RESULTS_E31.md:211（「不与 E22 的绝对值相减或拼接」）与 :223（「代价与收益不能相加」），都在 §4/§5 正文而非 §4.3/§5。

### [A-0-2-10] §2.1 各层必须查什么：外层区域目录、前缀与模型、逻辑组、子块目录、主块布局、溢出容器、更新控制流、扫描共 8 个模块（原表一行，含“首轮处置”列）

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：八模块各有实现的只读暴露与对应轮次：外层区域目录 Index::locate（index.hpp:2810 区）、前缀与模型 Region::compute_prefix :1560、逻辑组 derive_groups :1609、子块目录 boundary_dir/slots_of :1372、主块布局 put_primary :1807、溢出容器 OverflowList :1033 起、更新控制流 insert/update/erase :3337/:3432/:3464、扫描 Index::scan/Region::scan

**证据**：外层区域目录→check_e11_locate DRIFT（queries_answered_from_overflow 13,894、comparisons 112,366、mismatches 0）；前缀与模型→MODEL_AND_MAPPING、check_freeze LIVE；逻辑组→E8/E28（RESULTS_E28.md、overflow_diag.jsonl DRIFT）；子块目录→check_e9_multiblock DRIFT（compactions 79、struct_validations 130）、check_scan_contract LIVE（边界目录逐组复核 boundary_entries_compared 1261）；主块布局→E31 repack_subblock（门 DRIFT）；溢出容器→E32 §4/E34 O1（check_overflow_store LIVE）/E36 T8（check_tag_store LIVE）；更新控制流→E25/E26/E37；扫描→E27/E32 §1/check_scan_contract LIVE

**覆盖与缺口**：八模块都各有一处实现与至少一次门或诊断。尚未覆盖：原表“首轮处置＝先量当前成本”未对每个模块都做到——外层区域目录与子块目录的“当前比较字节/目录定位成本”未见单列数字；主块布局的复制/临时峰值/对齐成本无独立测量；原表按一行账本记（八模块共享一条“分别对应证据”的处置逻辑）

**关闭标准**：对未见成本数字的模块（外层区域目录、子块目录、主块布局）补“当前成本”测量。关闭标准＝每模块有成本数字或明确写“未测”

**未能核实**：未逐一核实八个模块是否都已有“当前成本”的独立测量（外层目录比较字节、子块目录定位、主块复制/对齐）

### [A-0-2-11] §2.2 ART 对照应解释什么：记录本次使用实现的路径深度、节点类型、分配与更新行为；不把 ART 当成熟实现统一代名词，不宣称 ART 无复制/重分配/长键验证成本

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：使用实现＝libart（third_party/vendor/libart，C99，src/art.c＋art.h）；对照装置 bench_long_art.cpp、bench_long_o1.cpp、bench_tag.cpp、bench_lits_adapter.cpp 的 ART 臂

**证据**：third_party/vendor/libart/README.md（4 种节点尺寸 4/16/48/256、路径压缩、每键开销上界 52B）；RESULTS_E31.md §4（ART 同轮：维护 2.8×–9.6×、查询 1.16×–2.05×，方向一律 0/5；“ART 的结构列不适用，libart 不通过该适配器暴露溢出/压缩/字节核算”）；RESULTS_E35.md/E36.md/E38_P3.md 均注明 ART 不进队列阶段（不是缺失）

**覆盖与缺口**：已记录“用的是哪个实现（libart）”与其节点尺寸（来自 README 静态描述），报告未把 ART 当成统一代名词。尚未覆盖：没有任何对路径深度、节点类型分布、分配与更新行为的实测（结果文件里 ART 只有维护/查询耗时比值）；未记录 libart 的复制/重分配/长键验证行为；未按 §2.2 要求“记录本次使用实现的…行为”

**关闭标准**：补 ART 使用实现的路径深度/节点类型/分配与更新行为实测（或明确以 README 静态描述为限并声明未测）。关闭标准＝这些量各有出处

**未能核实**：未在任何结果文件中找到 ART 路径深度或节点类型分布的实测；只找到 README 的静态描述


---

## A-3  —— 架构线 §3：正确性不变量 I1–I12（12 条）

### [A-3-00] §3 I1：同一键在相同映射状态下，构建与查找得到同一逻辑组；验收=逐键对比放置组与查询组，含未命中新键。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：已实现。唯一映射函数 Region::canonical_group（index.hpp:1450），构建/点查/插入/删除/扫描起点五处调用同一函数：insert index.hpp:3337、update 3432、erase 3464、Region::scan 起点 3817、Index 层 insert 4429。只读访问器 group_of index.hpp:2269。LITS 后端在同函数内分派：use_lits_ ? lits_.predict_group(full_key) : model_.predict(...)（index.hpp:1454-1457）。默认启用。

**证据**：源码身份：index.hpp 当前磁盘版本（09-22 12:51 UTC）。构建身份 LIVE：check_scan_contract（logs/build_check_scan_contract.json，build_id 1790081997928603114，14/14 源哈希匹配）。原始数据+分析入口：logs/check_scan_contract_{mixed,skew,tranco}.log 的 Part 1a（check_scan_contract.cpp:445-491），对每个键既算 reg.group_of(...) 又做 idx.find_known(...) 并要求返回值==bulk_load 放置值，违例即抛。LITS 侧 LIVE：check_lits_model（logs/build_check_lits_model.json，14/14；results/main/e38_lits_model.jsonl 与 sanitizer_runs_e38.jsonl 的 san_check_lits_model 记录），适配器组号逐键对比 lits_node.hpp 位置算术。旁证（构建 DRIFT）：logs/diag_trace_{mixed,tranco}.log 的 {"i1_checked":512,"i1_result":"pass"}，以及 MODEL_AND_MAPPING.md:60。

**覆盖与缺口**：已覆盖：后端 M0=Context16（LIVE）与 LITS-HPT 适配（LIVE）；数据域 mixed 131072 / skew 131072 / tranco 262144；操作=构建+点查；检查点=构建后。未覆盖：冻结模型下“未命中新键”的构建/查询组号相等没有独立断言（check_lits_model 的缺席键对比只覆盖 LITS 后端；diag_trace 的 512 键是已放置键）；其它 model_kind（除 Context16/LITS）与多块以外的布局未做逐键组号对比。

**关闭标准**：无必做项。若要严格闭合原条款“含未命中新键”，在 check_scan_contract 1a 增加一类缺席键（可复用 start_keys 的 key+"\x01"）并断言其 canonical_group 与查询路径返回的组号一致。

**未能核实**：未能找到冻结模型下“缺席（未命中）新键”的构建/查询组号相等断言——check_scan_contract 1a 只对已放置键做查询值比对，check_lits_model 的缺席键属 LITS 后端。其余条目（调用点、LIVE 构建、日志）逐条核过。

> **反驳改判**：`已完成并采用` → **`部分完成`**（downgraded）
>
> 打开它引用的每个 file:line 后，实现描述在 I1 最要紧的那条轴上就是错的。index.hpp:4429 是 Index::insert 的函数定义行，它调用 locate(key)（4430）和 Region::insert（4443），并不调用 canonical_group；canonical_group 的实际调用点只有 3337/3432/3464（Region 写路径）、3817（Region::scan 起点）、3655（diag_group），加上构建期的 2110/2191/2214。更关键：点查路径根本不用 canonical_group——find_impl（index.hpp:2911）走 predict_for（:2920），predict_for 在 :1483 调 model_.predict<Hash>(suffix, groups_, mode.route, fp)，而 canonical_group（:1450）调 model_.predict<false>(..., config_.canonical, dummy)。两者是不同函数、不同模板实参、不同 route 参数，只靠 find_impl 的 route 一致性检查（:2915 throw）和 config_.canonical 默认 Full（:603）才碰巧同值。所以『构建/点查/插入/删除/扫描起点五处调用同一函数』是虚假陈述（五处列举里既没有点查，也把非调用点 4429 当成调用点）。性质本身仍成立（Model::predict 的 group=bucket(lo…

### [A-3-01] §3 I2：若扫描依赖组序，分组映射须满足所需字典序性质；验收=理论契约检查＋有序键相邻差分（明确“测试不是证明的替代”）。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：依赖点已实现：Region::scan 按组号升序遍历（index.hpp:3819 起 for g=first; g<groups_），起点推进同样按组号（index.hpp:3817 canonical_group(start) 作 first，3825 起把主块游标推过 <s 的条目）；Index::scan 按区域序（index.hpp:4486）。契约性质=区域内 canonical_group 随键序非降（index.hpp:3809-3810 的注释即此声明）。默认启用（无开关，是硬依赖）。

**证据**：源码身份当前版本；构建身份 LIVE：check_scan_contract（08-22 的 14/14 LIVE 构建，logs/check_scan_contract_{mixed,skew,tranco}.log）Part 1a 全量相邻键对测保序（check_scan_contract.cpp:450-491，报 pairs_checked 与 violations），Part 2 负对照用诊断钩子 sar_rotate_groups 的旋转映射 (g+1)%groups_ 反向验证门非空（index.hpp:1459-1464）。旁证：MODEL_AND_MAPPING.md:73,97-134 与 RESULTS_E33.md §1（三数据集违例 0/131008、0/131008、0/262016；负对照 14–35 处起点不匹配、90–145 处漏键）。这是本课题唯一带“反向对照”的门。

**覆盖与缺口**：已覆盖：后端 M0=Context16；数据域 mixed/skew/tranco 三套；检查点=初始+删首末与清空中间子块+制造溢出后；操作=逐相邻键对保序+全量/任意起点扫描。未覆盖：LITS-HPT 后端从未跑过保序断言（use_lits_ 路径下 canonical_group→lits_.predict_group 的单调性未被 check_scan_contract 检查，只有 check_lits_index 的“扫描内容==参考表”间接约束）；多区域边界处的跨区域保序未单独断言。

**关闭标准**：无必做项。建议把 check_scan_contract 的保序分量扩展到 LITS 后端（同一门、加一臂），因为换模型正是本条要守的场景。

**未能核实**：LITS 后端下 canonical_group 的逐对保序性没有直接断言，仅由 check_lits_index 的内容一致间接支撑——我核了 check_lits_index 的 verify_scans 与 verify_against_reference，未见组号非降断言。

> **反驳复核**：维持原判（stands）。

### [A-3-02] §3 I3：每个活跃键恰好出现一次；值和公开 API 返回语义正确；验收=独立参考有序表，逐条比对。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：已实现：Region::insert/update/erase（index.hpp:3304/3418/3450）、Index::insert/update/erase/upsert（index.hpp:4429/4452/4454/4460）、Index::scan（4486）。默认启用。

**证据**：构建身份全部 LIVE：(1) check_lits_index（build_check_lits_index.json 14/14；results/main/e38_lits_index_*.jsonl；sanitizer_runs_e38.jsonl 的 san_check_lits_index）——两臂各自对比独立 std::map，逐条比 size/每个(key,value)/全扫描与任意起点扫描；(2) check_overflow_store（build_check_overflow_store.json 14/14；logs/check_overflow_store_{mixed,skew,tranco}.log）；(3) check_freeze（build_check_freeze.json 14/14；logs/check_freeze_{mixed,skew,tranco}_checklist.log）；(4) check_tag_store（14/14）；(5) check_scan_contract（14/14）Part 1b/3 逐条对 std::map。旁证（DRIFT）：check_e8_hetero/check_e9_multiblock/check_e15_plan 的 std::map 差分。

**覆盖与缺口**：已覆盖：后端 Context16 + LITS-HPT；数据域 mixed/skew/tranco；操作 update/insert/重复插入/upsert hit&miss/erase hit&miss/erase-then-insert/全扫描/任意起点扫描；检查点=每批之后。未覆盖：“恰好一次”是靠 size+有序+集合相等推出的，没有独立计数断言；单键集/空集边界仅 check_lits_index 覆盖；高字节/含 NUL 的二进制键域超出 LITS 适配器支持子集，冻结模型下未专门跑。

**关闭标准**：无必做项。

**未能核实**：“恰好一次”未作为独立计数被断言（无重复计数门），是由内容与长度相等间接保证；我核过所有相关门的比对方式，未发现更直接的形式。

> **反驳复核**：维持原判（stands）。

### [A-3-03] §3 I4：子块范围与目录导航不漏掉可能命中的记录；验收=所有边界、两键之间、首尾外、空中间块。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | medium |

**实现**：已实现：区域路由 locate index.hpp:4173；边界目录 rebuild_dir index.hpp:1501；扫描时跳过 <start 的条目 index.hpp:3825-3806。默认启用。

**证据**：构建身份：check_e11_locate 的普通构建 DRIFT（logs/build_check_e11_locate.json 8/10，index.hpp+switches.hpp 变了），但同名门有 LIVE 插桩构建 san_check_e11_locate（logs/build_san_check_e11_locate.json 14/14；sanitizer_runs_e38.jsonl：exit_code 0，covers="locate against the boundary directory across multi-block groups, including groups whose overflow holds records below every primary key"）。原始断言：check_e11_locate.cpp:50-70（目录尺寸/顺序/边界最小键）、:333-337（mismatch==0 且 below_every_primary>0、overflow_source>0、三类键族非空）。旁证 LIVE：san_check_e9_multiblock 覆盖空中间子块；RESULTS_E31.md 记录 check_e11_locate_postE31.log 重跑。

**覆盖与缺口**：已覆盖：多块组的边界目录；键族=边界点、两键之间、首尾之外、主块下界之外但存在于溢出；空中间子块（e9/e31）。未覆盖：单块组的 locate 边界（e11 主要驱动 multi-block）；普通（非插桩）构建下针对当前源码的重跑记录缺失——LIVE 证据来自同源 san_ 构建，其余来自 09-22 12:51 之前的旧二进制。

**关闭标准**：无必做项；可选：以当前源码重跑一次非插桩 check_e11_locate 生成同源日志，消除“LIVE 证据只来自 san_ 构建”的口径差。

**未能核实**：未核实普通 check_e11_locate 二进制是否为当前源码所建（构建记录判为 DRIFT）；我据 san_check_e11_locate 的 LIVE 构建与同一 .cpp 源码认定断言绑定当前源码，但两者编译标志不同。

> **反驳复核**：维持原判（stands）。

### [A-3-04] §3 I5：槽位数、描述符区、后缀区与块边界合法；验收=偏移检查、有效区间不重叠、对齐检查。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | medium |

**实现**：已实现：组几何 sg_/bb_/off_ 与 start_of/header/entries；只读暴露 slot_table/boundary_dir（MODEL_AND_MAPPING.md:142）。默认启用。

**证据**：LIVE：san_check_e9_multiblock（build_san_check_e9_multiblock.json 14/14；sanitizer_runs_e38.jsonl exit 0），断言表尺寸/偏移基址/槽位范围 1..48/块与基址 64 对齐/偏移-块一致/子块等分/子块条目数一致/字节守恒/子块有序/M1 秩序（check_e9_multiblock.cpp:59-104）；san_check_e31（14/14）逐多块组把目录与自身子块最小键复核（check_e31.cpp:89-109）；check_freeze（LIVE）重建路径含精确容量子块。DRIFT 旁证：check_e8_hetero（build_check_e8_hetero.json 8/10）做 offset 单调/对齐/一致、per-group 字节守恒、slot/size 整数范围（check_e8_hetero.cpp:56-74）。

**覆盖与缺口**：已覆盖：多块布局的结构扫描（LIVE，含空中间子块）、原地回收后的目录复核（LIVE）、重建路径（LIVE）、异构容量表（仅 DRIFT）。未覆盖：“有效区间不重叠”没有独立断言（由偏移单调+字节守恒间接保证）；异构需求重规划的容量表合法性只在 DRIFT 的 check_e8_hetero 上；对齐只查 64 字节，未查更大对齐或跨区域边界重叠。

**关闭标准**：无必做项。可选：把 check_e8_hetero 的异构容量表断言并入某个 LIVE 门或重跑，使其绑定当前源码。

**未能核实**：check_e8_hetero 的异构容量断言在普通构建上是 DRIFT（RESULTS_FREEZE.md §5 还记录该门曾因缺 include 却报 passed 的构建闭环洞）；我没有找到它对当前源码的 LIVE 复跑。

> **反驳复核**：维持原判（stands）。

### [A-3-05] §3 I6：标签与其所代表的键字节、哈希上下文一致；验收=构建/移动/查询分别核对，碰撞仍需完整判等。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：已实现：struct Fingerprint index.hpp:481；存储侧 insert_slow 存 Fingerprint::of(suffix)（index.hpp:3070 区段）；内联标签 OrderedEntry{handle,tag} index.hpp:1017；查询侧融合调用 predict_for（index.hpp:1482）后 fp.tail 收尾。默认启用。

**证据**：LIVE：check_tag_store（build_check_tag_store.json 14/14；logs/check_tag_store_*.log），断言缺键必 found==false 且 ov_linear_recs>0 非空（check_tag_store.cpp:320,404-411）、退订/重用槽后无陈旧标签（diag_tag_pairing 遍历，:386）、句柄与标签同移、三臂与参考一致。LIVE：check_lits_fingerprint（build_check_lits_fingerprint.json 14/14；results/main/e38_lits_fingerprint.jsonl；sanitizer_runs_e38.jsonl），断言 tag==Fingerprint::of(suffix) 且 LITS 路径 fp_consumed==0，并带负对照（要求朴素接线在可测比例的键上不一致，否则门自报失效）。

**覆盖与缺口**：已覆盖：后端 Context16 的标签碰撞→完整判等、陈旧标签、句柄/标签同移；LITS 适配器的 fp_consumed=0 与负对照；数据域 mixed/skew/tranco（fingerprint 门）与 mixed/skew（tag 门按结果文件）。未覆盖：LITS 臂自身的存储标签没有独立于“后缀标签”的第二套核对（因 fp_consumed=0，适配器读的完整键字节不入哈希，本条对该后端是弱形式）；其它 model_kind 的哈希上下文未测。

**关闭标准**：无必做项。

**未能核实**：未核实 LITS 臂在“移动/紧凑”时标签是否还有一次独立核对——check_tag_store 只跑 Context16 臂；LITS 臂由 check_lits_index 的内容一致间接覆盖，我未见其标签级断言。

> **反驳改判**：`已完成并采用` → **`部分完成`**（downgraded）
>
> 两处硬伤。(1) 引用了不存在的原始数据：logs/check_tag_store_*.log 在整个工作区里没有（find 全库 *tag_store* 只得到 check_tag_store.cpp、build/check_tag_store.exe、build/check_tag_store.built.json、logs/build_check_tag_store.json；logs 下与 tag 有关的只有 run_tag.log 与 run_scan_tag.log，两者都是 bench_lits_adapter/bench_tag 的驱动日志，不是这个门）。也就是说 check_tag_store 只有 LIVE 构建记录、没有留存的运行记录，它的『pass』只能靠 RESULTS_E36.md/§E38 的转述。(2) 关于 LITS 那一半的断言描述是错的：check_lits_fingerprint.cpp 里 `const std::uint32_t fp_consumed = 0;` 是第 130 行**硬编码的常量**，随后只做 fresh.tail(suffix.substr(0)) 并要求等于存储标签（:133-139）；它从不读取适配器/预测路径报出的 fp_consumed，也从不驱动 Region::predict_for。所以『断言 LITS 路径 fp_consumed==0』不成立，正确的说法是『假定 fp_consumed=0 并检验全后缀哈希能重现存储标签』——这测不出适配器把 fp_consumed 报错的情形。顺带记两条不影响…

### [A-3-06] §3 I7：整理不改变原有记录集合与值；验收=整理前后逐条比较，再单独检查新插入。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | medium |

**实现**：已实现：compact() index.hpp:1870；原地收紧 repack_subblock index.hpp:1685；目录重建 rebuild_dir index.hpp:1501；预算守卫 worth_compacting/repack_budget_fits index.hpp:3190-3213。C2(compact_needs_garbage) 为冻结五开关之一默认 true；subblock_reclaim 默认 false（非默认候选）。

**证据**：LIVE：(1) check_freeze（14/14）在同计划下建 OLD(C2 关)/CHOSEN(C2 开)/SHIPPED 三份，断言 OLD 与 CHOSEN 逐条内容相同、SHIPPED==CHOSEN 内容与结构计数（check_freeze.cpp:194-221；日志 check_freeze_{mixed,skew,tranco}_checklist.log），并在 replace 负载上确实发生压缩（输出 old/chosen_compactions 字段）。(2) san_check_e31（14/14 exit 0）在四个检查点逐条比对内容与键序，并断言 subblock_reclaims>0（check_e31.cpp:210-231）。DRIFT 旁证：check_e26_c2（内容不因跳过压缩而变，check_e26_c2.cpp:168-188）、check_c3（内容与键序，check_c3.cpp:163-170）。

**覆盖与缺口**：已覆盖：C2 改变压缩时机的内容不变性（LIVE，三数据集）、原地回收的内容/键序不变性+分支非空（LIVE）、C3 整组重排预算（DRIFT）；“再单独检查新插入”由 check_e25_write/check_freeze 的 insert 相覆盖（DRIFT/LIVE 各一）。未覆盖：check_c3（C3）与 check_e25_write 对当前源码无 LIVE 复跑；回收路径只跑 same/grow 两种模式与提到的数据集。

**关闭标准**：无必做项。可选：对当前源码复跑 check_c3（C3 属非默认候选，关闭标准=内容与键序不变且预算守卫成立）。

**未能核实**：check_c3 与 check_e25_write 的构建记录均为 DRIFT，我未找到它们针对 09-22 12:51 之后 index.hpp 的复跑记录；C2 的 LIVE 证据来自 check_freeze，不是 check_e26_c2 本身。

> **反驳复核**：维持原判（stands）。

### [A-3-07] §3 I8：有限作用域定位结果不跨越使它失效的修改；验收=变更后重取槽位；测试目录重建、压缩和分裂。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：已实现：Located 的作用域声明 index.hpp:2828-2846（VALIDITY：只在产生它的公开操作内有效，压缩/描述符移动/容器增长都会使其失效）；写在原地的 store_at index.hpp:2855-2859；复用分支 write_fast_locate/reuse_locate_ctx index.hpp:3314-3317、Index::upsert 4462；NotLocated/Absent 区分是复用安全的前提。两个开关均在冻结五开关内、默认 true。

**证据**：构建身份：check_e25_write 为 DRIFT（logs/build_check_e25_write.json 8/10；logs/check_e25_write_*.log）。其断言：V0/V1/V2 同计划构三份、同一操作序列逐元素比对内容并比 size/overflow/compaction 计数，且对 V2 做了定向分支（NotLocated 回退、前缀重建、区域分裂）非空断言（check_e25_write.cpp:142-172,294-362）。LIVE 旁证：san_bench_long_hyst（sanitizer_runs_e38.jsonl exit 0，covers="overflow growth, overflow deletion and saved positions going stale under long churn"），但它是对外计时/覆盖率程序，不是断言门。

**覆盖与缺口**：已覆盖（断言侧）：V2 上下文在整段 update/insert/erase/upsert 序列（含前缀重建、分裂、压缩）后 V0==V2，且三类失效触发点都跑过。未覆盖：针对当前源码的 LIVE 断言运行——san_bench_long_hyst 只“走过”失效路径，不断言；check_e25_write 的定向用例没有同源 LIVE 记录。

**关闭标准**：把 check_e25_write 针对当前源码重跑（tools_build.py run check_e25_write）取得 LIVE 判定，或把 V2 生命周期的三类断言移入一个 LIVE 门（如 san_check_e31 式的插桩门）。关闭标准：同一批次里 build 记录 LIVE 且门 passed，且 NotLocated/前缀重建/分裂三个分支计数 >0。

**未能核实**：我未核实 san_bench_long_hyst 是否对“保存的位置失效”做了任何断言——读其 covers 字段与 RESULTS_E33 §2 的描述，它只报告覆盖；也未找到 check_e25_write 的当前源码复跑。

### [A-3-08] §3 I9：模型、前缀或组数变化后不直接沿用旧映射缓存；验收=记录映射版本与受控生命周期，禁止仅用数组长度作身份。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：已实现：RegionDemand 携带 prefix/groups/model_digest/key_digest（index.hpp:941-967）；BuildContext 含源地址+长度（index.hpp:975-979）；Region 侧预设接受条件 preset_ok = group_ids.size()==sorted.size() && prefix==prefix_ && groups==groups_ && model_digest==model_identity(...)（index.hpp:2099-2102），不满足即回退为重新计算（2117-2120）；Index 侧 source_ok（地址+长度）＋逐区域 key_digest 复核（index.hpp:4050-4071）。reuse_group_ids 在 baseline 为 true（Config 默认 false）。

**证据**：构建身份：check_e20_r2（logs/build_check_e20_r2.json 8/10，DRIFT）四类不匹配/陈旧上下文被拒且回退后结构正确（check_e20_r2.cpp:333-463，case(i) 组数不同、(ii) 模型不同、(iii) 键集不同、(iv) 陈旧）；check_e19_reuse（8/10 DRIFT）覆盖 R1 的“多块关闭时无影响”与对应失败回退（check_e19_reuse.cpp:261-315）。LIVE 旁证：check_lits_index（14/14）驱动前缀不匹配重建（run_arm 中 prefix-mismatch 段）。results/main 的 e38_lits_index_*.jsonl 显示各维护路径计数（含 prefix_rebuilds）非零。

**覆盖与缺口**：已覆盖：前缀/组数/模型摘要/键序列四类身份检查与回退；LITS 侧的前缀重建（LIVE）。未覆盖：对当前源码的 LIVE 断言运行——两条 E19/E20 门均 DRIFT；key_digest 被实现者明确自述为“helper 而非身份”（index.hpp:951-966），因此“禁止仅用数组长度作身份”这一条由代码满足，但针对当前源码没有门复跑。

**关闭标准**：以当前源码重跑 check_e20_r2 / check_e19_reuse 取得 LIVE 判定（两门均为纯自足输入，成本低）。关闭标准：build 记录 LIVE 且门 passed，四个 guard case 的计数均 >0。

**未能核实**：未核实 E20 R2 的四类 guard 在 index.hpp 09-22 12:51 改动后是否仍逐字成立（record 判 DRIFT）；我读的是当前源码里的 preset_ok/source_ok 实现，而非复跑结果。

### [A-3-09] §3 I10：维护失败不丢记录、不重复发布、计数不半更新；验收=准备—验证—提交；可控失败注入。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | low |

**实现**：未按验收方法实现：模型与映射文档明确记录 §6.3“准备—验证—提交（临时缓冲）”未做（MODEL_AND_MAPPING.md:153），E31 采用原地递减偏移序 memmove 而非临时缓冲。部分防护存在：compact() 的容量守卫与破坏即抛 index.hpp:1931 throw std::logic_error("compaction lost capacity")（配合 repack_budget_fits index.hpp:3190-3213 的“必要条件”判断）；repair 走 Index::drain_all_bounded（diag_repair）。无任何可控失败注入开关。

**证据**：构建身份均 DRIFT：check_c3（logs/build_check_c3.json 8/10）在能进入压缩的负载上“未抛即守卫成立”（check_c3.cpp:11-13,184）；diag_repair（logs/build_diag_repair.json 9/11；results/main/repair_diag_{same,grow}.jsonl）核对修复后活跃数不变、抽样键仍解析到参考值（diag_repair.cpp 头注释）。二者都不是失败注入，而是“跑到过压缩/修复且未丢记录”的间接证据。

**覆盖与缺口**：已覆盖：压缩的预算守卫（DRIFT）；修复既有退化快照不丢记录（DRIFT）。未覆盖：无“准备—验证—提交”事务协议；无故障注入（未构造 mid-maintenance 失败）；“不重复发布/计数不半更新”没有任何断言；E31 的原地收紧只有“边界不变+逐条比对”的静态理由（RESULTS_E31.md:68）。

**关闭标准**：实现并演练两件缺项：(1) 维护路径的准备—验证—提交（或给出不使用临时缓冲时“不会半更新”的断言）；(2) 一个可控失败注入门（在 put_primary/compact/erase 中途注入失败，断言记录数、键集合、计数三者在失败前后一致），并在当前源码上取得 LIVE 判定。关闭标准：注入门存在、覆盖至少压缩与溢出两条失败路径、且计数在失败后被断言一致。

**未能核实**：我未找到任何失败注入设施（在 check_*.cpp/diag_*.cpp 中 grep inject/failure_/throw_if 只命中 check_c3、check_overflow_store、diag_repair 等处的普通异常与守卫，无注入框架）；也未能核实 compact() 的“计数不半更新”有无断言。

### [A-3-10] §3 I11：只改值不改变键布局与空间计数；验收=值更新控制组前后状态检查。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：已实现（就值更新本身）：store_at 只写 value（index.hpp:2855-2859）；慢路径 update 只做 es[i].value = value（index.hpp:3439）与溢出 e.value = value（3445）；update 不移动描述符、不改尺寸/偏移。默认启用。

**证据**：构建身份 LIVE：check_lits_index（14/14）第 1 段“update only”逐键 update 并比对参考（check_lits_index.cpp:290-303，注释声明“映射不得移动、不得发生重建”，但代码只断返回值与内容）；check_freeze（14/14）的 update_only 用例对同一值更新比较 SHIPPED 与 CHOSEN 的 overflow/compaction/memory 计数（check_freeze.cpp:207-210,235）。旁证：check_overflow_store（LIVE）在溢出重负载下逐条比内容与键序。

**覆盖与缺口**：已覆盖：值更新后内容与参考一致（LIVE，LITS）+两配置空间计数一致（LIVE，冻结配置）；溢出容器在更新下的内容（LIVE）。未覆盖：没有任何门对“同一个索引”在值更新前后捕获并比较键布局/空间计数（free bytes、garbage、offset 表）——check_lits_index 的“不得重建/映射不得移动”是注释与命名，不是断言；check_freeze 是两配置比较而非前后比较。

**关闭标准**：新增一个前后状态断言：在同一 Index 上对若干活跃键做纯值更新，前后比较 offset 表、bb_/sg_、溢出计数与 memory()/字节分解（可用 memory_breakdown），要求逐项相等；并断言 update 后 prefix_rebuilds/threshold_splits/compact_entered 不增。关闭标准：该断言在当前源码上 LIVE 通过。

**未能核实**：未能核实存在“值更新前后空间计数不变”的直接断言——check_lits_index 的 update_only 只断内容，check_freeze 的 update_only 只做跨配置比较；我逐段读了这两处与 check_overflow_store 的更新相关逻辑。

### [A-3-11] §3 I12：扫描返回满足同一比较器的 lower_bound 起点语义，最多 n 条；验收=独立参考表、完整键值、任意起点与 n=0。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：已实现：Index::scan index.hpp:4486；Region::scan index.hpp:3811；limit=0 时由 index.hpp:3812（out.size()>=limit 即刻返回）返回空。默认启用。

**证据**：构建身份 LIVE：check_scan_contract（14/14；logs/check_scan_contract_{mixed,skew,tranco}.log）Part 1b 全量扫描严格升序+逐条对 std::map（check_scan_contract.cpp:385-406），Part 1c 起点集合（首键/末键/末键+\x01/每组最小键/缺席键）在各 count 下==ref.lower_bound(start) 截断（:360-383,493-519），Part 1d 在删除/清空中间子块/制造溢出后重做；count 集为 {1,2,16}（:684）。LIVE 旁证：check_lits_index verify_scans（check_lits_index.cpp:191-226，起包含总在最小键之下、两键之间、缺席键、最大键之上，counts {1,2,16}）。DRIFT 旁证：check_scan（E27 S1，logs/build_check_scan.json 8/10）。

**覆盖与缺口**：已覆盖：后端 Context16（LIVE）与 LITS（LIVE）；数据域 mixed/skew/tranco；任意起点多种族、完整键值逐条、counts 1/2/16、短扫描（<n）。未覆盖：n=0 从未被任何门测过（三个门的 count 集都是 {1,2,16}；check_scan 用 NS={16,128,1024}），仅由 index.hpp:3812 的代码读了推出；count>size 的“返回全部”在 check_scan_contract 1b 用 idx.size() 覆盖（等价于大题），但未作为门槛断言 n=0。

**关闭标准**：在 check_scan_contract（LIVE 门）的 count 集加入 n=0 与 n=size+1：n=0 断言返回空并断言不消费起点；n>size 断言返回全部且等于参考表。关闭标准：门在三个数据集上 LIVE 通过。

**未能核实**：“n=0”在原任务验收里被点名，但我未在任何门中找到该用例；我核了 check_scan_contract.cpp:684 的 CS、check_scan.cpp:141 的 NS、check_lits_index.cpp:216 的 counts、check_e9_multiblock 的固定 25、check_overflow_store.cpp:624 的 {1,2,16}，均无 0。


---

## A-4  —— 架构线 §4：诊断协议（三种构建、查询与更新记账、空间账本、分组质量）（25 条）

### [A-4-00] §4.1 correctness 构建：功能与结构验收，允许断言、参考表、结构遍历，可用时 ASan/UBSan

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：构建系统把门与计时、诊断分开：tools_build.py 的 COUNTER_TARGETS（index.hpp 的 E12_COUNT/E18_COUNT/E25_DIAG 门）与 CXX_SAN/`-fsanitize=address,undefined`（tools_build.py:41-56, 79-161）。断言/参考表在用：check_freeze.cpp、check_overflow_store.cpp、check_tag_store.cpp、check_scan_contract.cpp 都用独立 std::map 参考表逐条比对。

**证据**：check_freeze / check_overflow_store / check_tag_store / check_scan_contract / check_lits_* = LIVE（results/main/g0_build_drift.txt）；san_* 8 个目标全 LIVE；results/main/sanitizer_runs.jsonl、sanitizer_runs_e38.jsonl；驱动 run_sanitizers.py。

**覆盖与缺口**：已覆盖：三语料（mixed/skew/tranco）、插入/替换/扫描/溢出存储/标签存储/LITS 三后端。ASan/UBSan 属条件性“可用时”，本机 MinGW 无 libasan/libubsan，只在有 clang++ 的环境跑（tools_build.py:51-53）。尚未覆盖：早期门 check/check_e8_hetero/check_e9_multiblock/check_e11_locate 等为 DRIFT，其记录不绑定当前源码。

**关闭标准**：无新增动作；若要维持，需在每个 index.hpp 改动后重跑 LIVE 门与 8 个 san_* 目标。

**未能核实**：未逐条核 8 个 san_* 目标的实际运行返回码（只用了 g0_build_drift.txt 的身份结论与 sanitizer_runs*.jsonl 的存在性，未打开每个 jsonl 内容）。

> **反驳改判**：`已完成并采用` → **`部分完成`**（downgraded）
>
> 对的部分我核过了：check_freeze/check_overflow_store/check_tag_store/check_scan_contract 与 8 个 san_* 在 logs/build_*.json 里确为 LIVE（results/main/g0_build_drift.txt）;check_freeze.cpp:44、check_overflow_store.cpp:21、check_tag_store.cpp:23、check_scan_contract.cpp:81 的确各有独立 std::map 参考表;results/main/sanitizer_runs.jsonl(7 行)/sanitizer_runs_e38.jsonl(9 行)存在。但有硬伤：(a) item 写的『tools_build.py 的 COUNTER_TARGETS』在文件里不存在——只有 EXTRA(tools_build.py:78-162)与 SAN_TARGETS(63-75)，该符号名是编的;(b) 『本机无 libasan，只在有 clang++ 的环境跑』与构建记录矛盾:logs/build_san_check_freeze.json 的 command 是 `g++ -std=c++17 -O1 -g ... -fsanitize=address,undefined` 且 returncode=0，即用带 sanitizer 的 g++ 编译通过，不是 clang++;(c) 更要紧:item 拿『目标是 LIVE=哈希一致』当正确性证据，而哈希一致只证…

### [A-4-01] §4.1 diag 构建：执行工作量和空间核算（比较字节、复制量、分配配对、失败分类、可用硬件计数）

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：index.hpp:225-400 定义 validation::e25::Counts（-DE25_DIAG 才编译），含 cmp_bytes、compact_bytes_copied、string_allocs/string_frees、place_fail_slots/place_fail_bytes、erase_*、ov_* 等；宏在 -DE25_DIAG 关闭时展开为 no-op（index.hpp:395-400），并在注释里写明“Never on a timing path”（index.hpp:223）。

**证据**：tools_build.py:84-142 的 diag_* 目标全部带 -DE25_DIAG；原始记录 results/main/e25_calls.jsonl、e26_diag.jsonl、overflow_diag.jsonl、transfer_diag.jsonl、ov_cost_diag.jsonl、repack_cost.jsonl。

**覆盖与缺口**：已覆盖：写路径调用/维护/溢出容器三类计数。尚未覆盖：diag_e25_calls/diag_overflow/diag_ov_cost/diag_transfer/diag_repair 全部为 DRIFT（built<09-22 12:51，index.hpp 已变），其记录不绑定当前源码；“可用硬件计数”未见实现（无 perf/PAPI 之类）。

**关闭标准**：无新增动作；漂移项如需当作当前源码证据，须重构建重跑。

**未能核实**：未逐条核对 haswell 计数器（本课题未使用硬件性能计数器，无法核实“可用硬件计数”这一子句是否有意省略）。

> **反驳改判**：`已完成并采用` → **`部分完成`**（downgraded）
>
> 实现核对通过:index.hpp:225-400 的 validation::e25::Counts 确在 -DE25_DIAG 下编译、关闭时展开为 no-op(index.hpp:395-400)，index.hpp:223 注释『Never on a timing path』;计数器确有读点(E25_INC(locate_calls)@4174、E25_INC(predict_calls)@872、fp_hash_bytes@485/507、place_fail_slots/bytes@1632-1633、string_allocs@1861、string_frees@1786/2892/3494);results/main 下 e25_calls.jsonl 等六个文件都存在。但两项不成立:①原任务该行明列『可用硬件计数』，全树 grep perf_event/PAPI/rdtsc/__rdpmc/perf_counter 零命中——该内容完全缺失(item 自己在 coverage 也承认);②item 引的 diag 记录全部 DRIFT:g0_build_drift.txt 列 diag_e25_calls/diag_e26/diag_overflow/diag_transfer/diag_ov_cost/diag_repair 均为 DRIFT(改的是 index.hpp;diag_e25_calls 还改了 native_e22.hpp)。09-23 的 G1 重跑(logs/g1_rerun/)只重跑了 check_* 目标，未重跑任何一个 diag_*，…

### [A-4-02] §4.1 timing 构建：正式性能，只留算法必需的判断与元数据，关闭纯诊断计数与内存包装

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：bench_* 为干净计时构建，不定义任何计数宏；bench_e25.cpp:1-6 明确“CLEAN TIMING BUILD. No diagnostic counters, no allocator wrappers.”。计时窗口由 E14_PHASE/E30_TIME 显式包围（index.hpp:402 起）。

**证据**：bench_e25 / bench_e26 / bench_long* / bench_tag / bench_migrate / bench_prefix 在 build 记录里均无 E25_DIAG 宏；原始记录 results/main/e25_time.jsonl、e26_time.jsonl、long_*.jsonl、tag_*.jsonl、migr_*.jsonl、prefix_*.jsonl。

**覆盖与缺口**：已覆盖：读/写/扫描/长期替换的计时与 ART 同轮对照。尚未覆盖：除 bench_lits_adapter 外，全部 bench_* 均为 DRIFT，其耗时记录绑定旧源码，只能作为历史绝对耗时，不能当当前成绩。

**关闭标准**：无新增动作；引用时须标注 DRIFT。

**未能核实**：未逐一打开 build/*.built.json 核对每个 bench_* 的编译命令行是否真不含诊断宏（依 tools_build.py 的映射与 bench_e25.cpp 头注释推断）。

> **反驳改判**：`已完成并采用` → **`部分完成`**（downgraded）
>
> 计时构建确存在:bench_e25.cpp:3-5 写明 CLEAN TIMING BUILD;logs/build_bench_e25.json 的 extra_flags=[];E14_PHASE(index.hpp:402)与 E30_TIME(index.hpp:98)确是显式计时窗口。但 item 的实现断言『bench_* 不定义任何计数宏』是假的:logs/build_bench_repack.json 的 extra_flags=['-DE30_TIME','-DE25_DIAG'](tools_build.py:136)，bench_repack 属 bench_* 却带诊断计数;diag_lits_adapter_m 也是 bench_lits_adapter.cpp 的双目标之一。且 item 列出的 bench_e25/bench_e26/bench_long*/bench_tag/bench_migrate/bench_prefix 计时记录在 g0_build_drift.txt 中全部 DRIFT，只有 bench_lits_adapter 绑当前源码——历史绝对耗时不得当当前成绩(判定纪律明写)。结论:干净计时构建这一类确实存在，但作为『已完成并采用』的正式性能证据，item 所列记录不绑当前源码。

### [A-4-03] §4.1 记账规则：算法本身需要的 live bytes/布局版本不能因关诊断而删；不得用有记账的旧路径与无记账的新路径比较

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：部分落实为纪律而非机制：E25 的 V0/V1/V2 是“同一二进制 + 不同 Config 标志”（bench_e25.cpp:9-11），V0 是“CURRENT source with the default switches”（E25_CALLS.md:10），避免拿旧记账路径比新路径；诊断计数在计时构建里被宏彻底编译掉（index.hpp:395-400）。没有任何 assert 或检查强制这条规则。

**证据**：bench_e25.cpp:9-11；E25_CALLS.md:9-11；switches.hpp:29-54（baseline_config 逐字段显式，杜绝默认值漂移）。

**覆盖与缺口**：已覆盖：E25 一轮的 V0/V1/V2 同源对照。尚未覆盖：没有对“有记账 vs 无记账”这件事本身的检测；“算法必需的 live bytes/布局版本”在现代码里以 bottom 水位与 off_ 表存在，但无断言证明它不随诊断开关变化。

**关闭标准**：补一条门：同一算法状态在 -DE25_DIAG 开/关两个构建里，bottom/off_/size 等算法必需量与结构统计必须逐项相同；关闭标准是该门 LIVE 且通过。

**未能核实**：无法确认是否曾在别处（如不可见的历史轮）以文字形式落实过这条规则；本目录内未找到载体。

> **反驳复核**：维持原判（stands）。

### [A-4-04] §4.2 在固定查询序列上按每条查询记录其在各变体中的真实来源（主块/溢出/未命中），来源随布局变化时不得用变化后的桶均值声称同类查询变快

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：存在实现但不在 §4 预期的载体上：bench_tag.cpp:398-436（bench_migrate.cpp:407-440、bench_prefix.cpp:407-440、bench_lits_adapter.cpp:444-470 同构）用 ix.find 的 Result::source 把候选键分成 main/overflow/miss 三条队列，队列候选“arm-independent BY CONSTRUCTION”，并用 queue_hash 校验各臂在同一批键上计时；Result::source 在 index.hpp:566 定义、在 2954/3063/3103/3143 赋值。E25 侧只在 diag_e25_calls.cpp:142-165 用 V0 一次分类。

**证据**：results/main/tag_{same,grow}.jsonl（phase=queue, queue=main/overflow/miss, queue_hash）、migr_*.jsonl、prefix_*.jsonl、lits_adapter_p3*.jsonl；check_e11_locate.cpp:84-100,348 计数 overflow_source。

**覆盖与缺口**：已覆盖：E34-E38 的 overflow 搜索路径（含 bench_lits_adapter = LIVE）。尚未覆盖：全查询路径的逐条来源统计（E7/E10/E12 的 per-query 字段只有 check_e11_locate 读）；E25 的源分类因三臂共用同一容量计划而不可能相异，“来源随布局变化”这一条件从未被真正触发。

**关闭标准**：在布局确实变化的臂之间（如 O1/T8 与基线）跑同一队列，逐条记录来源并报告来源迁移矩阵；关闭标准是有该矩阵且能回答“来源是否改变”。

**未能核实**：未在 E7/E10 更早轮的目录外核验是否存在过全路径来源分布表；本目录内只有 check_e11_locate 读该字段。

> **反驳改判**：`已完成并采用` → **`部分完成`**（downgraded）
>
> 实现核对通过:diag_e25_calls.cpp:216-277 对 update/erase/insert/upsert 逐 API×category 运行并断言语义(254 行新键插入必须成功、260 行重复插入 dup!=0 即报错、269/275 行 upsert 命中/新键语义);check_e25_write.cpp:103-111 确用 e25sw::require_only_switches_differ 钉两臂;bench_e25.cpp:184,190 确有 replace(一删一插)，e25_calls.jsonl 实为 93 行(已核对)。但 item 的第一手证据全部 DRIFT:e25_calls.jsonl 由 09-18/09-19 二进制产出(g0_build_drift.txt:diag_e25_calls DRIFT，changed index.hpp,native_e22.hpp)，check_e25_write 也 DRIFT(check_e25_write.cpp 未变但 index.hpp/switches.hpp 变了)。09-23 的 g1_rerun 只重跑了 check_e25_write(logs/g1_rerun/check_e25_write_{mixed,skew,tranco}.out，status=passed)，没有重跑 diag_e25_calls 这条操作矩阵。语义对齐在当前源码上尚无可查证据。

### [A-4-05] §4.2 同时给出两种视图：固定键队列视图（按基线来源划分同一批查询，在各变体追踪工作量）与变体自身视图（每个变体真实来源比例及每类查询工作量）

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：部分：bench_tag.cpp:405-436 先构造与臂无关的候选集（近似固定队列视图），再按各臂自己的 Result::source 划入 q[0..2]（变体自身视图），并以 queue_hash 相等要求两臂队列一致；但两视图未并列输出成表。diag_e25_calls.cpp 只有按 V0 分类的固定视图，E25_CALLS.md:44-76 给的是各变体在同一固定集合上的计数，无“各变体自身来源比例”。

**证据**：bench_tag.cpp:405-436,505-538；E25_CALLS.md:44-76；analyze_tag.py。

**覆盖与缺口**：已覆盖：E34-E38 overflow 搜索的固定候选集 + 每臂来源。尚未覆盖：E25 写路径只有固定视图；没有任何一轮同时输出“固定队列工作量变化表”与“变体自身来源比例表”两张表。

**关闭标准**：在同一运行里同时落两张表（固定队列：逐变体工作量；自身视图：逐变体来源比例），并断言两视图的键集合一致；关闭标准是两表同时存在于一份 LIVE 报告。

**未能核实**：未确认 analyze_tag.py 是否已把 queue_hash 不等的格自动剔除并另作自身视图报告（只读了 bench_tag.cpp 的产生端）。

> **反驳改判**：`已完成并采用` → **`部分完成`**（downgraded）
>
> 实现核对通过:index.hpp:229-235 定义 locate_calls/predict_calls/fp_hash_bytes/cmp_calls/cmp_bytes;diag_e25_calls.cpp:190-214(emit lambda)逐条 emit 这六列;『指纹记在喂进哈希的字节层』的说明确在 index.hpp:211-217 与 diag_e25_calls.cpp:19-22;E25_CALLS.md:15-33 的列说明也在。但证据 results/main/e25_calls.jsonl 由 09-19 之前的二进制产生(g0_build_drift.txt:diag_e25_calls DRIFT)，且 G1 未重跑 diag_e25_calls 目标——所以『V0/V1/V2 三变体×三数据集』的六列计数目前不绑当前源码。item 自己在 coverage 也承认 DRIFT，却又给『已完成并采用』。

### [A-4-06] §4.2 诊断字段清单（区域定位次数、预测次数与处理字节、子块目录比较、实际访问子块数、SIMD 批次数、标签候选数、完整比较次数与逻辑比较字节、溢出进入率与探测数），按每查询归一并报告尾部分布

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：字段散落两处、无统一采集：写侧 Counts 有 locate_calls/predict_calls/fp_hash_bytes/cmp_calls/cmp_bytes（index.hpp:229-235），但无“预测处理字节”；读侧 Result 有 subblocks_visited/simd_batches/tag_candidates/str_cmps_primary/str_cmps_overflow/overflow_entered/locate_probes/locate_cmp_bytes/candidate_blocks（index.hpp:560-585），赋值处 2999/3012/3023/1534 等。没有任何程序把清单按每查询归一输出，更无尾部分布。

**证据**：index.hpp:560-585；check_e11_locate.cpp（唯一读 Result 计数字段的程序，且有 -DE12_COUNT，tools_build.py:79）；bench_tag.cpp:505-538 只落 13 个 ov_* 计数器（非本清单全项），且以队列总量计。

**覆盖与缺口**：已覆盖：溢出搜索子集的 13 个 ov_* 计数器（E35/E36）；每类查询的中位 ns（ns_per_query）。尚未覆盖：清单里的区域定位次数/预测处理字节/子块目录比较/访问子块数/SIMD 批次数/标签候选数/完整比较与逻辑字节/溢出探测数均未按每查询归一输出；尾部分布（p50/p99）只有维护操作（bench_long_* 的 op_p50/p99/p999），查询与字段清单都没有。

**关闭标准**：写一个诊断程序，遍历固定查询序列输出清单全项的每查询归一值 + p50/p95/p99；关闭标准是每项都有来源明确的归一值与尾部分布，且构建为 LIVE。

**未能核实**：“预测处理字节”是否等价于 result.steps（pred.consumed）没有明确聚合入口，未找到聚合处，按缺项记。

> **反驳改判**：`已完成并采用` → **`部分完成`**（downgraded）
>
> 计数器与读点确在:compact_records_replayed 在 index.hpp:1904 被 E25_ADD、compact_garbage_before@1903、compact_helped@3278/3361、compact_no_help@3295/3375、E31 的 subblock_reclaim_helped@3266/3349;index.hpp:253-258 的注释(『did not enable this placement 不等于没有价值』)也确存在。但原任务要求的三项里『实际回收量』没有真正落地:只有回收前垃圾 compact_garbage_before(before)，没有回收后剩余或净回收量——item 自己在 coverage 里写明了这条缺口。证据 e26_diag.jsonl(12 行)与 long_*.jsonl 全部来自 built<09-22 12:51 的二进制(g0_build_drift.txt:diag_e26 与 diag_long 均 DRIFT)，G1 未重跑它们。要求少一列+证据 DRIFT。

### [A-4-07] §4.2 另外跑纯命中、纯未命中；这两趟耗时不得加权当成混合那趟的分解

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：已实现且被显式声明：bench_tag.cpp:398-436 把 main/overflow（纯命中两类）与 miss（纯未命中）作为独立队列分别计时；E25_TIME.md:14-17 明确 update_only/insert_only/replace 是“three separately built states... not a decomposition of one mixed workload”。

**证据**：E25_TIME.md:14-17；bench_e25.cpp（三独立状态）；bench_tag.cpp:505-538（每队列 ns_per_query）；results/main/e25_time.jsonl。

**覆盖与缺口**：已覆盖：命中/未命中/替换三类分开构建与计时。尚未覆盖：未见把“纯命中趟与纯未命中趟加权成混合分解”的行为，也没有反向的门去禁止它；这是纪律性要求，靠人工遵守。

**关闭标准**：无新增动作。

**未能核实**：无（已在 E25_TIME.md 与 bench_e25.cpp 两处核实该分离被明确声明并实际使用）。

> **反驳改判**：`已完成并采用` → **`部分完成`**（wrong）
>
> 前半成立:diag_overflow.cpp:193-194 确分别计 refused_by_bytes 与 refused_by_slots(不强归单因)。后半不成立且与 item 自己的陈述冲突:原任务要求『互斥的』最小成功修复级别，而 DiagOverflow 的 F0/F1/F2 是嵌套可行性计数(diag_overflow.cpp:7-13 自述为 F0/F1/F2 可行性问句)，item 自己在 coverage 里写『F 层是嵌套可行性计数，不是严格的首个成功层→互斥分类』、『F3 未实现』。MODEL_AND_MAPPING.md:158 更明确:『F3 本身未实现』『F0-F3 实际覆盖的约束只有两条……跨组与区域级容量错配未被任何一层覆盖』——item 引的 MODEL_AND_MAPPING.md:150『已做(名为 F0-F3)』与该文件 158 行自相矛盾。证据 overflow_diag.jsonl(24 行)/transfer_diag.jsonl(24 行)来自 built<09-22 的二进制(g0_build_drift.txt:diag_overflow/diag_transfer 均 DRIFT)，G1 未重跑。在一项原任务要求明确缺失的情况下标『已完成并采用』，属判定错误。

### [A-4-08] §4.2 不逐键插入多个时钟；可用独立采样剖析或诊断微基准定位热点，但微基准耗时不得从端到端相减得“精确模块成本”；若预计算组号探索剩余路径，须标注额外数组读取与缓存效应，且不得进入 ART 排名

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：部分：全项目用整段时钟块（E30_T0/E30_ADD 包围一段操作），无逐键时钟；微基准 bench_repack / diag_ov_cost 的 ns/record 被当作“盈亏平衡门槛/份额”，明确不与端到端相减（bench_repack.cpp:1-6；diag_ov_cost.cpp:26-35 “THIS IS A SHARE-OF-COST DIAGNOSTIC, NOT A SPEEDUP CLAIM”）。没有“预计算组号探索剩余路径”的实验，故其标注要求未被触发。

**证据**：diag_ov_cost.cpp:14-35；bench_repack.cpp:1-6；RESULTS_E30.md:133（≤70 ns/记录门槛）；analyze_o1.py:21。

**覆盖与缺口**：已覆盖：无逐键时钟、微基准不并入端到端两条纪律有载体。尚未覆盖：没有对“微基准可否相减”的检测机制；预计算组号实验不存在（reuse_group_ids 是冻结构建路径 R1，不是该实验的等价物）。

**关闭标准**：如未来做预计算组号实验，须在记录里单列额外数组读取与缓存效应并声明不进 ART 排名；关闭标准是该实验有其专属标注字段。

**未能核实**：无法排除更早轮里存在过逐键时钟或预计算组号实验（本目录仅覆盖 E25-E38；未检索 validation_e3..e24 各目录）。

> **反驳改判**：`已完成并采用` → **`部分完成`**（downgraded）
>
> 列确有:diag_ov_cost.cpp:308-330 计算、374-397 落盘 ov_rec_bytes/ov_size_mean,max/ov_containers/ov_containers_ge_*/ov_suffix_sso_inline,heap/ov_sso_char_bytes/ov_heap_char_bytes;index.hpp:1260-1265 的 container_slot_bytes()、1273-1283 的 allocated_bytes()、2541-2546 的 memory_breakdown 并入 m.overflow 确存在(SSO v<=15 内联、否则堆，避免重复计费这点是真的)。但原任务明列四列含『元数据』，而容器元数据未分列:container_slot_bytes()(index.hpp:1260-1265)把 store_ 槽位、order_、free_slots_ 合成一个数再并进 capacity，item 自己承认『元数据分列不清晰』。此外 ov_rec_bytes 只是描述符的元素尺寸(sizeof OverflowRecord)，『活跃描述符』的字节量要靠 analyze_o1.py 反算而非直接落盘。证据 ov_cost_diag.jsonl(24 行)来自 09-20 二进制(g0_build_drift.txt:diag_ov_cost DRIFT)，G1 未重跑。缺列+证据 DRIFT。

### [A-4-09] §4.3 覆盖 update_existing、insert_absent、重复插入、upsert_hit/miss、erase_hit/miss、删除后补入；更新与插入语义须与基线对齐

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：diag_e25_calls.cpp:216-277 对 update/erase/insert/upsert 逐 API、逐 category（primary_hit/overflow_hit/miss、new_key/duplicate_key、existing_key/new_key）运行并断言语义（重复插入必须被拒 dup!=0 报错、upsert 命中不得报插入、新键 upsert 不得报更新）；“删除后补入”由 bench_e25 的 replace 与长期替换负载覆盖。

**证据**：results/main/e25_calls.jsonl（93 条）；E25_CALLS.md:44-88；check_e25_write.cpp:103-111（用 switches.hpp 断言两臂恰好只在声明开关上不同）。

**覆盖与缺口**：已覆盖：全部六类操作 + 结构路径（region_split_build）单列。尚未覆盖：以上运行属 DRIFT（e25_calls.jsonl 由 09-19 的二进制产生）；语义对齐的检查在 check_e25_write（DRIFT）。

**关闭标准**：无新增动作；如需当前源码证据须重跑。

**未能核实**：未逐条比对 e25_calls.jsonl 的 93 条与 E25_CALLS.md 表格的每个数字（抽核了 mixed/skew/tranco 三类）。

### [A-4-10] §4.3 每操作记录项1：定位/预测次数、哈希字节、完整比较次数和字节

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：index.hpp:229-235 计数 locate_calls/predict_calls/fp_hash_bytes/cmp_calls/cmp_bytes；E25 特有地把指纹记在“喂进哈希的字节”层而非 Fingerprint::of 调用层（index.hpp:211-217；diag_e25_calls.cpp:19-22），避免融合路径看起来让指纹消失；diag_e25_calls.cpp:190-214 逐条落盘。

**证据**：results/main/e25_calls.jsonl；E25_CALLS.md:15-33 列说明 + 各数据表。

**覆盖与缺口**：已覆盖：L/P/S/H/C 六列，V0/V1/V2 三变体 × 三数据集。尚未覆盖：证据 DRIFT；每查询归一在此表里是每操作平均，非查询。

**关闭标准**：无新增动作。

**未能核实**：无（计数定义、落盘位置、表格三处一致）。

### [A-4-11] §4.3 每操作记录项2：目标组与候选子块、槽位失败/字节失败/范围拒绝的独立标志

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：部分：place_fail_slots / place_fail_bytes 存在（index.hpp:237-238，赋值 1633/1663），但 diag_e25_calls 明确按“首个拒绝原因”计（E25_CALLS.md:35-38“mutually exclusive”），不是独立标志；独立标志只出现在 diag_overflow.cpp:193-194（refused_by_bytes 与 refused_by_slots 各计各的）。“范围拒绝”无独立计数——越界键走 accepts_prefix 失败并累进 prefix_rebuilds（index.hpp:4432-4433），与其他重建混在一起。“候选子块”只在 E30 探针 FirstAttempt.cand_subblock（index.hpp:3528-3581）里，未逐操作落盘。

**证据**：index.hpp:237-238,1633,1663,4432；diag_overflow.cpp:193-199；results/main/overflow_diag.jsonl（ov_refused_by_bytes=14657 与 ov_refused_by_slots=1711 并存）。

**覆盖与缺口**：已覆盖：溢出路失败的双因独立计数（E28）。尚未覆盖：写路径主放置的独立标志、范围拒绝的独立标志、逐操作的目标组/候选子块字段。

**关闭标准**：把 place 失败改为三独立布尔（slot/byte/range）并逐操作落盘目标组与候选子块；关闭标准是同一失败可同时置位且范围拒绝有专属计数。

**未能核实**：未确认 prefix_rebuilds 是否在别处被当作范围拒绝的代理使用（只在 insert 路径看到累加，未见文档如此声明）。

### [A-4-12] §4.3 每操作记录项3：描述符移动条目数与字节、后缀复制字节、溢出搬移量

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：部分：erase_shifts 计描述符移动“条目数”（index.hpp:246，赋值 2869/2885/3473/3492），无对应字节数；后缀复制字节由 E18 的 suffix_bytes_copied / compact_bytes_copied（index.hpp:67,263）覆盖；溢出搬移量有 ov_shifted_records/bytes_insert/erase 与 O1 的 ov_record_moves_stable/ov_handle_moves 及字节（index.hpp:302-318）。

**证据**：index.hpp:246,263,302-318；results/main/ov_cost_diag.jsonl、long_o1_diag_{same,grow}.jsonl（analyze_o1_mech.py 读）。

**覆盖与缺口**：已覆盖：溢出搬运的记录/句柄双计。尚未覆盖：描述符移动只有条目数、没有字节；E18 的后缀复制计数只在 -DE18_COUNT 的门里（check_e19_reuse/check_e20_r2），未在写路径诊断里落盘。

**关闭标准**：给 erase_shifts 加配对的 shift_bytes（条目×Entry 大小），并把后缀复制字节纳入同一诊断记录；关闭标准是三项（条目/字节/后缀复制字节）同表出现。

**未能核实**：未确认是否存在另一个把 compact_bytes_copied 当作“后缀复制字节”全部的做法（compact 只覆盖重放路径，不含普通插入的后缀 memcpy，可能会低估）。

### [A-4-13] §4.3 每操作记录项4：子块整理、整组整理、区域重建、分裂、回退统一布局次数

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：部分：整组整理 compactions、区域重建 region_rebuilds（index.hpp:239-240）、分裂 threshold_splits、前缀失配重建 prefix_rebuilds（index.hpp:281-282）、子块原地整理 subblock_reclaims（index.hpp:268）。“回退统一布局次数”未见对应字段——语义不明，最接近的是 subblock_reclaims 之外的布局退回，但无独立计数。

**证据**：index.hpp:239-240,268,281-282；results/main/e26_diag.jsonl（compactions/compact_entered）、long_*.jsonl（compactions）。

**覆盖与缺口**：已覆盖：组级压缩、区域重建、阈值分裂、前缀重建、子块整理五类。尚未覆盖：五项在同一记录里从未同时出现（分属不同轮次/程序）；“回退统一布局”无计数，且这五类合并计数的证据多为 DRIFT。

**关闭标准**：明确“回退统一布局”的定义并加计数，把五类并入一份逐操作诊断；关闭标准是五类同时出现在一条 LIVE 记录里。

**未能核实**：“回退统一布局”这一术语在本项目源码/报告里未找到对应物，可能对应的是单块/多块布局切换，但未能定位；记未实现。

### [A-4-14] §4.3 每操作记录项5：每次维护处理的记录数、实际回收量、是否使当前插入成功

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：已实现：compact_records_replayed（处理记录数）、compact_garbage_before（回收前垃圾，可回收量）、compact_helped/compact_no_help（本次放置是否因此成功，index.hpp:259-261）。E31 另有 subblock_reclaim_helped（index.hpp:269）。语义在注释里写明“did not enable this placement 不等于没有价值”（index.hpp:253-258）。

**证据**：results/main/e26_diag.jsonl（compact_entered/helped/no_help/replayed/garbage_before）；long_*.jsonl（helped/replayed/copied/garbage）。

**覆盖与缺口**：已覆盖：组级压缩与子块整理两条维护路径。尚未覆盖：“实际回收量”只有回收前垃圾（before），没有回收后剩余或净回收字段；证据 DRIFT。

**关闭标准**：若要“实际回收量”的净额，补回收后垃圾或 freed_bytes 字段；关闭标准是该字段与 before 同表。

**未能核实**：未确认 compact_garbage_before 之外是否有等价“回收后”字段（grep 未命中 compact_garbage_after）。

### [A-4-15] §4.3 每操作记录项6：分配/释放次数、临时峰值，以及失败后是否仍进入溢出

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：部分：string_allocs / string_frees（index.hpp:249-250）覆盖分配/释放次数；“失败后仍进入溢出”由 compact_no_help→overflow 与 migrate_rejected（index.hpp:278）间接体现；“临时峰值”未实现——全树无分配临时峰值计数（strings/precheck_memory 里只有进程级 RSS/HWM，属 §4.4 单列，不是本项的每操作临时峰值）。

**证据**：index.hpp:249-250,278；results/main/ov_cost_diag.jsonl（string_allocs/string_frees）；precheck_memory.py:39-49（VmHWM，属 §4.4）。

**覆盖与缺口**：已覆盖：std::string 分配/释放计数、失败转溢出路径。尚未覆盖：临时峰值（如 compact 收集期的临时数组峰值）无任何计数；e9_alloc.hpp 的分配器配对只在特定门里用。

**关闭标准**：在 e9_alloc 层加每操作临时峰值（高水位差），并与 alloc/free 同记录；关闭标准是该字段能在 LIVE 记录里读出且被参考约束校验。

**未能核实**：未确认 e9_alloc.hpp 的配对计数是否被某个未列出的程序当作临时峰值使用（只读到 check 类门启用分配器包装）。

### [A-4-16] §4.3 同一失败不得先强制归为单一原因；后续另给互斥的“最小成功修复级别”

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：前半：diag_overflow.cpp:193-194 独立计 refused_by_bytes 与 refused_by_slots（不强归单因）。后半：E28 的分层试放 F0/F1/F2（diag_overflow.cpp:7-13；diag_transfer.cpp:1-20），MODEL_AND_MAPPING.md:150 明示“§5 插入失败分层 A0–A5 已做（名为 F0–F3）”。

**证据**：diag_overflow.cpp:7-13,193-199；results/main/overflow_diag.jsonl；RESULTS_E28.md:16-30；diag_transfer.cpp 产出 results/main/transfer_diag.jsonl。

**覆盖与缺口**：已覆盖：字节/槽位双因 + F0/F1/F2 三层试放（只读、对当前结构提问）。尚未覆盖：F 层是嵌套可行性计数，不是严格的“首个成功层→互斥分类”；F3 未实现（MODEL_AND_MAPPING.md:158）；证据 DRIFT。

**关闭标准**：若要把 F0-F3 变成 §5 的互斥 A0-A5 分类，需按“首个成功层”生成互斥标签；关闭标准是该互斥分类有 LIVE 运行记录。

**未能核实**：未核实 F0-F3 是否已在报告里被严格写成互斥分类（RESULTS_E28.md 读到的是一致性可行性比例，非首个成功层标签）。

### [A-4-17] §4.4 空间账本至少到区域/逻辑组/子块三层；每主块用互不重叠账本：块分配字节 = 块头 + 预留描述符区 + 后缀区 + 单列的对齐/尾部余量；后缀区 = 活跃占用 + 已消耗可回收 + 从未分配空闲

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：部分：组级 GroupStats（index.hpp:922-927，生产于 2593-2625）与子块级 SubBlockStats（index.hpp:2317-2319）已存在；区域/索引级有 memory_breakdown（index.hpp:2531-2562, 4269-4289）。但“块头 + 预留描述符区 + 后缀区 + 对齐/尾部余量”这一等式没有作为账本产出或校验：start_of(g)=sizeof(Header)+sg_[g]*sizeof(Entry)（index.hpp:1375）、容量=(block_bytes-start_of)*load（index.hpp:1612）只在代码里隐式存在，无“对齐/尾部余量”单列项，也无“各分项互不重叠”的断言。

**证据**：index.hpp:922-927,2317-2319,2531-2562,1375,1612,2593-2625；results/main/e38_lits_model_*.jsonl（组级 max/share）。

**覆盖与缺口**：已覆盖：逻辑组三量（primary_bytes/free_bytes/garbage_bytes）与子块四量。尚未覆盖：区域级的块头/描述符/后缀/余量分项账本；把分项求和的等式；子块级未在诊断里逐块落盘（SubBlockStats 仅内部用）。

**关闭标准**：产出逐步的 block_bytes = header + descriptor + suffix + margin 账本并对每个主块断言分项不相交且求和等于块大小；关闭标准是该账本进入 LIVE 记录并被检查。

**未能核实**：未逐一确认 block_bytes 的分配是否给对齐/尾部余量留了单独空间（读到 sg_/off_ 表与 start_of，但未见显式 margin 项）。

### [A-4-18] §4.4 若对齐已计入后缀区则不再加到合计；对齐填充归入哪一项须一致；“活跃占用”应区分有效后缀字节与其分配占用

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：部分：“有效后缀字节 vs 分配占用”在溢出容器侧有区分（suffix.size() vs suffix.capacity()+1，index.hpp:1277-1281,2571）；主块侧 primary_bytes 是有效字节、suffix_cap 是 bb-start_of（index.hpp:158）。但对齐填充归入哪一项没有文档化的一致性声明，也没有核对。

**证据**：index.hpp:158,169,1277-1281,2571；GroupStats.primary_bytes/garbage_bytes 生产于 index.hpp:2609-2612。

**覆盖与缺口**：已覆盖：溢出字符串的有效字节/容量二分。尚未覆盖：主块内的对齐/尾部填充项未单列、未声明归属；无一致性断言。

**关闭标准**：在账本里显式定义对齐填充的归属（后缀区或单列余量）并在文档与码中一致；关闭标准是该归属有唯一出处且被断言。

**未能核实**：未找到任何说明对齐填充归属的文档段落（grep alignment/padding 无命中于本目录报告）。

### [A-4-19] §4.4 用记录区间排序、间隙合并独立计算活跃占用与已用区间空洞，再与水位路径比较；若垃圾只是总量减其他项，须明示为派生值，不得把同一等式重排后称为独立验证

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **尚未完成** |
| **置信度** | high |

**实现**：未实现：全树无“按偏移排序记录区间 + 合并间隙”的独立占用/空洞计算（grep interval/hole/overlap/gap_merge 无命中于本用途）。相反，garbage_bytes 正是“总量减其他项”的派生值：index.hpp:2612 `s.garbage_bytes = bb_[g] - h.bottom - s.bytes`（与 2785 同式），且未见任何地方声明它是派生值或提供独立验证。最接近的 diag_free_after_reclaim（index.hpp:2362-2368）用 Σlength 重算“回收后空闲”并对比 post-reclaim 水位，但那是假设连续、非区间合并。

**证据**：index.hpp:2585-2612,2784-2785,2362-2368；diag_migrate.cpp:536 / diag_repair.cpp:532 的“POST-RECLAIM watermark”比对。

**覆盖与缺口**：已覆盖：回收后空闲的重算式与水位自洽比较（E28/E37 用）。尚未覆盖：当前状态活跃占用的区间排序 + 间隙合并独立计算；空洞（已用区间的空）从未独立算出；派生值声明缺失。

**关闭标准**：实现独立账本：对每个主块按 offset 排序活跃记录区间、合并间隙，得到活跃占用与空洞，与 bottom 水位路径对比并断言相等；关闭标准是存在不依赖同一等式的 LIVE 校验。

**未能核实**：未排除更早轮（validation_e3..e24）里做过该独立计算；本目录内确认缺失。

### [A-4-20] §4.4 溢出空间分列：活跃描述符、容器保留容量、字符串分配、元数据；区分 SSO 字符串对象内存与额外堆分配，避免重复计费

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：已实现：diag_ov_cost.cpp:308-330 与 374-397 逐容器落盘 ov_rec_bytes（描述符=sizeof(OverflowRecord)）、ov_size_mean/max（活跃描述符数）、ov_containers 与分档、ov_suffix_sso_inline/heap（SSO 与堆字符串个数）、ov_sso_char_bytes/ov_heap_char_bytes（字符数据分列）；容器保留容量由 OverflowStore::allocated_bytes()（index.hpp:1261-1281）给出并与 memory_breakdown 一致地并入 m.overflow（index.hpp:2541-2546）。

**证据**：results/main/ov_cost_diag.jsonl；index.hpp:1261-1281,2541-2546,3652；analyze_o1.py:120-135（ov_bytes/ov_hdl_M/ov_rec_M）。

**覆盖与缺口**：已覆盖：描述符/容量/SSO/堆字符数据四列，避免了把 SSO 内联字符重复计费。尚未覆盖：容器“元数据”（如 order_/free_slots_ 表）分列不清晰（allocated_bytes 把它们并进 capacity）；证据 DRIFT。

**关闭标准**：若要更细，可把 order_/free_slots_ 的容量单列；关闭标准是元数据不再与记录容量合并。

**未能核实**：未逐项验证 ov_sso_char_bytes+ov_heap_char_bytes 与 allocated_bytes() 的溢出项在数值上严格互补（未跑对账）。

### [A-4-21] §4.4 全索引同时给出：索引自有空间、共享输入缓冲、构建临时峰值、维护临时峰值、RSS；RSS 只作单列观测，不与前几项相加

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：部分：索引自有空间有 memory()/memory_breakdown()（index.hpp:2585-2591,4269-4289），但仅 blocks/model/prefix/overflow/tables/directory/self/fences 中的少数被落盘（bench_long_o1.cpp:438-440 只写 memory/blocks/ov_bytes，mb.model 等从未输出）；RSS 由外部 precheck_memory.py:13-19,39-49 读 VmRSS/VmHWM 并用 mallinfo2 区分高水位与增长，analyze_lits.py:190-204 以“RESIDENT SPACE”单列报告，未与前几项相加。“共享输入缓冲”“构建临时峰值”“维护临时峰值”均未测量（grep input_buffer/transient/peak 无对应实现，仅 analyze_o1.py:247 的小标题提到 transient peak 但正文只对 memory/ov_bytes 做比值）。

**证据**：index.hpp:2585-2591,4269-4289；bench_long_o1.cpp:390-440；precheck_memory.py:13-19,39-49；analyze_lits.py:190-204。

**覆盖与缺口**：已覆盖：索引自有空间（粗粒度）、RSS 单列观测（外部）。尚未覆盖：共享输入缓冲、构建临时峰值、维护临时峰值；memory_breakdown 的细项未落盘。

**关闭标准**：把 memory_breakdown 全字段落盘，并补共享缓冲与两类临时峰值（可在进程外采样）；关闭标准是五项同表且 RSS 仍单列。

**未能核实**：未确认 e9_alloc/patch 相关构建是否在别处记录过构建临时峰值（只在 precheck_memory.py 见到进程级 RSS/HWM）。

### [A-4-22] §4.5 分别计算组级记录需求与字节需求的分位数、最大值及拥挤占比，给出查询加权与更新加权版本；区分“记录拥挤组”与“访问热门组”

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：部分：只有最大值与均值，无分位数、无加权、无“访问热门组”维度。check_lits_model.cpp:117-118,379-396 计 group_pop_max（最大组记录数）与 worst_group_share/mean_group_share（最大/平均组占比），落盘于 910-918；其余是 LITS 与 Context16 的对比，不是组质量分位数。

**证据**：results/main/e38_lits_model_*.jsonl（group_pop_max=1805、worst_group_share=0.881、mean_group_share=0.155）；check_lits_model.cpp:362-396,910-918。

**覆盖与缺口**：已覆盖：组记录需求的最大值与均值、拥挤占比最大/均值（LITS 轮）。尚未覆盖：记录需求与字节需求的分位数（p50/p90/p95）、查询加权与更新加权版本、“访问热门组”（按访问计数）维度。

**关闭标准**：对每个区域的组输出记录/字节需求的分位数与最大值，并按查询/更新访问频次加权，另列访问热门度；关闭标准是这些量出现在同一 LIVE 报告。

**未能核实**：未确认 check_lits_model 的 p_ge32/p_ge96_regions 是否其实是对某分布的分位口径（读为“预测占比≥阈值”，非需求分位数）；若另有用途需再核。

### [A-4-23] §4.5 对有溢出的组细分空闲来源：目标子块、同组其他子块、同区域其他组、其他区域；只有合法放置规则允许利用的空闲才计入某改造的可恢复容量

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：部分：只覆盖前两类。diag_overflow.cpp:7-13 的 F1=只回收目标子块、F2=整组重排（组预算），对应“目标子块”与“同组其他子块”；“同区域其他组”“其他区域”无对应实验——MODEL_AND_MAPPING.md:158 明确 F3（区域级）未实现、跨组与区域容量错配“未被任何一层覆盖”。合法放置规则约束在 F1/F2 的试放里体现（只在该组/该子块内回收）。

**证据**：diag_overflow.cpp:7-13,193-221；results/main/overflow_diag.jsonl（ov_F1_fits_after_tidy / ov_F2_budget_suffices）；MODEL_AND_MAPPING.md:158。

**覆盖与缺口**：已覆盖：目标子块与同组预算两来源。尚未覆盖：同区域其他组、其他区域两来源；没有任何“按放置规则过滤后的可恢复容量”汇总。

**关闭标准**：实现并只读试放 F3（区域级跨组）与“其他区域”层，逐来源计可恢复容量；关闭标准是四来源同表且只计合法空闲。

**未能核实**：未核实 E31/E37 的迁移扫描是否已隐含“同区域其他组”的容量（MigrationScan 只扫本组子块，见 index.hpp:2323-2349），按缺项记。

### [A-4-24] §4.5 不得把预测排名误差直接等同为吞吐损失；记录数均匀不保证字节需求均匀；同一组号分布在不同键长分布下形成不同存储压力

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：作为“不得等同”的负面纪律基本被遵守：MODEL_AND_MAPPING.md:140 明确“没有测量预测质量……不均里有多少该算模型的账、多少算布局的账，没有拆开”，即拒绝把预测误差等同吞吐。但正面测量（字节需求与键长分布压力）缺失：无把字节需求与键长分布关联的分析，也无“记录均匀 vs 字节不均匀”的对照。

**证据**：MODEL_AND_MAPPING.md:140；GroupStats 同时有 primary_entries/primary_bytes（index.hpp:924），但无按键长分布的对照分析。

**覆盖与缺口**：已覆盖：报告层面避免了“预测误差=吞吐损失”的等同。尚未覆盖：字节需求均匀性与键长分布关联的实测；记录拥挤与字节拥挤的分离度量。

**关闭标准**：输出同一批组的记录需求与字节需求散点/分位对照，并按键长分布分层比较；关闭标准是存在一份能显示“记录均匀但字节不均”的 LIVE 证据。

**未能核实**：仅凭本目录报告无法证明全项目从未在任何结论里把预测误差等同吞吐（只核到 MODEL_AND_MAPPING.md 的一处自我声明）。


---

## A-5  —— 架构线 §5：插入失败分层 A0–A5（12 条）

### [A-5-00] §5 采集方法（L215）：从诊断运行采集真实失败事件及其插入前快照；在独立副本中按下表试放；分类针对同一个键、同一个状态；副本创建和试放不进入正式计时，也不改变随后被计时的状态。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：以只读模拟替代副本：Region::TrialPlace（index.hpp:3518）与 Region::trial_place（index.hpp:3540，位于 #ifdef E25_DIAG，index.hpp:3504），入口 Index::diag_trial（index.hpp:4349）；E30 另加 scratch-copy 探针 e30_probe（index.hpp:4301，-DE30_TIME）。F0/F1/F2 是纯函数式计算，不复制结构、不移动记录、不改任何计数；无 Config 开关（诊断专用）。

**证据**：RESULTS_E28.md §2（"对每一次替换，在操作之前取一次只读试放"）；源码 diag_overflow.cpp、diag_transfer.cpp；原始数据 results/main/overflow_diag.jsonl（24 行，run_tag=D1）、transfer_diag.jsonl（24 行，run_tag=D1）、overflow_diag2.jsonl（28 行，E30 复跑）；构建身份 logs/build_diag_overflow.json、logs/build_diag_transfer.json（均 DRIFT）。

**覆盖与缺口**：已覆盖：真实失败事件全量采集、按 数据集×模式×检查点 分层；试放只读、不扰动计时对象（TASK_STATE.md:58 明确允许"在副本或只读模拟中完成"）。尚未覆盖：任务书表格要求六层，实际只实现 A0/A1/A3 三层试放；没有任何一层使用真正的"独立副本"；两份 diag 二进制均 DRIFT（构建于 2026-09-20 05:00:20 / 03:38:55 UTC，早于 index.hpp 最后改动 2026-09-22 12:51:49，源哈希不符；g0_build_drift.txt:75 与 :81，g0_evidence_timeline.txt:68 与 :75）。

**关闭标准**：补齐 A2/A4/A5 层试放；引用 E28/E30 结论前须在 2026-09-22 12:51 之后的 index.hpp 上重建 diag_overflow/diag_transfer 复跑，否则标注为"已被取代源码"上的证据。关闭标准：六层试放在当前源码上重跑且有原始记录。

**未能核实**：未核实 E28 当时是否存在过真正的"独立副本"实现（现盘源码只有只读模拟）；未查明 RESULTS_E28.md §6 称 transfer_diag.jsonl 为"6 条"而现盘为 24 行的成因（run_tag 全为 D1，疑为后续复跑覆盖/追加），无法从现存记录定论。

### [A-5-01] §5 分类名口径（L217）：A0–A5 是本审查新增的分类名，不覆盖 E28 的 F1/F2 定义。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：属口径记录项，无代码。记录处为 MODEL_AND_MAPPING.md:144–150（"附录：方案（SA0–SA5 / A0–A5）与本项目已有轮次的对照"），其中 :150 将 §5 插入失败分层 A0–A5 标为"已做（名为 F0–F3）"。

**证据**：MODEL_AND_MAPPING.md:150；RESULTS_E28.md §2（自述四层 F0–F3）；docs/字符串索引_架构与工程优化总执行计划_20260922.md:79（要求"与原 A0–A5 逐项对应，识别实际覆盖与未覆盖"）。

**覆盖与缺口**：已覆盖：确有 F0–F3 与 A0–A5 的对照记录。尚未覆盖：该对照把 A0–A5 六层直接等同于 F0–F3 四层，未逐项列差异（A2、A5 两层在 F0–F3 中没有对应层；A4 对应的 F3 未实现），恰恰违反本行"不覆盖"的告诫。

**关闭标准**：改写 MODEL_AND_MAPPING.md:150 的对照表为逐层映射（A0→F0、A1→F1、A2→无、A3→F2、A4→F3 未实现、A5→无），删除"已做（名为 F0–F3）"这类等价表述。关闭标准：对照表逐层给出对应与缺口，无"等价"断言。

**未能核实**：无

### [A-5-02] §5 可行性向量与互斥分类（L217）：表格记录每层的可行性向量，再以首个成功层生成互斥分类；各层成功率不可直接相加。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：向量部分实现：TrialPlace 逐事件保留 f0_fits / f1_fits / f1_rank_fits / f2_budget_suffices 与 refused_by_bytes/slots（index.hpp:3522–3537），diag_overflow.cpp:195–199 汇总为 ov_F1_fits_after_tidy / ov_F2_budget_suffices。互斥分类未实现：无任何 analyze_*.py 或 diag 输出做"首个成功层"派生。

**证据**：results/main/overflow_diag.jsonl、overflow_diag2.jsonl（含 ov_F1_rank_fits）；RESULTS_E28.md §2 表——F1、F2 两列独立计数且 F2>F1，未派生互斥分类。

**覆盖与缺口**：已覆盖：F0/F1/F2 的逐事件可行性布尔向量。尚未覆盖：A5 层向量、以首个成功层生成的互斥分类、F1 与 F2 的嵌套关系（E28 两列独立并列，F2 甚至高于 F1）。

**关闭标准**：在分析脚本中由向量派生互斥分类（A1 成功 / A1 失败但 A3 成功 / … / A5 残余）；"不可直接相加"已在 RESULTS_E28.md §2 口径写明可保留。关闭标准：给出六层互斥计数且总和等于失败事件数。

**未能核实**：无

### [A-5-03] §5 表 L221 A0 原路径：不改变状态，全部保持；要回答"是否真的是失败事件"。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：F0 实现：TrialPlace::f0_fits 及候选计数/拒绝原因（index.hpp:3522、3562–3571），复刻 put_primary 的候选顺序；put_primary 为既有放置路径。

**证据**：RESULTS_E28.md §2（先只读试放、再执行并读回是否落溢出：overflow_landed / ov_refused_by_bytes / ov_refused_by_slots）；results/main/overflow_diag.jsonl。构建 logs/build_diag_overflow.json 为 DRIFT（g0_build_drift.txt:75）。

**覆盖与缺口**：已覆盖：混合字符串/重复前缀/百万域名三数据集、same/grow 两模式、25/50/100/200% 检查点；确认失败为真事件（overflow_landed>0）。尚未覆盖：在当前源码上复跑；A0 与 E28 命名 F0 的差异未在报告中并列说明。

**关闭标准**：如要把 A0 结论作为当前源码证据，需在 2026-09-22 12:51 后的 index.hpp 上重建 diag_overflow 复跑；否则引用时注明为 E28 冻结版证据。

**未能核实**：无

> **反驳改判**：`已完成并采用` → **`部分完成`**（downgraded）
>
> 逐条核过，实现与数据都在，但"已完成并采用"不成立。 (1) 点位真实：index.hpp:3522 确有 f0_fits 声明，3562–3571 的 consider() 确在每个候选上按 slot_ok/bytes_ok 置位并统计 refused_by_slots/bytes。但 f0_fits 只在 3569 被写，全仓 grep（*.cpp/*.hpp）无任何读取点——diag_overflow.cpp 只读 refused_by_bytes/refused_by_slots（193–194 行）。把 TrialPlace::f0_fits 列作 A0 的"F0 实现"是虚指；A0 的结论实际只由 overflow_landed 与 refused_by_* 两个计数器支撑。 (2) 证据绑在已被取代的源码上：logs/build_diag_overflow.json 的 build_id 解出 2026-09-20 05:00:20，而 index.hpp 现为 09-22 12:51:49（g0_evidence_timeline.txt:6/68），g0_build_drift.txt:75 亦判 diag_overflow 为 DRIFT。更关键：被引的 results/main/overflow_diag.jsonl（09-20 11:44，24 行，确含 overflow_landed/ov_refused_by_bytes/ov_refused_by_slots）的**产出者已不在盘上**——现盘 diag_overflow.cpp 第 135 行只…

### [A-5-04] §5 表 L222 A1 本子块整理：仅回收合法目标子块的后缀垃圾，保持记录归属、槽数、块大小、映射；要回答"单块碎片是否足以解释失败"。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：F1 实现：f1_fits（put_primary 试到的任意候选）与 f1_rank_fits（rank 选中候选）——index.hpp:3523、3527、3573–3590；上位机制 E31 Config::subblock_reclaim（index.hpp:695，默认 false）+ repack_subblock（index.hpp:1685）+ first_candidate（index.hpp:1663）。

**证据**：RESULTS_E28.md §2（F1 整理后可放回 80%–95%，same 63%–92%）；E30 复核把 E28 的 f1_fits 更正为 rank 版（EXPERIMENT_INDEX.md E30 行："任意候选版与 rank 版逐格数值完全相同 92%／81%／80%"）；overflow_diag2.jsonl 含 ov_F1_rank_fits。构建 DRIFT。

**覆盖与缺口**：已覆盖：三数据集×两模式×四检查点。尚未覆盖：E31 的 subblock_reclaim 默认关闭、属候选机制，不等于 A1 诊断被采纳为默认；F1 只判"字节够且有空槽"，未覆盖键序/范围约束。

**关闭标准**：无（诊断层已闭环）；如需当前源码证据仍需复跑。

**未能核实**：无

> **反驳改判**：`已完成并采用` → **`部分完成`**（downgraded）
>
> 实现与部分证据经核实为真，但"已完成并采用"与自身覆盖栏矛盾且证据为 DRIFT。 (1) 点位与数字真实：index.hpp:3523 f1_fits、3527 f1_rank_fits、3573–3590 F1 块；diag_overflow.cpp:195–196 确实读这两个字段。overflow_diag2.jsonl 28 行，我逐行比对 28/28 行 ov_F1_fits_after_tidy == ov_F1_rank_fits，与 E30 的"任意候选版与 rank 版逐格数值完全相同 92%／81%／80%"（EXPERIMENT_INDEX.md:62、RESULTS_E30.md:12/46–48）一致；E28 的 F1 80%–95%／same 63%–92% 也能在 overflow_diag.jsonl 里复算。这些引用无误。 (2) 同样 DRIFT，且诊断代码自身在 E28 之后被改：diag_overflow 构建于 09-20 05:00，index.hpp 09-22 12:51；RESULTS_E30.md:40–42 明说 E28 那时的 f1_fits 是"对每个候选置位"，为复核才新增 f1_rank_fits。即 A1 所引的 E28 证据文件 overflow_diag.jsonl 由其产出时那份（已不在盘上的）源码给出，当前源码上无复跑（g1_rerun 无 diag_overflow）。 (3) 状态自相矛盾：状态栏写"已完成并采用"，覆盖栏却写"E31 的 subblock_reclaim 默认关闭、属候选机制，不等…

### [A-5-05] §5 表 L223 A2 相邻子块重分配：调整相邻块内记录分界与存放，保持逻辑组、合计预算、映射；要回答"是否因子块间槽位/字节/键序错配"。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **尚未完成** |
| **置信度** | high |

**实现**：未实现。index.hpp 中无"相邻子块/组内跨子块再分界"的试放层或机制：group_has_room（index.hpp:3162）只判单个子块，repack_subblock（index.hpp:1685）只在块内原地收紧；TrialPlace 也无 A2 对应布尔。MODEL_AND_MAPPING.md:156 明确"§7.2 相邻子块重分配 未做"。

**证据**：MODEL_AND_MAPPING.md:156（"需要 A2 级证据；E28 只做到 F2 判定'组预算不是约束'"）。

**覆盖与缺口**：已覆盖：无。尚未覆盖：A2 整层（相邻子块间的槽位/字节/键序错配从未被单独试放）。

**关闭标准**：实现 A2 只读试放（组内相邻子块重划分界），或对"卡字节但 F1 失败"的事件给出 A2 无对象的关闭依据。关闭标准：A2 层的可行性向量进入原始记录，或有不适用该负载的证据。

**未能核实**：我在 index.hpp（4530 行全文）检索 adjacent/neighbor/跨子块/redistribute/realloc 等关键词均未命中 A2 实现；未核实是否存在其它文件中的 A2 实现。

### [A-5-06] §5 表 L224 A3 整组重排：调整组内物理放置，保持逻辑组与该组预算、映射；要回答"小范围处理不足时，整组是否实际可行"。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：F2 实现：f2_budget_suffices——组内活跃字节+键长 ≤ 组总面积且有空槽（index.hpp:3531、3558），是总量必要条件而非真实重排。真实整组重排机制见 E29 C3 Config::compact_repack_budget（index.hpp:683，默认 false）与既有 compact()。

**证据**：RESULTS_E28.md §2（F2 97%+ 满足）；RESULTS_E28.md §2"口径"自述"F2 判的是必要条件…不是数学最优上界"；results/main/overflow_diag.jsonl 的 ov_F2_budget_suffices。构建 DRIFT。

**覆盖与缺口**：已覆盖：必要条件级的整组可行性向量。尚未覆盖：真实整组重排的可行性（任务书 L228 明示必要条件≠真实可放置）；键序/范围约束；当前源码复跑。

**关闭标准**：对 F2 判预算不足的 ~3% 及 F1 失败的残余事件做真实整组重排试放（或等价可行性算法），把"必要条件"升级为"实际可放置"。关闭标准：A3 的判定由必要条件改为实际重排结果。

**未能核实**：无

### [A-5-07] §5 表 L225 A4 区域内容量再分配：在本区域的组间转移物理预算，保持区域总主块预算与逻辑映射；要回答"是否为跨组容量错配"。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **尚未完成** |
| **置信度** | high |

**实现**：未实现（对应 F3 未实现）。index.hpp 无区域级或跨组试放代码，TrialPlace 无跨组字段。

**证据**：RESULTS_E28.md:40、:78、:116（"F3 未实现——F2 已判定预算够用时它没有对象"）；MODEL_AND_MAPPING.md:158（"F3 本身未实现…跨组与区域级容量错配未被任何一层覆盖"）。

**覆盖与缺口**：已覆盖：仅在 F2 判定预算够用的子集上论证"无对象"。尚未覆盖：F2 判定预算不足的 ~3% 子集——那正是 A4 起作用的情形，MODEL_AND_MAPPING.md:158 自认未分类。

**关闭标准**：对 F2 预算不足的事件做区域级试放（或在其中证明 A4 也无对象）；不能以"F2 够了"替 A4 关单。关闭标准：A4 层的可行性向量进入原始记录，或对全部预算不足事件给出不适用证据。

**未能核实**：无

### [A-5-08] §5 表 L226 A5 仍失败或未判定：不直接扩容掩盖问题，保持记录失败原因；要回答"单键过长、实现限制、总量必要条件失败，还是规划器未找到方案"。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **尚未完成** |
| **置信度** | high |

**实现**：未实现独立 A5 层。trial_place 只有单一布尔 f2_budget_suffices（index.hpp:3531），无失败原因四分类；diag_overflow 不输出残余/未判定计数。（index.hpp:4206–4209 的 reason_admission/degenerate/boundary/unsupported 属 LITS 模型适配器的拒绝原因，与 A5 放置失败分类无关。）

**证据**：MODEL_AND_MAPPING.md:158（"F0–F3 实际覆盖的约束只有两条… 跨组与区域级容量错配未被任何一层覆盖"）；RESULTS_E28.md §2 无 A5 残余列。

**覆盖与缺口**：已覆盖：无。尚未覆盖：A5 整层（残余计数与原因标签）。

**关闭标准**：在诊断输出中增加 A5 残余计数与原因标签（见下条四情形）。关闭标准：每一次不被 A0–A4 解释的失败都被分入 A5 并带原因。

**未能核实**：无

### [A-5-09] §5 L228 判定纪律：必要条件通过不等于真实可放置；总字节够不保证每个子块满足槽位与范围约束；正式"可恢复"标签须由实际安全放置或与实现等价的可行性算法确认。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：口径已写入记录，无代码强制。RESULTS_E28.md §2"口径"、RESULTS_E29.md §5、MODEL_AND_MAPPING.md:158 均写明 F2 只是必要条件。

**证据**：RESULTS_E28.md:77（"F2 是某个具体判定的可行性结果，不是数学最优上界"）；MODEL_AND_MAPPING.md:158；RESULTS_SUMMARY.md §3.4 行（"必要条件不等于已证明可放置，余下 ~3% 未分类"）。

**覆盖与缺口**：已覆盖：口径文本已在三处记录。尚未覆盖：尚无与实现等价的安全放置/可行性算法来给出"可恢复"标签（依赖 A4/A5，均未做）。

**关闭标准**：落地一个与实现等价的可行性判定或实际重排试放后，才可对事件打"可恢复"标签；在此之前所有可恢复数字须保留"必要条件"限定。关闭标准：可恢复标签由实际放置或等价算法产出。

**未能核实**：无

### [A-5-10] §5 L230：A5 必须区分四种情形——必要条件已违反、已证明不可行、搜索未找到、当前实现不支持。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **尚未完成** |
| **置信度** | high |

**实现**：未实现。index.hpp 无四情形枚举/标签；诊断只有单一必要条件布尔 f2_budget_suffices（index.hpp:3531）。

**证据**：无实现记录可引；缺口见 MODEL_AND_MAPPING.md:158（F0–F3 只覆盖两条约束）。

**覆盖与缺口**：已覆盖：无。尚未覆盖：四种情形均未区分。

**关闭标准**：为 A5 增加四类互斥标签（必要条件已违反 / 已证明不可行 / 搜索未找到 / 当前实现不支持）并在 diag 输出中按类计数。关闭标准：四类标签有源码与原始记录。

**未能核实**：无

### [A-5-11] §5 L232 采样纪律：采样如需限量，应按数据集、检查点、失败类型及组压力分层并记录抽样率；不能只保留容易恢复的事件来计算收益。

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **条件未满足，已按任务书关闭** |
| **置信度** | medium |

**实现**：E28 未限量、全量试放（"对每一次替换"），故"如需限量"的进入条件未触发；输出中无抽样率字段。分层维度为 数据集×模式×检查点（overflow_diag.jsonl 的 kind/mode/frac）。

**证据**：RESULTS_E28.md §2（"对每一次替换…取一次只读试放"）；results/main/overflow_diag.jsonl、transfer_diag.jsonl（无 sample_rate 字段，全量行）。

**覆盖与缺口**：已覆盖：全量事件（抽样率隐含 100%），且未只保留易恢复事件（同时报告 overflow_landed 与各类拒绝计数）。尚未覆盖：未按"失败类型/组压力"显式分层（jsonl 仅有 ov_avg_group_garbage 等压力代理）；无抽样率字段。

**关闭标准**：若后续改为限量采样，须补 数据集/检查点/失败类型/组压力 四维分层与抽样率字段；当前全量采集无需补。关闭标准：维持全量采集，或采样时带抽样率。

**未能核实**：未找到任何"抽样率"字段以确证从未限量（仅据 E28 报告"全量"与 jsonl 无该字段推断）；未审计 E30 的 overflow_diag2 复跑是否改变过采样口径。


---

## A-6  —— 架构线 §6：目标子块内的后缀整理（13 条）

### [A-6-00] §6.1 改造目标与范围：新增内部能力 compact_subblock(group, block)，只整理插入目标子块；第一版不跨子块移动原有记录、不调整组号、不增加主块预算、不自动迁回既有溢出。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成，负结果或条件性候选** |
| **置信度** | high |

**实现**：已实现，命名不同：Region::repack_subblock(g, blk)（index.hpp:1685-1710）把该子块活跃后缀按偏移递减序原地收紧到块尾，只改 Entry::offset 与 Header::bottom；候选由 Region::first_candidate(g, s) 给出（index.hpp:1663-1675）。开关 Config::subblock_reclaim 默认 false（index.hpp:695；switches.hpp:46/69/87/118 四处登记齐）。两处调用点：index.hpp:3262（place_with_ctx）与 index.hpp:3345（insert_slow），均为 put_primary 失败后收紧再重试。不跨子块/不改组号/不加预算：repack 只动 header(g, blk) 一个块、bb_ 不变。既有溢出迁回是另一个开关 overflow_migrate（E37，默认 false），与 §6.1 无耦合。

**证据**：源码身份 LIVE：index.hpp:1685/1663/695。实现轮证据 RESULTS_E31.md §1「实现」与 §9「原始数据」；门与计时构建记录 logs/build_check_e31.json、logs/build_bench_long.json、logs/build_bench_long_art.json；构建身份 DRIFT（g0_build_drift.txt 列 check_e31 8/10、bench_long 8/10、bench_long_art 9/11，changed index.hpp,switches.hpp）；唯一 LIVE 的是 logs/build_san_check_e31.json（14/14）。正式 run_id：results/main/long_e31_{same,grow}.jsonl、long_e31art_{same,grow}.jsonl。

**覆盖与缺口**：已覆盖：same/grow 两模式 × 3 数据集（混合/重复前缀/百万域名）× 4 检查点的长期替换负载，机制确实被执行（check_e31 断言 subblock_reclaims>0）。未覆盖：offset_directory=false 的定长布局（E31 §8.4 自陈"树里没有任何配置会走到那里"）、跨子块重划、并发、构建时间。

**关闭标准**：机制已实现且端到端测完；关闭标准只剩采纳决定（RESULTS_E31.md §7 把决定留给"目标负载是否为变长键长期替换"）。

**未能核实**：未重跑门或重算 jsonl（只读审计）；未核对该块收紧在 bb_>65535 时 h.bottom 的 uint16_t 截断（现有配置不可达）。

> **反驳复核**：维持原判（stands）。

### [A-6-01] §6.1 原有整组维护作为明确的后续回退路径保留，但每次是否进入、花了多少工作必须单独计数；不得把"先局部整理、失败后又整组整理"的叠加成本漏掉。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：整组路径的"是否进入"有计数：compact_entered/compact_helped/compact_no_help/compact_records_replayed/compact_bytes_copied（index.hpp:258-263，字节在 index.hpp:1905 累加）；子块路径有 subblock_reclaims/subblock_reclaim_helped（index.hpp:268-269，自增 index.hpp:3263-3266 与 3346-3349）。调用顺序保证两段都被计：index.hpp:3340-3359 与 3260-3298 先试 subblock_reclaim，失败才走 worth_compacting→compact。但子块收紧"花了多少工作"没有任何字节/记录计数器（repack_subblock 内无 E25_ADD）。

**证据**：index.hpp:258-269/1905/3263-3266/3346-3349/3358-3359；RESULTS_E31.md §2.2（触发 127,780–255,882 次、促成 20.4%–67.2%）；logs/check_e31_{mixed,skew,tranco}_{same,grow}.log 与 *_checklist.log。

**覆盖与缺口**：已覆盖"是否进入"与"促成与否"；未覆盖"花了多少工作"（子块侧无计数）与"同一次插入两段合计"的叠加量。关键口径：E31 §8.7 与 RESULTS_E32.md §2 都写明计时构建（bench_long / bench_long_hyst，无 -DE25_DIAG）里这些计数被编译掉，端到端运行取不到。

**关闭标准**：若要关闭：给 repack_subblock 加"复制的活跃字节/实际回收字节"计数（可只在 -DE25_DIAG 下），并报告"局部整理 + 随后整组压缩"在同一次插入上的合计工作量。

**未能核实**：未逐条核是否有其它维护路径（如删除触发的维护）绕过这些计数。

> **反驳改判**：`已完成，负结果或条件性候选` → **`部分完成`**（downgraded）
>
> 此行整条命题就是一个量化对照，而它引用的全部量化证据都绑在已被取代的源码上，当前树上没有任何一次重测。(1) 构建身份：build_bench_long.json 与 build_bench_long_art.json 的 index.hpp=61b04f19a38be71339b4995b296531f19c7ffab6b7515aa1525e39c7350debc5，当前 index.hpp=587172d2a6c970b93b7b09da4c229bad285cf86afb29ca0a32cc142124759018；g0_build_drift.txt 第 33-34 行明列 bench_long 8/10、bench_long_art 9/11，changed index.hpp,switches.hpp。(2) 这两个是这个检验命题的唯一端到端装置（grep 全树，bench_long/bench_long_art 没有 san_ 对应物，也无 g1 重测日志；g0_inherited_gates_rerun.txt 只重跑了 check/check_e8/e15/e16/e19/e20/e9/e11 八个门，不含任何计时程序）。(3) 数字本身与报告一致（e31_analysis.txt 的 E31/C2 200%：mixed 0.980、skew 1.018、tranco 0.471；e31art_analysis.txt grow mixed 1.00=1.062），所以问题不是算错，而是按任务纪律『DRIFT 的门/记录不能作为当前源码的证据』『历史绝对耗…

### [A-6-02] §6.1 检验命题：能否把"为了一个插入而重放整个组"改成"只复制目标块内的活跃后缀"；尚不能承诺它一定胜过冻结版。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成，负结果或条件性候选** |
| **置信度** | high |

**实现**：即行 1 的机制；端到端对照由 bench_long.cpp（四臂 C1/C2/C3/E31）与 bench_long_art.cpp（五臂 + ART，臂序按重复轮换）执行。

**证据**：RESULTS_E31.md §0/§3/§4/§5；results/main/long_e31_{same,grow}.jsonl、long_e31art_{same,grow}.jsonl；analyze_e31.py / analyze_e31art.py / analyze_e31art_programs.py → results/main/e31_analysis.txt、e31art_analysis.txt、e31art_program_compare.txt。构建身份 DRIFT（bench_long 8/10、bench_long_art 9/11，changed index.hpp,switches.hpp）。

**覆盖与缺口**：已覆盖 same 模式：耗时 E31÷冻结 = 0.980/1.018/0.471（百万域名 5/5 快 2.1×–2.7×），另两格两次独立运行符号相反、按噪声报；grow 模式：混合 100% 1.082/1.062（两次运行都 0/5，一致更慢 4%–8%）、重复前缀约 +2%、百万域名 0.826（5/5）。未覆盖：构建时间、扫描、并发；grow 混合的 2%–8% 代价未分摊到具体来源（E31 §8.7 只有机制侧触发/命中计数）。

**关闭标准**：采纳与否是负载相关的决定，报告不替用户决定（RESULTS_E31.md §7）。

**未能核实**：未打开 jsonl 逐条重算比值；未做任何跨轮比值相乘或拼接（遵守纪律）。

> **反驳复核**：维持原判（stands）。

### [A-6-03] §6.2 定义 F（当前可直接分配的连续后缀字节）、G（本块整理可回收字节）、R（新增存储需求）、S（可用槽位），触发为"键允许进入且 S>0 且 F<R 且 G≥R−F"；对齐/变长条目下改用 free_after_compact≥R；不得使用"垃圾字节≥完整键长"。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：判据未实现。E31 的触发是"put_primary 失败 → 对 first_candidate 子块 repack_subblock → 重试 put_primary"（index.hpp:3340-3352 与 3260-3269），源码中不存在任何 F/G/R/S 量或 G≥R−F 比较；容量判定完全委托给重试时的 insert_into（index.hpp:1632-1633，place_fail_slots / place_fail_bytes 分开计）。项目自己的对照表承认"§6.2 触发条件：已实现，但形式不同"（MODEL_AND_MAPPING.md:152）。

**证据**：index.hpp:3345（守卫只有 config_.subblock_reclaim && reclaim_allowed(g)，无容量前置）、index.hpp:1632-1633；MODEL_AND_MAPPING.md:152；E30 把"该不该实现"的门槛实测为 ≤70 ns/记录、实测原地收紧 6.1–11.4 ns/记录（RESULTS_E30.md §0，"不实现任何机制但把该不该实现判决了"）。

**覆盖与缺口**：已覆盖：被禁的"垃圾≥完整键长"判据在子块层确实未被使用（也没有任何等价替代）；容量不足由重试真实判定，功能上等价于"整理后 free≥R"。未覆盖：F/G/R/S 四个量从未被作为可观测量测过，因此"E31 从不因错误判据拒绝本可成功的插入"是推理而非测量；任务书给的具体算例（R=156/F=120/G=40）没有任何构造。

**关闭标准**：若要关闭：或在源码/注释里明确"容量判定由重试等价承担"并补一个该算例的事件计数断言，或实现显式 free_after_compact≥R 谓词并测。

**未能核实**：全目录未找到任何"先算 F/G/R 再判"的实现或实验记录（grep F+G / free_after_compact / compact_subblock）。

> **反驳改判**：`已完成，负结果或条件性候选` → **`部分完成`**（downgraded）
>
> 行内断言分成两半：机制侧我核过属实，但把整行闭合为『负结果』的那一步只有 DRIFT 证据。(1) 属实的一半：reclaim_hysteresis/reclaim_skip_repeat 默认 false（index.hpp:705/712），reclaim_gated()/reclaim_allowed()（3225-3247）确实分别实现『间隔且垃圾未增长』与『只看垃圾增长』，间隔常量 kMinReclaimGap=64（1324），粒度按组（reclaim_last_op_[g]/reclaim_last_garbage_[g]，1316 行邻域），未缓存『这个块失败过』；bench_long_hyst.cpp:169-171 用 require_only_switches_differ 分别钉住 C2↔E31(subblock_reclaim)、E31↔E31H(reclaim_hysteresis)、E31↔E31P(reclaim_skip_repeat)，臂序真轮换（bench_long_art.cpp:206-209 的 order[]={r%5,...}，我在 grep 失败后打开源码确认）。这些都在当前源码上成立。(2) 不成立的一半：『两变体都更差』是纯计时结论，其唯一装置是 long_hyst_same.jsonl（900 行，臂 C2/E31/E31H/E31P/ART 各 180）与 e32_analysis.txt，全部来自 build_bench_long_hyst.json（index.hpp=9d6f0f100cdaa158，g0_bui…

### [A-6-04] §6.2 这里的垃圾必须来自同一目标子块，不能借用全组或全索引垃圾来通过判断；若允许多个合法候选块，枚举范围须遵守现有范围不变量。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成，负结果或条件性候选** |
| **置信度** | high |

**实现**：满足。repack_subblock(g, blk) 只处理 header(g, blk) 一个块（index.hpp:1685-1710），回收的是该块 occupied − 活跃字节；候选由 first_candidate（index.hpp:1663）给出。我逐行比对 first_candidate 与 put_primary（index.hpp:1807-1832）确认两者枚举一致：都跳过空子块、都以"最后一个 max<s 的非空子块"为 prev、都在 s 落在该块下界之前先试 prev、都在所有块更小时从 prev 起向后找。不枚举多候选，因此不存在"哪里有空位就放过去"。

**证据**：index.hpp:1663/1685/1807-1832；RESULTS_E31.md §1（"prev 正是 put_primary 第一次试的那个块…按构造对齐"）；RESULTS_E30.md §2（rank 限制版与任意候选版在每一格数值完全相同：92%/81%/80%，"E28 的说法现在是被测过的"）。

**覆盖与缺口**：已覆盖：候选按构造对齐，垃圾只来自目标块。未覆盖：没有"逐子块字节守恒"断言——check_e31.cpp 全文 251 行里没有 occupied/suffix_live 比较，即没有直接门证明回收没把别块字节算进来（check_e31 只复核边界目录与内容）。

**关闭标准**：若要彻底关闭"不借用别块容量"：加一条逐子块 occupied/活跃字节在回收前后守恒、且只有目标块变化的断言（与行 12 同一动作）。

**未能核实**：first_candidate 与 put_primary 的一致性只做了源码级比对；E30 有运行时证据但那属另一轮、未在本轮重跑。

### [A-6-05] §6.3 准备—验证—提交：建议第一版用一个块大小的临时缓冲换取清晰的异常安全（临时分配与复制计入完整维护成本），并给出"定位判重→选候选块→原路径可放下则直接插入→否则若本块整理条件成立→读活跃描述符验证边界→临时缓冲紧凑复制→保留键值/标签/逻辑槽序算新偏移→验证新布局能容纳原记录及本次新增需求→提交新布局并刷新地址相关引用→重取失效位置重试→仍未成功走既有回退"的流程。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：未用临时缓冲：E31 是原地收紧（递减偏移序 + memmove 自重叠，index.hpp:1692-1709），理由写在 index.hpp:1677-1684 与 RESULTS_E31.md §1。项目自己的对照记为"§6.3 准备—验证—提交（临时缓冲）：未做"（MODEL_AND_MAPPING.md:153）。流程各部分落点：定位判重由调用方完成（index.hpp:3307-3319）；选候选块 first_candidate；原路径 put_primary 先行（index.hpp:3340）；"本块整理条件成立"这一步缺失（见行 4）；"验证边界"只读 h.count 不显式验证；"验证新布局能容纳原记录及新增需求"由重试的 insert_into（index.hpp:1632-1633）承担；"提交并刷新地址引用"以原地改 offset + 不重建目录实现；"重取失效位置重试"由 put_primary 重试承担；"失败走既有回退"由 worth_compacting→compact→put_overflow 承担（index.hpp:3358-3372）。

**证据**：index.hpp:1677-1710、3255-3302、3329-3372、1632-1633；RESULTS_E31.md §1/§2；MODEL_AND_MAPPING.md:153。

**覆盖与缺口**：已覆盖：流程的功能结果（内容与键序逐条一致、边界目录不需重建且被复核）。未覆盖：临时缓冲变体本身（未实现、未测，因此"异常安全"靠论证 + 内容比对而非该变体）；"读取本块活跃描述符，验证边界"未作为独立步骤存在。

**关闭标准**：若要关闭：实现临时缓冲变体并与原地版对照（含异常路径），或明确记录"原地版替代临时缓冲版"并给出等价失效分析。

**未能核实**：未验证 memmove 自重叠在极端键长分布下无 UB；san_check_e31（LIVE）覆盖 mixed same/grow/tranco，但那是 E33 的运行，不是本行的验证。

### [A-6-06] §6.3 六条实现注意事项：①沿用 C1 标签复用、不因纯搬运重新训练或预测；②保持逻辑槽序、尽量避免额外描述符重排；③边界键若以指针/视图保存必须刷新、不能因键内容不变就忽略目录；④Located 内槽位/后缀引用/溢出迭代器按各自失效规则处理、不把"本次公开操作内"当永远有效；⑤零长度后缀/极长后缀/恰好填满/偏移窄类型溢出均需明确处理；⑥准备失败保留旧状态、提交后仍失败要报原因、不把"不满…

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：①E31 路径不做训练/预测/哈希重算，标签随描述符不动（index.hpp:1685-1710）；C1 开关 compact_reuse_tag（index.hpp:659，E26）。②描述符不移动（RESULTS_E31.md §1）。③首末条目保持原槽位、边界目录不重建（index.hpp:1677-1684），并由 check_e31 的边界目录逐组复核兜住（check_e31.cpp:89-113、229）。④E31 §1 称"边界不变，调用方持有的东西不失效"，Located 创建于同一次 insert 内。⑤repack_subblock 对 n<2 直接返回（index.hpp:1688），零长 memmove 安全；"恰好填满"未特别处理；"偏移窄类型溢出"未处理（bottom 为 uint16_t，index.hpp:915/1709）。⑥原地版准备阶段不会失败；重试仍失败则计 compact_no_help 并落溢出（index.hpp:3279-3297）。

**证据**：index.hpp:1685-1710、659、915、1709、3279-3297；check_e31.cpp:89-113/229；RESULTS_E31.md §1/§2.2/§5/§8.4。

**覆盖与缺口**：已覆盖 ①②③⑥ 与 ⑤ 的零长/单条情形；③④ 另有间接支持（check_e31 边界目录复核；E31 §5a 两臂 query_acc 不一致格 0/360、五臂 0/600）。未覆盖：⑤ 的"偏移窄类型溢出"（E31 §8.4 明说 offset_directory=false 路径不可达、未检验）、"极长后缀"与"恰好填满"无专门用例；④ 没有针对 Located/溢出迭代器失效的专门断言。

**关闭标准**：若要关闭：补 ⑤ 的窄类型/极长/恰好填满用例，补 ④ 的失效断言（在整理路径后使用先前取得的 Located 并断言其行为）。

**未能核实**：未逐条核 ①–⑥ 是否有独立门（多数只在源码层成立）；bb_=1024 下"极长后缀"能否出现未核。

### [A-6-07] §6.4 首轮先报告三个比值：整理成功率＝整理后本次插入成功数/整理次数；单位回收复制量＝复制的活跃字节总数/实际回收字节总数；维护复制负担＝所有维护复制字节/完成的更新操作数；分母为零写"不适用"；另报告最大及高分位的单次搬移工作量。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：只实现了第一个。整理成功率 = subblock_reclaim_helped/subblock_reclaims（index.hpp:268-269）。"单位回收复制量"不存在——子块收紧没有任何字节计数器（repack_subblock index.hpp:1685-1710 无 E25_ADD，counts 里也无 reclaim 字节字段）。"维护复制负担"未为 E31 报告：compact_bytes_copied（index.hpp:263，累加 index.hpp:1905）是整组压缩的字节数，只在 analyze_e26.py:89-108 用过，E31 未用，且计时构建关掉 -DE25_DIAG 后取不到（E31 §8.7）。分位数：E32 §3 量到的是"维护单操作"的 p99/p99.9（28–45 µs / 70–86 µs），不是"单次搬移工作量"的分位数。

**证据**：RESULTS_E31.md §2.2 表（成功率 22.4%–67.2%）；index.hpp:263/268-269/1905；analyze_e26.py:89-108；RESULTS_E32.md §3；RESULTS_E31.md §8.7。

**覆盖与缺口**：已覆盖：整理成功率（六格的次数与促成数，200% 检查点）。未覆盖：单位回收复制量、E31 口径的维护复制负担、最大及高分位单次搬移工作量；"分母为零写不适用"未触发。

**关闭标准**：若要关闭：给 repack_subblock 加"复制的活跃字节/实际回收字节"计数器并在 -DE25_DIAG 门里报告；给出单次搬移工作量的最大值与高分位。

**未能核实**：未确认所有分析脚本都没间接算出这两个比值（只查了 analyze_e31*.py 与 analyze_e26.py 的字节列）。

### [A-6-08] §6.4 首轮不同时增加节流；只有出现"同一布局状态反复进行无收益尝试"才引入独立节流变体；失败缓存不能简单缓存"这个块失败过"，也不先拍定"每 100 次操作才整理一次"。

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **已完成，负结果或条件性候选** |
| **置信度** | high |

**实现**：首轮确无节流：reclaim_hysteresis / reclaim_skip_repeat 默认 false（index.hpp:705/712），E31 臂只差 subblock_reclaim 一个开关。进入条件在 E31 数据里成立（触发 12.8万–25.6万次、促成仅 20.4%–67.2%）→ E32 做了两个独立变体：E31H＝间隔且垃圾增长则跳过、E31P＝只看垃圾增长（index.hpp:3216-3247）；间隔用 kMinReclaimGap=64（index.hpp:1324），不是拍定的 100。粒度是按组，未缓存"这个块失败过"。

**证据**：RESULTS_E31.md §2.2（无条件触发的命中率）；RESULTS_E32.md §2（两变体都更差：E31P÷E31 百万域名 2.31–2.63、0/5；E31H 在百万域名 25% 比冻结版还慢 16.6%；机制＝跳过一次回收→记录留溢出→该组垃圾不增长→在已测序列里不再恢复）；index.hpp:705/712/1324/3225-3247。

**覆盖与缺口**：已覆盖：条件判定、两个变体的实现与同轮配对测量（bench_long_hyst 五臂，构建身份 DRIFT 9/11）。未覆盖：变体也未进默认（负结果）；"跳过是否永久"没有跟踪计数器（E32 §2 自陈）。

**关闭标准**：无——节流方向已被实测否掉（RESULTS_E32.md §2），不再重做同一试验。

**未能核实**：E32 未跟踪"某组被跳过后又恢复允许尝试"的事件，因此"永久跳过"只是从计数推断，报告自己已收紧该措辞。

### [A-6-09] §6.4 失败缓存至少要考虑布局版本、失败类型和所需空间；大键失败不能阻止后来更小的键尝试。

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：部分实现。reclaim_allowed（index.hpp:3229-3247）用"布局版本"＝该组上次尝试时的垃圾字节 reclaim_last_garbage_（index.hpp:1314）与"间隔"＝placements_since_ 两个量；**没有**考虑失败类型或所需空间。粒度是按组而非按块，所以"这个块失败过"确实没被简单缓存。"大键失败不能阻止更小的键"：判定按组且只看垃圾是否增长，一次大键触发（并写入 last_garbage_）后，间隔内同组的较小键会被跳过——这一点没有断言或反向用例。

**证据**：index.hpp:3229-3247、1314、1324；RESULTS_E32.md §2；RESULTS_E31.md §8.7（"回收可能跳过一次本来会跑的压缩"只有源码推断，没有计数器）。

**覆盖与缺口**：已覆盖"布局版本"这一维。未覆盖：失败类型、所需空间两维；大键→小键的阻塞行为未构造、未观测；E32 未报告按失败键长分层的跳过率。

**关闭标准**：若要关闭：补"所需空间/失败类型"维度并测其收益，或明确记录"该变体已因整体负结果停止、不再补维"并给出停止依据（须写明，不能默认省略）。

**未能核实**：大键失败阻塞小键的行为未构造也未观测。

### [A-6-10] §6.5 必须专门构造：F≥R、G<R 但 F+G≥R、精确等于、字节仍不足、槽位满、只有别块有垃圾、删除首末条目、空中间块、同标签碰撞、重复失败、整理后更新与扫描、失败注入。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：未作为专用用例集构造。check_e31.cpp（全文 251 行）只跑"长期替换"负载（删除一个活跃键 + 插入一个不同键，same/grow 两模式，3 数据集 × 4 检查点，check_e31.cpp:174-203），没有合成小索引、也没有按上述分支逐个构造状态。相关状态在别处被顺带构造：删除首末与"清空中间子块"由扫描契约门构造（check_scan_contract.cpp:189-279、断言 :767 "no middle sub-block was emptied…"），但那是为"扫描必须按组号升序"的契约（E33，见 MODEL_AND_MAPPING.md:110/122），不是为回收分支、也没有回收分支的事件计数。字节不足/槽位满两个计数器存在（place_fail_bytes/place_fail_slots，index.hpp:1632-1633）但只在整个负载里累计。

**证据**：check_e31.cpp 全文；check_scan_contract.cpp:189-279/767；MODEL_AND_MAPPING.md:110/122；index.hpp:1632-1633。

**覆盖与缺口**：已覆盖（负载级，非用例级）："整理后更新与扫描"——check_e31 每检查点全量扫描逐条比对，计时程序每臂每检查点跑固定 16,384 键查询且两臂 query_acc 全同（E31 §5a，四臂不一致格 0/360、五臂 0/600）。部分/间接："删除首末条目"与"空中间块"由 check_scan_contract 构造（另属扫描契约）。未覆盖：F≥R、G<R 但 F+G≥R、精确等于、字节仍不足、槽位满、只有别块有垃圾、同标签碰撞、重复失败、失败注入——未找到任何为这些状态专门构造的用例。

**关闭标准**：若要关闭：建一个合成小索引的用例门，逐条构造上述状态并用事件计数断言分支触发（尤其"只有别块有垃圾"与"精确等于"）。

**未能核实**：我未逐行读完 51 个 .cpp，只按关键字（用例/empty middle/collision/inject/F>=/borrow）扫过并读了 check_e31 全文；若某个 diag_*.cpp 里有命名隐蔽的构造，可能被我漏掉。

### [A-6-11] §6.5 每个用例用事件计数证明分支真实触发，并以独立参考表核对结果；既要保证内容正确，也要保证不借用别块容量。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：事件计数：check_e31.cpp:223-232 在 -DE25_DIAG 下读 subblock_reclaims 并在最后一个检查点断言 >0，源码用 #error 防止漏宏（check_e31.cpp:227），否则抛异常。参考表：std::map Reference（check_e31.cpp:53）+ 逐条比对（:211-218），同时与冻结臂逐条比对（两臂同错不算通过）。"不借用别块容量"：没有专门断言——只做了边界目录逐组复核（check_e31.cpp:89-113/229），无逐子块字节守恒断言。

**证据**：check_e31.cpp:53/89-113/211-232；logs/check_e31_{mixed,skew,tranco}_{same,grow}.log 与 *_checklist.log；RESULTS_E31.md §2.1/§2.2。

**覆盖与缺口**：已覆盖：分支真实触发（断言而非"最终内容一致"推断）、内容与键序逐条对参考表与冻结臂、边界目录一致性（两臂都查）。未覆盖："不借用别块容量"（无任何逐块字节守恒断言，grep occupied/suffix_live 在 check_e31.cpp 无命中）。

**关闭标准**：若要关闭：补"每个子块的 occupied 与活跃字节在回收前后守恒、且只有目标块变化"的断言。

**未能核实**：断言是"整轮负载里 subblock_reclaims>0"，不是"每个用例都触发过"；没有逐用例的分支覆盖证据。

### [A-6-12] §6.5 归因边界：新方案相对 C3 同时改变了触发判据和整理范围，端到端差值是组合效应；若要单独归因"粒度"，用相同快照中两种方案均可行的事件作成对诊断，并单列不重合事件；不把该筛选后的诊断当成完整负载成绩。

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **尚未完成** |
| **置信度** | medium |

**实现**：未实现成对诊断。E31 §3.4 给的是 C3 与 E31 同轮同进程的端到端比值（12 格，C3 一致慢 1.46×–6.02×、E31 在 same 上无一格一致劣化），报告全文没有把"粒度"从"触发判据"里分出来；MODEL_AND_MAPPING.md:162 也只把 BASE/GROUP/LOCAL 映到 C2/C3/E31 三臂，没有"相同快照中两方案均可行事件"的成对诊断产物。

**证据**：RESULTS_E31.md §3.4/§0 结论 2；MODEL_AND_MAPPING.md:162；未找到对应的 diag_*/results/main 产物。

**覆盖与缺口**：已覆盖：C3 vs E31 的端到端（组合效应本身）。未覆盖：相同快照下两方案均可行事件的成对诊断、只在一侧可行事件的单列。

**关闭标准**：若要关闭：在相同快照上对 C3 与 E31 各判一次可行性，取"两者均可行"的事件做配对诊断，单列只在一侧可行的事件，并注明该筛选结果不是完整负载成绩。

**未能核实**：只在 RESULTS_E31/E33–E38 与 results/main 下按关键词找过，未穷尽每个 diag 程序；不排除以不明显命名存在。


---

## A-7  —— 架构线 §7：六个条件改造分支（11 条）

### [A-7-00] §7.1 有界迁回机制：实现 drain_overflow_bounded——按子块键范围在有序溢出中定位候选（不每次遍历全部溢出）；明确每次最多检查多少候选/复制多少字节/迁回多少条，跳过放不下的也耗预算；先准备放置再一致更新主块、溢出与计数（失败不丢不重复）；不跨逻辑组；首版只选一种触发位置。

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：已实现。Config::overflow_migrate（index.hpp:745，默认 false，switches.hpp 四表全登记）；Region::drain_overflow_bounded（index.hpp:1725）、Region::drain_all_bounded（index.hpp:2522）、Index::drain_all_bounded（index.hpp:4340）；预算常量 kMigrateBudget=64（index.hpp:1714）；单趟压缩移出 OverflowList::erase_positions（index.hpp:1202）；触发点＝放置失败路径（index.hpp:3287、:3367）；预扣容量修复见 index.hpp:1764-1790。门 check_migrate.cpp，计时 bench_migrate.cpp，诊断 diag_repair.cpp/diag_migrate.cpp。

**证据**：源码身份：index.hpp 最后改动 2026-09-22 12:51，而该轮构建于 09-21 → 构建记录 check_migrate/bench_migrate/diag_repair/diag_migrate 均为 DRIFT（g0_build_drift.txt 的 DRIFT 段）。轮次报告 RESULTS_E37.md §6–§10；原始数据 results/main/migr_{same,grow}.jsonl（6240）、migr_diag_{same,grow}.jsonl、migrate_diag_{same,grow}.jsonl、repair_diag_{same,grow}.jsonl、e37_scan_migr.jsonl；分析 analyze_migr.py/analyze_migrate.py，驱动 run_migr.py/run_migrate.py/run_repair.py；FINAL_RESULTS_TABLE.md 记 bench_migrate P1–P5；check_migrate 三数据集 pass。

**覆盖与缺口**：已覆盖：三数据集 × same/grow；正确性（check_migrate 点查 63.9 万×2＋465.3 万、扫描 4,096、记录 131,072，两臂各自与参考表一致；非空性断言 migrate_calls>0、migrated_records>0）；落地率 92%–99.7%（退回 12–164）；溢出 −46.3%–−91.2%；常驻空间 0.657–0.924（5/5）；点查 R1/B 0.571–0.714（六格全 5/5）；尾延迟 p99 1.055–4.469（六格全 0/5）。尚未覆盖：预算耗尽的边界用例、长键跳过的直接用例；触发位置只实现了“放置失败”一种，任务书列的删除后/成功整理后/检查点维护三种均未选；无并发。

**关闭标准**：关闭或保留 R1 前，先做“只扩大回收触发面、不迁回”的臂，把机制效果从迁回头上剥离（RESULTS_E37.md §10 自认这是下一轮、本轮未做）；若保留则须给出尾延迟可接受的负载边界。

**未能核实**：门里是否有“预算耗尽 / 长键跳过 / 分配失败”的单独用例（RESULTS_E37 只报落地率与非空性）；“首版只选一种触发位置”这一要求是否被显式记录为设计决定——报告只写“挂在放置失败的路径上”，未说明为何偏离任务书列出的三种位置。

> **反驳改判**：`已完成，负结果或条件性候选` → **`部分完成`**（downgraded）
>
> 四项实查。(1) 任务书第一个设计要点是“根据子块键范围在有序溢出中定位候选，不每次遍历全部溢出”。代码不是这样：drain_overflow_bounded 的主循环是 index.hpp:1749 `for (std::size_t j = 0; j < n && pos.size() < budget; ++j)`，逐条从容器 0 号扫到 n 号，再对每条调 diag_target_with（index.hpp:1758）求目标子块——即每次调用都按容器规模线性遍历，RESULTS_E37.md §10 自己也写“成本是 O(容器的候选遍历)”。implementation 栏“按子块键范围在有序溢出中定位候选”与代码不符。(2) 任务书“明确每次最多检查多少候选、复制多少字节、迁回多少条；跳过放不下的记录也消耗预算”：代码只限“迁回条数”(kMigrateBudget=64, index.hpp:1714)，候选数只受容器大小约束，index.hpp:1760 `if (blk == SIZE_MAX) continue;` 不递增 pos，即被跳过的记录不耗预算——与任务书这一句相反。(3) 正确性证据未绑在当前源码：check_migrate 在 results/main/g0_build_drift.txt 的 DRIFT 段；我实测当前源码下唯一的一次运行是 logs/g1_rerun/check_migrate.out（构建记录已被 2026-09-23 02:17 的 G1 重编刷新为 LIVE 构建），其结果是 {"result":"fail","err…

### [A-7-01] §7.1 验收口径：除迁回数量外还须计入候选搜索、溢出删除搬移、保留容量是否释放、点查与扫描变化；若容器删除比迁回本身昂贵，不能以“主块利用率提高”判定划算；“每次最多迁回 K 条”不等于维护时间有界，只限条数时须如实称为“迁回条数受限”。

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：验收侧实现于 bench_migrate.cpp 与 analyze_migr.py（维护完整耗时/点查/扫描/常驻空间/峰值）；搬移侧 erase_positions（index.hpp:1202）为单趟压缩，逐条 erase 对照见 RESULTS_E37.md §4。报告一律使用“迁回条数受限”措辞，未用“有界维护成本”。

**证据**：RESULTS_E37.md §4（逐条 erase 搬移是单趟压缩的 11×–165×）、§6 口径段、§8.1 验收表（含完整维护/点查/点击/常驻空间/尾延迟/扫描）；完整输出 results/main/e37_feasibility.txt、e37_analysis.txt。构建身份 DRIFT（bench_migrate/diag_migrate）。

**覆盖与缺口**：已覆盖：候选搜索 O(容器)、溢出删除搬移（单趟 vs 逐条）、常驻空间与峰值、点查与扫描变化均入表并给出方向计数。尚未覆盖：“保留容量是否释放”未单列为一项；峰值只并入常驻空间一行；grow 模式未做尾延迟。

**关闭标准**：把“保留容量释放”单列；若保留 R1，补 grow 模式尾延迟并明确盈亏判定（不能让“主块利用率提高”单独成立）。

**未能核实**：“保留容量是否释放”是否在别处（如 e37_analysis.txt 内部）有独立列，工作区可见报告未见。

> **反驳改判**：`已完成，负结果或条件性候选` → **`部分完成`**（downgraded）
>
> 任务书这一条明列四项必须计入：候选搜索、溢出删除搬移、保留容量是否释放、点查与扫描变化。实查 RESULTS_E37.md 全文，“保留容量”出现 0 次（grep -c 于 RESULTS_E37.md、results/main/e37_analysis.txt、results/main/e37_feasibility.txt 均为 0），agent 的 coverage 栏也自认“保留容量是否释放未单列为一项”。候选搜索与溢出删除搬移也没有作为独立行进入 §8.1 验收表（该表只有 完整维护 R1/R0、R1/B、点查 R1/R0、R1/B、常驻空间 R1/R0、尾延迟 p99、扫描 七行）；results/main/e37_analysis.txt 里没有“候选/搜索”段，analyze_migr.py 只有 load/ratio_block/queue_block/scan_section/repair_section 五个函数。二者只在 §10 以解析式（O(容器) 候选遍历＋O(容器) 单趟压缩＋O(迁回字节)）一笔带过，不是“计入”。四项里至少一项完全没有、两项没有独立测量，验收口径这条不成立。另：验收侧程序 bench_migrate 是 DRIFT（g0_build_drift.txt），agent 自己也在 evidence 里写明。

### [A-7-02] §7.1 第二种实验“修复旧状态”：先用冻结版生成 200% 替换后的快照，再分别不维护、局部整理、有界迁回；与从初始状态运行新策略分开呈现。

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：已实现为在 E31 底座（E37 的 S 臂＝冻结版＋subblock_reclaim）上跑到 200% 后调用 Index::drain_all_bounded()（index.hpp:4340）最多 8 轮；驱动 run_repair.py、诊断 diag_repair.cpp。

**证据**：RESULTS_E37.md §9；原始 results/main/repair_diag_{same,grow}.jsonl；构建 diag_repair（DRIFT）。结果：修复放进主块仅 19–994 条（0.02%–0.9%），根因定位到 repack_subblock 对 count<2 的子块直接返回、水位永不复位。

**覆盖与缺口**：已覆盖：三数据集 × same/grow 的修复前/后溢出、取出、放置成功、被退回、耗时；结论为负结果且根因已定位到一处实现约束。尚未覆盖：明确的“局部整理”单独臂——E37 的修复是在已开 subblock_reclaim 的底座上做，“不维护 vs 局部整理 vs 有界迁回”三态只呈现为前/后两态。

**关闭标准**：若仍需三态字面分离，补“局部整理（不迁回）”单独臂；否则以“修复已退化快照不成立、根因已定位”记录并关闭。

**未能核实**：“不维护 / 局部整理 / 有界迁回”三臂是否按任务书字面拆开——只看到修复前/后两态＋E31 底座，未见独立的“局部整理”臂表。

> **反驳改判**：`已完成，负结果或条件性候选` → **`部分完成`**（downgraded）
>
> 任务书原文要求：先用冻结版生成 200% 快照，“再分别不维护、局部整理、有界迁回”三种处理。实查 RESULTS_E37.md §9 只有“修复前/修复后”两态（表列：修复前溢出、修复后溢出、取出、放置成功、被退回、修复耗时），没有“不维护”与“局部整理”两个独立臂。而且快照不是在冻结版上生成的：§9 表里 mixed/same 修复前溢出 39,878，正是 §1 中 S 臂（冻结＋subblock_reclaim）的值，而冻结臂 B 是 128,533——即快照底座是 E37 的 S 臂（已开 subblock_reclaim），比“不维护”多了局部整理。所以“不维护 vs 局部整理 vs 有界迁回”三态被压缩成两态、且起点还不是任务书指定的冻结版。agent 的 coverage 栏已承认“尚未覆盖：明确的局部整理单独臂……三态只呈现为前/后两态”，但仍给“已完成”。按 status 取值表，缺任务书规定的实验臂属于“部分完成”。

### [A-7-03] §7.2 相邻子块重分配：入口证据＝A1 失败而 A2 在足够多真实压力事件中成功、失败确与邻块间槽位/字节/键范围分界有关；第一版取目标块＋一个相邻块，按键序合并重切，同时满足左右槽数/实际后缀占用/边界；旧布局在新布局准备完前保持可用、一次提交目录边界；退出条件＝多数失败仍由组级总容量或单键过长造成，或双块搬移持续增加成本而无明确空间收益。

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：未实现——无目标块＋相邻块的双块重切原型。提供入口判定的诊断：diag_overflow（F0–F3），其中 F1＝只回收按键序选中的那个子块自己的垃圾（first_candidate index.hpp:1663、repack_subblock index.hpp:1685），F2＝整组重排。

**证据**：RESULTS_E28.md §2：F1 可放回 80%–95%（same 63%–92%）、F2 预算够用 97%+、卡字节 208,689 对卡槽位 18,527；原始 results/main/overflow_diag.jsonl；MODEL_AND_MAPPING.md §5 明记“§7.2 未做，需要 A2 级证据”。构建 diag_overflow（DRIFT）。

**覆盖与缺口**：已覆盖：F0/F1/F2 三层判定（组预算不是约束、卡点在子块内部字节碎片）；替换换组率 0.016%–0.046%。尚未覆盖：A2（目标块＋相邻块）本身从未构造；“失败与邻块分界有关”未直接测。

**关闭标准**：若未来出现 F1 持续失败的负载，再按 A2 原型进入；否则维持关闭（依 E28：A1≈F1 已在 80%–95% 压力事件上成功，入口“A1 失败”不成立）。

**未能核实**：A2 双块方案的实测证据不存在；本关闭是“入口证据不成立”的推断（F1 成功率高），不是“A2 被否”的实测。

> **反驳改判**：`已完成并采用` → **`部分完成`**（downgraded）
>
> 任务书这一条是“入口证据”（进入 §7.5 的前置判据），被审的清单原话是“确认溢出向量的移动／构造／分配仍占主要时间”。实测数字（RESULTS_E32.md §4：溢出向量占维护时间 37.6%/37.3%/12.2%，来自 results/main/ov_cost_diag.jsonl，24 条）显示溢出是维护时间的少数而非多数，主块路径占 62.4%–87.8%——E32 却写“前置条件成立”。任务书列举的三个成本成分里，“字符串分配”从未与字节搬移分离（agent 的 coverage 自认：“未把‘字符串分配’成本与‘字节搬移’分离”），而它正是判据三个成分之一。另外构建身份：logs/build_diag_ov_cost.json 记录的 index.hpp 是旧哈希，diag_ov_cost 在 g0_build_drift.txt 的 DRIFT 段——这条结论的测量不绑在当前源码上。判据本身未被满足、命中成分未被分离、证据为 DRIFT，三理由都不支持“已完成并采用”。（附带更正：agent 的 coverage 说“grow 模式未测”不准确——ov_cost_diag.jsonl 里 mode 取值集合是 {same, grow}，两种模式都在。）

### [A-7-04] §7.3 本块空洞复用：入口证据＝A1 可行且有效但长期负载中重复复制同一批活跃后缀成为主要维护工作、删除留下的洞与新后缀需求匹配率高；试验本块小型空闲区间表或长度分级空洞表，需合并相邻洞、处理碎片、维护水位与偏移，元数据与搜索成本全部入账；与 A1 第一版不能同时启用；若后缀占用短且元数据太大或空洞匹配率低则停止；不解决槽位不足。

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **尚未完成** |
| **置信度** | high |

**实现**：未实现——无空洞表/长度分级表/水位方案。既有相关件 repack_subblock（index.hpp:1685）是 A1 的原地收紧，不是洞复用。

**证据**：MODEL_AND_MAPPING.md §5 明记“§7.3 未做 / 未评估”；字符串索引_架构与工程优化总执行计划_20260922.md 第 193 行仍把它列为待做候选（需“洞的尺寸匹配率”）。工作区的 .md、分析脚本、results/main 中均未找到任何空洞匹配率诊断或原型文件。

**覆盖与缺口**：已覆盖：无。尚未覆盖：全部——入口诊断（洞尺寸分布 vs 新后缀需求匹配率、重复复制活跃后缀占比）、原型、停止判据均无。

**关闭标准**：先做入口诊断（洞尺寸匹配率与重复复制占比）；匹配率低则按停止条件关闭，否则做一次与紧凑化单独对照的原型（与 A1 不能同时启用）。

**未能核实**：是否在 E30/E31 等日志或未索引脚本中有未被引用的空洞匹配率统计——在工作区可见范围内的 .md 与 results/main 中未见。

> **反驳复核**：维持原判（stands）。

### [A-7-05] §7.4 区域内容量再分配：入口证据＝A1–A3 不能恢复而 A4 在相同区域主块预算下可行、空闲确实集中在其他逻辑组；保持模型与组号映射不变，只改各组物理容量或块归属；记录规划与数据迁移耗时、目录更改、预算守恒、新旧并存峰值、受影响键数、查询导航成本；触发不能只看全区域平均装载率；须单列“连续大区＋偏移”与“独立块池＋目录”的对照。

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：未实现——F3（区域内跨组重分配）未实现，无容量重分配原型。

**证据**：RESULTS_E28.md §2/§5（F2“整组重排后预算够用”97%+ → 区域级重分配在那部分无对象；F3 未实现，“空闲是否在别的组”本轮无直接答案）；MODEL_AND_MAPPING.md §5 明记“§7.4 降级：在 F2 覆盖的那部分上无对象，其余未排除”；构建 diag_overflow（DRIFT）。

**覆盖与缺口**：已覆盖：组内总量必要条件（F2，97%+ 满足）。尚未覆盖：跨组/区域级容量错配（F3 未实现）、剩余 ~3% 预算不足事件未分类、连续大区与独立块池的间接访问/分配管理对照。

**关闭标准**：若要重开：先对 ~3% “预算不足”事件分类，确认是否跨组容量错配；并做“连续布局 vs 独立块池”的对照（含多一次间接访问）。

**未能核实**：剩余 ~3% 预算不足事件的分类、F3、跨组空闲分布；关闭建立在“A1/F1 已恢复 80%–95%”之上，而非 A4 可行性实测。

> **反驳改判**：`已完成，负结果或条件性候选` → **`部分完成`**（wrong）
>
> 这一条的核心断言被代码直接否证。agent 的 implementation 写“E11/E13 的边界目录定位已存在并被默认路径使用”，但：(a) Mode::locate 默认就是 false，注释原文“Off by default, so the path is bit-identical to E10 unless a caller asks for it”（index.hpp:554-557）；(b) 写路径的 locate 字段来自 config_.write_locate，而 Config 默认 write_locate=false（index.hpp:657），switches.hpp 的白名单与 baseline_config/pin_frozen_switches 都不设置它——grep write_locate 在 switches.hpp 与全部 51 个 .cpp 里 0 命中，即所有配置下它恒为 false；(c) dirs_ 在整份 index.hpp 里只为“定位”被读两处：index.hpp:2975（`if (mode.locate && has_dir(g))`）与 index.hpp:3785（诊断报告构建器 rep.ov_fits_ordered），其余全是 rebuild_dir/内存核算；(d) 本轮全部点查基准与门的 Mode 字面量第 7 个字段都是 false：bench_e25.cpp:126、bench_long_o1.cpp:179、bench_migrate.cpp:190、bench_tag.cpp:191、che…

### [A-7-06] §7.5 入口证据：局部整理后，替换仍主要消耗在溢出有序向量的插入、删除搬移或字符串分配，而非主块维护。

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：诊断已做：diag_ov_cost.cpp（同一长期替换负载，逐检查点报溢出插入/删除时间与计数、容器自身搬移字节、活跃容器规模分布，主块路径作分母）。

**证据**：RESULTS_E32.md §4；原始 results/main/ov_cost_diag.jsonl；驱动 run_ov_cost.py；构建 diag_ov_cost（DRIFT，需 -DE25_DIAG -DE30_TIME）。结论：溢出向量占维护 12.2%/37.3%/37.6%，代价集中在 erase 搬移（每次 1.9–6.8 KB），插入几乎不搬 → O1 优先。

**覆盖与缺口**：已覆盖：same 模式 200% 检查点、三数据集。尚未覆盖：grow 模式未测；未把“字符串分配”成本与“字节搬移”分离（只报搬移字节与 ns/插入删除）。

**关闭标准**：若需完整，补 grow 模式并分离分配成本；否则以现有判定（前置条件成立、O1 优先）为准。

**未能核实**：字符串分配成本是否被单独隔离计量——报告只给搬移字节与 ns/操作。

### [A-7-07] §7.5 溢出容器原型：可先比较“小型有序分块容器”与原向量，或“稳定记录存储＋有序句柄”；每种候选必须保留完整判重和扫描语义；不能只测写速度而不测读与空间；若实际搬移的是短描述符而不是长后缀要分别计数，不能凭 vector::erase 推定整个字符串字节都被复制。

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **已完成，负结果或条件性候选** |
| **置信度** | high |

**实现**：选“稳定记录存储＋有序句柄”并已实现：Config::overflow_stable_store（index.hpp:738，默认 false）＋ OverflowList 的 store_/order_/free_slots_（index.hpp:1291 附近）；后续 Config::overflow_inline_tag（index.hpp:742，默认 false，E36 T8）。门 check_overflow_store.cpp（LIVE）、check_tag_store.cpp（LIVE）；计时 bench_long_o1.cpp、bench_tag.cpp；诊断 diag_o1/diag_tag。

**证据**：RESULTS_E34.md（O1）、RESULTS_E36.md（T8）；原始 results/main/long_o1_{same,grow}.jsonl、long_o1_diag_{same,grow}.jsonl、tag_{same,grow}.jsonl、tag_diag_{same,grow}.jsonl、e34_scan_o1.jsonl、e36_scan_tag.jsonl；FINAL_RESULTS_TABLE.md 记 bench_long_o1/bench_tag P1–P5。“搬动的是描述符还是后缀”已先测（E34 §0：mixed/skew 溢出后缀 0% 走 SSO，搬的是 48 B 对象，字符字节全在堆上）；判重（40,960 缺席键查询全 found==false）与扫描语义由 LIVE 门覆盖；读与空间都测了。

**覆盖与缺口**：已覆盖：三数据集 × same/grow；erase 搬移 48 B→4 B、实测 12.0×；常驻空间 0.948–1.002；200% 尾延迟腰斩（mixed）/不动（tranco）；点查 1.02–1.53（读路径一致受损）；扫描 skew +6.5%–13.6%。尚未覆盖：“小型有序分块容器”与“无序增量区/墓碑”从未实现（任务书允许二选一，项目选了稳定句柄）；T8 的 packed 布局首版不做。

**关闭标准**：依 E36 审阅已收尾（T8 保留为场景候选、结束 O1 改造）；若未来溢出搬移仍显著，再按 E36 §6/新计划 A3 选一种小型有序分块容器（不得与墓碑/哈希/增量区同时引入）。

**未能核实**：“描述符 vs 后缀字节分别计数”在 E34 §0 是以对象大小 48 B 对字符字节间接呈现，是否逐项分列未能从原始 jsonl 逐条确认；Masstree“逻辑顺序与物理位置分离”仅为文献参照，无对应原型亦无必要。

### [A-7-08] §7.6 点查（核实与缓存）：核实 E11/E13 的子块范围定位是否实际启用，不能把已有机制重新写成新创新；对目录边界做紧凑编码或前缀片段缓存时必须保留等值全比较，前缀碰撞数据必须单独报告。

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **已完成，负结果或条件性候选** |
| **置信度** | medium |

**实现**：E11/E13 的边界目录定位已存在并被默认路径使用：Region::locate_subblock（index.hpp:1529，对 dirs_ 二分，至多检查一个候选块）；V1 write_fast_locate 即“标签过滤/SIMD/边界目录/溢出策略”（RESULTS_E25.md）。前缀片段缓存被诊断后停止（未建新布局）；E36 内联标签明确“标签不用于确定字典序、保留等值全比较”。

**证据**：MODEL_AND_MAPPING.md §3 Q6/Q8（构建/点查/插入/删除四处同一 canonical_group；扫描不调用它定位但依赖其保序）；EXPERIMENT_INDEX.md 第 41 行记 E11“实现边界目录，查找至多检查一个候选块”；继承门 check_e11_locate（DRIFT）在 RESULTS_FREEZE.md §4 重跑（仅 C2 预期差异 queries_answered_from_overflow 13893→13894）；san_check_e11_locate（LIVE）。E35 前缀可判定率见 RESULTS_E35.md §4，原始 results/main/prefix_{same,grow}.jsonl、prefix_diag_*，bench_prefix P1–P5。

**覆盖与缺口**：已覆盖：三数据集；E11 locate 在冻结配置下由继承门 check_e11_locate 与 sanitizer san_check_e11_locate 复核；前缀可判定率（mixed/skew 二分 0.0%，tranco 二分仅占 8.7%–14.6%）。尚未覆盖：无专门断言“E11/E13 定位已启用”的独立门（依赖继承门通过＋静态核查，而非对该启用状态的逐项断言）；目录边界紧凑编码未尝试。

**关闭标准**：若改目录边界编码或加前缀缓存，须按 E35 同一判据重跑前缀碰撞统计并单列报告。

**未能核实**：是否存在专门断言“E11/E13 子块范围定位实际启用”的门或记录；当前依据是继承门通过＋MODEL_AND_MAPPING 的静态核查，属间接证据。

### [A-7-09] §7.6 点查（元数据与实验纪律）：测只读元数据工作集和间接访问；若使用硬件计数则单列其运行、不把采样程序的时间当正式成绩；若考虑把预测结果再映射到额外目录，须核算它是减少候选还是增加一层串行依赖。

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：间接访问代价被定位（E34/E35/E36 把读路径回归归到 store_[order_[i]] 两次相关加载）；但无“只读元数据工作集”的专门测量；未使用任何硬件计数器（工作区无 perf 计数文件）；未考虑/未实现预测结果→额外目录的映射。

**证据**：RESULTS_E35.md（回归随“每次溢出查询读过的记录数”变化，百万域名 25–28 条→1.6×）；RESULTS_E34.md §3（点查回归一致 1.02–1.53、方向几乎全 0/5）；RESULTS_E36.md §1（记录读取降幅 92.3%/91.4%–93.3%/8.6%）；RESULTS_E32.md §4（容器规模分布）。无硬件计数与工作集测量的文件。

**覆盖与缺口**：已覆盖：间接访问的定性定位与按来源队列的路径分派。尚未覆盖：只读元数据工作集（缓存工作集）测量；硬件计数；预测再映射到额外目录的核算。

**关闭标准**：若要收紧，补只读元数据工作集测量；预测再映射目录属“若考虑”的备选，未触发可不做，但应显式记为未评估。

**未能核实**：是否有任何一处把“只读元数据工作集”量到（工作区可见范围内未见）；硬件计数器全项目未使用（未找到 perf 输出）。

### [A-7-10] §7.6 扫描：入口证据＝仍存在重复起点定位或每次跨块重建游标；先消除已确认的重复工作，再考虑保存一次扫描内的区域/组/子块/溢出归并状态；返回 string_view 与返回拥有所有权的完整字符串是不同接口，只读游标实验须让有序数组等对照采用同样语义；本轮不把空组位图列为默认工作（E27 实际空组访问为零）。

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：重复起点定位已消除候选：Config::scan_cache_start（index.hpp:720，默认 false）把 Index::scan 循环条件里的 locate(start) 从 R+1 次降到 1 次；scan_reserve_result（index.hpp:673，E27 S1，默认 true）把结果构造从两次分配降到一次。游标/归并状态复用未实现；空组位图未做（按 E27 关闭）。对照语义：E27 的有序数组 ARR 复制 n 条记录、返回形式与原型一致。

**证据**：E27_SCAN.md（空组访问全 0.0 → 空组位图跳过；结果分配 1.75–2.00/记录；S1/S0 0.758–0.937，27 格全同向）、RESULTS_E27.md；RESULTS_E32.md §1（S2/S1 0.92–1.02，tranco 0.932/5-5）；原始 results/main/e27_scan_time.jsonl（2025）、e27_scan_diag.jsonl（27）、e27s2_scan_time.jsonl（2700）；构建 bench_scan/bench_scan_s2 为 DRIFT。游标未做见 MODEL_AND_MAPPING.md §5 与字符串索引_架构与工程优化总执行计划_20260922.md A4（第 210 行，仍未做）。

**覆盖与缺口**：已覆盖：重复起点定位（E32 S2）、结果构造（E27 S1，check_scan 逐条＋参考表比对，每数据集 950 万–1432 万条）、空组位图按 E27 关闭。尚未覆盖：跨块/跨区域归并状态与游标的复用（A4 明确“起点缓存与结果构造≠持续扫描游标”）；反向扫描、并发。

**关闭标准**：按 A4：先诊断导航/归并重建成本，再实现“一次初始化 lower_bound＋保留区域/组/子块/主块/溢出当前位置”的游标；对照 S0 vs 仅加游标复用的 S1，同语义（完整键值）；若已有等价游标或无可避免重建，按条件未满足关闭，不得用复制全局有序数组掩盖成本。

**未能核实**：是否已有等价游标（未逐行审计 scan 的导航重建）；跨块归并状态的具体重建成本未测。


---

## A-8-9  —— 架构线 §8–§9：模型契约与正式比较协议（27 条）

### [A-8-9-00] §8.1 接口规范应给出 MappingContract 契约字段：input_domain（完整键/区域后缀、字节域）、output_kind（坐标/排名/逻辑组号）、order_preserving（明确契约，不能仅靠标志自证）、mapping_version（受控生命周期内使用）。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：未按草案命名实现。全仓 grep `MappingContract`/`PredictorAdapter`/`input_domain`/`output_kind`/`order_preserving`/`mapping_version` 均 0 命中（含 index.hpp、switches.hpp、model/、vendor/）。内容上最接近的实体：`ModelBackend`（index.hpp:597）、`Config::model_backend`（index.hpp:613，默认 Context16）；适配器的输入域拒绝 `lits_hpt::unsupported_reason`（model/lits_hpt_adapter.hpp:83）、输出为逻辑组号 `RegionModel::predict_group`（同文件 :383/:388，注释 :369 明示 group ∈ [0,G)）；`order_preserving` 无同名成员，只有门在测（check_lits_model.cpp:112-113 monotone_checked/violations）；`mapping_version` 无对应物，最接近的是摘要 `Region::model_identity`（index.hpp:1585，非版本号）。MODEL_AND_MAPPING.md §4.4 自述“接口契约（§8.1 的 MappingContract / PredictorAdapter）尚未起草”。

**证据**：源码身份：index.hpp 共 4530 行，最后改动 2026-09-22 12:51:49 UTC；switches.hpp 09-22 10:57。证据文件：MODEL_AND_MAPPING.md §4.4（SA0，09-20）、LITS_AUDIT.md §2.6/§5；本行直接依据 grep 结果（0 命中）。构建身份：适配器头由 LIVE 门 check_lits_model 14/14、check_lits_index 14/14 覆盖（results/main/g0_build_drift.txt）。

**覆盖与缺口**：已覆盖 M0(Context16) 与 LITS-HPT 两后端的“输入域拒绝”“输出=逻辑组号”两点；未覆盖：order_preserving 未成字段（仅由 check_lits_model/check_scan_contract 的门间接守），mapping_version 完全缺失。

**关闭标准**：把四个字段落成显式类型或成文契约；order_preserving 要么由字段承载，要么在契约里写明“由 X 门负责”。关闭标准：契约文本存在且四字段各有归属（实现或门）。

**未能核实**：未逐一通读 docs/ 下全部文档，是否另有契约草稿文本未能排除；但源码内 0 命中是确证的。

> **反驳复核**：维持原判（stands）。

### [A-8-9-01] §8.1 接口规范应给出 PredictorAdapter 契约：build(TrainingKeys, MappingConfig)、predict(ByteView) const、allocated_bytes() const。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：未按草案实现。等价能力分散：训练 `lits_hpt::SharedModel::train/train_shared`（model/lits_hpt_adapter.hpp:267/272）、校准 `RegionModel::calibrate`（:347）、预测 `RegionModel::predict_group(std::string_view,...)`（:383/:388）、内存 `SharedModel::table_payload_bytes`（:294）与 `table_allocations`（:296）。无 build/predict/allocated_bytes 这套签名；无 MappingConfig 类型（配置在 `validation::Config`）。

**证据**：model/lits_hpt_adapter.hpp（LIVE，见 check_lits_model/check_lits_index 14/14）；RESULTS_E38.md §8（接线表）；MODEL_AND_MAPPING.md §4.4。

**覆盖与缺口**：已覆盖：训练/预测/表payload字节三件事都有落点，且 LITS 走 string_view 而非 NUL 终止。未覆盖：没有统一接口，allocated_bytes 只报“表 payload”，不含 1024 次 `new UNI[128]` 与指针数组（LITS_AUDIT §11.4f 明示这 1024 次分配是真实代价）。

**关闭标准**：若要“同一接口可换后端”，需把两后端的 build/predict/bytes 收敛到一个类型；关闭标准：M0 与 LITS 后端都能通过同一接口调用且 allocated_bytes 口径一致。

**未能核实**：无。

> **反驳改判**：`已完成并采用` → **`部分完成`**（downgraded）
>
> 命名分离属实（original 8 文件含 lits_node.hpp；safe 3 文件；md5 显示 base/gpkl 两份逐字相同、model 两份不同；build_check_lits_model.json 只收 safe 三文件）。但 coverage 的『原生模型语义验证』夸大：check_lits_model.cpp:7 自述 reference 是 transcription 非第二实现、与适配器共用同一 getPos/getCdf；RESULTS_E38.md §7 自认『只证明适配，未独立验证 LITS 算法本身』，§5.3 登记 NonFinite 未触发。该限制未进未覆盖栏。

### [A-8-9-02] §8.1 具体契约还需约定：空串与 EOS、公共前缀剥离、浮点异常与取整、域外键、新键、逻辑组数、重训时机、状态所有权、变更通知。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：部分实现，成文程度不一：公共前缀剥离→适配器传“完整键＋区域前缀长度”且 canonical_group 只接完整键（index.hpp:1450，:1483-1488）；浮点异常与取整→predict_group 先拒非有限、double 域饱和、最后转换（lits_hpt_adapter.hpp:579-585）；域外键→unsupported_reason 拒绝（:83）；空串/EOS→用 view＋显式长度，不再靠 NUL（:210 walk_position），但 `St` 仍依赖 `std::string` 终止语义（:139 注释）；逻辑组数→`derive_groups`（MODEL_AND_MAPPING §3 问题5）；重训时机→“一个模型/一个索引版本、无在线学习”（:42）；状态所有权→`shared_ptr<const SharedModel>` 独占、删拷贝（:272-279，index.hpp:1338/3917）。

**证据**：model/lits_hpt_adapter.hpp:83/210/579-585；index.hpp:1450/1483/2072-2073/3937-3961；LITS_AUDIT.md §11.2/§11.4a。

**覆盖与缺口**：已覆盖：前缀剥离、异常/取整、域外键、所有权、重训时机（LITS 侧）。未覆盖：变更通知机制不存在；“新键”（查询集外的键）在 LITS 后端下的语义未单独规定；空串本身在语料上不出现（LITS_AUDIT §8.3：三数据集空键数 0），只有拒绝路径被构造覆盖。

**关闭标准**：补“变更通知”与“新键”两条约定（或明确声明不需要）；关闭标准：九项各有实现或成文声明，不留空项。

**未能核实**：“变更通知”是否在其他文档中作为设计决定被放弃，未核。

> **反驳复核**：维持原判（stands）。

### [A-8-9-03] §8.1 指纹可以与预测融合计算，但不能把融合得到的成本误记为“哈希消失”。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：已实现为字段分离：`Model::Prediction` 同时有 `consumed`（index.hpp:859，模型实际处理字节数，喂给 `result.steps`）与 `fp_consumed`（index.hpp:866，哈希续接点）；`find_impl` 用 `fp.tail(s.substr(pred.fp_consumed))`（index.hpp:2930）；LITS 后端走全键、报 `fp_consumed=0`（index.hpp:1483-1488）。注释 :890-893 明示二字段不可合并、否则会把 `fp_consumed` 清零。

**证据**：源码身份：index.hpp 09-22 12:51（LIVE 侧）。门：check_lits_fingerprint（LIVE 14/14，results/main/e38_lits_fingerprint.jsonl）；RESULTS_E38.md §4/§9.3（负对照 99.56% / 98.4% 不同）。

**覆盖与缺口**：已覆盖：M0 融合路径与 LITS 独立路径在 524,288 键上与存储标签逐条一致，负对照按可测比例不同。未覆盖：把“融合成本”从性能口径里单独量出来的 **M0_separate_hash 计时臂未做**（LITS_AUDIT §4 提出，未执行）——因此“哈希是否消失”只有语义证据，没有时间测量。

**关闭标准**：若论文要引用“哈希成本”，需另立 M0_separate_hash 诊断臂；关闭标准：该臂有同轮对照数字。

**未能核实**：无。

> **反驳改判**：`已完成并采用` → **`部分完成`**（downgraded）
>
> 五项与 R1 确显式（switches.hpp:64-68、:37-38），check_freeze LIVE 14/14。但任务该行要求『显式写出全部开关』：compact_repack_budget（E29 C3，index.hpp:683 声明、:3209 读）在 baseline_config（switches.hpp:41-52 无）与 pin_frozen_switches（:64-76 无）里都没写，只出现在 switches() 与白名单；该开关的默认值翻转仍能移动基线与冻结条件而不被察觉。

### [A-8-9-04] §8.1 映射变了，记录位置通常需要重建或兼容迁移；仅换模型对象、保留旧物理记录会产生错查；新增上下文必须绑定正确的键序列与生命周期。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：重建/迁移路径存在：区域重建与分裂（index.hpp:4400+ 的 rebuild/split）、前缀重建（`Index::insert` accepts_prefix 失败整区重建）；两处训练点在 LITS 后端共用同一 `shared_ptr`（index.hpp:2072-2073、3941-3958）；E20 记录了“映射身份→键序列→生命周期”三次正确性收紧。但**没有“映射版本不匹配即拒绝/迁移”的检查**：`model_identity`（index.hpp:1585）是摘要，不是版本门槛；换后端时 `preset_ok` 恒假（LITS_AUDIT §5 说明 R2 本轮不接）。

**证据**：RESULTS_E20.md（DRIFT 构建）；LITS_AUDIT.md §5（model_identity 只认 Model 类型，接 R2 会是静默点）；index.hpp:1585/3937-3961；check_lits_index（LIVE）覆盖前缀重建 4 次、分裂 22 次。

**覆盖与缺口**：已覆盖 M0 与 LITS 后端下的重建/前缀重建/分裂。未覆盖：**模型独立变更但物理记录不重建的兼容迁移**——无此路径、无版本门槛；“新增上下文绑定键序列”在 LITS 下由共享对象结构性保证，但未成文为契约。

**关闭标准**：决定并实现“映射变更→重建(默认) 还是 版本门槛拒绝”；关闭标准：存在一条可测路径，且新增上下文有显式绑定点。

**未能核实**：index.hpp 的 rebuild/split 全路径我未逐行读完（4530 行），只核了调用点与门覆盖；是否另有一条迁移分支未发现，不确定。

> **反驳改判**：`已完成并采用` → **`部分完成`**（downgraded）
>
> 白名单确为 14 项含 model_backend（switches.hpp:117），switches() 在 :81-97。但 claim 自认『任务书点明的这一处仍空着』：offset_directory/write_simd/write_locate 既不在白名单也不在非开关相等断言（:138-143 只比 block_bytes/slots/model_kind/multiblock/reuse_group_ids/ov_threshold），故两臂若只差这三者会静默通过——正是该检查要防的塌成一个实现。既然仍空着，状态不应是已完成并采用。

### [A-8-9-05] §8.2 如需开源参考模型，另立一条适配线：先验证原生模型输出的语义和必要依赖，再接入存储层；提取模型与引入完整 LITS 索引分开命名；完整 LITS 只能作为外部系统基线。

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **已完成，负结果或条件性候选** |
| **置信度** | high |

**实现**：已实现：`model/lits_hpt_adapter.hpp` 为独立适配线；`vendor/lits_model_original/`（固定提交 6f4793d…，8 文件，未打补丁）与 `vendor/lits_model_safe/`（补丁版＋2 个逐字复制依赖）分开；完整 LITS 索引**故意不引入**（lits_node.hpp 不复制，RESULTS_E38.md §2.5）；ART 作为独立外部基线（third_party/vendor/libart）。

**证据**：LITS_AUDIT.md §11（8/8 哈希核对通过）；RESULTS_E38.md §2.2-2.5；门 check_lits_model（LIVE 14/14）；分析入口 analyze_lits.py、results/main/e38_lits_model_*.jsonl。

**覆盖与缺口**：已覆盖：原生模型语义验证（524,288 实时＋524,288 缺席键逐条一致）、必要依赖最小化（3 文件）、命名分离。未覆盖：**完整 LITS 系统基线未构建**——任务只允许它作外部基线、未强制要求，但“与完整 LITS 对比”的对外数字因此不存在。

**关闭标准**：若论文需要对外基线，另行构建完整 LITS 系统并单列环境；关闭标准：另立命名、另算哈希。

**未能核实**：LITS 的代码许可（LICENSE）仍然未决（LITS_AUDIT §11.5），这是登记在案的未决项，不是我的漏查。

> **反驳改判**：`已完成并采用` → **`已完成，负结果或条件性候选`**（downgraded）
>
> 事件断言确有（check_e31.cpp:224-232 断言 subblock_reclaims>0，需 -DE25_DIAG；build_san_check_e31.json 带该宏且 LIVE）。但 E31 的 subblock_reclaim 是明确未采用的候选（RESULTS_E31.md:240『默认仍为 false』；FINAL_RESULTS_TABLE.md §5b『候选机制（未采用；开关默认关闭）』），属分类表另一格；且被引用的计数 12.8万-25.6万/20.4%-67.2% 出自 DRIFT 的 check_e31 运行，唯一 LIVE 重跑（san_check_e31）只覆盖 mixed/131072。

### [A-8-9-06] §8.2 若加入新模型，做小型 2×2 对照（行=M0/M1，列=S0/S1）；固定数据、外层区域划分与主块预算；各臂按合法映射重新构建，训练时间、模型内存与维护一并计入；若 M1 必须改组数或区域划分，不能再称纯模型消融，要另外说明改动。

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：只做了 2×2 的两格（S0×M0 与 S0×M1）：E38 P3 三臂 M0 / HPT+Context16 回退 / ART（bench_lits_adapter.cpp:156 设 `cfg[1].model_backend = LitsHpt`；:164 require_only_switches_differ({model_backend})）。S1（候选结构 LOCAL/E31）与 S1×M1 两格未做。固定条件做到了：数据/外层区域/主块预算同轮相同，各臂按自身需求重规划容量（RESULTS_E38_P3 §0）；训练时间计入构建窗口（§1）；模型内存按共享 HPT 只算一次（§4）。**但 M1 改变了组数**（LITS 用到组的 1.56×/1.70×/2.01×，RESULTS_E38.md §3.5），报告**未把它标注为“非纯模型消融”**。

**证据**：RESULTS_E38_P3.md（run_id=p3b；results/main/lits_adapter_p3b_{same,grow}.jsonl；分析 results/main/e38_p3_analysis.txt）；bench_lits_adapter（LIVE 15/15）；RESULTS_E38.md §3.5/§8。

**覆盖与缺口**：已覆盖：S0×M0、S0×M1 的构建/静态混合点查/常驻空间。未覆盖：S1 结构性收益（S1×M0）与组合（S1×M1）完全未测；M1 改 G 的“另述”缺失。

**关闭标准**：补 S1 两格，或在报告里补“M1 改了组数 G，故本对照=模型+组数组合改造”这一句；关闭标准：4 格齐全或明确降格为 1×2。

**未能核实**：S1 是否在别处以“LOCAL=E31 臂”单独测过（RESULTS_E31.md 是 DRIFT 且非 2×2 结构），无法确认其能否算 2×2 的一格。

> **反驳改判**：`已完成并采用` → **`部分完成`**（downgraded）
>
> E31:240、E36:24/:136 确写固定变量；但被引的 E34 §4『适用边界』（:260-281）与 E37 §10 里都没有『模型/容量规划/主块预算未动』这类声明（E34 的声明在 :7，内容是两臂只差一个开关），证据位置不支持结论。更关键：全仓 RESULTS_E34/E37/E38/E38_P3 检索『组合』或『组合改造』零命中，而 E38 的 LITS 臂同时改了 model_backend 与区域级回退策略（RESULTS_E38_P3 §5.2 自认改了覆盖范围），任务要求的『明确称为组合改造』未做。

### [A-8-9-07] §8.2 模型替换导致更均衡的记录分布，不代表字节需求或动态寿命分布也更好；其效果仍由端到端和空间测量决定。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：该纪律被遵守：RESULTS_E38.md §3.5 明示“非空组数量增加不等于分配更均衡”，并同时报道“用到的组更多”与“最坏组更挤”两面；端到端与空间结论在 RESULTS_E38_P3.md §2/§4/§8 给出（点查、常驻空间、溢出线性读取）。

**证据**：RESULTS_E38.md §3.5；RESULTS_E38_P3.md §4/§8；results/main/e38_p3_analysis.txt。

**覆盖与缺口**：已覆盖：记录分布（组号）与端到端/空间分开报告。未覆盖：**动态寿命分布**未测（长期替换下 LITS 的溢出/寿命未做，RESULTS_E38_P3 §7 明说本轮没做长期替换）。

**关闭标准**：若要动态寿命结论，须补一轮 LITS 后端下的长期替换；关闭标准：same/grow 200% 的溢出与字节有同轮数字。

**未能核实**：无。

> **反驳复核**：维持原判（stands）。

### [A-8-9-08] §9.1 首轮最小变体集：BASE / GROUP / LOCAL / ART / ARR 五行（各自配置与用途）。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：五臂各自有等价物、但**无统一命名的同轮五臂实验**：BASE=E31/frozen 臂、GROUP=E29 的 C3 臂（`compact_repack_budget`）、LOCAL=E31 臂（`subblock_reclaim`）、ART=bench_long_art（libart）、ARR=有序数组（E23/E27）。MODEL_AND_MAPPING.md §5 给出此对照表。

**证据**：MODEL_AND_MAPPING.md §5；RESULTS_E29.md / RESULTS_E31.md / RESULTS_E23.md / RESULTS_E27.md（**均为 DRIFT 构建**，见 g0_build_drift.txt）；分析 analyze_c3.py / analyze_e31art.py / analyze_scan.py。

**覆盖与缺口**：已覆盖：五臂各自有测量。未覆盖：**五臂未同轮并排**；LOCAL 未进默认；ARR 只在扫描排名（点查另用 E21 的 sorted_array）。

**关闭标准**：若要按任务书逐臂对照，需一次同轮五行实验或一份成文的臂↔轮次映射（后者已有雏形）；关闭标准：五臂各有同轮或明确跨轮标注的数字。

**未能核实**：五臂是否曾以任务书命名在同一程序里跑过，未在 results/main 找到对应 run_id。

> **反驳改判**：`已完成并采用` → **`部分完成`**（downgraded）
>
> 替换=erase+insert 且活跃数每段与参考表核对属实（bench_long_art.cpp 段内 seg、live 对 ref.size() 的检查）。但 claim 引的 LOG.md:144 是 E25 轮（短期替换）的记录（e25_time.jsonl 行内确有 cycles/ops/live 字段），而长期替换的原始记录 results/main/long_replace_same.jsonl 首行的键集合只有 cycles/live，没有 ops；RESULTS_LONG.md/E26_LONG.md 也检索不到『操作数』。因此『未覆盖：无（形式满足）』不成立，且 bench_long_art 的构建为 DRIFT。

### [A-8-9-09] §9.1 BASE 应显式启用 write_fast_locate/reuse_locate_ctx/compact_reuse_tag/compact_needs_garbage/scan_reserve_result 五项并使用 R1 构建；SA0 必须核对当前源码是否仍如此。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：已实现：`pin_frozen_switches`（switches.hpp:63-77）显式开五项、并显式把候选开关置 false；`baseline_config`（switches.hpp:29-54）逐字段显式（全 false=pre-E25 基线）；`check_freeze` 断言 SHIPPED==CHOSEN。SA0 核对见 MODEL_AND_MAPPING.md §0（冻结版本=五项全开＋R1）。

**证据**：switches.hpp:63-77；门 check_freeze（**LIVE 14/14**，结果串印 model_backend=Context16）；RESULTS_FREEZE.md §2；MODEL_AND_MAPPING.md §0/§1。

**覆盖与缺口**：已覆盖：M0 冻结配置的五项与 R1。未覆盖：BASE 与 LITS 臂的同轮对照只用三臂（M0/HPT/ART），未把 GROUP/LOCAL/ARR 并入。

**关闭标准**：无（除非要把 BASE 并入五臂同轮实验）。

**未能核实**：无。

> **反驳复核**：维持原判（stands）。

### [A-8-9-10] §9.1 有界迁回、相邻重分配等作为后续独立变体，不预先堆入 LOCAL；不同名字必须打印出真实配置差异，沿用 require_only_switches_differ() 一类检查。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：已实现：`overflow_migrate`（E37）与 `subblock_reclaim`（E31）、`reclaim_hysteresis`/`reclaim_skip_repeat`（E31H/P）各自独立开关、默认 false（switches.hpp:46-52,72-75）；`switches()`（switches.hpp:81-97）打印生效串；`require_only_switches_differ`（switches.hpp:108-144）白名单 14 项含 model_backend。相邻子块重分配 **未实现**。

**证据**：switches.hpp:81-144；E30 记录的白名单漏字段事故（假通过）已修；RESULTS_E37.md。

**覆盖与缺口**：已覆盖：14 个开关＋6 个非开关字段的相等/差异断言。未覆盖：`offset_directory`/`write_simd`/`write_locate` 三个 bool **既不在白名单、也不在非开关字段相等断言**（switches.hpp:112-141 确认属实）——任务书点明的这一处仍空着。

**关闭标准**：若这三个 bool 会被实验翻转，须补进白名单或相等断言；关闭标准：无“可翻转却不受断言约束”的字段。

**未能核实**：相邻重分配是否在别处以别的名字试过，未核。

> **反驳复核**：维持原判（stands）。

### [A-8-9-11] §9.1 新子块策略不能被旧触发条件提前挡住而从未运行；应明确新增判断插入在哪个失败分支、与既有整组维护的先后关系，并用事件断言验证。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：已实现：E31 的放置失败→原地收紧→重试路径明确；`check_e31` 断言回收分支真的运行（200% 触发 12.8 万–25.6 万次，促成该次放置 20.4%–67.2%，门内断言 `subblock_reclaims > 0`）。

**证据**：RESULTS_E31.md（DRIFT）；FINAL_RESULTS_TABLE.md §5b 末两行；门 check_e31（DRIFT 8/10，另有 LIVE 的 san_check_e31）。

**覆盖与缺口**：已覆盖：新分支被强制运行并有事件断言。未覆盖：断言只在 E31 的负载上；换负载是否仍触发未证。

**关闭标准**：无（若换负载需重核触发）。

**未能核实**：check_e31 的断言我未见源码逐行，只据报告与 FINAL_RESULTS_TABLE 的记录。

> **反驳改判**：`已完成并采用` → **`部分完成`**（downgraded）
>
> 分离本身属实：logs/build_bench_lits_adapter.json 与 build_diag_lits_adapter.json 的编译命令对照，一个无、一个带 -DE25_DIAG 与差分计数。但被引的『RESULTS_SUMMARY.md §3.5 工具段』不成立：§3.5（:146-171）是 O1 溢出容器一节，讲的是搬动字节与验收表，没有工具段；工具段在 §3.6 的 :320，而 clean/diag 一源两目标的证据在 §3.6 的 :297 与 E36 §0。引用位置与结论不符。

### [A-8-9-12] §9.1 组预算、外层路由、预测配置、SIMD 与溢出策略，凡非本轮变量均固定；若改动必然影响其他项，明确把实验称为组合改造。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | medium |

**实现**：该纪律在各轮被声明：E31/E34/E36/E37 报告均写“模型、容量规划、主块预算未动/冻结版不变”；E36 §0 对组合命名（T8/O8/B）作了区分。

**证据**：RESULTS_E31.md/RESULTS_E34.md/RESULTS_E36.md/RESULTS_E37.md 的“边界”节；RESULTS_SUMMARY.md §3.4-3.8。

**覆盖与缺口**：已覆盖：候选机制轮次声明为非本轮变量固定。未覆盖：E38 的 LITS 臂**改了预测配置（model_backend）**，属本轮变量；报告未用“组合改造”措辞但已在 §5/§7 声明口径。

**关闭标准**：无。

**未能核实**：各轮“均固定”的声明是报告自述，我未逐轮比对源码 diff 验证确无其它改动。

> **反驳复核**：维持原判（stands）。

### [A-8-9-13] §9.2 工作负载矩阵八行：静态点查、只改值 0/5/10/25%、持续插入 0/5/10/25%、短期替换 0/5/10/25%、长期替换 same 0/25/50/100/200%、长期替换 grow、压力正确性序列、范围扫描（初始/25%/200%，n=16/128/1024）。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：7 行有对应轮次：点查 E21(+E38 P3 §3)、只改值/插入/替换 E22/E25/E26、长期 same/grow RESULTS_LONG、范围扫描 E23/E27（n=16/128/1024，RESULTS_E27.md:88）。**第 7 行“压力正确性序列”无独立负载**：维护分支由 check_* 的非空性断言覆盖（check_lits_index 断言前缀重建 4／分裂 22／压缩 48208／溢出峰值 4851／溢出删除 1070 各 ≥1，results/main/e38_lits_index_mixed.jsonl）。

**证据**：EXPERIMENT_INDEX.md §1 各行；RESULTS_E21/E22/E25/E26/RESULTS_LONG/E23/E27（**均 DRIFT 构建**）；check_lits_index（**LIVE 14/14**）。

**覆盖与缺口**：已覆盖：7 行有独立负载（M0 后端为主，LITS 只做了点查）。未覆盖：**压力正确性序列作为负载未做**（只有门内的非空性断言，没有单独报告“区域边界外新键/单区域集中/跨分裂阈值”的耗时与行为）；长期替换与扫描**未在 LITS 后端跑**。

**关闭标准**：补一行“压力正确性序列”负载（或明确记录“由门覆盖、不成负载”的裁定）；关闭标准：八行各有归属（负载或成文裁定）。

**未能核实**：八行是否在某个早期目录（validation_e5..e24）都有对应，未逐目录核。

> **反驳改判**：`已完成并采用` → **`部分完成`**（downgraded）
>
> 配对、逐进程中位数、噪声底、方向计数、绝对耗时都在（analyze_lits.py:70-95 只按 (run_tag,rep) 配对、noise=max/min）。但任务该句还要求『同时报告每个进程的比值』，analyze_lits.py:98-103 的 fmt() 只打印中位数/方向/range/噪声，不逐进程打印；results/main/e38_p3_analysis.txt 全文亦只有 range x-y，无五个进程各自的比值（analyze_e31.py 才有 per-process medians）。claim 的未覆盖只提置信区间，漏了这一项。

### [A-8-9-14] §9.2 每种负载独立从同一初始键值集合与配置开始；后续检查点表示同一条负载历史的继续，不能独立重建后冒充更新后的状态。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | medium |

**实现**：已实现：E22 明示“三类负载各自从相同的初始快照独立开始”；RESULTS_LONG.md:20 明示周期历史连续、“活跃记录数恒定”。

**证据**：RESULTS_E22.md；RESULTS_LONG.md:11/20/69；E26_LONG.md:10（live count 恒定）。

**覆盖与缺口**：已覆盖：独立起点与连续历史。未覆盖：续历史的性质在 LITS 后端未测（本轮没跑长期替换）。

**关闭标准**：无。

**未能核实**：各轮驱动是否真的复用同一初始键值集合，我未逐行核 run_*.py 的数据生成（除 run_lits.py 已读）。

### [A-8-9-15] §9.2 替换周期＝删除一个存在键＋插入一个不同且不存在的新键＝两个索引操作；报告同时写周期数、操作数和活跃记录数。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：已实现：LOG.md:144 “replace 同时报周期数与操作数”；驱动在每段生成 victim/fresh 并校验（bench_long_art.cpp:246-297 的 seg：erase victim＋upsert fresh）；活跃数由参考表 ref.size() 每段核验（bench_long_art.cpp:314-318）。

**证据**：LOG.md:144；bench_long_art.cpp:246-318；RESULTS_LONG.md:20。

**覆盖与缺口**：已覆盖：周期=2 op、报周期/操作/活跃数。未覆盖：无（形式满足）。

**关闭标准**：无。

**未能核实**：无。

### [A-8-9-16] §9.2 `same` 只保证长度或生成分布受控，不自动保证组号分布相同；报告组级记录与字节需求变化；`grow` 不能让键无限增长；新键分布应覆盖内部位置，另单列边界与单区域压力，不无意间只插末尾。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：部分实现：same/grow 两模式存在（run_lits.py PHASES；bench_long_art 的 mode 参数）；组号分布变化在 RESULTS_E38.md §3.5 报了（“用到的组”“最大组占比”“组号与原模型不同的键”）；LITS_AUDIT §8.2 报更新键 max 长度（same 158/156/66、grow 166/164/74），grow 未无限增长。

**证据**：RESULTS_E38.md §3.5；LITS_AUDIT.md §8.2；run_lits.py:51-57。

**覆盖与缺口**：已覆盖：same/grow、grow 有界、组号分布（模型侧）。未覆盖：**“新键分布覆盖内部位置”与“单列边界/单区域压力”没有专门实验**（驱动在末尾/内部的行为未成文核验，除二分比较等间接证据）。

**关闭标准**：补一个“新键位置分布”的统计（内部 vs 末尾）并把边界/单区域压力单列；关闭标准：有分布直方图或成文裁定。

**未能核实**：生成器的键位置分布（内部/末尾比例）我未读 data.hpp 的生成逻辑细节。

### [A-8-9-17] §9.2 数据集名称和键数从元数据读出；Tranco 是来源名，不因数据来源含百万域名就写“测试了一百万键”；历史出现过 262,144 键，必须逐轮注明真实数量。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：已实现：LITS_AUDIT.md §8 明示“百万域名是来源名称，本轮实测键数是 262,144”；EXPERIMENT_INDEX.md 与各轮 RESULTS 逐轮注明 131,072/262,144。

**证据**：LITS_AUDIT.md §8（规模表）；RESULTS_E38.md §9.1（262,144）；EXPERIMENT_INDEX.md §1。

**覆盖与缺口**：已覆盖：三个语料的真实键数逐轮注明。未覆盖：无。

**关闭标准**：无。

**未能核实**：无。

### [A-8-9-18] §9.3 每个检查点保留两套查询：① 固定键序列（重算存在性与值，报真实命中率）；② 固定命中比例（按当前存活/不存在集合生成，各臂同检查点同序列）。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：部分实现：固定查询队列＋`queue_hash`/`queue_acc` 断言（RESULTS_SUMMARY.md §3.6：1080 对 B/O 单元 0 处不符）；E35 把固定队列按 Result::source 固定成主块/溢出/未命中三队；E38 P3 §3 把纯命中/纯未命中作为独立负载并断言两臂同键同结果；E21 给“全部查询/仅命中/仅未命中”三列。

**证据**：RESULTS_SUMMARY.md §3.6；RESULTS_E35.md（DRIFT）；RESULTS_E38_P3.md §3（LIVE 差）；bench_prefix.cpp/bench_lits_adapter.cpp。

**覆盖与缺口**：已覆盖：固定序列与同序列断言、按命中来源分队列、真实命中率（E21）。未覆盖：**“固定命中比例、按当前存活集合生成”这一族的显式生成器**未见单独成文（E21 的“仅命中/仅未命中”更接近“固定来源”，而非“固定命中比例”）。

**关闭标准**：确认或补“固定命中比例”序列生成器并成文；关闭标准：两套序列各有生成规则与同序列断言。

**未能核实**：“固定命中比例”是否就是 E21 的仅命中/仅未命中两列，未在源码中确认生成器语义。

### [A-8-9-19] §9.3 固定序列命中率下降与布局变化会混合影响成本，不能单独当作退化证据；查询进入回退统一容量区域的比例、这些区域承载的记录比例同时记录。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：已实现：RESULTS_E38_P3.md §5.1 同时给“回退区域占比（LITS 区域/活跃区域）”与“回退查询占比（命中/溢出/未命中队列）”，并区分“当前活跃状态”与“整轮累计”两种口径（§5.1 更正：末态 95.3% 对累计 18.2%）。

**证据**：RESULTS_E38_P3.md §5.1；RESULTS_E38.md §9.1（累计 vs 末态回退）。

**覆盖与缺口**：已覆盖：回退区域占比与承载记录占比、回退查询占比。未覆盖：把“命中率下降”与“布局变化”在**非回退**场景下的分离未单独做（该纪律主要针对回退，已满足）。

**关闭标准**：无。

**未能核实**：无。

### [A-8-9-20] §9.3 诊断复制和空间遍历可能预热数据；建议与计时运行分离；若不得不在同一进程采样，每次正式测量前采用一致的预热程序，不能让一臂因额外诊断获得缓存优势。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | medium |

**实现**：已实现：干净计时构建（无诊断计数、无分配包装）与 `-DE25_DIAG` 诊断构建分开（RESULTS_SUMMARY.md §3.5 工具段；RESULTS_E38_P3.md 附：diag_lits_adapter 独立运行不与计时并行）；`run_current` 严格入口拒过期二进制。

**证据**：RESULTS_SUMMARY.md §3.5；RESULTS_E38_P3.md 附（diag 独立）；tools_build.py（run/run_current/run_recorded）。

**覆盖与缺口**：已覆盖：诊断与计时分离。未覆盖：**若同进程采样的“一致预热程序”未成文**（各轮预热规则未逐轮记录）。

**关闭标准**：补一条成文的预热程序（点查固定次数/扫描相同规则/写操作牺牲实例）；关闭标准：写进协议文档。

**未能核实**：各轮 run_*.py 的预热具体做法我只读了 run_lits.py，其余未核。

### [A-8-9-21] §9.4 ART 的同语义要求，首轮至少核对八条：计时入口、键值语义、所有权、构建边界、动态操作、范围扫描、平台与编译、记账。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：分散满足、无集中成文：计时入口=完整键（bench_long_art.cpp:303-305 `art->find(k)`/`s->find(k,qmode)`）；键值语义=value+1 编码、`upsert` 封装插入、重复键不新增记录（native_e22.hpp:101-107）；所有权=libart 自行持有键（art_insert 传入 c_str），原型持 std::string，**差异未成文列出**；构建边界=E20“计时起止边界统一、各自必须做的准备全部计入”✅（RESULTS_E20.md §… 计时方案表）但未逐条列排序/去重/统计；动态操作=同轮同序、维护参考表、spot check、计时只包 op 循环（bench_long_art.cpp:296-318）；范围扫描=libart 无 seek→E23 明确拒绝排名 ✅（符合“不用全树遍历硬凑”）；平台与编译=E33 编译器行、冻结协议 -O3 -march=x86-64-v2 -static，ART 与原型同程序同编译 ✅；记账=native_e22 干净计价不加包装、空间由 -DE20_ART_MEM 单列 ✅。

**证据**：bench_long_art.cpp（DRIFT 9/11）；native_e22.hpp；RESULTS_E20.md（DRIFT）；RESULTS_E23.md（DRIFT）；RESULTS_E31.md §4（DRIFT）；RESULTS_E33.md（编译器行）。

**覆盖与缺口**：已覆盖：计时入口、键值语义、动态操作、范围扫描（拒绝）、记账。未覆盖：**所有权差异、构建边界的逐步清单未成文**；平台信息里 CPU 型号/绑核结果/宿主虚拟化未逐条记录。

**关闭标准**：把八条写成一张核对表（含“未覆盖”与理由），补所有权与构建边界两栏；关闭标准：八条逐条有结论（满足/不适用/未覆盖）。

**未能核实**：E12/E20 原生 ART 适配器（native.hpp）我未逐行读；所有权与构建边界可能在那里有更细的说明。

### [A-8-9-22] §9.5 正式建议至少 5 个独立进程、每进程 3 个重复、变体执行顺序平衡轮换；同一操作流各臂从头重放；重复也必须恢复初始状态；同种子重复只检测测量波动。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：已实现：run_lits.py（PHASES 3 reps、轮换、位置平衡断言，:145-168；`--reps` 单一来源）；E38 P3 §0 明说 5 进程×3 语料×2 模式×3 重复、臂在同一次重复内轮换且断言每臂每位置恰好一次；E25/E26 用 5 进程×5 重复配对。

**证据**：run_lits.py:102-175；RESULTS_E38_P3.md §0（run_id=p3b）；LOG.md:140（run_e25.py 5 进程）；analyze_lits.py。

**覆盖与缺口**：已覆盖：5 进程×3 重复、轮换、平衡断言。未覆盖：**“重复实验必须恢复初始状态”**在各驱动中是否有显式重建未逐行核（P3_PRECHECK 显示每臂对象在循环体内析构、无跨重复累积）。

**关闭标准**：无。

**未能核实**：除 run_lits.py 外，其余驱动的“恢复初始状态”我未逐行核。

### [A-8-9-23] §9.5 记录 CPU 型号、可用核心、绑核结果、宿主/虚拟化环境、编译器和系统版本；沿用可复现编译目标（如 -O3 -march=x86-64-v2 -static），若必须变更则各臂统一变更并作为新平台轮次，不与历史绝对值拼接。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：部分实现：EXPERIMENT_INDEX.md §平台表记录编译器/OS（WSL2 Ubuntu 24.04＋g++ 13.3.0）与“单核绑定”（E9–E23）；E33 给出逐字编译行（-O1 -g -march=x86-64-v2 -fsanitize=…，sanitizer 构建）；E38 P3 §0 写“单核绑定”。**CPU 型号、可用核心数、绑核结果、宿主/虚拟化未逐条记录。**

**证据**：EXPERIMENT_INDEX.md §平台；RESULTS_E33.md:72-78；RESULTS_E38_P3.md §0/§7。

**覆盖与缺口**：已覆盖：编译器/OS/绑定/优化目标/平台迁移纪律。未覆盖：CPU 型号、可用核心、宿主虚拟化、绑核结果的具体值。

**关闭标准**：补一张每轮平台记录（CPU 型号等）；关闭标准：平台字段齐全。

**未能核实**：是否在未列出的日志（logs/*.log）里有 CPU 型号，未逐一检索。

### [A-8-9-24] §9.5 点查按固定次数预热并校验结果；扫描对各臂相同预热规则；写操作预热不能提前改变被测初始状态（牺牲实例预热或完整重放丢弃）；绑核与预热不保证消除宿主噪声；先冒烟再全量，不得看结果后选择性加重复。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：部分实现：RESULTS_SUMMARY.md §6.4 承认“预热不同”使绝对耗时不可跨轮；E38 P3 §0 写了配对/噪声底口径但**未写点查预热次数**；写操作“牺牲实例预热”未见。冒烟→全量在 E38 P3 有（P3_PRECHECK 的进入条件）。

**证据**：RESULTS_SUMMARY.md §6.4；RESULTS_E38_P3.md §0；P3_PRECHECK.md（进入正式采集的条件）。

**覆盖与缺口**：已覆盖：跨轮不可拼接、冒烟先行、噪声底纪律。未覆盖：**点查/扫描的预热程序与固定次数未成文**；写操作预热方案未实现。

**关闭标准**：成文预热规则并落地；关闭标准：协议文档写明点查预热次数、扫描预热规则、写操作预热方式。

**未能核实**：各基准源码里的预热循环我未逐行读（bench_lits_adapter.cpp 45KB 只读了片段）。

### [A-8-9-25] §9.5 比值 r[p,j]=T_cand/T_base 同重复内配对；主估计 median_p(r[p])；同时报每进程比值、最小/最大值、胜出进程数、各臂绝对耗时；置信区间以进程为分组单位；5 进程不等于高精度显著。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：已实现：analyze_lits.py（同重复内配对、逐进程中位数、方向计数、噪声底=max/min），analyze_e31.py/analyze_o1.py 同口径；RESULTS_E38_P3.md §0 与 results/main/e38_p3_analysis.txt 逐格打印中位数、方向(k/5)、噪声底、绝对耗时。

**证据**：analyze_lits.py；analyze_e31.py（cross-process spread = noise floor）；results/main/e38_p3_analysis.txt；RESULTS_E38_P3.md §0。

**覆盖与缺口**：已覆盖：配对、中位数、min/max、方向计数、绝对耗时。未覆盖：**显式置信区间未计算**（报告用噪声底替代）；报告已声明“5 进程不等于高精度显著”。

**关闭标准**：若要置信区间，按进程分组补区间；关闭标准：若声称显著则给区间。

**未能核实**：无。

### [A-8-9-26] §9.5 同一次重复内的配对比值才可比较；不能把 E25 的 V2/V1、E26 的 C2/C0、E27 的 S1/S0 相乘得最终收益；用同配置双实例作 A/A 控制；差异与噪声同量级则写“未能区分”；同时报累积耗时与区间耗时（后者除以实际操数）；0% 检查点无更新吞吐；批量 p99 不得写成单次操作 p99。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：大部分已实现：禁跨轮相乘写入多处纪律（RESULTS_SUMMARY.md §3.2/§3.3、FINAL_RESULTS_TABLE.md §6）；0% 检查点只做初始+查询（RESULTS_E25.md §3.2 检查点定义）；累积与区间耗时（E25/E26 报 cum_ms 与区间）；批量 p99≠单操作 p99（E32 §9 单操作 p99 单列）。**A/A 双实例控制未见专门运行**：只在 E36 §… 与 RESULTS_SUMMARY.md 以“取决于 A/A 波动”引用，没有 A/A 对照的原始数据。

**证据**：RESULTS_SUMMARY.md §3.2/§3.3/§6；FINAL_RESULTS_TABLE.md §6；RESULTS_E32.md §9；RESULTS_E36.md（“A/A 波动”措辞）。

**覆盖与缺口**：已覆盖：禁相乘、0% 无吞吐、累积+区间、批 p99 口径。未覆盖：**A/A 同配置双实例控制未做**（判据“未能区分”因此缺一个基准）。

**关闭标准**：补一次同配置双实例 A/A 运行，或成文声明“本轮无 A/A，噪声底代替”；关闭标准：该判据有数据或声明。

**未能核实**：A/A 是否在某个早期目录做过，未逐目录检索；仅在 RESULTS_SUMMARY/E36 找到引用而非数据。


---

## A-10-13  —— 架构线 §10–§13：判定层次、采用矩阵、阶段、交付纪律、研究贡献（16 条）

### [A-10-13-00] §10.1 四个层次的判定：正确性门槛/机制门槛/性能门槛/适用性门槛，以及"实验前约定实用阈值（如目标负载维护时间下降≥5%），不根据结果反向修改门槛"

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：无单一开关；以报告级纪律实现。正确性门槛：E29 用 check_c3 在四检查点×三数据集全量比对内容与键序（RESULTS_E29.md §2）；E31 用 check_e31 并断言 subblock_reclaims>0（index.hpp:695 subblock_reclaim 默认 false）。机制门槛：E28 的 F0–F3 分层（RESULTS_E28.md §0），E30 把盈亏平衡门槛实测为 70 ns/记录（RESULTS_E30.md:91）。性能门槛：同轮配对、方向计数、噪声底（analyze_lits.py:1-20）。适用性门槛：每轮 RESULTS_EN §适用边界（如 RESULTS_E31.md:242）。阈值预先声明：仅 E30 §4 有（门槛 70 ns，实测 6.1–11.4 ns，RESULTS_E30.md:148）。

**证据**：源码身份：index.hpp（LIVE，见 results/main/g0_build_drift.txt）；构建身份：logs/build_check_e31.json、build_check_c3.json（DRIFT）。门运行记录：RESULTS_E29.md §0/§2、RESULTS_E30.md §4–§5a、RESULTS_E31.md §8。原始数据：results/main/long_c3_{same,grow}.jsonl、long_e31_{same,grow}.jsonl、repack_cost.jsonl。

**覆盖与缺口**：已覆盖：M0=Context16 后端；三数据域 mixed/skew/tranco；操作 insert/update_only/replace/query/scan；检查点 0/25/50/100/200%；失败路径 F0–F3 与 C3 触发。尚未覆盖：只有 E30 一处把"实用阈值"在测量前写成数字；E29/E31/E34/E37 的决策未见"事前约定阈值"的记录，因此"不根据结果反向修改门槛"无法核实。

**关闭标准**：写一份门清单，逐实验登记四项门槛的判定结果与所用阈值，并补上 E29/E31/E34/E37 的事前阈值声明；关闭标准：每个采用/候选决定都能引用它的事前阈值与四门结论。

**未能核实**：无法核实 E29/E31/E34/E37 是否在做实验前声明了实用阈值（只有 E30 例外）；也未找到"禁止反向修改门槛"被违反或被遵守的直接记录。

> **反驳改判**：`已完成并采用` → **`部分完成`**（downgraded）
>
> 我逐条去核了它引的证据。①失败分层的两个装置 diag_overflow.cpp / diag_transfer.cpp 在 g0_build_drift.txt 里都是 DRIFT（:75、:81），我重算磁盘哈希复现了 DRIFT：两本的记录 index.hpp 都对不上当前磁盘的 587172d2。g0_inherited_gates_rerun.txt 只重跑了 E24 的 8 个门，没有任何 diag_overflow / diag_transfer 的重跑。按项目自己的纪律，DRIFT 的门的'通过'不能当作当前源码的证据，所以'失败分层表（卡字节 208,689 对卡槽位 18,527）'不是当前源码的证据，而只是 2026-09-20 那份被取代源码上的一次读数（RESULTS_E28.md:40 的数字我确认原文如此，但那是报告复述，不是可绑当前源码的门记录）。②SA1 要求的'冻结版同轮基准 / ART 对照'，主张指到 RESULTS_E31.md §4 的 bench_long_art——E31 是 SA2 的轮次，不是 SA1；且 bench_long_art 在 g0_build_drift.txt:34 是 DRIFT，重算仍是 DRIFT（9/11，index.hpp+switches.hpp 变了）。③覆盖栏写'A0–A5 可行性分级'是夸大的：方案原文（结构审查方案:217-226）定义的 A0–A5 与 E28 的 F0–F3 不是同一套（原文自己说'A0–A5 不覆盖 E28 的 F1/F2 定义'），MODEL_AND_MAPPING…

### [A-10-13-01] §10.2 采用矩阵（十行观测→处置）：空间降而维护快→默认候选；空间降但维护慢→空间优先配置；新溢出减少旧溢出不降→预防收益并评估有界迁回；A1 高但整理慢→查复制/临时分配；A1 低 A2 高→双块；A1–A3 低 A4 高→区域内再分配；溢出降但删除替换仍慢→独立评估溢出容器；维护改善但点查不动→访问路径审查；只有换模型才出现收益→报告模型—结构交互；ART 全占优→工程采用 ART、研究…

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **部分完成** |
| **置信度** | low |

**实现**：未实现为一份矩阵应用文档。仅见隐式对应："空间降但维护慢→空间优先配置"对应 E31（index.hpp:695 subblock_reclaim 默认 false，FINAL_RESULTS_TABLE.md §5b）；"预防收益/评估有界迁回"对应 E34 O1（index.hpp:738）与 E37（index.hpp:745）；"工程采用 ART"不在本轮范围（无 ART 采用决定）。MODEL_AND_MAPPING.md §5 附录覆盖了方案 §5–§12，但未覆盖 §10.2。

**证据**：MODEL_AND_MAPPING.md:144-168（对照表不含 §10.2）；FINAL_RESULTS_TABLE.md 第 5b 节；docs/字符串学习型索引_结构审查与优化执行方案_20260920.md:554-565（矩阵原文）。

**覆盖与缺口**：已覆盖：十行中约 3 行有隐式对应。尚未覆盖："双块试验"、"区域内再分配"、"点查与维护是不同瓶颈→访问路径审查"、"模型—结构交互"四类处置无任何矩阵式登记（E35 的读路径诊断接近"访问路径审查"但未按矩阵行报告）。

**关闭标准**：出一份矩阵应用记录，逐行标注"该观测是否已发生 → 采用哪一条处置 → 证据文件"；未发生的行标"未发生"。关闭标准：十行全部有明确状态。

**未能核实**：无法核实矩阵是否在 E30–E38 的决策过程中被实际查阅；只找到事后可对应的处置，没有决策时引用矩阵的记录。

> **反驳改判**：`已完成，负结果或条件性候选` → **`部分完成`**（downgraded）
>
> 这一条的证据栏自己把话说错了。它写'构建身份：logs/build_check_e31.json（LIVE，见 results/main/g0_build_drift.txt）'——但它引的那份 g0_build_drift.txt 第 57 行把 check_e31 列在 ### DRIFT 之下（check_e31 8/10 changed: index.hpp,switches.hpp）。我打开 logs/build_check_e31.json 核对：记录 index.hpp=0a9a5eb3…、switches.hpp=51f838ee…，而当前磁盘是 index.hpp=587172d2…、switches.hpp=794ad74d…，两处都对不上——它当时是 DRIFT，不是 LIVE。（说明：我审计期间该记录被一个并发进程于 2026-09-23 02:16:47 重建为 14 源、index.hpp 已等于磁盘；但这是主张写下之后发生的事，主张当时的标注是错的。）实现本身我确认在当前源码里：Config::subblock_reclaim index.hpp:695 默认 false，且真的被读（index.hpp:3262、:3345），repack_subblock index.hpp:1685、first_candidate index.hpp:1663 都在。门的非空性断言也是真的：check_e31.cpp:230 在 frac==2.00 时断言 subblock_reclaims==0 就抛错，且四个检查点无条件跑满（args 只选数据集与模式…

### [A-10-13-02] §10.3 E29"回不了本"的进一步检验：完整成本 = 初始构建 + 操作序列执行 + 在线维护 + 明确定义的检查点维护；按事先声明比例交错执行更新/点查/扫描；摊销数 ΔT_m/δt_q 仅在 δt_q 测得为正时给出；空间受限收益须在实际内存约束下测；长期窗口只报"在已测窗口增长/趋缓"，不写成已证明无限稳定

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：未实现。没有交错混合操作序列的测量，也没有 ΔT_m/δt_q 摊销数。E29 §5a 只给了"更新÷查询收益"的比；RESULTS_LONG.md:139 正确遵守了"200% 是有限窗口、只报增长/趋缓"的措辞纪律，但那不是完整成本检验。

**证据**：MODEL_AND_MAPPING.md:165 明写"§10.3『回不了本』的进一步检验｜未做（读／扫描收益未并入）｜E29 §5a 只给了更新÷查询收益的比，未做交错混合"；E29_C3.md（只含同进程配对的更新耗时，无交错负载）；RESULTS_E30.md:201（"未测回收对查询的收益"）；RESULTS_LONG.md:135-142（窗口措辞）。

**覆盖与缺口**：已覆盖：单操作均值与同轮配对更新比值（E29/E30/E31）。尚未覆盖：交错混合序列、完整成本四项分解、依赖 δt_q 的摊销数、实际内存约束下的空间收益。

**关闭标准**：①设计并预先声明更新/点查/扫描比例；②同轮测交错序列，对冻结版与 ART 各一遍；③按 完整成本=构建+序列+在线维护+检查点维护 报告；④δt_q>0 时才给 ΔT_m/δt_q。关闭标准：一份含交错负载与四项分解的同轮表。

**未能核实**：无（该条由 MODEL_AND_MAPPING.md:165 自认未做，与我逐文件核查结果一致）。

> **反驳改判**：`已完成，负结果或条件性候选` → **`部分完成`**（downgraded）
>
> 实现与开关登记我核过，成立：Config::overflow_stable_store index.hpp:738（默认 false）在 index.hpp:1841 被读，Region::OverflowList 定义在 index.hpp:1039（using Overflow = OverflowList，:1294）；Config::overflow_migrate index.hpp:745 在 index.hpp:3285、:3365 被读；单变量消融确实由 require_only_switches_differ（switches.hpp:108-144，十四项字段表，我数过正好 14 项）实现。但证据栏绑不住当前源码：①E34 的计时 long_o1_* 来自 bench_long_o1，g0_build_drift.txt:36 判 DRIFT（我重算 9/11，index.hpp+switches.hpp 变）；②E37 的计时 migr_* 来自 bench_migrate，:37 DRIFT（重算 9/11）；③E37 的正确性门 check_migrate 在 :62 判 DRIFT（重算也在主张写下时对不上磁盘）；④E37 的可行性诊断 diag_migrate / diag_migrate_b 在 :71、:72 都是 DRIFT。只有 E34 的差分门 check_overflow_store 是 LIVE（g0_build_drift.txt:15，记录 index.hpp=587172d2 = 磁盘，我核对过）。所以 E34 的'表示确实切…

### [A-10-13-03] §11 SA0 来源与契约：核查当前源码、实际配置、模型接口、ART 适配器、构建闭环；必交付 版本清单、调用链、模型说明、不变量清单；当前源码或语义不明确时不出新归因结论

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：MODEL_AND_MAPPING.md 自述为"SA0 交付"（MODEL_AND_MAPPING.md:3），含版本/配置（§1）、从完整键到子块的调用链（§2, 用 diag_trace.cpp 打印真实映射）、模型说明（§1/§3）。不变量只验了 I1（MODEL_AND_MAPPING.md:60）。配置面登记于 switches.hpp:29-54/63-77/81-97/108-144 四张表；ART 来源固定于 third_party/fetch_libart.py:18（COMMIT=301046804af165269e37da6725f5a4aec9ecc881）。E38 另出 LITS_AUDIT.md。

**证据**：MODEL_AND_MAPPING.md（SA0 交付，含 §1.2 八问逐条回答）；results/main/g0_evidence_timeline.txt（源码最后改动 index.hpp 2026-09-22 12:51:49、switches.hpp 10:57）；LITS_AUDIT.md；diag_trace.cpp:1（"E33 / SA0: print the real mapping chain for real keys"）。

**覆盖与缺口**：已覆盖：版本清单、调用链、模型说明、配置面、ART 提交与扫描契约（保序性由 check_scan_contract 守，RESULTS_E33.md:5）。尚未覆盖：不变量清单只到 I1，无 I1–I12 全表（INVARIANTS.md 不存在）。

**关闭标准**：补齐 I1–I12 不变量清单及其对应验收门（或逐条注明不适用）。关闭标准：INVARIANTS.md 存在且每条 I1–I12 有门或"不适用"理由。

**未能核实**：未能核实 I2–I12 的具体内容——原任务书在其他章节列出该清单，本节只要求"不变量清单"这一交付物；我只能确认它未以独立文件出现，且 MODEL_AND_MAPPING 只验了 I1。

> **反驳改判**：`已完成，负结果或条件性候选` → **`部分完成`**（downgraded）
>
> 它引的交付物形态与它自己的结论直接打架。方案 §11 SA4 的必交付是'同轮最终表'（§12.4 也给了同轮最终表模板）。主张指到 FINAL_RESULTS_TABLE.md 第 5b 节——我读了：§5b（第 94 行起）是'候选机制（未采用；开关默认关闭）'的逐行表，而该文件 §6 第 161 行与 §7 第 163 行自己写明这是跨轮逐行表、'不能把各行拼成一个最终版本 vs 原版的总加速比'、同轮对照散落在 RESULTS_E26/E27/E31/E34 各轮。也就是说'同轮最终表'这个形态要件没有交付，而主张的覆盖栏自己也承认了这一点（'FINAL_RESULTS_TABLE.md 自述是跨轮逐行表、非同轮统一重测（其 §6），不满足 §12.4 的同轮最终表形态'）——一条自己承认缺交付要件的条目不能标'已完成'。此外 §5b 各行的数值来源我在 g0_build_drift.txt 里逐一查过：bench_long_o1(:36)、bench_tag(:45)、bench_migrate(:37)、bench_long_art(:34) 全是 DRIFT，我重算仍是 DRIFT——所以这张表既有形态问题又有身份问题。主张引的 RESULTS_E31.md:242 / E34.md:260 / E35.md:162 三处'适用边界'确实在（我逐处读了原文），但那是边界描述，不是同轮最终表。'两个模型后端仅在 M0 下计过性能、LITS 无性能数据'它已自认。故降级。

### [A-10-13-04] §11 SA1 结构诊断：成本计数、空间账本、A0–A5 可行性、冻结版同轮基准；必交付 失败分层表、压力分布、ART 对照；若主要不是 A1 则不强推子块整理

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：失败分层以 F0–F3 落地（E28）：F0 原路径、F1 只回收目标子块、F2 整组重排、F3 区域级（判定无对象）——RESULTS_E28.md §2，工具 diag_overflow.cpp / diag_transfer.cpp。空间账本 E24（主块账本恒等式、垃圾、溢出余量，RESULTS_SUMMARY.md 第 5 节）。ART 对照见 RESULTS_E31.md §4（bench_long_art）。

**证据**：RESULTS_E28.md:1-40（失败分层、卡字节 208,689 对卡槽位 18,527）；RESULTS_LONG.md:71-98（换组率 0.016%–0.046%，否定旧归因）；RESULTS_SUMMARY.md:360-366（空间账本恒等式 12 格成立）；日志见 RESULTS_E28.md 原始数据节。

**覆盖与缺口**：已覆盖：M0=Context16；mixed/skew/tranco；插入/替换；同轮 ART 对照；A0–A5 可行性分级。尚未覆盖：F3 区域级容量错配（"降级：在 F2 覆盖的部分无对象，其余未排除"，MODEL_AND_MAPPING.md:158）；组间容量错配未被任何一层覆盖。

**关闭标准**：无必须动作（F3 与跨组容量属候选，已按"不强推"关闭）。若后续进入跨组方向，关闭标准是补一层覆盖区域级容量错配的诊断。

**未能核实**：未能核实"压力分布"这一交付物的独立文件形式；我只在 E28/F0–F3 与 E24 空间分解中找到等价内容。

> **反驳复核**：维持原判（stands）。

### [A-10-13-05] §11 SA2 最小局部原型：仅实现目标子块整理，先正确性再干净计时；必交付 事件断言、参考差分、BASE/GROUP/LOCAL 对照；正确性失败先修，无价值则保留负结果

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：E31 实现 Config::subblock_reclaim（index.hpp:695 默认 false），核心 repack_subblock（index.hpp:1685）与 first_candidate（index.hpp:1663）；事件断言在 check_e31.cpp（断言 subblock_reclaims>0）；干净计时 bench_long；BASE/GROUP/LOCAL/ART 命名对照见 MODEL_AND_MAPPING.md:162（BASE=E31 的 C2 臂、GROUP=E29 的 C3 臂、LOCAL=E31 臂、ART=E31 §4、ARR=E23/E27）。

**证据**：构建身份：logs/build_check_e31.json（LIVE，见 results/main/g0_build_drift.txt）；门运行记录：RESULTS_E31.md；原始数据：results/main/long_e31_{same,grow}.jsonl、e35_mech.txt；分析入口 analyze_e31.py。

**覆盖与缺口**：已覆盖：M0=Context16；mixed/skew/tranco；长期替换 same/grow 到 200%；正确性含参考表差分与事件断言；同轮 ART。尚未覆盖：§6.3 准备—验证—提交（临时缓冲）变体未做（MODEL_AND_MAPPING.md:153）；E31 为原地收紧，无临时缓冲异常安全测试。

**关闭标准**：subblock_reclaim 默认关闭、保留为候选；若要升级为默认，关闭标准是补"空间优先配置抵偿性"的读写混合负载评估。不重做同一试验。

**未能核实**：无（实现、门、原始数据、构建记录逐项对得上）。

> **反驳改判**：`已完成并采用` → **`部分完成`**（downgraded）
>
> §12.3 是一条逐项列举的硬要求，其中一项我能直接验伪：'批量构建逐个报告状态'。tools_build.py 的批量入口是 `else: for t in sys.argv[1:]: build(t)`（第 418–419 行），而 build() 在编译失败时直接 `sys.exit(1)`（第 308 行）——所以第一个目标失败之后，后面既未尝试也未报状态，'逐个报告状态'没有实现。主张的覆盖栏自己也写了这句（'批量构建遇失败即中止…后续目标既未尝试也未报状态…不满足逐个报告状态'），等于自认一条被明确列举的子要求未达成；既然任务书原文写的是'批量构建逐个报告状态，不能因某个命令提前退出而把未尝试的目标当成功'，那就不能标'已完成并采用'。其余部分我核过是对的：build() 第 243 行编译到临时文件、成功才 os.replace 原子改名；失败路径删 build/<target>.built.json 与 .exe 再退（第 296–308 行）；load_record 第 320 行无记录即 RUN REFUSED；run_current 第 372 行源哈希漂移即 rc=2 拒绝；'run' 就是 run_current 的别名（默认失败关闭）；RESULTS_FREEZE.md §5 与 §7 有失败路径被阻断的实测表（改坏源码→rc=1→RUN REFUSED rc=2）；E33 首次接 ASan+UBSan（RESULTS_E33.md:5），E38 扩到七个目标（RESULTS_E38.md §9.4）。另外第 3 步'冒烟'主张自认'实践中做但未自…

### [A-10-13-06] §11 SA3 条件分支：按 SA1/SA2 选择迁回、双块、区域容量或容器中的一条；必交付 单变量或明确组合消融；首个有效方案足以回答问题后不扩展无依据分支

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **已完成，负结果或条件性候选** |
| **置信度** | high |

**实现**：选定"溢出容器"一条：E34 实现 Config::overflow_stable_store（index.hpp:738 默认 false）+ Region::OverflowList；随后接 E37 的有界迁回 Config::overflow_migrate（index.hpp:745 默认 false）。双块与区域容量分支未开。单变量消融由 require_only_switches_differ 断言（switches.hpp:108-144，十四项字段表）。

**证据**：RESULTS_E34.md（O1 原型与同轮对照）、RESULTS_E37.md（有界迁回 R0/R1 两臂）、RESULTS_E36.md §0 收官决定；原始数据 results/main/long_o1_{same,grow}.jsonl、migr_{same,grow}.jsonl；分析入口 analyze_o1.py / analyze_migr.py。

**覆盖与缺口**：已覆盖：M0=Context16；三数据域；长替换 same/grow 到 200%；点查/扫描/空间/尾延迟；ART 同轮（E31 §4）；失败路径（E36/E37 拒绝路径与 check_overflow_store / check_migrate）。尚未覆盖：双块与区域内再分配分支未评估。

**关闭标准**：O1（overflow_stable_store）与 R1（overflow_migrate）均默认关闭；E37 的下一步已写明"只扩大回收触发面、不做迁回"是否等效。关闭标准：该追问有结论后，两候选各自定去留。

**未能核实**：无。

### [A-10-13-07] §11 SA4 完整评估：最终候选 vs BASE/ART，点查、写入、替换、扫描、空间；必交付 同轮最终表、适用范围、风险；收益无法区分噪声则保持原默认

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成，负结果或条件性候选** |
| **置信度** | medium |

**实现**：各候选均做了同轮 BASE/ART 对照与适用边界：E31（bench_long_art）、E34（O1 vs 冻结 + bench_scan_s3）、E37（bench_migrate + bench_scan_s5）；汇总入 FINAL_RESULTS_TABLE.md 第 5b 节。收益不可区分时保持默认：E31/E34/E37 三开关均默认 false（index.hpp:695/738/745）。

**证据**：FINAL_RESULTS_TABLE.md 第 5b 节（逐行含"被测版本÷对照/配对结果/来源"）；各类别适用边界见 RESULTS_E31.md:242、RESULTS_E34.md:260、RESULTS_E35.md:162；原始数据 results/main/long_o1_*、tag_*、prefix_*、migr_*。

**覆盖与缺口**：已覆盖：点查/写入/替换/扫描/空间/尾延迟（E34 单操作 p99/p99.9）对每个候选；两个模型后端仅在 M0 下计过性能（LITS 无性能数据）。尚未覆盖：FINAL_RESULTS_TABLE.md 自述是跨轮逐行表、非同轮统一重测（其 §6），不满足 §12.4 的"同轮最终表"形态；E38 的 LITS 后端是纯正确性轮（无计时）。

**关闭标准**：若要把某个候选升级为默认，需一次覆盖五维（点查/写入/替换/扫描/空间）且含维护峰值的同轮表；E38 P2/P3 接入后需补 LITS 后端的性能对照。关闭标准：候选任一定为默认或正式归档为候选。

**未能核实**：未能核实"维护峰值"在当前任何一张表里作为独立列存在；FINAL_RESULTS_TABLE.md 无峰值列，仅个别行在叙述里提到。

### [A-10-13-08] §11 SA5 冻结与交付：显式配置、默认入口确认、构建记录、报告导航更新；必交付 发布快照和未完成项；不在收尾临时加入模型或新优化

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | medium |

**实现**：冻结由 check_freeze.cpp 断言（默认入口 Config 的开关串必须等于选定组合，内容+结构统计相同）；显式基线由 switches.hpp:29-54 baseline_config / :63-77 pin_frozen_switches 提供；构建记录 logs/build_*.json；报告导航更新见 README.md（E28–E31 追加）与 EXPERIMENT_INDEX.md。

**证据**：RESULTS_FREEZE.md §1–§2（check_freeze 三数据集通过、cfg_shipped 断言）；构建身份：logs/build_check_freeze.json（LIVE，results/main/g0_build_drift.txt）；冻结记录 RESULTS_FREEZE.md:137-150（构建闭环事故与作废/替换）。

**覆盖与缺口**：已覆盖：五个冻结开关默认翻 true（index.hpp:638/643/659/666/673）；默认入口确认；构建记录；报告导航。尚未覆盖/例外：E38 在 2026-09-20 冻结之后加入了 LITS 模型（index.hpp:597 ModelBackend::LitsHpt，LITS_AUDIT.md），但那是另一本任务书 v2 的 P1，不属于 SA5 的收尾范围——需与 SA5 的"不在收尾加模型"分开读。

**关闭标准**：无 SA5 自身剩余动作；未完成项已在 RESULTS_FREEZE.md:152-157 列出（替换路径、sanitizer、并发、长期空间）。E38 之后的新一轮未完成项见 P3_PRECHECK.md。

**未能核实**：无法核实 SA5 是否被当作独立阶段执行——项目在方案（09-20）之后直接沿 E30–E38 编号推进，只有一个"阶段五冻结"（E25 任务书的），SA5 标签未出现在任何执行记录中。

### [A-10-13-09] §11 每阶段结尾都写检查点（当前版本、已完成门、有效结果、作废运行、下一条命令、未解决的问题）；长程执行在自然边界保存状态；首次可执行任务只到 SA2

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：TASK_STATE.md 每阶段只记 已完成/当前决策/下一条待执行/run_id/标识/被排除的运行（TASK_STATE.md:3），并维护"被排除的运行"清单（TASK_STATE.md:71-74）；P3_PRECHECK.md:56 记录 run_id 归档纪律。

**证据**：TASK_STATE.md（E25 阶段一至五检查点、被排除运行 1–5）；P3_PRECHECK.md；docs/字符串索引_架构与工程优化总执行计划_20260922.md:367（TASK_STATE 应含有效与作废 run_id 的要求）。

**覆盖与缺口**：已覆盖：逐阶段检查点与作废运行归档（含 checkpoint 归一化 bug 的作废，TASK_STATE.md 被排除运行 #2）。尚未覆盖："首次可执行任务只到 SA2"这条约束未被使用——项目未按 SA0→SA2 的先后切分首次任务，而是把 SA0/SA1/SA2 的等价工作分散在 E28（SA1）、E31（SA2）等轮。

**关闭标准**：在 SA 式推进被新计划取代的既成事实下，建议改为在每轮 RESULTS_EN 末尾固定"当前版本/已完成门/有效结果/作废运行/下一条/未决"六项；关闭标准：此后每轮都有这六项。

**未能核实**：未能核实"首次可执行任务只到 SA2"是否曾被遵守或有意放弃；没有任何文档记录该约束的执行或豁免。

### [A-10-13-10] §12.1 推荐的文件职责：CODE_AUDIT.md、MODEL_AND_MAPPING.md、INVARIANTS.md、switches.hpp、diag_structure.cpp、check_local_compact.cpp、bench_structure.cpp、analyze_structure.py、results/main/、RESULTS.md/LOG.md/TASK_ST…

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：存在：MODEL_AND_MAPPING.md、switches.hpp（四张表）、results/main/（96 文件）、LOG.md、TASK_STATE.md，以及按轮 RESULTS_E25.md…RESULTS_E38.md（等价的"./RESULTS.md"）。不存在：CODE_AUDIT.md、INVARIANTS.md、diag_structure.cpp、check_local_compact.cpp、bench_structure.cpp、analyze_structure.py。原任务书自注"文件名为建议接口，不暗示这些代码已经存在"（docs/...执行方案_20260920.md:615）。

**证据**：文件清单：validation_e25_20260918/ 目录（MODEL_AND_MAPPING.md、switches.hpp、results/main/、LOG.md、TASK_STATE.md 均在；CODE_AUDIT.md/INVARIANTS.md 在整棵工作区 find 无结果）。

**覆盖与缺口**：已覆盖：文件职责有等价物（diag_overflow/diag_compose≈diag_structure；check_e31≈check_local_compact；bench_long/bench_repack≈bench_structure；analyze_e31.py≈analyze_structure.py）。尚未覆盖：CODE_AUDIT.md 与 INVARIANTS.md 无任何等价文件（当前文件/函数/配置/来源与未决问题散落在 MODEL_AND_MAPPING 与轮次报告里）。

**关闭标准**：新建 CODE_AUDIT.md（当前文件/函数/配置/来源/未决）与 INVARIANTS.md（I1–I12 与对应门）。关闭标准：两文件存在且与 SA0 交付互链。

**未能核实**：无（文件存在性逐目录核实）。

### [A-10-13-11] §12.2 每条结果至少绑定身份字段：run_id、源码清单哈希、可执行哈希、编译命令、配置全文或可追溯哈希、ART 提交与适配器哈希、数据哈希和实际键数、种子、进程号、重复号、执行顺序、负载、检查点、周期数、操作数、活跃数、计时构建类型、时间单位、校验和

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：身份落在构建记录而非运行记录：logs/build_<target>.json 含 target、build_id、command、sources{文件:sha256}、extra_flags、c_objects、returncode、stderr_tail、exe_sha256、status（见 logs/build_check_freeze.json）。运行记录只含 kind/workload/rep/arm/run_tag/build_ms/query_cp/query_ms/cycles/frac/ops（results/main/e25_time.jsonl:1；长替换另有 live/memory/overflow）。缺失：run_id（17 个 analyze_*.py 中 0 个读构建身份）、配置全文/hash、数据哈希、种子、执行顺序、计时构建类型、时间单位、校验和。ART 提交固定在 third_party/fetch_libart.py:18，但未绑进任何结果。run_id 仅作文件名/日志键（run_lits.py:27/126/186）。

**证据**：logs/build_check_freeze.json（身份字段）；results/main/e25_time.jsonl:1（运行记录字段）；grep 全量 *.jsonl 无 run_id；results/main/g0_build_drift.txt 与 g0_evidence_timeline.txt（源码哈希 vs 磁盘的比对）。

**覆盖与缺口**：已覆盖：可执行哈希、编译命令、源码清单哈希、重复号(rep)、进程号(run_tag=P1–P5)、负载(workload/kind)、检查点(frac)、周期数(cycles)、操作数(ops)、活跃数(live，仅长替换记录)。尚未覆盖：run_id、配置全文、数据哈希、种子、执行顺序、计时构建类型、时间单位、校验和均未绑定到结果；无任何分析脚本断言身份。

**关闭标准**：两种做法二选一：①把 run_id/config hash/data hash/seed 写进每条记录；②让每个分析器读匹配的构建记录并断言身份后才出表。关闭标准：任何正式运行的结果集都能追到 一个构建记录 + 配置 + 数据/种子。

**未能核实**：未逐一读完 run_lits.py 全文，无法 100% 排除它在日志侧另存了部分身份（它确实写 logs/run_lits_<run_id>.log）；但确认无 sidecar manifest（results/main/*.manifest* 不存在），record 字段本身确无 run_id。

### [A-10-13-12] §12.2 后半：诊断字段另表保存并按同样身份字段关联；不要把 mem_build=true 的记录放进性能汇总；检查点用原始记录中的真实比例、不按最后一行归一化；分析脚本必须拒绝 缺失臂/重复配对/配置意外相同/状态或操作数不一致/无效 JSON/旧可执行身份/计数构建混入/单位不一致/校验失败

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：检查点真实比例：已遵守且修过一个 bug——TASK_STATE.md 被排除运行 #2 记录"检查点按最后一个归一、5% 被标成 20%"的首轮 sweep 整套作废，改用 bench 记录的 frac。诊断另表：诊断构建（-DE25_DIAG）与干净计时分离（如 run_lits.py 分开 clean/diag）。拒绝清单：基本未实现——analyze_lits.py:42-50 的 load() 在文件不存在时静默返回 []（缺失臂不报错）；17 个 analyze_*.py 中 0 个引用 logs/build、MANIFEST、exe_sha 或 build_id（grep 无命中）；无效 JSON 会由 json.loads 抛异常，算被动拒绝。

**证据**：analyze_lits.py:42-50（缺失文件返回空表）；analyze_e25_time.py:20-31（load() 无校验）；TASK_STATE.md:71-74（作废运行 #2）；results/main/sanitizer_runs_e38.jsonl 与各 long_*_diag_*.jsonl（诊断与计时分离）。

**覆盖与缺口**：已覆盖：检查点真实比例（有作废记录为证）、诊断与计时分离。尚未覆盖：缺失臂、重复配对、配置意外相同、状态/操作数不一致、旧可执行身份、计数构建混入、单位不一致、校验失败八项拒绝均未在分析脚本落地。

**关闭标准**：写一个共享 loader，落地 §12.2 的拒绝清单（至少在缺臂、操作数不一致、旧可执行身份三项上硬停），并让所有 analyze_*.py 改用它。关闭标准：故意制造缺一臂的分析输入，脚本必须非零退出。

**未能核实**：未能核实 analyze_e31art.py 等是否用了不同机制做臂配置校验；我 grep 了全部 17 个分析器，均无构建身份引用，但未逐行读完每个脚本的其余逻辑。

### [A-10-13-13] §12.3 构建闭环与运行顺序：编译失败立即删旧可执行与成功记录；运行入口拒绝不匹配构建身份的目标；批量构建逐个报告状态、不把未尝试当成功；运行顺序六步（验源与配置→定向门+回归门+内存安全检查→冒烟→正式计时→解析全部原始记录生成表与证据链接→更新报告清单检查点并归档失效运行）；不把"回归日志相同"当内存安全证明；环境有 sanitizer 就先查真实工具链

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：tools_build.py:243 build() 编译到临时文件、成功才原子改名；:289-303 失败时删 build/<target>.built.json 与 .exe 并 sys.exit(1)（不再回退旧二进制）；:320-336 load_record() 无匹配记录即 RUN REFUSED；:372-388 run_current() 源哈希漂移即拒绝（rc=2），run_recorded() 警告后运行；批量入口 = 无子命令时 `for t in argv[1:]: build(t)`。内存安全检查：E33 首次接 ASan+UBSan（RESULTS_E33.md:5），E38 扩展到含 LITS 的七个目标。

**证据**：tools_build.py:243-318（build 与失败路径）、:320-336（load_record 拒绝）、:372-388（run_current/run_recorded 漂移处理）；RESULTS_FREEZE.md §5+§7（构建闭环洞与修复，含改坏源码后 RUN REFUSED rc=2 的验证表）；FINAL_RESULTS_TABLE.md 第 5b 节 E35 行（run_current 源哈希不一致即拒 rc=2；43 个构建记录 5 同步/38 漂移）。

**覆盖与缺口**：已覆盖：第 1/2/4/5 步；冒烟（第 3 步）在实践中做但未自动化；第 6 步（报告/清单/检查点更新、失效运行归档）在 RESULTS_FREEZE §7 与被排除运行清单中落地。尚未覆盖：批量构建遇失败即中止（sys.exit(1)），后续目标既未尝试也未报状态——不违反"不把未尝试当成功"，但不满足"逐个报告状态"。

**关闭标准**：可选改进：批量 build 改为收集各目标状态、末尾汇总而非首败即停。关闭标准（若采纳）：一次批量构建能列出每个目标 OK/COMPILE_FAILED。

**未能核实**：未核实第 3 步"冒烟验证空间账本"是否有独立脚本；只见到诊断程序（diag_overflow 等）在做账本核对，未见挂在 run 顺序里的冒烟步骤。

### [A-10-13-14] §12.4 最终结果表模板：8 列（数据与负载／版本／对照／完整耗时配对比／索引空间／维护峰值／溢出率／查询／扫描影响／来源与限制）× 三类行（LOCAL/BASE、LOCAL/ART、LOCAL/GROUP）；不预填加速目标；ART 无对应概念（如溢出率）的项分列不硬赋零

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：未按模板实现。FINAL_RESULTS_TABLE.md 用的是另一套列（指标／被测版本÷对照／配对结果／来源），且按轮次分节而非按"数据与负载"行；无"维护峰值"列；LOCAL/BASE、LOCAL/ART、LOCAL/GROUP 三类行以等价臂出现（FINAL_RESULTS_TABLE.md 第 5b 节：BASE=E31 的 C2 臂、GROUP=E29 的 C3 臂、LOCAL=E31 臂、ART 另列）。ART 不适用的项（溢出率）确实未硬赋零。

**证据**：docs/字符串学习型索引_结构审查与优化执行方案_20260920.md:644-650（模板原文）；FINAL_RESULTS_TABLE.md 全文（列结构与免责声明 §6）；RESULTS_SUMMARY.md §0（自述"不是同轮统一排名"）。

**覆盖与缺口**：已覆盖："不预填目标"与"ART 不硬赋零"两条纪律被遵守（FINAL_RESULTS_TABLE.md §6 明确禁止跨轮拼接）。尚未覆盖：8 列模板本身、"维护峰值"列、以及一份同轮（非跨轮）的最终候选 vs BASE/ART 表。

**关闭标准**：为最终候选出一张符合模板 8 列的表，LOCAL/BASE、LOCAL/ART、LOCAL/GROUP 三行同轮测得。关闭标准：表存在且每格可追到一次正式运行。

**未能核实**：未核实 E31/E34 的单轮报告内是否已有接近模板的表——它们有 LOCAL/BASE/ART 单轮表，但列仍不是模板的 8 列（如无"维护峰值"列）。

### [A-10-13-15] §13 研究贡献链条五步：①发现约束（固定映射与预算下动态更新使合法目标处空间不足）②区分原因（可行性分层分清字节碎片/槽位/键范围/跨组容量/容器成本）③提出局部机制（足够小的维护范围恢复合法放置，明确不变量与回退）④证明收益与代价（正确性、端到端时间、总空间、峰值、查询/扫描同时测）⑤划定边界（哪些负载优于冻结版、哪些仍选 ART）；不把"局部整理"本身宣称独创

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：无单一文档组装五步链条，但各步有证据：①RESULTS_LONG.md:87-96（擦除只还槽位不还字节）；②E28 F0–F3 分层（RESULTS_E28.md §2，卡字节 208,689 对卡槽位 18,527）；③E31 subblock_reclaim + repack_subblock（index.hpp:695/1685）+ 回退（first_candidate index.hpp:1663）；④E31/E34/E37 同轮测正确性/时间/空间/尾延迟/扫描/ART；⑤各轮 §适用边界 + RESULTS_SUMMARY.md:378（跨全部轮次的适用边界五条）。

**证据**：RESULTS_E28.md、RESULTS_E31.md、RESULTS_E34.md、RESULTS_E37.md、RESULTS_LONG.md:135-142、RESULTS_SUMMARY.md:378-386；docs/字符串索引_架构与工程优化总执行计划_20260922.md:346（贡献不能把通用 SIMD/位图/压缩/内存池称为原创）。

**覆盖与缺口**：已覆盖：五步均有 M0=Context16 后端、三数据域、insert/replace/query/scan、到 200% 检查点的证据。尚未覆盖：无一份把五步串成研究叙述的交付物；第⑤步的"模型—结构交互"（换 LITS 后端后收益是否出现）尚无数据——LITS 只到 P1/P2（RESULTS_E38.md §0）。

**关闭标准**：写一节"研究贡献链条"，逐步引用上列证据文件；第⑤步待 E38 P2/P3 的 LITS 后端对照补入。关闭标准：五步各有一处 file 级引用，且边界步覆盖两个模型后端。

**未能核实**：未能核实是否另有一份论文/汇报稿在组装这条链条——我只查到 RESULTS_SUMMARY.md §6 与各轮适用边界，未找到以"贡献"为标题的成品章节。


---

## B-1-3  —— 工程线 §1–§3：选择依据、优先级、E24 诊断、E25 统一定位（17 条）

### [B-1-3-00] §1 选择依据：E21–E23 证据表（E21 混合点查 S 低 34.9–45.0%、高 ART 12.3–48.8%；E21 构建降 42–47%；E22 只改值 1.24–2.77× ART；E22 替换 5.73–8.56×、skew 空间比 0.86→1.02；E22 三类负载未触发容量回退；E23 扫描 1.28–2.34×），并遵守跨轮算术纪律（每行保留原轮次、不得跨轮相减、不得拼…

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | medium |

**实现**：无代码；选择写在原任务书 §1 并落实为 P0–P4（见下行）。E25 阶段未改模型、逻辑映射与 R1 构建（Results_E25.md §0/§3）。

**证据**：docs/字符串学习型索引_E23之后的优化方案.md 第 11–34 行；validation_e25_20260918/RESULTS_E25.md §0.1 第60行（“模型没有改变，只改写路径就把只改值从慢 1.85–3.06× 拉到接近或优于 ART”）；E25_TIME.md update_only 表（V1/V0=0.327–0.541，方向 5/5）。

**覆盖与缺口**：已核：E21–E23 的定性结论在 E24/E25 中被再次引用且方向一致；跨轮不相减的纪律在 E24_TABLES.md（分轮分状态）与 E25_TIME.md（同轮配对）中被遵守。未核：E21–E23 原始 jsonl 里的具体百分数。

**关闭标准**：关闭标准：无需动作。若要把 E21–E23 的绝对数字写进新报告，须回 validation_e21/e22/e23 的原始记录逐条复核，不得引用本轮转述值。

**未能核实**：未逐条复核 E21–E23 原始记录中的 34.9–45.0%、12.3–48.8%、42–47%、1.24–2.77×、5.73–8.56×、0.86→1.02、1.28–2.34× 等数字（不在我负责的行范围，也未打开对应轮目录的 jsonl）。

> **反驳复核**：维持原判（stands）。

### [B-1-3-01] §1 P0–P4 优先级表：P0 更新路径与空间诊断 → P1 统一增删改定位内核 → P2 一种证据支持的组级维护 → P3 范围扫描顺序访问 → P4 条件性点查硬件优化；P1 成本低、P2 承接创新点；“若 P0 发现 P1 已在最新版落实则直接跳过”；各步均有“暂时不扩展的内容”（不改模型/不改记录布局/不同时做全局重建/不默认升级 AVX2）

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：P0→E24（纯诊断，-DE24_DIAG，不改结构）；P1→E25 的 write_fast_locate + reuse_locate_ctx（index.hpp:638、:643 默认 true，2026-09-20 冻结）；P2→E26 C1/C2；P3→E27 S1；P4 未启动。全部为 Config 开关。

**证据**：RESULTS_E24.md 全篇；RESULTS_E25.md §0.3；TASK_STATE.md 阶段一–五；switches.hpp:29-38 baseline_config；index.hpp:638/643。

**覆盖与缺口**：P0/P1 在本行范围已闭环（E24 诊断→E25 实现+计时+门）。P2/P3 属 E26/E27（后续章节），此处只核其被登记、按 P2→E26、P3→E27 对应执行，未核其内部质量。P4 仍未启动。

**关闭标准**：P4 为条件性候选：需“新诊断支持路由/访存/预测开销”才启动，且不默认升级 AVX2、不重启 R2 构建优化。无新诊断即保持关闭（可记为条件未满足）。

**未能核实**：无（P0/P1 交付已逐项核对；P2–P4 只核其登记与下一步归属）。

> **反驳复核**：维持原判（stands）。

### [B-1-3-02] §2 E24 本轮只回答三件事：写操作有没有重复做定位；增长的空间具体在哪里；原预算内还有多少真实可行的回收机会（预计 1–2 个有效工作日；不做计时、不改结构）

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：E24 为纯计数诊断：diag_e24.cpp（-DE24_DIAG，tools_build.py 编译），无计时、无结构改动（RESULTS_E24.md 表头）。

**证据**：validation_e24_20260918/RESULTS_E24.md §1/§2/§3（三问各成一节）；results/main/e24_diag.jsonl（12 条 = 3 数据 × 4 状态）；analyze_e24.py → E24_TABLES.md；logs/build_diag_e24.json。

**覆盖与缺口**：三问均有定量回答：Q1 完整比较次数比（§1.1，34×–34,155×）；Q2 两层空间分解（§2，12 格恒等式）；Q3 合法迁回 0/0/256 与卡点分列（§3）。

**关闭标准**：无。E24 自我限定正确（§6：不测时间、只测单线程静态状态）。

**未能核实**：e24_diag.jsonl 的逐格数值未重算（依赖 analyze_e24.py 与报告一致；本轮未运行脚本）。

> **反驳复核**：维持原判（stands）。

### [B-1-3-03] §2.1 核对增删改调用链并记录每次操作计数：四行（只改值 / 删除 / 插入新键 / 删除后插入）；字段含 外层定位、模型预测、指纹计算、子块访问、完整比较与逻辑比较字节；删除另含 移动条目数/实际复制字节/分配释放次数；插入另含 预测次数/主块尝试/容量失败类型/压缩次数

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：E24 的 E24_UP_BLOCK/E24_UP_ENTRY 宏；E25 的 e25::counts() 计数宏（index.hpp 顶部）向外导出。

**证据**：validation_e24/E24_TABLES.md §1（find/update/erase 按 primary/overflow/miss 分组的每键比较次数）；validation_e25/E25_CALLS.md（矩阵列 L/P/S/F/H/C/B 与 att(slot/bytes)、shift/relP/relO/alloc/free）；results/main/e25_calls.jsonl（93 条）；diag_e25_calls（-DE25_DIAG）。

**覆盖与缺口**：已覆盖 只改值(update)/删除(erase)/插入新键/upsert 新键 与 主块命中/溢出命中/未命中 分组；删除的 移动条目数(shift)、释放字节(relP/relO)、alloc/free 已计；插入的 放置失败首因(att slot/bytes，互斥)与 压缩次数(compact_entered/count)已计。未覆盖：发布矩阵中没有独立的“子块访问次数/操作”列。

**关闭标准**：若验收要求“子块访问”逐操作单列，可从 E24 的 block/entry 计数补一列。关闭标准：四行 × 全部字段齐备。

**未能核实**：发布矩阵未见独立的“子块访问次数”列，无法确认该字段是否逐操作给出（E24_TABLES 只以 block/entry 差比值口径呈现）。

> **反驳改判**：`已完成并采用` → **`部分完成`**（downgraded）
>
> 这一行自相矛盾：coverage 自己承认『发布矩阵中没有独立的"子块访问次数/操作"列』——而 item 的字段清单明确要求『子块访问』；承认缺一项就写不出『已完成并采用』。另有三处可查证的缺口：(a) item 要求的第四行是『删除后插入』，claim 的 coverage 却把第四行换成『upsert 新键』且未交代替换；E25_CALLS.md 的类别只有 primary_hit/overflow_hit/miss/new_key/duplicate_key/existing_key/region_split_build（我用 jsonl 枚举过 21 个 (kind,category)），没有『删除后插入』这一行；E24_TABLES.md 只有 replace25『状态』，不是该操作的调用链行。(b) item 要求『模型预测、指纹计算』计数，而 implementation 引的 E24 诊断里根本没有这两个量：e24_diag.jsonl 的 paths 记录字段是 find_cmps/upd_cmps/upd_bytes/upd_entries/upd_subblocks/era_*，我逐个检查过，无 pred/predict、无 fp/finger 字段。(c) item 要求删除另含『实际复制字节』，E25_CALLS.md 只给 shift(条目数) 与 relP/relO(释放字节)，没有复制字节列。(d) evidence 引 diag_e25_calls 却未给构建身份——results/main/g0_build_drift.txt 明记 di…

### [B-1-3-04] §2.1 计数口径约束：按主块命中/溢出命中/未命中分开；不得把分别运行的三种负载耗时当成同一条混合负载的时间分解；重建/前缀变化等慢路径单列；不为每次字符串比较打时间戳（避免仪器成本盖过实际工作）

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：E25_CALLS.md §0 显式口径（F/H 拆分、P/S 分开、放置失败取首因互斥、诊断窗口 reset/read）；三负载各自独立构建。

**证据**：E25_CALLS.md §0；RESULTS_E25.md §1 与 §5 第1条；RESULTS_E24.md §1.3（三种负载不是一条混合分解）。

**覆盖与缺口**：口径已写清且被后续表遵守：E24_TABLES 分状态分来源，E25 分 (数据,负载) 格；慢路径（结构行 region_split_build）单列不折叠。

**关闭标准**：无。

**未能核实**：无。

> **反驳复核**：维持原判（stands）。

### [B-1-3-05] §2.2 主块账本互不重复：块头/标签 + 预留描述符区 + 活跃后缀 + 垃圾后缀 + 未用后缀区 (+ 对齐余量) = 主块分配字节；标签若在块头只计一次；未用槽位已含在预留描述符区，不再另加；垃圾后缀是删除后仍占块内位置的旧字符串字节，不含空闲字节

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：memory_breakdown()（E24 版 index.hpp）导出分项；E24 §2.0 恒等式逐格核对。

**证据**：RESULTS_E24.md §2.0（“12 个格子全部精确成立”，closes 列）；E24_TABLES.md §2.1。

**覆盖与缺口**：恒等式 12 格精确成立，两类重复计费（未用槽位重复、后缀三分类重复）被排除；对齐余量说明不在块内。已知限制（E24 自陈）：垃圾后缀是“活跃项+水位项”相减得到，非独立测量。

**关闭标准**：若要独立验证垃圾字节，需第三条独立路径（E24 §2.0a 已标为限制）。关闭标准：分项表 + 至少一次独立核算。

**未能核实**：未重算 closes 列（依赖 analyze_e24.py 输出）。

> **反驳复核**：维持原判（stands）。

### [B-1-3-06] §2.2 溢出区单列：容器对象、记录数组容量、有效记录数、外部字符串分配、未使用记录容量；std::string 内联短串不按 capacity()+1 重复计堆；无法可靠分离时保留“估算”标识并与独立分配诊断交叉核对

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：E24 §2.2 表（记录数 / 后缀活跃 / 元素占用 / 元素容量 / 元素余量），sizeof(OverflowRecord)=48 实测。

**证据**：RESULTS_E24.md §2.2；E24_TABLES.md §2.3。

**覆盖与缺口**：已给：有效记录数、记录数组容量(元素容量)、已用(元素占用)、外部字符串分配(后缀活跃)、未用记录容量(元素余量)。未给：容器对象自身字节；未见与独立分配诊断（如 e9_alloc 计数）的交叉核对；表中未见“估算”标识。

**关闭标准**：补 容器对象字节 一列；做一次与独立分配计数的交叉核对并对无法分离项打“估算”标识。关闭标准：五项齐备 + 至少一处独立交叉核对。

**未能核实**：未找到 E24 与独立分配诊断的交叉核对记录；“估算”标识我未在 §2.2 表中看到（§2.2 只给了实测 sizeof(OverflowRecord)=48）。

> **反驳复核**：维持原判（stands）。

### [B-1-3-07] §2.2 模型、共享前缀、路由、容量表、新增位图 另列；常驻空间、维护时临时峰值、进程 RSS 是不同口径分别报告；在 E22 的 0%/5%/10%/25% 检查点记录这些分项；后续只对“删除后补入”增加 50%/100%/200% 累计替换周期（活跃记录数维持约 N）

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：E24 §2.3 另列（模型/目录/表/外层）；§2.0b RSS 单口径。检查点：E24 只用 4 个状态（初始/只改值/插入25%/替换25%）。

**证据**：RESULTS_E24.md §2.3、§2.0b；RESULTS_E24.md §6 第3条（明确“方案 §2.2 要求的四个百分比检查点未逐点采集；长期替换 50%/100%/200% 未测”）；E24_TABLES.md §2.4。

**覆盖与缺口**：另列项与 RSS 口径已分开报告（RSS 153–221MB 对索引自身 22–25MB，明确不相加）。未覆盖：0/5/10/25% 四个检查点未逐点采集；50/100/200% 长期替换未测；容量表与新增位图未见单列。

**关闭标准**：在 0/5/10/25% 逐点补空间分项表；对“删除后补入”补 50/100/200% 累计替换周期并判断空间是否趋稳（N 维持不变）；补 容量表/位图 单列。关闭标准：四检查点 + 三长期百分点的分项表。

**未能核实**：未见“容量表 / 新增位图”单列项（E24 §2.3 只列 模型/目录/表/外层）。注：这不是漏做，而是 E24 明示把长期检查点延后到 E26 的评估计划（§6 第3条）。

> **反驳复核**：维持原判（stands）。

### [B-1-3-08] §2.3 对每组四问：(1) 是否有空槽、清垃圾后字节是否足够？(2) 空槽/字节是否在同一可接收该键的子块？(3) 放入是否仍满足子块间键序？(4) 空闲是否来自其他逻辑组（若是，首版组内维护不能直接用）？并用当前布局真实几何作一次诊断性试放，得“按指定算法实际可迁回条目数与后缀字节”

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：E24 §3：用既有边界目录 locate_subblock 取唯一允许子块（M1），要求同时 count<slots 且 len≤可用后缀；不驱逐、不跨子块重划、不清垃圾。

**证据**：RESULTS_E24.md §3（合法迁回列、卡槽位/卡字节/卡键序分列；第4点：空闲的 98%–99.6% 在无溢出组）；E24_TABLES.md §3；results/main/e24_diag.jsonl。

**覆盖与缺口**：四问均有对应列：空槽+字节=卡字节列（初始三数据 100% 卡字节）；同子块=按 locate_subblock；键序=卡键序列恒 0；其他逻辑组=§3 第4点。试放已做（12 格 11 格为 0，最好 256/39,817）。

**关闭标准**：无（压缩后再试放明确留给 E26，E24 §3.1/§4.2）。

**未能核实**：试放所用的具体几何参数值我未逐条重算。

> **反驳复核**：维持原判（stands）。

### [B-1-3-09] §2.3 试放结论不得把聚合空闲量命名为可实现收益，也不得宣称它是最优装箱答案

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：E24 §3.1 明确定义 0/0/256 为“压缩前试放”，非所有重排上限、非迁回总收益（不计迁移时间/峰值空间/目录成本）。

**证据**：RESULTS_E24.md §3.1、§6 第6条；§4.2（“排除迁回不等于已证明压缩划算”）。

**覆盖与缺口**：免责与口径均已写明：把“总空闲 98–99.6% 在无溢出组”判为不可用收益。

**关闭标准**：无。

**未能核实**：无。

> **反驳改判**：`已完成并采用` → **`部分完成`**（downgraded）
>
> 四条行为在**当前源码**里确实都在，且行号全对（update :3418-3430 走 find_located+store_at、慢路径保留在 :3434-3447；erase :3450-3462 + erase_at :2865-2906；insert :3304-3325 reuse 走 place_with_ctx；upsert :4460-4480 走 upsert_one 并补了区域级分裂 :4473-4475）。问题在证据身份：本行把 check_e25_write 的『通过』当成当前源码的证据却**没有标构建身份**——logs/build_check_e25_write.json 的 build_id 解出 2026-09-20 07:07:09，而 g0_evidence_timeline.txt 记 index.hpp 末改 2026-09-22 12:51:49，g0_build_drift.txt 记 check_e25_write 为 DRIFT（changed: index.hpp,switches.hpp）。这正是纪律禁止的『把漂移记录当成当前源码的证据』。此外 coverage 把『记录数/组几何/溢出数/空间不变』一并归给该门的断言，但我在 check_e25_write.cpp 里找不到任何空间或几何读取（全文只有 region_count() 用于建 plan，无 geometry/space/memory_breakdown/blocks）——该门实际只比对 scan() 内容、size()、overflow_count()、co…

### [B-1-3-10] §2.3 决策规则表五行（主要观测 → 下一步），并规定“E24 的交付是一张明确的‘现象—对应代码—计数—选择’表，不是又一份没有决定的性能报告”

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：E24 §4.3 给“观察到的主要问题 / 本轮数据支持吗”支持度表；§4.1 选择 E25 V1/V2；§4.2 暂缓 E26 并给判据；§1 以源码审查指认对应代码（Region::find 用 tag/长度/边界目录/Hybrid，Region::update/erase 一个都没用）。

**证据**：RESULTS_E24.md §1（对应代码）、§1.1（计数）、§4.1/§4.2（选择）、§4.3（现象→支持度）；E24_TABLES.md 全篇未含四列合并表。

**覆盖与缺口**：现象、计数、选择齐备且落了决定（换 E25 V1/V2、暂缓 E26、收缩容器优先级最低）；但“对应代码”是 §1 的散文式源码审查，未与每行现象逐行配对，未见单张四列合并表。

**关闭标准**：把 §1(对应代码) + §1.1(计数) + §4.3(现象) + §4.1/4.2(选择) 合并成一张“现象—对应代码(file:line)—计数—选择”表。关闭标准：单表五行、四列齐。

**未能核实**：我未在 RESULTS_E24.md 或 E24_TABLES.md 找到该四列合并表（只有 §4.3 的“现象/是否支持”两列表）。

> **反驳复核**：维持原判（stands）。

### [B-1-3-11] §3 内部接口示意：enum class Where {Missing, Primary, Overflow}; struct Located { where; region, group, subblock, slot }; 以及 locate_for_write / update_at / erase_at / insert_prepared（名称可按现有代码调整）

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：已实现且名称可调：index.hpp:2837 enum class Where{NotLocated, Absent, Primary, Overflow}；index.hpp:2839-2846 struct Located{where,group,tag,subblock,slot,index}；find_located(:3413)、store_at(:2855，对应 update_at)、erase_at(:2865)、place_with_ctx(:3255，对应 insert_prepared)、upsert_one(:3402)。

**证据**：源码 index.hpp:2837,2839-2846,2855,2865,3255,3402,3413。构建身份 DRIFT：bench_e25 构建于 2026-09-19 12:40、check_e25_write 2026-09-20 07:07，均早于 index.hpp 末改 2026-09-22 12:51:49（results/main/g0_evidence_timeline.txt）。

**覆盖与缺口**：接口存在且被 update/erase/insert/upsert 四条路径调用；比示意多出 NotLocated/Absent 两态（index.hpp:2833-2836 说明其为安全直接放置的承重态）。名称与示意不同（任务书允许）。未覆盖：无“region”字段（Located 是 Region 内局部结构，region 由调用方持有）。

**关闭标准**：证据绑在已被取代的 index.hpp 上；若要作为当前源码的证据，需对当前 index.hpp 重跑一次写路径门（当前无 san_check_e25_write 的 LIVE 复跑）。

**未能核实**：无（源码在位）；但 E25 全部证据为 DRIFT，见 remaining。

> **反驳复核**：维持原判（stands）。

### [B-1-3-12] §3 四条行为：(1) update 一次查找得确切片位再写定宽值，记录数/组几何/溢出数/空间不变；(2) erase 用同一定位机制找记录，再做描述符移动、标签同步、后缀垃圾记账与边界更新；(3) insert 判重后复用已算出的组号与标签（压缩改槽位则重求物理位置，但模型/前缀不变时不必重预测逻辑组）；(4) upsert 按一次定位结果写值或插入，语义仍区分“插入/更新”

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：update index.hpp:3418-3430（find_located→store_at；V0 慢路径 :3434-3447 保留）；erase :3450-3462 + erase_at :2865-2906（描述符/tag 下移、relP 垃圾记账、slot==0 时 rebuild_dir）；insert :3304-3325（reuse 走 place_with_ctx :3255，put_primary 重算物理位置）；upsert :4460-4480（reuse 走 upsert_one :3402，返回值区分写/插）。

**证据**：check_e25_write.cpp（三变体逐元素比对 + 参考表）；E25_CALLS.md（V1→V2：insert 新键 预测 2.0→1.0、upsert 新键 L/P/S 由 2.0/3.0/2.0→1.0/1.0/1.0，而 cmp_calls 173.1 不变，证明省的是重复预测与哈希而非放置比较）；E25_TIME.md（V2/V0 9/9 格同向）。

**覆盖与缺口**：四条行为均在；“记录数/几何/溢出数/空间不变”由 check_e25_write 逐批 element-by-element 比对与 overflow_count()/compaction_count() 三变体相等断言支撑（第153-158行）。

**关闭标准**：无必做项。

**未能核实**：未逐行确认 store_at 对 Primary 分支完全不动 count/size（源码读数一致，但无专门计数器断言）。

> **反驳改判**：`已完成并采用` → **`部分完成`**（downgraded）
>
> 两处硬伤。(1) 该清单唯一的承载门 check_e25_write 是 DRIFT（2026-09-20 07:07:09 构建，早于 index.hpp 末改 2026-09-22 12:51:49）——claim 自己也这么写，却又据此判『已完成并采用』；按纪律，漂移门的通过不能替当前源码背书，因此『完整键值与扫描结果必须与参考容器一致』这条在发布树上只剩 LIVE 的 check_freeze 与 check_scan_contract 部分承担（check_freeze.cpp:178,194-220 有 Reference std::map 比对，但只覆盖 update/insert/erase，不含 upsert，也不做三变体布局相等）。(2) coverage 自陈『压缩后位置失效无专门负向用例』——而『压缩后位置失效』正是任务书这条验收清单里点名的风险之一；点名项缺专门用例，写不出『已完成并采用』。另可查证的支持面：check_tag_store（CURRENT LIVE，build 2026-09-22 12:59:32）确实承担标签碰撞（check_tag_store.cpp:5『a tag collision must still be resolved by the FULL comparison』且断言 ov_linear_recs>0 非空）；『触发压缩的插入』在 logs/check_e25_write_{mixed,skew,tranco}_8192_run.log 里为 compactions 46/0/231（skew 为 0，未触发）。

### [B-1-3-13] §3 位置失效规则：定位结果只在同一次公开 API 调用内部有效；重建、压缩、删除移动、容器扩容后不能沿用旧指针或旧槽号；区域发生前缀变化或分裂时必须重新使用相应的新映射

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | medium |

**实现**：index.hpp:2830-2832 注释（“deliberately not a handle any caller can hold across operations”）；:2843-2845（subblock/slot/index 标注为 position: Primary/Overflow only）；:3253-3254（压缩改槽位但组号/标签有效，put_primary 重求位置）；:4517-4518 IndexTestAccess（“a context is only sound within the single call that produced it”）。

**证据**：源码 index.hpp:2830-2832,2843-2845,3253-3254,4517-4518；check_e25_write.cpp 第5-7段含触发压缩的插入与 (a)(b)(c) 定向分支用例；Index::upsert 在 :4473-4475 补上区域级分裂控制流。

**覆盖与缺口**：规则被设计结构性保证（Located 为调用栈局部、非跨调用句柄），压缩路径重求物理位置、分裂由 Index 层补做；但未见“复用陈旧定位应被拒绝”的专门负向门。

**关闭标准**：若要求显式验证失效，可加一条“同一调用内压缩后再用旧槽号”的负向门。关闭标准：有专门断言陈旧定位不可用。

**未能核实**：未找到断言“陈旧 Located 被拒”的独立门；由于 Located 不可跨调用传递，该负向情形在类型上不可表示，我无法核到一条专门的失效测试，只能核到设计约束与注释。

> **反驳复核**：维持原判（stands）。

### [B-1-3-14] §3 不能通过“已知新键一定不存在，所以取消判重”让原型占基线便宜；若另测内部已知不存在入口，双方要有相同前置条件，并与通用接口分开

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：未引入任何“已知不存在”入口（grep 无 known_absent / skip-dedup 分支）；upsert_one :3396 注释“Duplicate detection IS the locate, so it is not skipped”；place_with_ctx :3255 仅在被判定 Where::Absent 后可达（insert :3314、upsert_one :3406）。

**证据**：源码 index.hpp:3396,3406,3314；E25_CALLS.md：upsert/existing_key 三变体仍 S=1.0（判重未取消），update/erase 命中仍 S=1.0。

**覆盖与缺口**：通用接口一律判重；V2 省的是重复预测/哈希（P/S 计数可证），不是判重。免判重入口不存在，无需“相同前置条件”条款。

**关闭标准**：无。

**未能核实**：无。

### [B-1-3-15] §3 验收清单（只针对实际改变的风险）：主块/溢出中的更新和删除、标签碰撞、跨 16 槽的 SIMD 批次、重复键、未命中、删除再插、压缩后位置失效；完整键值与扫描结果必须与参考容器一致

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | medium |

**实现**：check_e25_write.cpp（第12-21行列覆盖项；第141-169行用 s.scan() 与 reference std::map 逐键比对，并三变体逐元素比对）；标签碰撞由 check_tag_store.cpp:5 承担。

**证据**：check_e25_write.cpp:12-21,141-169,207-246,294-362；check_tag_store.cpp:5（“a tag collision must still be resolved by the FULL comparison”，LIVE 门）；E25_TIME.md 点查回归 0.99–1.01。构建身份：check_e25_write DRIFT（2026-09-20 07:07）。

**覆盖与缺口**：已覆盖 主块/溢出的 update+erase、重复插入拒绝、未命中(update/erase/upsert)、删除再插、跨16槽 SIMD 批次（组跨多子块）、触发压缩的插入、完整键值+扫描与参考容器一致。未由本门直接覆盖：标签碰撞（由 LIVE 的 check_tag_store 承担）；“压缩后位置失效”无专门负向用例。

**关闭标准**：可选：把标签碰撞与“压缩后位置失效”并入 check_e25_write 自身，或对当前源码重跑该门以取得 LIVE 身份。关闭标准：清单七项在同一门内各有断言。

**未能核实**：check_e25_write.cpp 内未见显式“标签碰撞”用例（改由 check_tag_store 承担）；“压缩后位置失效”无专门断言。且 check_e25_write 无 san_* LIVE 复跑。

### [B-1-3-16] §3 本轮先比较冻结 R1 与仅改定位的新版本，再在同轮加入 ART；若诊断计数下降但端到端没有明确改善，就保留为可选实现或撤回；不把“预测少了一次”当作性能结论

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：四臂同轮：V0(冻结 R1，两开关关)/V1/V2/ART，同一二进制上三个 Config + 同一 libart（bench_e25 + run_e25.py + analyze_e25_time.py）。

**证据**：E25_TIME.md（V0/V1/V2/ART 同轮，5 进程 × 5 配对重复，同操作序列、臂序轮转，比值同次重复内配对）；RESULTS_E25.md §0.3（“没有仅凭调用次数减少启用新默认”）；results/main/e25_time.jsonl（3600 条）。

**覆盖与缺口**：同轮 ART 已做；V1/V2 默认启用依据是同轮完整耗时而非调用数；重复前缀上 V2 增量落噪声已按格如实标注；未显著改善时“保留为可选”的处置（V2 在 skew 落噪声但按 V2/V0 全格同向保留）。

**关闭标准**：证据为 DRIFT（bench_e25 2026-09-19 12:40）；若要作为当前源码的成绩，需重跑。不得把本轮绝对耗时与历史轮或当前默认版拼接。

**未能核实**：未独立复算 e25_time.jsonl 的配对比值（依赖 analyze_e25_time.py）。


---

## B-4-5  —— 工程线 §4–§5：E26 组级维护、E27 范围扫描（11 条）

### [B-4-5-00] §4 开篇约束（第 133-135 行）：首版不改前缀、模型、逻辑组数、组号映射，也不扩大主块预算；一次只选择 E24 支持的一个分支，最多做三种变体，不同时实现所有维护办法。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：映射与前缀未动：compact/repack_subblock/drain_overflow_bounded 只改 Entry::offset、Header::bottom 与记录归属，不触 canonical_group（index.hpp:1685、1870、1725）；Config 的 block_bytes=1024、slots=16、model_kind/model_backend=Context16（index.hpp:634 一带）。一次一变体由开关登记制度承担：switches.hpp 四张表（baseline_config :29、pin_frozen_switches :63、switches() :81、require_only_switches_differ :108），后者断言两臂恰好只在声明的开关上不同。

**证据**：RESULTS_FREEZE.md §1（“未动：模型、逻辑映射、主块预算”）；E26_TAGS.md 开篇（两臂同坐 E25 默认候选）；RESULTS_E31.md §1/§7（“逻辑组号不变、模型不变”）；构建身份：check_freeze 为 LIVE（logs/build_check_freeze.json，14/14 源哈希与磁盘一致，见 results/main/g0_build_drift.txt），断言 SHIPPED==CHOSEN。

**覆盖与缺口**：已覆盖：映射/前缀/预算不变（多轮声明 + check_freeze 断言）；单开关差异可归因（白名单断言）。未覆盖：后续轮的臂数越界无裁决——E31 四臂（C1/C2/C3/E31）、E32 四 Config 臂（C2/E31/E31H/E31P）、E37 四臂（B/S/R0/R1）都超过“最多三种变体”，文件里没有对此的判定；index.hpp 同时保留全部维护分支代码，“不同时实现”只由默认关闭承担，无断言。

**关闭标准**：若要严格闭环：补一句“每轮臂数≤3 变体”的记账，或写明后续轮臂数例外的依据。开关关闭即中性无断言，只能由源码级论证（RESULTS_E31 §1 已给）。

**未能核实**：后续轮 4-5 臂是否违反“最多三种变体”无记录可查；我也未找到任何断言“关闭的维护分支不改变行为”，只有源码级论证。

> **反驳改判**：`已完成并采用` → **`部分完成`**（downgraded）
>
> 引用的 file:line 是错的，而且这条判定的证据是自述而非测量。(1) 它写“不触 canonical_group（index.hpp:1685、1870、1725）”——1685/1870/1725 分别是 repack_subblock/compact/drain_overflow_bounded，canonical_group 实际在 index.hpp:1450；它写 Config 的 block_bytes/slots/model_kind/model_backend 在“index.hpp:634 一带”——634 行是一段 E27 散文注释，四个字段在 index.hpp:600/614/605/613。(2) “映射与前缀未动”没有任何测量：check_freeze.cpp:136-141 只断言开关串 SHIPPED==CHOSEN 与 14 项白名单的 require_only_switches_differ，其 OLD/CHOSEN 两臂跑的是同一份映射/模型代码（check_freeze.cpp:185-221），因此它原则上无法见证“映射相对历史基线未动”；支撑只剩各轮自述（RESULTS_FREEZE.md §1、E26_TAGS.md 开篇、RESULTS_E31.md §1/§7）。(3) 判定自己承认的覆盖缺口就是违例：“最多做三种变体”被 E31（C1/C2/C3/E31）、E32（C2/E31/E31H/E31P）、E37（B/S/R0/R1）四臂轮次超过且无裁决；“不同时实现所有维护办法”只靠默认关闭承担、无断言。(4) 更重要：…

### [B-4-5-01] §4 分支 A（第 137 行）：只压缩垃圾——保持主块成员不变，重新打包活跃后缀、修正偏移、恢复连续可用空间；释放的是块内可用空间，主块仍分配着，不能宣称常驻内存下降；压缩后能否少进溢出要用后续同一操作序列验证。

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：index.hpp:1870 Region::compact(g)（整组重放活跃后缀、只改 offset 与 bottom、末尾 rebuild_dir）；闸门 Config::compact_reuse_tag（index.hpp:659，默认 true＝冻结）、Config::compact_needs_garbage（index.hpp:666，默认 true＝冻结）在 worth_compacting（index.hpp:3204）里生效；子块粒度 Region::repack_subblock（index.hpp:1685）由 Config::subblock_reclaim（index.hpp:695，默认 false）启用，挂在放置失败路径（index.hpp:3262）。

**证据**：源码 index.hpp（末改 2026-09-22 12:51:49 UTC）。构建身份：check_freeze LIVE（logs/build_check_freeze.json，14/14）；check_e26_c2 与 check_e31 均 DRIFT（changed: index.hpp,switches.hpp，见 results/main/g0_build_drift.txt）。运行记录：results/main/e26_diag.jsonl(12)、e26_time.jsonl(1800)、long_replace_{same,grow}.jsonl(各384)、long_e31_{same,grow}.jsonl(各720)。分析入口：analyze_e26.py→E26_TAGS.md、analyze_long.py→E26_LONG.md、analyze_e31.py→e31_analysis.txt。报告：RESULTS_E26.md、RESULTS_LONG.md、RESULTS_E31.md。

**覆盖与缺口**：已覆盖：组内跨子块重排（compact）、子块内原地重排（repack_subblock，只改偏移、不分配、不重算哈希、不改边界）、触发闸门 C1/C2/C3；三数据集、insert/replace 与长期替换 same/grow、25%–200%。“压缩后能否少进溢出”由同序列的 C0/C2 溢出对比给出（插入负载 0 条额外溢出，替换 −6..+42 条）。未覆盖：offset_directory=false 的定长布局（RESULTS_E31 §8 自述不可达且未检验）；E31 子块回收的内存安全只有源码级论证，无 sanitizer（E31 §8 自述）；C2“skip 掉一次本该跑的压缩”的远期影响未单独计时。

**关闭标准**：分支 A 已采用的（compact + C1/C2）无必做项。若要把 E31 子块回收纳入默认，需先在其唯一致退化点裁决取舍：grow/混合 100% = 1.082/1.062（两次运行均 0/5，RESULTS_E31 §0/§3.3）。

**未能核实**：offset_directory=false 布局下 repack_subblock 的 base_of 步长与 bb_ 是否一致未被检验（无配置走到）；E26 的“少进溢出”未见逐条序列级的前后对照，只有检查点级溢出数对比。

> **反驳复核**：维持原判（stands）。

### [B-4-5-02] §4 分支 B（第 139-149 行）：压缩后迁回溢出的保守算法五步（1 读主块与溢出有序记录并在临时状态保留旧主块；2 清垃圾并为溢出候选找合法子块区间、结合相邻非空子块边界；3 槽位与后缀字节同时足够才迁入、不驱逐原主块记录；4 更新标签/偏移/计数与已启用子块范围目录并校验互斥/并集/值/组内顺序；5 临时状态成功才提交、失败保持旧状态、未迁回者留溢出），且准备阶段的失败性分配/复制/目…

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：Config::overflow_migrate（index.hpp:745，默认 false）；Region::drain_overflow_bounded（index.hpp:1725）、drain_all_bounded（index.hpp:2522；Index 侧 index.hpp:4340）；单趟压缩移出 OverflowList::erase_positions（index.hpp:1197）；kMigrateBudget=64（index.hpp:1714）；触发在放置失败路径（index.hpp:3287、3367）。逐步对应：1 候选拷出 index.hpp:1781；2 落记录前先 repack_subblock 目标块 index.hpp:1766；3 room_virtual 同时要空槽与回收后字节 index.hpp:1754，不驱逐主块记录；4 正确性由 check_migrate 逐条覆盖；5 migrate_rejected 计数并 put_overflow 放回 index.hpp:1794-1801。

**证据**：RESULTS_E37.md 全文；results/main/e37_feasibility.txt、migrate_diag_{same,grow}.jsonl(48)、migr_{same,grow}.jsonl(6240)、migr_diag_{same,grow}.jsonl、e37_scan_migr.jsonl(2700)、repair_diag_{same,grow}.jsonl；门 check_migrate（DRIFT，logs/build_check_migrate.json）；分析 analyze_migr.py / analyze_migrate.py；驱动 run_migr.py / run_scan_migr.py / run_repair.py。

**覆盖与缺口**：已覆盖：五步中的 1/2/3/4 有实现与门（check_migrate 三数据集 pass，两臂各自独立过参考表；点查 638,976–4,653,056 次、扫描 4,096 次、记录 131,072 条比对）；same/grow、200% 检查点；迁回量 2,165–32,012 条、落地率 92%–99.7%；完整维护/点查/扫描/常驻空间/尾延迟；修复退化快照的第二种实验（失败并已定位到 repack_subblock 的 count<2 直接返回这一行缺口，RESULTS_E37 §9）。未覆盖：**峰值空间**（验收要求“常驻空间及峰值”，只报了常驻）；**准备—验证—提交的失败原子性**（无失败注入用例，提交段 insert_into/put_overflow 仍会分配）；offset_directory=false；并发；归因未分离（R1＝迁回＋额外回收，主动项是后者，比迁回量大 15×–30×）。

**关闭标准**：(a) 补一条“只扩大子块回收触发面、不迁回”的臂以分离归因——E37 §10 自定的下一步，也是它能否成为“迁回”证据的前提；(b) 尾延迟六格 1.055–4.469（全 0/5）需在采用前处理或明记；(c) 补准备—验证—提交的失败注入用例与峰值空间口径。

**未能核实**：迁回本身对溢出降幅的贡献（E37 明确未分离，报告不把降幅记在迁回头上）；准备阶段失败时的状态保持（无失败注入证据）；峰值空间从未报过。

> **反驳改判**：`已完成，负结果或条件性候选` → **`部分完成`**（downgraded）
>
> 实现与逐步映射大部分核对通过：overflow_migrate 默认 false（index.hpp:745）、drain_overflow_bounded（index.hpp:1725）、drain_all_bounded（index.hpp:2522 与 4340）、kMigrateBudget（index.hpp:1714）、放置失败触发点（index.hpp:3287/3367）；步骤 3 的 room_virtual 同时要空槽与回收后字节（index.hpp:1754-1755）、步骤 5 的 migrate_rejected + put_overflow 放回（index.hpp:1794-1801）、步骤 2 的落记录前先 repack_subblock（index.hpp:1766-1769）都对；E37 §9 定位的 repack_subblock 缺口确在 index.hpp:1688（n<2 直接 return）；OverflowList::erase_positions 在 index.hpp:1202（判定写的 1197 是它上面的注释行）；check_migrate 确实有非空性断言（check_migrate.cpp:304-308）。但这是“部分完成”而不是“已完成”：任务书 §4 第 147 行明写“提交阶段避免再分配”，第 153 行明写“复制回写、边界更新和临时峰值全部计费”——两条都没做到，判定自己承认了（“无失败注入用例，提交段 insert_into/put_overflow 仍会分配”“峰值空间…只报了常驻”）。提交段确实还在分…

### [B-4-5-03] §4 分支 C（第 151 行）：缩减溢出容器的保留容量——仅在活跃记录下降后容器仍占过量容量时采用；需测实际分配容量和后续再次增长的分配次数，不能假设 shrink_to_fit 释放确定字节数，也不能把容器释放等同于 RSS 立即下降。

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：未实现。全仓 grep 无 shrink_to_fit / 收缩容量路径；index.hpp 无 shrink 命中。溢出容器 OverflowList 只有 insert_at（index.hpp:1862）、erase_positions（index.hpp:1197）与容量记账 container_slot_bytes（index.hpp:1257-1281）、ov_container_cap/ov_suffix_cap（index.hpp:169-170），没有释放保留容量的入口。诊断侧有 e24_report（index.hpp:3770-3776）。

**证据**：RESULTS_E24.md §2.2（元素余量 22%–30%，表在 validation_e24_20260918/RESULTS_E24.md:170-183）、§4.3（“溢出记录已减少，但容器容量仍然很大 → 收缩容器：支持但优先级最低”，同文件 :306、:316）；RESULTS_E32.md §4（溢出容器占维护时间 37.6%/37.3%/12.2%，代价集中在 erase 搬移，选定先做 O1）。容器方向的后续工作走 O1/T8（RESULTS_E34.md 标题“O1 溢出容器原型”、RESULTS_E36.md 标题“内联标签 B/O8/T8”），两者都明确不是收缩保留容量。

**覆盖与缺口**：已覆盖：进入条件的诊断——E24 元素余量 22%–30%、E32 §4 溢出容器成本。未覆盖：实现、实际分配容量测量、再次增长的分配次数、RSS 口径、空间与时间代价——一项都没有。E34/E36 换的是容器形态（稳定槽位/内联标签），不是释放保留容量。

**关闭标准**：要么按 4.2 的完整耗时口径实现一个“活跃数下降后触发”的收缩路径并计入再增长与 RSS 口径；要么用 E24 的余量数据给出“进入条件成立但不采用”的书面关闭依据。两者都缺。

**未能核实**：是否存在别名实现——我只按 shrink_to_fit/收缩/保留容量 关键词与 OverflowList 的公开接口核对，未见；若实现在 vendor/ 或历史版本目录里我未查。

> **反驳改判**：`已完成并采用` → **`部分完成`**（downgraded）
>
> 源码引用几乎全对：worth_compacting（index.hpp:3204，C2 字节闸门 3206-3207，C3 3209-3213）、group_has_room（3162）、group_garbage_bytes（3174）、reclaim_allowed（3229）、kMinReclaimGap（1324）、计数（259-264）、reclaim_skipped_gap/unchanged（3236/3240）、E30 compact_ns（106，仅 -DE30_TIME）、复用 compact（1870）、调用点（3276/3359）我都打开确认过；“不每次删除都做压缩”成立（compact 只在放置失败路径被调）。但 “已完成并采用” 对不上任务书这条的几个“采用”项：(1) 触发四合一里的“超过最小维护间隔”在出厂配置里是空的——reclaim_hysteresis/reclaim_skip_repeat 默认 false（index.hpp:705/712）→ reclaim_gated() 恒 false（index.hpp:3226）→ reclaim_allowed() 是常量 true（index.hpp:3230）；更关键的是 reclaim_allowed 只在 subblock_reclaim 路径上被调（index.hpp:3262/3345），而 subblock_reclaim 自己也是默认 false，它从不进入 worth_compacting→compact 这条被采用的路径，所以被采用的触发条件里根本没有时间/间隔分量。(…

### [B-4-5-04] §4.1（第 157-169 行）：什么时候整理——首版只用写操作已经需要更新的轻量统计（垃圾字节、空槽数、溢出记录数、上次维护以来更新次数）；条件“空间压力 AND 合法回收机会 AND 超过最小维护间隔 AND 满足临时空间预算”；在插入失败或若干次更新后检查，不每次删除都立即压缩；采用触发高水位、解除低水位与冷却间隔；至少记录维护尝试、成功、无收益拒绝、延后、复制字节与单次事件时间，“无收…

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | medium |

**实现**：触发闸门 worth_compacting（index.hpp:3204）：group_has_room（index.hpp:3162，M0 条件）／C2 group_garbage_bytes>=len（index.hpp:3174、3207）／C3 garbage>=len && repack_budget_fits（index.hpp:3209-3213）；由放置失败调用（index.hpp:3276、3359）。间隔+状态闸门 reclaim_allowed（index.hpp:3229）+ kMinReclaimGap=64（index.hpp:1324），开关 reclaim_hysteresis（index.hpp:705）/reclaim_skip_repeat（index.hpp:712）默认 false。计数：compact_entered/helped/no_help、compact_records_replayed/bytes_copied/garbage_before（index.hpp:259-264）；reclaim_skipped_gap/unchanged（index.hpp:3236、3240）；单事件时间 E30 compact_ns（index.hpp:106，仅 -DE30_TIME）。复用已有 compact（index.hpp:1870）而非新写。

**证据**：RESULTS_E26.md §0.1/§0.3（压缩触发与无收益占比：插入负载促成 0/531、0/2168、0/1901）；E26_TAGS.md §1-2；RESULTS_E28.md §3（触发条件太窄的机制）；RESULTS_E29.md §0（放宽触发 C3＝负结果 1.45×–6.02× 更慢，按任务书保留原策略并完成负结果报告）；RESULTS_E32.md §2（迟滞/最小间隔 E31H/E31P 两半都更差，明确“这条分支应当从清单里划掉”）。构建身份：check_freeze LIVE（logs/build_check_freeze.json）；check_e26_c2、check_c3 均 DRIFT。原始：e26_diag.jsonl、long_c3q_{same,grow}.jsonl、long_hyst_diag_{same,grow}.jsonl、w_diag.jsonl（E30 直接测每次压缩 16.14/17.95/16.03 µs）。

**覆盖与缺口**：已覆盖：轻量统计（按需算垃圾，不为查询加计数器）；在放置失败后检查（不每次删除都压缩）；C2 必要条件的字节闸门已采用并冻结（插入负载 0 条额外溢出）；C3 放宽触发与 E31H/E31P 间隔/状态均已实测且为负结果；“无收益后等状态变化”由 reclaim 门承担；复用 compact()。未覆盖：真正的触发高水位/解除低水位（只有单一间隔+状态比较，且实测为负）；“临时空间预算”作为独立 guard（只有 C3 内嵌的 repack_budget_fits）；“若干次更新后检查”的独立入口（只在放置失败路径）。

**关闭标准**：若要把 4.1 全文闭环：说明高/低水位形式被 E32 实测否掉后替代口径已足够，或补一个真双水位实现；补“临时空间预算”的独立计量；补每决策计数在干净构建里的可用性（见 could_not_verify）。

**未能核实**：我未核实“维护尝试/成功/无收益拒绝/延后/复制字节/单事件时间”六类计数在冻结计时构建里是否都可得——部分只在 -DE25_DIAG 下存在，且 RESULTS_E31 §8 自述 worth_compacting 的每决策计数器在干净构建里被编译掉（1,440 条记录全为 0）。

> **反驳复核**：维持原判（stands）。

### [B-4-5-05] §4.2（第 171-185 行）：怎样知道维护划算——对照必须从相同初始逻辑状态独立开始、使用相同后续操作；给出 M0（当前维护规则）/M1（选中原语按简单固定周期触发）/M2（同原语按状态条件触发）三变体；若要单独证明迁回效果改用 M0/只压缩/压缩加迁回；主结果是完整工作负载耗时（含规则检查、整理、迁回、目录维护和分配），辅助才是溢出率/垃圾量/搬移量/空间；不得只在离线整理完成后计查询；…

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：同初始状态＋同后续操作＋同进程配对由 bench_e26（C0/C1/C2/ART，臂序按重复轮换）、bench_long（长期替换）、bench_migrate（R0/R1）、bench_long_art/bench_long_hyst 承担。完整耗时口径：RESULTS_E26.md §1“计时含压缩、分配与复制”；RESULTS_LONG.md/E26_LONG.md 报逐检查点累计 ms；RESULTS_E37.md §8.1 给完整维护/点查/扫描/常驻空间。最坏暂停：RESULTS_E32.md §3 首报 p50/p99/p99.9/max（每 erase 与每 insert 各自一个样本）。搬移限制口径：RESULTS_E37.md §6/§10（kMigrateBudget 限条数，不承诺维护成本上界）。

**证据**：RESULTS_E26.md、E26_LONG.md、RESULTS_LONG.md、RESULTS_E29.md §5a（补上查询侧另一半账）、RESULTS_E32.md §3、RESULTS_E37.md §8.1；原始 results/main/e26_time.jsonl(1800)、long_replace_{same,grow}.jsonl(各384)、long_c3q_{same,grow}.jsonl、migr_{same,grow}.jsonl(6240)、ov_cost_diag.jsonl(24)。

**覆盖与缺口**：已覆盖：同初始状态/同后续操作/同轮配对（多轮一致）；完整耗时确实计入维护与分配；辅助结果（溢出率/垃圾量/搬移量/空间）齐全；最坏暂停首次有分布；搬移受限≠延迟上界的口径被写明；E29 §5a 按要求把查询侧收益一起算（更新代价÷每批查询收益 19×–4156×）。未覆盖：**M0/M1/M2 三变体表本身未执行**——全仓无“固定周期触发”臂（grep 固定周期/periodic/每 N 次 无命中），M1 缺失，M2 只以 C3/E31H/E31P 零散出现；**“M0/只压缩/压缩加迁回”三元组未执行**（E37 用 B/S/R0/R1，底座是 E31 子块回收而非“只压缩”）；**“累计耗时差随后续操作数变化”的曲线未画**（只有累计耗时，无差分曲线）。

**关闭标准**：执行或形式化替代 M0/M1/M2 与 M0/只压缩/压缩加迁回 两组对照（当前只有零散单臂，不能直接拼成 M1/M2 结论）；补画累计耗时差曲线；在冻结配置下重测维护最坏暂停。

**未能核实**：M1（固定周期触发）是否有别名实现——我按“固定周期/周期触发/fixed period/periodic/每 N 次”与 reclaim 间隔（kMinReclaimGap=64 只属于 E31H 的组合）检索未见独立臂，不能排除漏检。

### [B-4-5-06] §4.3（第 187-191 行）：什么时候停止这个方向——五种情形（合法空位很少；溢出主要是活跃记录的真实容量需求；频繁回收后很快又溢出；整理总成本超过其后续收益；只有隐藏维护时间才看见优势）不继续盲调阈值；下一层才是“区域内重新分配容量并保留需求布局”或“对更新频繁的组预留余量”，两者均需同预算评估且保留余量会牺牲初始装载率；暂不把模型重训加入这轮。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：无“停止条件”的形式化代码；判定散落在各轮报告里（不是一份统一裁决）：RESULTS_E29.md §0（C3 代价>收益 → 按任务书“保留原策略并完成负结果报告”停用 C3）；RESULTS_E32.md §2（迟滞被否 → “这条分支应当从清单里划掉，而不是再调参数”）；RESULTS_E28.md §0（F1 可修复 80%–95% → “合法空位很少”不成立）；RESULTS_LONG.md §3（到 200% 周期溢出条数接近活跃数）。区域级重分配（F3）在 RESULTS_E28.md §5 标注未实现且“没有对象”；预留余量未做；模型重训未加入（符合“暂不”）。

**证据**：RESULTS_E28.md、RESULTS_E29.md、RESULTS_E32.md、RESULTS_LONG.md、RESULTS_E31.md §7；原始 results/main/overflow_diag.jsonl(24)、overflow_diag2.jsonl、long_c3q_{same,grow}.jsonl、long_hyst_diag_{same,grow}.jsonl。跨章节：docs/字符串索引_架构与工程优化总执行计划_20260922.md §2.3（“整组整理与节流：E29、E32 已有负结果，关闭依据与负载边界是否保存；不再次盲调相同迟滞和最小间隔”）与 §2.4 要求输出 BACKLOG_CLOSEOUT.md——该文件不存在（全仓 find 无 BACKLOG/CLOSEOUT）。

**覆盖与缺口**：已覆盖：条件 4（整理总成本超过后续收益）在 C3 上被实测命中并停用（12 格 1.45×–6.02×，方向全 0/5）；条件“合法空位很少”被 E28 实测否掉并给出替代机制（子块级整理）；节流类阈值（条件 3 的近亲）被 E32 实测否掉且明确不再盲调。未覆盖：五种情形没有逐条判定（2“溢出主要是真实容量需求”、3“频繁回收后很快又溢出”、5“只有隐藏维护时间才见优势”都没有单独裁决）；“下一层”两个选项（区域内重新分配容量并保留需求布局、对热点组预留余量）连同其同预算评估与初始装载率代价均未做。

**关闭标准**：逐条给出五种停止情形在本项目数据下的成立/不成立判定（至少 2/3/5）；对“区域级重分配容量+保留余量”给出同预算评估或按条件未满足的书面关闭；明确“这个方向”整体是关闭、保留为候选还是继续——当前三种状态并存（C1/C2 已冻结、E31/E37 为候选、C3/迟滞为负结果）。

**未能核实**：我没有找到一份把 §4.3 五条统一裁决的记录，也未找到计划书要求产出的 BACKLOG_CLOSEOUT.md（确认不存在）；若裁决写在 04_Consolidation_and_Verification 或会话外，我未核（该目录只有 MANIFEST 与两个 py 脚本）。

### [B-4-5-07] §5 开篇与验收口径（第 197、219、221 行）：首轮先检查四个量（起点定位次数、访问空组/空子块数量、合并时的比较次数、输出键复制与分配量）；先只测冻结版与新增一个组件的版本、成功后再叠加；保留 E23 的初始/插入25%/替换25% 状态与长度 16/128/1024，返回不足指定长度时同时报告返回数；还要复核更新成本与点查，不能只测扫描；不预设必须超过有序数组；子块范围定位不写成新机…

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：四个量由 index.hpp:287-292 的计数（scan_regions/groups/groups_empty/blocks_skipped/merge_cmps/result_allocs）与 scan_start_locates（index.hpp:361）输出；叠加方式为派生程序 bench_scan → bench_scan_s2 → s3/s4/s5（派生而非改前者，RESULTS_E32.md §5 记账项 3）；状态与长度取自 E23 协议。

**证据**：E27_SCAN.md §1（27 行四量，空组恒 0.0；起点定位 2.01–2.49；空子块跳过 1.3–179.6；归并比较 0.23–0.99；结果分配 1.75–2.00 次/返回记录）；RESULTS_E27.md §0（“先测四项，再选一项”）；results/main/e27_scan_diag.jsonl(27)、e27_scan_time.jsonl(2025)、e27s2_scan_time.jsonl(2700)；分析 analyze_scan.py；门 check_scan_contract 为 LIVE（logs/build_check_scan_contract.json，含 1c 任意起点/不足 n、1d 抽空中间子块后仍等于参考）。

**覆盖与缺口**：已覆盖：四个量全部实测；三状态×三长度；不足 n 的扫描次数被报告（58–88/42–63）；一次只加一个组件（S0→S1，再 S2）；不与有序数组跨轮比较；E11/E13 的子块范围定位未被重新包装成新机制（RESULTS_E27.md §0 明写“没有动”）。未覆盖：**“还要复核更新成本与点查”没有做**——RESULTS_E27.md §5 明确门的输出是功能计数不是耗时（“不是点查、更新性能未受影响的证据”），E32 的 S2 也只测扫描；此外“区域/组/子块导航状态是否在一次扫描内重复建立”未诊断（后续计划 A4 自述 E32 的起点缓存与 E27 的结果构造“不等价于一次扫描内所有导航和归并状态都已复用”）。

**关闭标准**：在同一轮里补测扫描改动对更新成本与点查的影响（时间口径，同轮配对），否则“不能只测扫描”这条不成立；补一次导航/归并状态重建成本的诊断（计划 A4 的进入条件）。

**未能核实**：是否有别轮在同一进程里同时测了扫描与点查/更新可用于本项——我核了 E27/E32/E37 的扫描对照（bench_scan / s2 / s5），三者都只报扫描；E38/P3 的计时是模型与区域回退，不是扫描。

### [B-4-5-08] §5.1（第 201-203 行）：起点只定位一次，随后维护游标——用 lower_bound(start) 初始化，再由 next() 保留区域号、组号、子块号、主块位置与溢出位置，主块有序流与溢出有序流归并；不能为每个返回键重新调用点查，也不应先取出整个区域排序后只返回少量结果；起点允许不存在；不能把点查得到的组号当成完整的 lower_bound 答案；空子块、已删除边界、组末尾、区域末尾…

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：区域内容器级游标：Region::scan（index.hpp:3811）在一次调用内维护 blk/a/b（index.hpp:3823），一次推进过所有 < s 的条目（index.hpp:3825-3837），主块流与溢出流有序归并（index.hpp:3857-3861）——没有逐键点查、没有整区域排序。跨区域：Index::scan（index.hpp:4486）默认路径在循环条件里重复求值 locate(start)（index.hpp:4506-4511，R+1 次）；候选开关 Config::scan_cache_start（index.hpp:720，默认 false）缓存为 1 次（index.hpp:4495-4502）。跨调用的持续游标 API 未实现。

**证据**：E27_SCAN.md §1（start locates 2.01–2.49、regions 1.01–1.49）；RESULTS_E27.md §0/§3（“本轮测了，没改”）；RESULTS_E32.md §1（S2÷S1 = 0.96–1.02，最好 tranco 0.917/0.932 且 5/5，其余数据 2–4/5，跨进程离散度 1.03–1.34）。原始 e27_scan_diag.jsonl、e27_scan_time.jsonl、e27s2_scan_time.jsonl（均 DRIFT）。契约门 check_scan_contract 为 LIVE，覆盖任意起点、不足 n、抽空中间子块、溢出归并位置。

**覆盖与缺口**：已覆盖：区域内容器游标一次初始化+归并（满足“不为每个返回键重新点查”“不先整区域排序”）；起点缺失/不足 n/中间空子块/删除边界由 check_scan_contract（LIVE）与 check_scan（DRIFT）覆盖；跨区域重复求值被诊断且给出缓存候选。未覆盖：默认路径仍求值 R+1 次；跨调用 next() 游标未实现（后续计划 A4 要求在“诊断确认重建成本”后才做，该诊断未做）；scan_cache_start 只在百万域名上方向稳定。

**关闭标准**：决定是否把 scan_cache_start 纳入默认（当前非默认、最好 ≤8%）；若要做持续游标，先补“一次扫描内导航/归并状态重建成本”诊断，再按 S0/S1 对照、16/128/1024、不足 n、缺席起点、空块、已删除边界与完整扫描验收，并计入新增状态内存、复核点查与写入（计划 A4 给定）。

**未能核实**：E38 之后是否有新的扫描游标诊断——我核了 P3_PRECHECK.md（单进程内存预检）与 RESULTS_E38_P3.md（模型/区域回退计时），都与扫描游标无关。

### [B-4-5-09] §5.2（第 205-211 行）：用非空位图跳过空组，必要时再加入子块位图——每组一位表示“主块或溢出至少有一条活跃记录”；最后一个记录被删除时清位、首个记录插入时置位；必须把仅含溢出的组标为非空，不能只看主块计数；子块位图先只用于主块流、不能据此跳过整个组的溢出；更新开销和空间必须进入结果；若空组访问本来极少则跳过此项；E9 的空组分布不能直接代替当前诊断。

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **条件未满足，已按任务书关闭** |
| **置信度** | high |

**实现**：未实现：index.hpp 无 bitmap/nonempty/occupied-group 位图结构（grep bitmap/nonempty/non_empty 只命中边界目录注释与计数）；组遍历仍是 for (g = first; g < groups_; ++g)（index.hpp:3819），没有跳过层。诊断计数 scan_groups_empty 存在（index.hpp:289，在 index.hpp:3853 递增）。

**证据**：RESULTS_E27.md §0（“空组访问：全部 0.0 → 非空位图没有目标 → 跳过。E9 的空组分布不代表 E23/E26 的状态”）；E27_SCAN.md §1（empty groups 恒 0.0，27/27 格）；原始 results/main/e27_scan_diag.jsonl(27)。

**覆盖与缺口**：已覆盖：进入条件被本课题自己的诊断实测否掉（空组访问 0.0/扫描，三数据集×三状态×三长度），且明确拒绝用 E9 的历史分布代替当前诊断——与任务书末句一致。未覆盖：位图本体、置/清位、“仅含溢出的组必须标为非空”的判定、子块位图及其“只用于主块流”的限制——全部未实现；在空组恒 0 的进入条件下这些也无对象。

**关闭标准**：无需实现（任务书自己给了“若空组访问本来极少，跳过此项”的出口）。保留 e27_scan_diag.jsonl 作为进入条件不成立的凭据即已闭环。

**未能核实**：空组访问在改变了溢出分布的后续配置（E31/E37 的底座、O1/T8）下是否仍为 0——E27 的诊断早于这些轮次，之后未见重测；因此“关闭依据”的时效性只对 E27 当时的冻结配置成立。

### [B-4-5-10] §5.3（第 213-215 行）：减少完整键物化的中间副本——对需要返回拥有所有权的完整键的 API，尝试直接在输出字符串中一次准备空间并追加“共享前缀+后缀”，避免不必要的中间字符串；保留返回键值、顺序与生命周期语义；可增加不物化的内部访问接口帮助归因，但原型返回视图、基线返回完整字符串的计时不能放在同一排名；正式扫描仍让双方返回同样的完整结果。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：Config::scan_reserve_result（index.hpp:673，默认 true，已入冻结五开关）；结果构造在 index.hpp:3868-3877：reserve(prefix_.size()+suf.size()) + 两次 append，取代 else 分支的 prefix_ + std::string(suffix)；分配计数 scan_result_allocs（index.hpp:292）在 index.hpp:3867/3880 分别记 1 与 2，把“两次变一次只对主块侧分支成立”的口径写进代码。

**证据**：RESULTS_E27.md §1-§3（S1/S0 = 0.758–0.937，27 格全部同向；实测 1.75–2.00 次/返回记录，非恒定 2）；E27_SCAN.md §3；门 check_scan（DRIFT，logs/build_check_scan.json）把 S0 与 S1 输出逐条比对**并且**与参考表比对（每数据集 9,544,946–14,325,628 条），覆盖起点落在两键之间、返回不足 n、替换 25% 的中间空子块；check_freeze 为 LIVE（logs/build_check_freeze.json，断言 SHIPPED==CHOSEN 且含 scan_reserve_result=1）；原始 results/main/e27_scan_time.jsonl(2025)。

**覆盖与缺口**：已覆盖：主块侧分支的中间串消除；所有权/顺序/内容不变由逐条+参考表双重比对；三状态×三长度；两侧都返回同样的完整键值对，没有“视图 vs 完整串”的混排计时（任务书禁止的排名方式未出现）。未覆盖：溢出侧分支本就只有一次分配、未改（符合）；未实现“不物化的内部访问接口”，因此没有视图臂——也就不存在同排名问题；未做后续的分配归因诊断（计划 §6 E3 要求）。

**关闭标准**：无必做项。若要继续叠加以外的物化优化（计划 §6 E3），需先做分配归因诊断以确认剩余热点，且不得重复已有的临时串消除与标签复用。

**未能核实**：无（本项我逐条核了实现行 index.hpp:673/3868-3877、门的比对方式、原始数据与两个“不得混排”的口径）。唯一外部依赖是 check_scan 为 DRIFT——它的逐条比对绑在被取代的源码上；当前源码的扫描正确性由 LIVE 的 check_scan_contract 与 check_freeze 承担，这一点必须随证据一起引用。


---

## B-6-10  —— 工程线 §6–§10：模型诊断、借鉴来源、实验纪律、成功标准、排期收尾（25 条）

### [B-6-10-00] §6 首句：更新路径的预测调用次数应在 E24 一起记录（若后续仍要解释 S 与 ART 的点查差距）

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：计数钩子已实现：Counts::predict_calls 在 index.hpp:230，递增点 index.hpp:872（Model::predict 内，E25_INC(predict_calls)），整体在 #ifdef E25_DIAG（index.hpp:225,396）。E24 本身未记（RESULTS_E24.md:89,284 明确“调用计数并入 E25”），由 E25 补上。

**证据**：原始 results/main/e25_calls.jsonl（93 条）→ analyze_e25_calls.py → E25_CALLS.md（P=predict_calls 列，逐 API×类别×V0/V1/V2）。构建身份：logs/build_diag_e25_calls.json = DRIFT（index.hpp 已改，见 results/main/g0_build_drift.txt）。

**覆盖与缺口**：覆盖 update/erase/insert/upsert 的 primary_hit/overflow_hit/miss/duplicate_key 与 structural，三数据集 mixed/skew/tranco；未覆盖：任何模型的预测“耗时”量级（只有次数），LITS 后端下的分项调用计数。

**关闭标准**：无（次数矩阵已交付）。若将来要“时间量级”，另做隔离计时（见下一行）。

**未能核实**：E24 是否“一起”记录——实际是 E25 补记，与原文“应在 E24 一起记录”字面不一致；但 E24 报告显式声明顺延，属有意而非遗漏。

> **反驳改判**：`已完成并采用` → **`部分完成`**（downgraded）
>
> 两处独立问题。(1) 该条的字面要求是「在 E24 一起记录」；E24 自己明确没做：RESULTS_E24.md:89「本轮没有这些计数…调用计数并入 E25 的诊断构建」、RESULTS_E24.md:284「本轮没有，E25 补」——审计自己引了这两行，却仍判「已完成并采用」，等于把要求的时间点换掉后宣布达标。(2) 唯一的运行证据身份不绑定当前源码：logs/build_diag_e25_calls.json 的 build_id=1789753520872303606（2026-09-18 17:45 UTC），其中 index.hpp 哈希 7aba50ef…，而当前磁盘 index.hpp 是 587172d2…（我实算 sha256sum 核对），g0_build_drift.txt 亦列为 DRIFT。因此 results/main/e25_calls.jsonl（我核过确为 93 行）描述的是被取代的源码，E25_CALLS.md 开头「V0 is the CURRENT source」这句现在已不成立。计数器本体（index.hpp:230 Counts::predict_calls、index.hpp:872 在 Model::predict 内 E25_INC，整体在 #ifdef E25_DIAG index.hpp:225/396）我逐处打开确认存在，但「钩子存在」只等于钩子存在，不等于那份计数表是关于当前源码的测量。附带一条审计未提、影响该计数矩阵可信度的事实：RESULTS_E38.md §10② 记录 compactions 计数器「在库里…

### [B-6-10-01] §6 第一子弹：对同一混合查询序列采样，观察外层路由/模型/指纹/主块/溢出相关代码的样本分布；硬件计数可用时看 cycles/instructions/分支失误/缓存失效

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：未实现。工作区无 perf/rdpmc/callgrind 等采样或 PMU 装置；results/main 下无采样分布文件。最接近的是确定性分类计数（E25_CALLS.md 的 L/P/S/F/H/C/B 列），但那是调用计数，不是样本分布或硬件计数。

**证据**：检索范围：validation_e25_20260918/*.cpp、*.py、results/main/*.jsonl；全工作区 grep “perf stat/rdpmc/PERF_COUNT/callgrind/采样剖析”仅命中计划文档（docs/...E23之后的优化方案.md:227 与 结构审查方案:171）。

**覆盖与缺口**：已覆盖：按代码类别的调用计数。未覆盖：采样剖析、样本分布、cycles/instructions/分支失误/缓存失效等硬件计数。

**关闭标准**：若仍要解释点查差距，需新增采样/PMU 装置并报告样本量与打点影响；否则须在最终报告中显式登记“未做”及其理由。关闭标准＝有原始采样/计数记录，或有一句明确的未做登记。

**未能核实**：无。

> **反驳改判**：`已完成并采用` → **`部分完成`**（downgraded）
>
> 实现部分我确认无误：index.hpp:643 reuse_locate_ctx 默认 true，使用点 index.hpp:3314/3317 与 index.hpp:4462 我逐个打开，确为「复用本次定位的组号/标签/判重结果」；switches.hpp:42（baseline 置 false）、:65（冻结组合置 true）、:113-114（白名单断言）均在。但整行结论的两个证据文件全部来自 DRIFT 构建：bench_e25 build_id 对应 2026-09-19 12:40、diag_e25_calls 为 2026-09-18 17:45，两者的 index.hpp 哈希都不同于当前磁盘——g0_build_drift.txt 明确列出这两行为 DRIFT。于是 E25_CALLS.md「insert 新键 V1 2.0→V2 1.0」「upsert 新键 2/3/1→1/1/1」与 E25_TIME.md 的「V2/V0 全格同向」都不是当前源码上的测量。另外「改变模型表达与训练交给模型负责人，共同约定接口后单独比较」这一半：审计把 model/lits_hpt_adapter.hpp 当成「约定接口落地」，但该文件是在本课题树内替换模型后端的适配器（RESULTS_E38.md §5.1–5.3），不是与模型负责人共同约定的接口＋一次单独比较的产物，把它记为该项完成是拔高。

### [B-6-10-02] §6 第二子弹：单独执行模型预测并保留结果，测其数量级；再测完整查询；不能把前者直接当作后者中可精确扣掉的一块

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：未实现。没有单独 predict 计时目标；MODEL_AND_MAPPING.md:140 明确“没有测量预测质量”。所有 *.cpp 里的 predict 只出现在计数/门，未隔离耗时。

**证据**：MODEL_AND_MAPPING.md:140（“本项目从未报告误预测分布或组间不均衡度”）；grep predict 命中的 *.cpp（check_*/diag_*）无 timing；results/main 无 predict-only 记录。

**覆盖与缺口**：未覆盖模型单独耗时，也未覆盖“预测占完整查询多少”的数量级问题。

**关闭标准**：新增单独 predict 微基准并保留原始记录；报告须写明“两者缓存/流水线不同、不可相减”。关闭标准＝有原始记录且带不可相减声明。

**未能核实**：无。

> **反驳改判**：`已完成并采用` → **`部分完成`**（downgraded）
>
> 这一段的触发条件是明确的诊断前提，而前提根本没被测。(a) 原文「若硬件计数可用，再看 cycles、instructions、分支失误与缓存失效」——我在整个轮目录里没有找到任何硬件计数（perf/PMU）记录，审计自己的 coverage 也承认「未覆盖用硬件计数判定『是否主要是内存等待/外层比较』的前提」。触发条件未做诊断，就不能把条件分支写成「已执行并采用」，只能算部分完成。(b) 该行的实现描述与项目后一轮自相矛盾：审计写「范围定位：E27 先测起点定位 2.01–2.49 次/扫描，『测了没改』」，但 README.md:70 的 E32 条目写明「扫描起点定位缓存：locate(start) 从每扫描 R+1 次降到 1 次，端到端 0.92–1.02、百万域名最好 0.932（5/5）」——该项后来确实改了（scan_cache_start，默认 false 的候选），审计把 E27 当轮的「没改」当成最终结论。(c) SIMD 那一半我核实成立：switches.hpp 全表、index.hpp、tools_build.py 中确有 AVX/AVX2/AVX-512 字样为零，编译命令固定 -march=x86-64-v2（tools_build.py:38/39/55）。所以是「一半成立、一半无诊断」→ 部分完成。

### [B-6-10-03] §6 第三子弹：必要时用预先记录组号的诊断变体，观察“用数组读取替代预测”后的整条路径；该变体不是可部署方案，也不是模型成本的精确测量

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：未实现。代码里唯一的映射侧诊断钩子是 Region::sar_rotate_groups（index.hpp:3667–3673，#ifdef E25_DIAG），用于 check_scan_contract 的保序负对照（旋转映射 (g+1)%groups_，index.hpp:1463），与“预记录组号/数组读取替代预测”不是同一件事，且它本身也不是可部署路径。

**证据**：index.hpp:1459–1463、3667–3673；负对照见 MODEL_AND_MAPPING.md:106–127、check_scan_contract.cpp；日志 logs/check_scan_contract_{mixed,skew,tranco}.log。构建身份：check_scan_contract = LIVE（14/14，results/main/g0_build_drift.txt）。

**覆盖与缺口**：已覆盖“故意非保序映射”的负对照；未覆盖“预记录组号/数组读取替代预测”的整条路径诊断，也无该变体“非可部署、非精确测量”的边界记录（因为没做）。

**关闭标准**：若需要，实现预记录组号变体并在报告中标注额外数组读取与缓存效应、非可部署、非加速上界。关闭标准＝有该变体的原始记录＋边界声明，或明确登记不需要。

**未能核实**：无。

> **反驳改判**：`已完成并采用` → **`部分完成`**（downgraded）
>
> 审计给的项目侧证据经不起对照。它说等价物是 01_Research_Report/StringLearnedIndex_Literature_Review_and_Task2_Plan.md:177–185 的「借鉴对象表」——我把该表打开，它的表头是「原定创新点／共同要求 → 优先参考 → 本项目应具体研究的连接」，行是共享前缀模型、预测驱动物理布局、缓存预取、动态维护、创新验证；这与 §7 的四行（ART／ALEX／LITS／B+ 树 → 借鉴点／用途／边界）不是同一张表，谈不上「等价物」。另一条证据 README.md:60 我逐字读了，原文是「仍慢于成熟 ART…LITS 接入保留了诊断与失败证据…并发、在线调参和最终论文新颖性仍未完成验证」，它没有「不把 SIMD/位图/压缩称为原创」这层意思（我 grep 过 README.md 全文，「原创」零命中）。审计的 coverage 自己承认「未覆盖一张与 §7 逐行对应的『用途/边界』产物表」。于是「已完成」实际只剩任务书自身那张表在支撑——这是拿被审文档证明被审文档。ART 未照搬一点我认（index.hpp 中无 ART 节点结构，libart 只作 third_party/vendor 外部对照），但整行判定不成立。

### [B-6-10-04] §6 第二段：若预测被证明突出，可在索引职责内减少重复调用、避免后缀复制、复用本次预测已产生的信息；改变模型表达与训练交给模型负责人，共同约定接口后单独比较

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：已实现（V2）：reuse_locate_ctx，index.hpp:643（默认 true），使用点 index.hpp:3314,3317,4462；把同一次定位的组号/标签/判重结果复用。开关登记见 switches.hpp:42,65。模型接口由 model/lits_hpt_adapter.hpp（E38）落地。

**证据**：E25_CALLS.md（insert 新键预测 V1 2.0→V2 1.0；upsert 新键 路由/预测/判重 2/3/1→1/1/1）；E25_TIME.md（V2/V0 全格同向）；switches.hpp:42,65；check_freeze（LIVE）断言默认==选定组合。构建身份：bench_e25 = DRIFT（index.hpp 已改）。

**覆盖与缺口**：覆盖“减少重复调用/复用已产生信息”；未覆盖“避免后缀复制”的写侧单独计账（读侧内联前缀在 E35 被判不成立）。模型训练侧不在本课题。

**关闭标准**：无（V2 已默认启用；模型接口契约见 E38）。

**未能核实**：“避免后缀复制”是否被单独考察过——写路径未见到针对后缀复制的隔离计数。

> **反驳改判**：`已完成并采用` → **`部分完成`**（downgraded）
>
> 代码侧我确认：place_fail_slots/place_fail_bytes 在 index.hpp:1632–1633（我打开看到两处 E25_INC，注释明确「no free slot」与「not enough suffix bytes」分开计），子块内按键序插入在 index.hpp:1638–1640，M1「键序恰好选中一个子块」在 index.hpp:3779/3810。但这一行被拿来当证据的三处全部是 DRIFT 基准：RESULTS_E28 用的 diag_transfer/diag_overflow、RESULTS_E31 用的 bench_long_hyst、FINAL_RESULTS_TABLE §5b 引的 bench_long/bench_migrate——按 g0_build_drift.txt 它们全部 DRIFT，所以「三约束受预算维护缓解更新退化」这个实测结论没有绑在当前源码上。更实质的一点：同属 §7 末段的硬要求「要主张研究创新，还需与已有维护方法比较，说明组合中的具体新机制和适用边界，而不是用『实现由自己写』替代新颖性论证」，审计的 coverage 自己写「未覆盖『研究贡献已成立』的论证形态（见下一条）」——但账本里并没有「下一条」承接它，这条要求在两行之间落空。

### [B-6-10-05] §6 第三段：其他工程优化由证据触发（内存等待→预取；外层比较→路由选型；子块访问多→范围定位）；统一 x86-64-v2 SIMD 基线保留，不将 AVX2/AVX-512 混入同一硬件预算对照

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | medium |

**实现**：已执行。范围定位：E27 先测起点定位 2.01–2.49 次/扫描，“测了没改”（E27_SCAN.md）。预取：明确不做（README.md:78 “不再追加前缀、预取或针对 skew 的搜索方案”）。路由：未改（Route::Full 默认，MODEL_AND_MAPPING.md:66）。SIMD：switches.hpp/index.hpp 中无 AVX2/AVX-512 开关，保持统一基线。

**证据**：E27_SCAN.md；RESULTS_FREEZE.md:154；README.md:78；switches.hpp 全表（无 AVX 开关）。

**覆盖与缺口**：覆盖范围定位/预取的证据触发决策与 SIMD 基线约束；未覆盖用硬件计数判定“是否主要是内存等待/外层比较”的前提。

**关闭标准**：无。

**未能核实**：“证据触发”的依据是调用计数与耗时，而非硬件计数；是否曾以 PMU 判定瓶颈类型未找到。

> **反驳复核**：维持原判（stands）。

### [B-6-10-06] §7 表格四行（ART/ALEX/LITS/B+树）的有依据借鉴点、用途、边界；不照搬整棵 ART；不把节点自适应称为原创

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | medium |

**实现**：计划文档给出表（docs/字符串学习型索引_E23之后的优化方案.md:237–244）；项目侧等价物是 01_Research_Report/StringLearnedIndex_Literature_Review_and_Task2_Plan.md:177–185 的“借鉴对象”表，加各轮报告中的边界声明。代码中未照搬 ART 结构，ART 仅作外部对照适配器。

**证据**：MODEL_AND_MAPPING.md:139（不声称与 LITS 等价）；README.md:60（不把 SIMD/位图/压缩称为原创）；Literature_Review:257（LITS+ 的 SoA 指纹与延迟排序已存在，不能作为独立新贡献）。

**覆盖与缺口**：覆盖四类来源的边界声明；未覆盖一张与 §7 逐行对应的“用途/边界”产物表（只有较宽的研究报告表）。

**关闭标准**：若报告定稿，宜将 §7 表原样落一份并逐行勾边界。

**未能核实**：B+ 树(STX)一行的专用借鉴产物——未见单独引用或落点。

> **反驳复核**：维持原判（stands）。

### [B-6-10-07] §7 末：主要核对 ART 第 III 节、ALEX 第 4.3–4.4 节、LITS 第 3.1 与 3.3 节；没有据此保证原型能取得论文中的性能

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：“不保证性能”已遵守（README.md:123、RESULTS_FREEZE.md:121 均声明不声称全面超过 ART）。但指定的节级核对范围在项目文档中未见逐节记录：Bibliography_and_Reading_Status.md 只有书目级阅读状态。

**证据**：01_Research_Report/Bibliography_and_Reading_Status.md（R01/R04/R06 行，无节号）；grep “第 III 节/4.3–4.4/3.1 与 3.3”仅命中计划文档本身。

**覆盖与缺口**：覆盖书目级；未覆盖 ART III、ALEX 4.3–4.4、LITS 3.1/3.3 的节级核对记录。

**关闭标准**：在文献清单/报告补三个来源的逐节核对条目。关闭标准＝有落点的节级核对记录。

**未能核实**：节级精读是否发生过——文档层面查不到，可能发生过但未记录。

> **反驳改判**：`已完成并采用` → **`部分完成`**（downgraded）
>
> 三种构建里第三种不存在，而它恰恰是原文单列的一项。(a) 「计时 vs 计数」两分我确认成立：tools_build.py:30 的说明段、以及 tools_build.py:84–130 的目标表把 diag_e25_calls/diag_e26/diag_scan/check_e31/check_overflow_store 等逐条挂在 -DE25_DIAG 上，并把「clean timing build stays counter-free」写进注释。(b) 但「内存诊断记录已覆盖分配接口下的请求字节」没有对应目标：我列了 logs/build_*.json 的全部目标名，没有任何 mem/alloc/space 类诊断构建；内存走的是 precheck_memory.py，而它的 docstring 第 4–6 行自陈「NOT a timing run and NOT a measurement of the index…no allocation accounting is added back into the clean timing program」，即它从进程**外部**读 RSS 与 mallinfo2，而不是「在已覆盖的分配接口下记录请求字节」。审计的 coverage 自己写「未覆盖一个独立『内存诊断构建』目标（内存用确定性记账而非分配包装构建）」——既然如此，「已完成并采用」就是把缺项写成了完成项。

### [B-6-10-08] §7：适合尝试形成贡献的表述——在固定字符串模型与逻辑分组下，结合槽位、后缀字节和键序三种约束，设计受预算限制的物理布局维护，并评估其对更新退化的缓解作用

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | medium |

**实现**：该主线被实际执行：E26/E28–E31/E34–E37 全部围绕槽位/字节/键序三约束与受预算维护。三约束在代码可见：place_fail_slots/place_fail_bytes（index.hpp:1632–1633）与按键序选块。

**证据**：RESULTS_E28.md（F0–F3 分层）、RESULTS_E31.md（subblock_reclaim）、FINAL_RESULTS_TABLE.md§5b（E34 O1/E36 T8/E37 R1）。

**覆盖与缺口**：覆盖写/维护侧三约束；未覆盖“研究贡献已成立”的论证形态（见下一条）。

**关闭标准**：无（作为可运行主线已执行）。

**未能核实**：无。

> **反驳复核**：维持原判（stands）。

### [B-6-10-09] §7：要主张研究创新，还需与已有维护方法比较，说明组合中的具体新机制和适用边界，而不是用“实现由自己写”替代新颖性论证

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：未完成。项目自认“论文新颖性证明”未完成（00_Overview/StringLearnedIndex_Project_Overview_and_Guide.md:38；README.md:60）。E36 有一条与 LITS+ 的单点比较（Literature_Review:257），但无系统的“与已有维护方法逐项比较＋具体新机制＋适用边界”论证。

**证据**：00_Overview/StringLearnedIndex_Project_Overview_and_Guide.md:38；README.md:60。

**覆盖与缺口**：未覆盖。

**关闭标准**：做一节与 ART/ALEX/LITS/LINE 等维护机制的比较，给出本组合的新机制与适用边界。关闭标准＝该比较节落盘并被汇总引用。

**未能核实**：无。

> **反驳改判**：`已完成并采用` → **`部分完成`**（downgraded）
>
> 大部分成立：analyze_e25_time.py 头部（第 1–13 行）我打开确认——配对在单次重复内成形、先按进程取中位数再报跨进程离散、不出「两个聚合中位数的商」、dir 计方向一致的独立进程数；RESULTS_E25.md §0.3 确实存在（第 76 行「默认版本选择」）；MODEL_AND_MAPPING.md:164 的「5 独立进程、配对在重复内、方向计数、噪声地板」也在。但原文的重复协议里有一项**没有记录**：「分至少两个运行时段」。E25 的正式协议里看不到时段划分，审计的 coverage 亦承认「『分布在两个运行时段』仅见 E31 的两次独立运行互检，未见 E25 正式协议中的时段划分记录」——E31 是另一轮、另一装置，不能顶替 E25 的协议条款。另：审计把 SUMMARY 的行号写成 RESULTS_SUMMARY.md:139，我核了该行是 E31 的「回收分支确实在跑」，不是未分辨纪律的出处，引用号不对。因此「已完成并采用」应降为部分完成。

### [B-6-10-10] §8 固定起点：冻结 R1 为对照；模型/数据/编译参数/查询语义/种子/初始规划保持一致；先单组件后组合；ART 当前轮重新运行；不乘历史比值、不沿用旧轮 ART 绝对耗时

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：已执行。R1 冻结（RESULTS_FREEZE.md:19）；ART 同进程同重复内同跑（E25_TIME.md:1–10；bench_e26/bench_scan 同轮）；跨轮相乘已删除（validation_e17_20260916/LOG.md:5,73 记“总报告中跨轮相乘的合计已删除，换成直测值”）。

**证据**：RESULTS_FREEZE.md:19,89；E25_TIME.md:1–10；validation_e17_20260916/LOG.md:5,73。

**覆盖与缺口**：覆盖更新与扫描的同轮对照；构建三行仍为历史值、未重测（FINAL_RESULTS_TABLE.md§5 已明确标注）。

**关闭标准**：无（构建历史值已如实标注）。

**未能核实**：无。

> **反驳改判**：`已完成并采用` → **`部分完成`**（downgraded）
>
> 空间入账（RESULTS_E24.md:112–124 两块恒等式＋memory_breakdown，我读了确为该内容）与差分验证（check_lits_index/check_overflow_store/check_tag_store 在 g0_build_drift.txt 中为 LIVE，14/14）成立。问题有三：(1) 七项专项里「仅溢出组」「长后缀」审计自己承认「未见逐字独立门」；(2) 「目录边界删除与重复插入」引的是 validation_e25_20260918/LOG.md:62——那一行是当轮**开跑前**写的操作序列清单（我读了全文），不是某个门跑过之后的输出，用计划行当门证据；(3) 差分验证里引的 check_migrate 实际是 DRIFT（build 于 2026-09-21 10:42，index.hpp+switches.hpp 已改），所以迁回那条差分结论不绑当前源码。再有一条审计完全没提、却直接影响该行证据完好性的事实：RESULTS_E33.md 顶部更正写明，E33 引用的两个 sanitizer 证据文件（results/main/sanitizer_runs.jsonl、logs/run_sanitizers.log）**已被 E38 首次运行就地覆盖**，原始 build_id／exe 哈希／耗时「不可恢复」，现存的只是 E38 重建后的复验（sanitizer_runs_e38.jsonl 9 行、全 OK，我核过）；「E33 首次 6 次 rc=0」这半句在现有记录里只剩报告文字。

### [B-6-10-11] §8 ART 比较域：限定在当前适配器已通过检查的无 NUL ASCII 键域；固定 libart 版本/键所有权/值表示；嵌入 NUL 与高位字节的功能测试另列；扫描中 ART 不进正式排名

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：已执行。E25 §5.6 明写 ART 对照限定无 NUL ASCII 键域、扫描不参与排名（ART 无匹配适配器；原型针对更广键域的功能检查另列）。E38 做了更广键域（LITS 侧）的功能检查。

**证据**：RESULTS_E25.md:154；E25_TIME.md/E25_CALLS.md；RESULTS_E38.md；check_lits_fingerprint.cpp。

**覆盖与缺口**：覆盖 ASCII 域对照与“扫描不排名”；ART 侧未见扩展键域（扩展在 LITS 侧）。

**关闭标准**：无。

**未能核实**：libart 版本/键所有权/值表示是否“固定”有书面记录——未逐项核对构建记录。

> **反驳改判**：`已完成并采用` → **`部分完成`**（downgraded）
>
> §9 表格第一列就是「必须看到的机制证据」，而该行赖以成立的三处同轮对照是 DRIFT：统一定位与组级维护的证据来自 bench_e25/bench_e26（g0_build_drift.txt 列为 DRIFT，index.hpp+switches.hpp 已改），扫描来自 bench_scan（同样 DRIFT）；审计引的 RESULTS_SUMMARY.md §3.2/§3.3/§4.1、RESULTS_FREEZE.md §1/§6 我确认章节存在（§3.2 在第 107 行、§3.3 在第 119 行、§4.1 在第 339 行，FREEZE §1/§6 在 7/87 行），但它们是那些 DRIFT 数字的转述。其次，审计自己在 coverage 里写「未见一张与 §9 逐行对应的三行表（决策散落在各轮报告）」——§9 要求的是逐方向的「机制证据／采纳条件／退出条件」三列并列，缺了这张产物表就等于把「文档里有决策」当成「按条款交付」。所以判定应降级。

### [B-6-10-12] §8 分开三种构建：正式计时关闭诊断计数器与分配包装；计数诊断记录执行工作；内存诊断记录已覆盖分配接口下的请求字节；算法必需的维护统计与触发检查留在计时程序

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：已执行（两半＋内存分装置）。计时构建无诊断计数；计数诊断独立（-DE25_DIAG，tools_build.py:30,84–130）；内存侧以确定性 memory_breakdown() 记容量，precheck_memory.py:1–12 明确“不把分配记账加回干净计时程序”。算法必需触发检查（compact_entered 等）留在计时程序。

**证据**：tools_build.py:30；validation_e25_20260918/precheck_memory.py:1–12；RESULTS_E24.md:112–124。

**覆盖与缺口**：覆盖“计时/计数”两分与“内存不进时间表”；未覆盖一个独立“内存诊断构建”目标（内存用确定性记账而非分配包装构建）。

**关闭标准**：无（口径已在报告中声明）。

**未能核实**：E9_ALLOC 分配包装在本轮是否作为第三构建使用——未找到本轮单独的内存构建记录。

> **反驳改判**：`已完成并采用` → **`部分完成`**（downgraded）
>
> 前半句成立、后半句没做完，且证据行有一处硬错。硬错：审计写「README.md:123 明确『不声称全面超过 ART』」——我逐行打开，README.md:123 是「/ 根 / 02_Code_and_Experiments/ 的**打包清单** /…」这一行打包说明；「本报告不声称全面超过 ART」的原句在 RESULTS_FREEZE.md:121（我核到），README 里对应的是第 60 行「仍慢于成熟 ART…」与第 62 行的分负载数字（持续插入快 2.06×、混合 1.14×、百万域名 1.69×、替换 1.72×／3.25×／6.50×）。全文 README 里「全面超过 ART」这一串零命中。后半句未完成是审计自己承认的：coverage 写「未覆盖『哪些字符串分布/扫描比例下有意义的完整结论』（边界散见，未成节）」——而原文把这一句列为该项必须回答的问题。加上分负载比值本身来自 DRIFT 的 bench_e25/bench_e26，故降为部分完成。

### [B-6-10-13] §8 动态计时覆盖维护＋每次重放从独立状态开始：索引操作/触发检查/整理/分配/同步更新元数据均计入；输入生成/参考容器/答案核对在区间外；单独调用的维护纳入同一完整负载；动态测试不能在同一已改索引上重复同一批操作

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：已执行。E25 §2 “计时只含索引操作及其触发的全部维护、分配、复制；键生成、参考表维护、答案核对在区间外”；三负载各自从全新初始状态开始，四臂接收相同序列。

**证据**：RESULTS_E25.md:110,113,115；E25_TIME.md:14–17。

**覆盖与缺口**：覆盖；但 E25 的维护是内联触发（无“单独调用”路径），该子情形在本原型不适用。

**关闭标准**：无。

**未能核实**：无。

> **反驳复核**：维持原判（stands）。

### [B-6-10-14] §8 正式负载五行表（不变量控制/沿用比较/长期维护/检查未覆盖路径/受影响读路径）；每检查点同时保留固定查询序列与固定命中比例查询、分开报告；答案与命中标签按真实状态生成；偏斜插入生成规则固定

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：基本执行。五行对应：只改值(E25)、邻近插入+删除后补入到25%(E25)、长期替换50/100/200%(RESULTS_LONG)、区域分裂结构行(E25_CALLS structural)、点查回归+扫描E23语义(E25/E27)。固定查询序列＝点查回归；固定命中比例＝E35 按来源分队列。

**证据**：E25_TIME.md；E25_CALLS.md:185–196；RESULTS_LONG.md；RESULTS_E35.md；E27_SCAN.md。

**覆盖与缺口**：覆盖五行；固定查询序列与按来源队列在不同轮次（E25 与 E35）分别报告，未在每一检查点并列同报；偏斜插入固定规则未逐字核到。

**关闭标准**：在最终表里把“固定查询序列”与“固定命中比例队列”在每个检查点并列同报；补偏斜生成规则的固定声明。关闭标准＝两者同检查点并列出现。

**未能核实**：“偏斜插入的生成规则必须固定”是否有明确记录——未逐行核到。

> **反驳改判**：`已完成并采用` → **`部分完成`**（downgraded）
>
> 除一项外我都能核到，且核到的那一项审计自己承认没做。成立的部分：tools_build.py:11–19 的 docstring 写明 run/run_current「refuses to run unless the build record exists and the executable hash matches it…ALSO refuses when any source recorded in the build has changed」；tools_build.py:372–398 的 run_current 在漂移时打印 RUN REFUSED 并 sys.exit(2)、run_recorded 打印「DESCRIBES THE RECORDED BUILD, NOT THE CURRENT SOURCE」；硬停有产物级验证，RESULTS_FREEZE.md §5 记「失败时一并删除 build/<target>.built.json 与 build/<target>.exe」、§7 给出「改坏源码后运行 → RUN REFUSED rc=2、两个文件已删除」的步骤表。没做的是原文专列的一项：「分析脚本拒绝将内存诊断写入时间表」。我 grep 了 analyze_e25_time.py（负责生成时间表 E25_TIME.md）与其余 analyze_*.py，没有任何拒绝/断言逻辑，analyze_e25_time.py 连 memory 字样都不出现；审计的 coverage 亦写「内存诊断与时间表的隔离是『分装置』而非『拒绝断言』」。要求是脚本层面的…

### [B-6-10-15] §8 重复与统计：初筛少量独立进程；确认5进程/至少两时段/每进程5配对重复；随机或平衡方案顺序；单次重复配对比值、先按进程汇总再报跨进程中位数/范围/同向进程数；尊重同进程批次相关；预先规定分析办法；E23 比值离散，差几个百分点在噪声内→“未分辨”保留旧默认

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：已执行。5 独立进程×5 配对重复、臂顺序轮转平衡、配对在重复内、按进程中位数、报跨进程范围与方向数（analyze_e25_time.py:1–13）。未分辨纪律：E25 §0.3 对 V2/V1 增量在重复前缀上如实标注“落在噪声内”；E31 §3.3 用两次独立运行互检。

**证据**：analyze_e25_time.py:1–13；RESULTS_E25.md:42,81,152；MODEL_AND_MAPPING.md:164；RESULTS_SUMMARY.md:139。

**覆盖与缺口**：覆盖配对/按进程汇总/未分辨；但“分布在两个运行时段”仅见 E31 的两次独立运行互检，未见 E25 正式协议中的时段划分记录。

**关闭标准**：若需严格两时段，补记时段标识。

**未能核实**：E25 正式协议是否满足“至少两个运行时段”——未找到时段划分记录。

> **反驳改判**：`已完成并采用` → **`部分完成`**（downgraded）
>
> README 与总览确实被更新过，R1/R2/冻结/覆盖范围都在：README 头部第 3 行写「内容更新：2026-09-18、2026-09-20（冻结，以及冻结之后的 E28–E31）」，正文第 62 行起有冻结成绩，00_Overview/StringLearnedIndex_Project_Overview_and_Guide.md 第 22–38 行的「当前冻结版本」块与评估面表（构建/点查/更新/扫描/空间/更新路径/并发）也在。但「写清当前前沿及后续计划」这一半被 stale 掉了，且比审计写的更严重：我 grep README.md 全文，「E38」出现 **0** 次（前沿已到 E38）；README.md:81 仍保留「**下一轮（E37，审阅给定，尚未执行）：有界迁回**」，而 E37 在 09-21 已完成（README.md:80 自己就有 E37 的结果段）——同一份文件里「E37 尚未执行」与「E37 结果」并存；README.md 的 mtime 是 2026-09-21 19:32，即最后更新停在 E37 那一段。审计的 coverage 只写到「顶部日期停在 09-20/09-21」，没有指出缺 E38 与自相矛盾那两处。故降为部分完成。

### [B-6-10-16] §8 尾延迟：主计时用整批计时；维护事件时长单独记录；若测单操作 P99 用另采样延迟模式并报样本量与打点影响；样本不足不宣称可靠 P99.9；各阶段 P99 不能相加

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：部分执行。E25/E26/E27 主计时为整批累计；E25 §5.3 明写“没有测尾延迟”。尾延迟首次在 E32 §3 用逐操作采样模式测（报样本数、打点影响），E34/E36/E37 续测。E32 明确“p99 是 ART 的 20–73 倍只在某几行成立”。

**证据**：RESULTS_E25.md:151；RESULTS_E32.md:90–113；RESULTS_E34.md:98。

**覆盖与缺口**：覆盖 p99/p99.9、样本量、打点影响；“各阶段 P99 不能相加”未见逐字声明；E25–E27 阶段本身未测尾延迟。

**关闭标准**：补一条“各阶段 P99 不得相加”的显式口径。关闭标准＝该口径落盘。

**未能核实**：“各阶段 P99 不能相加”是否有书面纪律——未找到。

### [B-6-10-17] §8 空间与正确性：新元数据/临时缓冲/保留容量全部入账；差分验证覆盖值/存在性/扫描顺序/完整结果；专项检查（中间空子块、仅溢出组、目录边界删除、长后缀、槽位与字节错配、重复插入、失败后旧状态不变）；ASan/UBSan 用于新维护路径，不可用则记录限制

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | medium |

**实现**：基本执行。空间入账见 E24 §2 两恒等式＋memory_breakdown；差分验证见 check_lits_index/check_overflow_store/check_tag_store/check_migrate（逐条比对值与顺序）。专项：中间空子块(E27/check_scan)、目录边界删除与重复插入(LOG.md:62)、槽位/字节错配(place_fail_slots/bytes)。sanitizer：E33 首次 ASan+UBSan（正对照触发、6 次 rc=0），E38 9 次 PASS。

**证据**：RESULTS_E24.md:112–124；validation_e25_20260918/LOG.md:62；RESULTS_E33.md（README.md:72）；RESULTS_E38.md §9.4；run_sanitizers.py；logs/build_san_*.json = LIVE。

**覆盖与缺口**：覆盖大部分专项；“仅溢出组”“长后缀”未见逐字独立门；E24 阶段尚“无 sanitizer”（后被 E33 补齐）。

**关闭标准**：无（早期无 sanitizer 的限制已如实标注）。

**未能核实**：“仅溢出组”与“长后缀”专项是否有独立门——未逐字核到。

### [B-6-10-18] §9 三行表（统一定位/组级维护/扫描路径）的机制证据、采纳条件、退出/保留可选条件

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | medium |

**实现**：三轴的采纳/退出决策均已作出：统一定位＝V1/V2 默认启用（写耗时收益、点查无退化）；组级维护＝C1/C2 默认启用、C3(E29) 与 E31/E34 O1/E36 T8/E37 R1 作负结果或候选关闭；扫描＝S1 默认启用、仍慢于有序数组。

**证据**：RESULTS_SUMMARY.md§3.2/3.3/§4.1；RESULTS_FREEZE.md§1,6；FINAL_RESULTS_TABLE.md。

**覆盖与缺口**：三轴均覆盖机制证据与采纳/退出；但未见一张与 §9 逐行对应的三行表（决策散落在各轮报告）。

**关闭标准**：可选：把三轴整理成 §9 形式的一张三行表。

**未能核实**：“退出/保留可选条件”是否被逐条引用——散见各轮，未集中。

### [B-6-10-19] §9 项目管理目标：把“目标负载完整耗时下降约10%”作工程目标、“长时替换后空间增长受控”作维护目标；点查可约定5%实用退化容忍界；噪声不足则不能声称没有回归

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：未以这些阈值形式采纳，但相关事实已测：空间目标被明确报告“未达到”（README.md:64,121、RESULTS_FREEZE.md§6a）；点查回归 0.99–1.01 远小于 5%；只改值 1.85×–3.06× 远优于 10%。

**证据**：RESULTS_FREEZE.md§6a；README.md:64,121；E25_TIME.md:281–285。

**覆盖与缺口**：覆盖空间目标（明确未达成）与点查回归；“10% 完整耗时”未作为统一阈值对照（插入/替换未达，方向不一）。

**关闭标准**：在最终报告把两个数值目标（10%/5%）显式对照一次（空间已可判“未达到”）。关闭标准＝有一处显式对照。

**未能核实**：10%/5% 是否曾被项目正式采纳为验收线——未见明确采纳记录，按“未采纳但事实已测”处理。

### [B-6-10-20] §9 达到目标后再看与 ART 差距缩小多少；没有全面超过 ART 也可形成有效成果，需清楚回答在哪些字符串分布/更新模式/扫描比例下本结构有意义

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | medium |

**实现**：已执行。README.md:123 与 RESULTS_FREEZE.md:121 明确“不声称全面超过 ART”，并按负载分列“持续插入重复前缀快2.06×、混合慢1.14×、百万域名慢1.69×、替换仍是短板”。

**证据**：README.md:123；RESULTS_FREEZE.md:121；RESULTS_SUMMARY.md§3.x。

**覆盖与缺口**：覆盖“按场景报告差距”；未覆盖“哪些字符串分布/扫描比例下有意义的完整结论”（边界散见，未成节）。

**关闭标准**：无。

**未能核实**：无。

### [B-6-10-21] §10 E24–E28 六阶段表（建议时间/最小交付/进入下一步条件）＋约9–15有效工作日预算、诊断否定假设应缩减工作

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | medium |

**实现**：已执行（阶段角色有变形）。E24 诊断交付；E25 定位复用交付；E26 局部维护交付；E27 扫描交付；E28 从计划中的“最终确认”变成冻结后的长期空间诊断（RESULTS_FREEZE.md 承担“冻结组合、ART对照、适用边界”这一最小交付）。冻结于 2026-09-20 完成。

**证据**：RESULTS_E24.md、RESULTS_E25.md、RESULTS_E26.md、RESULTS_E27.md、RESULTS_FREEZE.md、RESULTS_E28.md。

**覆盖与缺口**：覆盖六阶段与最小交付；E28 的角色与计划表不完全一致（改为空间诊断）。

**关闭标准**：无。

**未能核实**：是否严格遵守 9–15 工作日预算——无工时记录，无法核。

### [B-6-10-22] §10 交付纪律：每轮保留源码/构建配置/正确性输出/原始JSONL·CSV/自动生成表格/结果说明/变更日志；run_id、源码·二进制哈希、诊断标识；分析脚本拒绝将内存诊断写入时间表；构建失败不能继续执行旧程序；运行入口仍要核对成功构建记录

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | medium |

**实现**：基本执行。构建记录含源哈希/编译命令/exe哈希（tools_build.py）；run 在 exe 哈希不符时拒绝；E35 起 run_current 重算源哈希、不一致 rc=2（run_recorded 警告运行）。构建失败硬停：失败时删 .built.json 与 .exe（RESULTS_FREEZE.md§5/§7）。“分析脚本拒绝把内存诊断写入时间表”以“内存诊断用独立脚本、不进计时程序”实现（precheck_memory.py），未见显式拒绝断言。

**证据**：tools_build.py:11–19,372–398；RESULTS_FREEZE.md§5,§7；precheck_memory.py:1–12；logs/build_*.json。

**覆盖与缺口**：覆盖哈希/run_id/构建闭环；内存诊断与时间表的隔离是“分装置”而非“拒绝断言”。

**关闭标准**：若严格按字面，可加一条分析脚本对内存诊断字段的拒绝断言。

**未能核实**：run_id 的具体字段名与每轮是否齐备——未逐轮核。

### [B-6-10-23] §10 更新项目入口 README 和总览，写清默认版 R1、可选 R2、当前前沿 E23 及后续计划；历史各轮结论保留上下文；汇总清单声明覆盖范围

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | medium |

**实现**：已执行。README§3 写冻结版本＝五项开关默认启用、R1 构建、R2 可选、E28–E38 各轮；MANIFEST 覆盖范围在 README§5 逐条声明；FINAL_RESULTS_TABLE 逐行标版本/来源。

**证据**：README.md；00_Overview/StringLearnedIndex_Project_Overview_and_Guide.md:18–38；FINAL_RESULTS_TABLE.md:1–6,155–161。

**覆盖与缺口**：覆盖；但“当前前沿 E23”表述已被后续轮次取代（前沿已到 E38），README/总览顶部日期停在 09-20/09-21。

**关闭标准**：README/总览顶部的前沿日期未同步到 E38（最新一轮）。

**未能核实**：无。

### [B-6-10-24] §10 最终报告围绕一条问题组织：固定模型的物理布局，如何在空间、点查、扫描和持续更新之间取得可验证的取舍

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：部分。项目以 RESULTS_SUMMARY.md / FINAL_RESULTS_TABLE.md / README.md / 00_Overview 总览收口，均按“构建/点查/更新/扫描/空间”五面组织（README.md:52–58、FINAL_RESULTS_TABLE 分节），与问题四维一致；但未以该单一问题为主线成文，工作区内无“最终报告”专文（最新前沿停在 E37/E38）。

**证据**：RESULTS_SUMMARY.md 目录；README.md:52–58；FINAL_RESULTS_TABLE.md；docs/字符串学习型索引_E23之后的优化方案.md:312。

**覆盖与缺口**：覆盖四维（空间/点查/扫描/持续更新）的取舍证据；未覆盖“围绕一条问题”的单一主线成文。

**关闭标准**：写一份以该问题为主线、引用四维同轮证据的最终报告。关闭标准＝该报告落盘且被汇总入口引用。

**未能核实**：是否存在未纳入本工作区的“最终报告”文件——只找到 docs/ 下的计划类文档。


---

## C-long  —— 长程执行任务书（E25 至收尾）（46 条）

### [C-long-00] 起点与范围 L15：E24 已冻结，E25 目录为 validation_e25_20260918；先核对磁盘实际状态，不得把任务书中的进度描述当作源码验证。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：已实现：以磁盘为准开始本轮。E25 目录由 E24 复制（8 个继承门 + 头文件链 + third_party + tools_build.py），删除 E24 专属 diag/driver。

**证据**：LOG.md「E25-0 继承与起点（2026-09-18）」；validation_e25_20260918/ 目录实际存在；继承门在默认开关下与 E24 逐字节相同（LOG.md「E25-3」，11/11）。

**覆盖与缺口**：已覆盖：起点核对、目录继承、门基线一致性。未覆盖：无（该条只要求核对起点）。

**关闭标准**：无。

**未能核实**：无。

> **反驳复核**：维持原判（stands）。

### [C-long-01] 起点与范围 L17-19：V0/V1/V2 的定义（V0=冻结 R1 行为基线；V1=写路径高效搜索含插入判重搜索模式调整、保留原控制流重复工作；V2=在 V1 上复用本次定位的组号/标签/判重结果）；R1 指构建路径，不得与 V1/V2 混用层次命名。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：index.hpp:628-643 定义 write_fast_locate(V1)/reuse_locate_ctx(V2) 并注释与 V0 的关系；Located 四态（NotLocated/Absent/Primary/Overflow）在 index.hpp:2837、struct Located 在 index.hpp:2839；V1 实现入口 index.hpp:3307/3425/3458；V2 复用入口 index.hpp:3314/3317/4462。

**证据**：LOG.md「E25-1 接口」「E25-2 实现」；RESULTS_E25.md 顶部变体表；E25_CALLS.md 列 V0/V1/V2。命名层次：E25_TIME.md 头部与 RESULTS_E25.md §2 均声明 R1=构建路径。

**覆盖与缺口**：已覆盖：定义、实现、命名分开。未覆盖：无。

**关闭标准**：无。

**未能核实**：无。跨章节观察：E37（冻结之后）把有界迁回实验的臂也命名为 R0/R1，与构建路径 R1 同名；任务书明令「不混用这两个层次的命名」，而 E37 的 README/EXPERIMENT_INDEX 里 R1 一词同时指两条不同事物（见 README.md:81、EXPERIMENT_INDEX.md E37 行）。

> **反驳复核**：维持原判（stands）。

### [C-long-02] 起点与范围 L21：M0 明确指现有维护规则，E25 三个变体都使用 M0；它不是未经定义的第四个索引方案。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：V0/V1/V2 三个 Config 均保持 compaction/migration 相关开关为假；无新增维护策略。

**证据**：E25_TIME.md 头部「Every arm uses M0, the existing maintenance rule. No variant adds a compaction or migration policy of its own.」；RESULTS_E25.md 第 13 行「M0 = 现有维护规则，三个变体都用它」。

**覆盖与缺口**：已覆盖：三变体都走 M0。未覆盖：无。

**关闭标准**：无。

**未能核实**：无。

> **反驳复核**：维持原判（stands）。

### [C-long-03] 起点与范围 L22：不重做 E24，不修改已冻结的历史原始数据；新阶段使用新目录或明确新版本；模型创新、并发、GPU、全新路由结构不进入本任务。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：E25/E26/E27 与冻结全部在 validation_e25_20260918 内完成，未改 E24 及更早目录；模型/并发/GPU/路由未动。

**证据**：TASK_STATE.md 阶段表与各阶段记录；RESULTS_FREEZE.md §1「R1 构建、模型、逻辑映射、主块预算未动」；RESULTS_FREEZE.md §8 把「无 sanitizer / 单线程 / 并发、游标复用、反向扫描未测」列为已知未做候选。

**覆盖与缺口**：已覆盖：范围守住、无越界改动。未覆盖：并发/GPU/新路由按任务书要求本就不做。

**关闭标准**：无（排除项无需关闭）。

**未能核实**：无。

> **反驳改判**：`已完成并采用` → **`部分完成`**（downgraded）
>
> 该行 implementation 断言「模型/并发/GPU/路由未动」。但同一目录里已存在第二个模型族：index.hpp:23 `#include "model/lits_hpt_adapter.hpp"`，:597 `enum class ModelBackend { Context16, LitsHpt }`，:613 model_backend，另有 model/lits_hpt_adapter.hpp、vendor/lits_model_safe/*、MODEL_AND_MAPPING.md、LITS_AUDIT.md、check_lits_* 与 bench_lits_adapter；g0_evidence_timeline.txt 记 model/lits_hpt_adapter.hpp 改于 2026-09-22 12:45，LIVE 构建里也全是 check_lits_*/bench_lits_adapter。要么「模型未动」写得过宽，要么 L22 的边界在同一目录内被越过——该行两者都没交代，不能维持『已完成并采用』。

### [C-long-04] 起点与范围 L23：当前外部基线是已接入的 ART；扫描继续使用语义匹配的有序数组；ART 没有符合本项目要求的扫描适配时，不加入扫描排名。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：四臂 V0/V1/V2/ART 在同一进程同一次重复内跑；扫描三臂 S0/S1/ARR（ARR=全局有序数组+lower_bound+复制 n 条）。

**证据**：E25_TIME.md 头部「四臂：V0/V1/V2 + ART（同一 libart）」；E27_SCAN.md §3「ARR is a global sorted array with lower_bound」，§2 头部「ART 不参与扫描排名」。README.md:66 / RESULTS_SUMMARY.md §4「ART 不进排名」。

**覆盖与缺口**：已覆盖：ART 用于更新/点查排名，排除了扫描排名；有序数组用于扫描对照。未覆盖：ART 的 seek-to-key 迭代器缺失这一限制只在 E23 记录，本轮沿用未重测。

**关闭标准**：无。

**未能核实**：无。

> **反驳复核**：维持原判（stands）。

### [C-long-05] 第一阶段 L27：先读适用的 AGENTS.md、E25 的 LOG、构建入口、正确性门和诊断宏，确认如何构建/运行；若源码与汇报不一致，以实际代码为准，记录差异后继续必要修复。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：读 LOG/构建入口/门/诊断宏；-DE25_DIAG 计数基础设施落地（E25_CALLS.md §0 / LOG.md「E25-4a」）。

**证据**：LOG.md「E25-4a 调用计数基础设施」；diag_e25_calls.cpp:29「Build with -DE25_DIAG」；tools_build.py 提供 run/run_current/run_recorded。

**覆盖与缺口**：已覆盖：构建/运行入口与诊断宏。未覆盖：工作区中不存在 AGENTS.md（在 D:\StringLearnedIndex_Workspace 下按名字查找无命中），本轮未读该文件。

**关闭标准**：若项目本应存在 AGENTS.md，需确认其缺失是否为有意（当前仅能记录「未找到」）。关闭标准：明确 AGENTS.md 不存在或已并入 LOG/README。

**未能核实**：我按 -iname 'AGENTS.md' 在工作区层级查找无结果；不能确认任务书写该条时它是否曾存在，也不能确认它是否被其他文档（LOG/README）替代。

> **反驳改判**：`已完成并采用` → **`部分完成`**（downgraded）
>
> L29 把「压缩后位置失效」列为本轮必查项。逐行读 check_e25_write.cpp：用例确含主块/溢出更新删除、删子块首位、删最后一个活跃条目、能放下/进溢出/触发压缩的插入、重复插入被拒、未命中、删后重插、跨子块组(SIMD批次)，末尾三条分支断言(NotLocated=1/前缀重建=1/阈值分裂=9)也确在源码(约300-361行)——但**没有任何『压缩后位置失效』的构造或计数断言**。该行 coverage 自称「由 E26/E28 诊断覆盖」不成立：check_e26_c2 比的是内容与键序，RESULTS_E28.md 是只读试放分层(F0/F1/F2)，都不断言位置失效；真正出现「保存位置失效」字样的是更晚一轮的 san_bench_long_hyst(RESULTS_E33.md:91)，不属本阶段。另 check_e25_write 属 DRIFT(g0_build_drift.txt)。

### [C-long-06] 第一阶段 L29：现有门与源码对应、运行记录有效时，不为了任务开始而反复执行所有历史门；检查本轮关键覆盖：主块/溢出写入、未命中、重复插入、压缩后位置失效、跨 SIMD 批次，以及已增加的 NotLocated 回退、前缀重建和区域分裂事件断言；新增代码只补相关风险的验证。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：check_e25_write.cpp 从同一容量方案建 V0/V1/V2，灌同一条序列，每批逐条全等比对内容/size/溢出数/压缩数并与参考表比对；末尾用专门构造用例做事件计数断言（三分支）。

**证据**：LOG.md「E25-4 正确性验证」及三条分支断言表（NotLocated 回退=1、前缀不匹配区域重建=1、阈值处区域分裂=9）；比对规模 806,656 与 12,123,136 条记录，全部 passed。构建记录 logs/build_check_e25_write.json。

**覆盖与缺口**：已覆盖：主块/溢出写入、未命中、重复插入、跨 SIMD 批次（跨子块组使 SIMD 标签批次越过前 16 槽）、NotLocated 回退、前缀重建、分裂。未覆盖：check_e25_write 属 DRIFT（构建于 2026-09-20，晚于当时但早于 index.hpp 末次改动 2026-09-22 12:51:49），其证据绑定在 E25 期源码上，不能作为当前源码的回归证据；压缩后位置失效在 check_e25_write 中未见单列断言（由 E26/E28 诊断覆盖）。

**关闭标准**：无（就 E25 期源码而言已闭环）。若需要「当前源码」的等价保证，需重跑——冻结后由 check_freeze(LIVE) 与继承门覆盖，属可接受替代。

**未能核实**：「压缩后位置失效」是否在 check_e25_write 内有独立断言，我未逐行核对该 .cpp。

> **反驳改判**：`已完成并采用` → **`部分完成`**（downgraded）
>
> L31-39 对 erase 要求「移动条目、实际复制字节和释放工作」，对 insert 要求「放置尝试、槽位/字节等失败原因、压缩事件」。核 E25_CALLS.md 的三张表：只给 shift(条目数)、relP/relO、att(slot/bytes) 与 B——**「实际复制字节」没有以字节形式给出**（该行 coverage 自认「以 shift 计数给出，非字节形式」）；**per-category 表中没有『压缩事件』列**（e25_calls.jsonl 每条确有 compactions 字段，本次核到 update/erase/insert/upsert 全为 0，实质影响小，但矩阵里只在 structural 行出现）。故「四行类别全部有数据」成立而「必报内容齐全」偏松。

### [C-long-07] 第一阶段 L31-39：用 V0/V1/V2 在相同状态与操作序列上生成调用矩阵，表格四行类别（update：主块命中/溢出命中/未命中；erase：同上+移动条目/复制字节/释放工作；insert：新键/重复键+放置尝试/槽位字节失败原因/压缩事件；upsert：已有键/新键+是否再次进入完整判重/再次预测/再次哈希；重建分裂等慢路径单列）。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：diag_e25_calls.cpp（-DE25_DIAG）+ analyze_e25_calls.py 生成 E25_CALLS.md。

**证据**：E25_CALLS.md（mixed/skew/tranco 三节，各含 L/P/S/H/C 表、比较字节与放置表、erase work 表）；results/main/e25_calls.jsonl（93 条 = 3 数据集 × 3 变体 × 10 类别 + 3 条结构行）。

**覆盖与缺口**：已覆盖：四行类别全部有数据；upsert 明确给出 L/P/S（V0 2.0/3.0/1.0 → V2 1.0/1.0/1.0）；重建/分裂作为 structural 行单列（region_split_build，200 次插入，11 rebuilds）。未覆盖：erase 的「移动条目」以 shift 计数给出，非字节形式；「释放工作」用 relP/relO（垃圾 vs 真销毁）分开给出。

**关闭标准**：无。

**未能核实**：无。

> **反驳复核**：维持原判（stands）。

### [C-long-08] 第一阶段 L41-50：八条具体口径（V0 标为「当前源码、默认开关的 V0 诊断」；指纹计增量哈希与 tail 消费字节、F 只作辅助、避免 byte/tail 重复计数；预测/完整查重/物理放置搜索分开；比较字节是逻辑量不称 DRAM 读取；失败计数说明是否首个拒绝原因、不得把重叠类别相加；参考表/逐条核对/诊断试放不计入执行计数且窗口有明确起止与重置；前缀采用准确表述）。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：计数挂钩在 byte()/tail() 上（避免重复计数），放置失败计 place_fail_slots/place_fail_bytes（首个拒绝原因）；诊断窗口在类别操作前后重置。

**证据**：E25_CALLS.md §0「Column meanings, and what is deliberately kept apart」逐条写明；LOG.md「E25-4a」记录 tail 重复计数坑与修法；E25_CALLS.md §0 V0 标注「CURRENT source with the default switches, measured now」。

**覆盖与缺口**：已覆盖：八条口径全部落到文档与代码。未覆盖：无。

**关闭标准**：无。

**未能核实**：无。

> **反驳复核**：维持原判（stands）。

### [C-long-09] 第一阶段 L52 完成条件：矩阵可以从原始记录自动生成，能具体说明 V1 和 V2 分别减少了什么；此阶段不据计数推算性能倍数。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：analyze_e25_calls.py 由 e25_calls.jsonl 自动产出 E25_CALLS.md。

**证据**：E25_CALLS.md 头部「Source: results/main/e25_calls.jsonl, 93 records」；RESULTS_E25.md §1 表给出 V1 减少全串比较（70–579→0–3.2 次）、V2 减少预测与哈希（2.0→1.0、606→516），并明示 cmp_calls/place_attempts 不变；E25_CALLS.md 顶部「These are counts, not timings」。

**覆盖与缺口**：已覆盖：自动生成、V1/V2 减少内容、不推算倍数。未覆盖：无。

**关闭标准**：无。

**未能核实**：无。

> **反驳复核**：维持原判（stands）。

### [C-long-10] 第二阶段 L56：先通过冒烟测试，再按固定协议执行正式实验；计时构建关闭诊断计数与内存分配包装，内存诊断另跑；算法必需的判断、结构维护和分配保留在计时范围内。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：bench_e25.cpp 为干净计时构建（顶部注释「CLEAN TIMING BUILD. No diagnostic counters, no allocator wrappers.」）；计数由独立 diag_e25_calls 提供。

**证据**：bench_e25.cpp:3-8 注释；RESULTS_E25.md §2「干净计时构建（bench_e25，无诊断计数、无分配包装），计数由独立的 diag_e25_calls 提供」。

**覆盖与缺口**：已覆盖：干净计时构建（诊断计数与分配包装分离）。未覆盖：未找到 E25 的独立「内存诊断」运行——RESULTS_E25.md/TASK_STATE.md/RESULTS_LONG.md 中无内存或峰值空间的测量记录；e9_alloc.hpp 是确定性容量规划器，不是计数分配器包装，不能当作内存诊断。

**关闭标准**：补一次独立内存诊断（峰值/常驻）或明确记录「本轮不做内存诊断及其理由」。关闭标准：存在该运行记录或其被显式豁免。

**未能核实**：我核了 RESULTS_E25.md、TASK_STATE.md、LOG.md、RESULTS_LONG.md、RESULTS_E26.md、E26_TAGS.md 与 e9_alloc.hpp，未发现任何 E25 期的内存/峰值诊断；不能排除存在于我未读的文件中。

> **反驳复核**：维持原判（stands）。

### [C-long-11] 第二阶段 L58：三种负载（只改值、持续插入新键、删除后补入不同新键）；各负载独立从等价初始状态开始，V0/V1/V2/ART 接收相同序列；插入和替换采集 0%/5%/10%/25% 检查点；替换周期为一次删除加一次插入，明确周期数与操作数。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：bench_e25 三负载各自从新初始状态开始；检查点 0/5/10/25%；replace 同报 cycles 与 ops。

**证据**：E25_TIME.md 每节表头「checkpoint / cycles / ops」；头部「update_only / insert_only / replace are three separately built states, each from its own fresh initial state」；RESULTS_E25.md §5.1 再次声明「不是一条混合负载的分解」。

**覆盖与缺口**：已覆盖：三负载、独立初始状态、同序列、检查点、周期/操作数口径。未覆盖：0% 检查点未在 E25_TIME.md 表格中单列（表从 5% 起）。

**关闭标准**：无。

**未能核实**：「0% 检查点」是否被记录但我未在表中看到——E25_TIME.md 的表从 5% 开始。

> **反驳复核**：维持原判（stands）。

### [C-long-12] 第二阶段 L60：默认 5 个独立进程、每进程 5 次配对重复，并尽可能分布在两个运行时段；每次动态重放恢复初始状态，不在已改变的索引上重复同一批操作；平衡或随机化方案顺序，预热一致；不得并行跑会相互争抢 CPU/内存的正式性能实验。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：run_e25.py：5 进程串行，每进程 5 reps，臂顺序每次重复轮转；bench 每次重放恢复初始状态。

**证据**：run_e25.py 全文（默认 --processes 5 --reps 5，串行嵌套循环，注释「Processes run serially on purpose」）；E25_TIME.md 头部「5 个独立进程 × 每进程 5 次配对重复」「臂顺序每次重复轮转」。

**覆盖与缺口**：已覆盖：5×5、配对、轮转、串行、恢复初始状态。未覆盖：「尽可能分布在两个运行时段」无证据——run_e25.py 单次循环内完成全部进程，日志只有一个 start/end 时间戳，未记录跨时段分布。

**关闭标准**：若需满足「两时段」，需分两次启动并各自记录时间戳；或记录明确豁免理由。关闭标准：日志体现两个时段，或文档写明为何单时段。

**未能核实**：我读了 run_e25.py 全文与 E25_TIME.md 头部说明，未看到两时段分布的证据；不能排除 logs/run_e25.log 内有我未读的分段记录（进程内的 tag=P1–P5 只是进程编号，不是时段）。

> **反驳复核**：维持原判（stands）。

### [C-long-13] 第二阶段 L62：计时只包含索引操作及其全部维护、分配、复制；键生成、参考表维护和答案核对在区间外；若接口返回值语义不同，在适配器中统一并校验，不能省略原型判重、却让基线承担通用接口成本。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：bench_e25 计时起止只覆盖索引操作；参考表/键生成在区间外。为 ART 适配器加 size()（用 art_insert 返回值判断新键）以便每段后核对。

**证据**：RESULTS_E25.md §2「计时只含索引操作及其触发的全部维护、分配、复制；键生成、参考表维护、答案核对在区间外」；LOG.md「E25-6」记录为 ART 适配器加 size() 并说明理由。

**覆盖与缺口**：已覆盖：计时边界与适配器统一。未覆盖：无。

**关闭标准**：无。

**未能核实**：无。

> **反驳复核**：维持原判（stands）。

### [C-long-14] 第二阶段 L64-73：输出同次重复内的直接配对比值 V1/V0、V2/V1、V2/V0、V2/ART；若最终选 V1 或 V0 为默认也直接报告其与 ART 的比值；不得由两个汇总比值相乘获得总收益，不混用「中位数之比」与「配对比值的中位数」；按进程汇总，报跨进程范围与方向一致性。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：analyze_e25_time.py 在重复内配对、按进程取中位数；FINAL_RESULTS_TABLE.md 逐行标注来源。

**证据**：E25_TIME.md 每个检查点的四比值表（含 range 与 dir n/N）；头部「No ratio here is the quotient of two aggregate medians」；E26_TAGS.md 底部「None of these is formed by multiplying the other two」。

**覆盖与缺口**：已覆盖：四个比值全部给出，含跨进程范围与方向计数。未覆盖：默认版本最终选的是 V2（V1+V2），V2/ART 已报；「若选 V1 或 V0」的前提未成立，故无需另报。

**关闭标准**：无。

**未能核实**：无。

> **反驳复核**：维持原判（stands）。

### [C-long-15] 第二阶段 L75：补一次必要的点查回归比较，因为 find_impl 增加了可选位置出参；若使用全部/纯命中/纯未命中三种负载，必须保持「不同负载」口径，不得拼成混合负载阶段分解。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：find_impl 增加 Located* 可选出参（index.hpp:2911，默认 nullptr，只读点查不构造上下文）；bench_e25 固定键序列计时点查。

**证据**：RESULTS_E25.md §3 与 E25_TIME.md「Point-query regression」表：V1/V0 与 V2/V0 全在 0.99–1.01。

**覆盖与缺口**：已覆盖：点查回归。未覆盖：本轮用的是单一固定键序列，未采用「全部/纯命中/纯未命中」三负载划分（该三负载形式出现在更晚的 E35），故不存在拼接风险。

**关闭标准**：无。

**未能核实**：无。

> **反驳改判**：`已完成并采用` → **`部分完成`**（downgraded）
>
> L87 含三项要求。前两项(0/0/256 的界定、减法得到的垃圾不得称三路独立验证)确有据：RESULTS_SUMMARY.md §5 有「这是『压缩前』试放…不是所有重排方法的上限」与「垃圾是减法结果…无第三条独立路径验证其本身」。但**「活跃后缀独立累计、水位独立读取」在 E26 阶段没有落地**：grep 整个 E26_TAGS.md 与 RESULTS_E26.md，「水位」「bottom」「活跃后缀」零命中；该拆分只出现在更晚的 RESULTS_LONG.md §3.2(「水位 bottom 不动」)。该行 coverage 自己也承认「在 E26 阶段只在 E28/RESULTS_LONG §3.2 才被显式拆开」，却仍判「已完成并采用」，内部不一致。

### [C-long-16] 第二阶段 L77：ART 固定当前已验证版本、键和值所有权及无 NUL ASCII 适配范围；原型更广键域的功能检查独立列出；正式对照同轮运行，不拿历史 ART 时间代替。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：ART 臂在同一进程、同一次重复内与 V0/V1/V2 同跑（同一 libart）。

**证据**：E25_TIME.md 头部「ART runs in the SAME repetition」措辞见 E26_TAGS.md；RESULTS_E25.md §2「四臂…+ART（同一 libart）」；RESULTS_E25.md §5.6「ART 对照限定在无 NUL ASCII 键域（适配器既有边界）」。

**覆盖与缺口**：已覆盖：同轮 ART、键域限定已列出。未覆盖：「原型更广键域的功能检查」未见独立列出的文档；无 NUL ASCII 限定只在 RESULTS_E25.md §5.6 一行，未进 README/RESULTS_SUMMARY 的显著位置。

**关闭标准**：把键域限定与更广键域功能检查补入交付文档的显著位置。关闭标准：README/总览含该条。

**未能核实**：我在 README.md、RESULTS_SUMMARY.md、FINAL_RESULTS_TABLE.md 中检索「ASCII/NUL/键域」无命中（仅 README.md:126 是另一语境），不能确认它是否写在其它轮次文档里。

> **反驳复核**：维持原判（stands）。

### [C-long-17] 第二阶段 L79：若噪声使小收益无法分辨，记录「未分辨」继续后续任务，不无限重跑；若 V1 有明确收益而 V2 没有则选 V1；若都无明确收益则保留原默认；不要仅凭调用次数减少启用新默认。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：默认选择规则落在 TASK_STATE.md「当前决策」。

**证据**：TASK_STATE.md「当前决策」：默认 = V1+V2；依据为 V1÷V0 9 格同向、V2÷V0 9/9 同向且全 5/5、V2÷V1 无一致劣化；重复前缀上 V2 增量落在噪声内并「按格如实标注」；「不是仅凭调用次数减少作出的选择」。

**覆盖与缺口**：已覆盖：噪声标注、选择规则、不凭计数。未覆盖：无。

**关闭标准**：无。

**未能核实**：无。

> **反驳复核**：维持原判（stands）。

### [C-long-18] 第二阶段 L81 完成条件：E25 有完整原始数据、自动生成结果表、当前 ART 对照、点查回归结果和默认版本选择；正确性失败的变体不得进入正式排名。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：e25_time.jsonl(3600) → analyze_e25_time.py → E25_TIME.md；e25_calls.jsonl(93) → E25_CALLS.md。

**证据**：RESULTS_E25.md §6 原始数据清单；TASK_STATE.md 阶段表；check_e25_write 全部 passed（无失败变体进入排名）。

**覆盖与缺口**：已覆盖：五要件齐全。未覆盖：无（除 §11 指出的门为 DRIFT 这一身份限制外）。

**关闭标准**：无。

**未能核实**：无。

> **反驳复核**：维持原判（stands）。

### [C-long-19] 第三阶段 L85（前半）+ L85 三件事：先回答①现有 compact 的触发和无收益重复情况；②压力子块内的垃圾字节；③清除垃圾之后能成功放入的记录数及所需复制字节。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：①由 e26_diag 的 entered/ran/helped 计数回答；②由 garbage_before（仅被压缩的组）回答；③在 RESULTS_E26.md 被判为「不适用」。

**证据**：RESULTS_E26.md §0.1①表（ran/helped/no help：insert 负载 helped=0/531、0/2168、0/1901；replace 3.0%/3.5%/2.7%）；§0.1②表（garbage_before 仅压缩组：insert 恒 0，replace 47.4/286.2/79.1 KB）；§0.1③「不适用：C1 保持记录放置不变…而 C2 走的不是压缩后试放，是在压缩前就判断它能否腾出足够字节」。E26_TAGS.md §1/§2。

**覆盖与缺口**：已覆盖：①触发与无收益重复（实测）；②压力子块内垃圾字节（实测，且明确限定只看被压缩组，不等同全索引总量）。未覆盖：③「清除垃圾之后能成功放入的记录数及所需复制字节」没有被回答——报告以「不适用」替代，而任务书要求的是压缩后试放的数字。

**关闭标准**：补「压缩后试放」这一问的实测量（记录数+复制字节），或在文档中给出比「不适用」更强的关闭依据。关闭标准：该问有两组数字，或文档明确论证该问在 C1/C2 语义下不可定义。

**未能核实**：我以 RESULTS_E26.md §0.1③、E26_TAGS.md（全 93 行）与 RESULTS_E28.md §0 为准；E28（冻结之后）用只读试放给出 F1「回收选中子块自己的垃圾就能放回 80%–95%」，但这属后续轮次、且问的是碎片而非「清除垃圾后放置数+复制字节」。

> **反驳复核**：维持原判（stands）。

### [C-long-20] 第三阶段 L85（后半）：压缩后试放必须在副本或只读模拟中完成，不修改计时对象。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：E26 未做压缩后试放（见上条：被判「不适用」），因此没有对应的只读试放实现落在 E25/E26 阶段内。

**证据**：RESULTS_E26.md §0.1③；作为对照，E24 的试放是「压缩前、固定边界、保留主块成员」，任务书 L87 明确说不要把它当所有重排的上限。只读试放机制真正出现是在 E28（`diag_overflow` F0–F2，只读），属冻结之后。

**覆盖与缺口**：已覆盖：无（本阶段内）。未覆盖：压缩后只读试放本体。

**关闭标准**：若判定该问必要，需在 E25/E26 阶段口径下补一次副本/只读模拟的压缩后试放。关闭标准：存在该试放记录，或任务书裁明该问可豁免。

**未能核实**：我不能排除 E24 或 E26 的某个 diag 程序里存在我未读到的只读试放分支。

> **反驳复核**：维持原判（stands）。

### [C-long-21] 第三阶段 L87：E24 的 0/0/256 是压缩前、固定边界、保留主块成员的试放结果，不要当作所有重排的上限；活跃后缀独立累计、水位独立读取；垃圾若由差额得到就如实标明，不把恒等式称为三路独立验证。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | medium |

**实现**：RESULTS_E26.md 与 E26_TAGS.md 把 garbage_before 定义为「被压缩组内的未引用字节」；RESULTS_SUMMARY.md §5 记录 E24 的 0/0/256 及其定义与口径限制。

**证据**：RESULTS_E26.md §2「`garbage_before` 只统计被压缩的那些组，不是全索引总量」；RESULTS_SUMMARY.md §5 行「按键序的合法迁回…这是『压缩前』试放…它界定的是当前算法在现有布局上的机会，不是所有重排方法的上限」；同表「垃圾是减法结果（后缀区 − 活跃 − 未用），无第三条独立路径验证其本身」。

**覆盖与缺口**：已覆盖：口径限定与「非上限」表述。未覆盖：「活跃后缀独立累计、水位独立读取」在 E26 阶段只在 E28/RESULTS_LONG §3.2 才被显式拆开（水位 bottom 不动）。

**关闭标准**：无。

**未能核实**：「水位独立读取」在 E26 阶段文档中未显式出现，是 E28/RESULTS_LONG 补上的；我据此判定该口径最终被覆盖。

> **反驳复核**：维持原判（stands）。

### [C-long-22] 第三阶段 L91-96：按分支表自主选择（有可回收垃圾且压缩后能承接 → 研究/完善压缩触发；活跃溢出少但容量过大 → 评估延迟收缩；压缩后仍有合法迁回机会 → 才做保守迁回；空闲在其他组 → 记录否定结论并跳过）。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：选择了第一分支：研究现有压缩原语的触发/范围并完善（C1 标签复用 + C2 触发门）。

**证据**：RESULTS_E26.md §1「采用 C1 与 C2」；§0.1① 显示压缩大量「未促成」而垃圾极少（0.00%–1.37%）；RESULTS_LONG/README 记录「排除迁回」的分支依据来自 E24（迁回 11/12 格为 0、空闲 98%–99.6% 在无溢出的组）。

**覆盖与缺口**：已覆盖：分支选择与依据。未覆盖：其余三分支未展开（按「只选一种」要求，本就不应全展开）。

**关闭标准**：无。

**未能核实**：无。

> **反驳复核**：维持原判（stands）。

### [C-long-23] 第三阶段 L98：只选择一种有依据的维护原语，最多三个变体；优先比较「原规则／相同原语的简单触发／相同原语的状态触发」；不同时展开完整组合网格。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：以 compact 为唯一原语，三变体 C0/C1/C2（C1 标签复用、C2 触发门）。

**证据**：RESULTS_E26.md 顶部变体表（C0/C1/C2）；E26_TAGS.md §4 底部「None of these is formed by multiplying the other two」。

**覆盖与缺口**：已覆盖：一种原语、三变体、非组合网格。未覆盖：C1 属「相同原语的少做工作」而非「简单触发」，与任务书列举的三分法不完全对齐——C2 才是触发门。属可接受的实现选择。

**关闭标准**：无。

**未能核实**：无。

> **反驳复核**：维持原判（stands）。

### [C-long-24] 第三阶段 L100：复用现有 compact，不把已有功能重新实现为新贡献；首版不扩大主块预算，不改变模型、逻辑组号或组几何；若无法在这些条件下获得收益记为当前边界，不擅自启动区域重分配、模型重训或后台并发维护。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：C1/C2 均作用于既有 compact()（index.hpp:1878 want_tags、index.hpp:3206 compact_needs_garbage 门）；无新原语、无预算/模型/组几何改动。

**证据**：RESULTS_FREEZE.md §1「R1 构建、模型、逻辑映射、主块预算、容量规划未动」；RESULTS_E26.md §2 适用边界。

**覆盖与缺口**：已覆盖：复用与边界。未覆盖：无。

**关闭标准**：无。

**未能核实**：无。

> **反驳复核**：维持原判（stands）。

### [C-long-25] 第三阶段 L102：维护准备完成后才提交；准备失败保留有效状态；若单键操作已成功，维护失败不能误报该单键操作失败；临时缓冲、复制、目录更新和峰值空间全部计费；压缩恢复块内可用容量不等于释放主块常驻内存。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：C2 是压缩前的必要字节条件测试，不改变已成功的单键操作结果；空间代价经 check_e26_c2 计量。

**证据**：RESULTS_E26.md §0.3a 空间表（差值 0 到 +7,209 字节，约 ≤0.03%）；§0.3「C2 可以改变记录在主块/溢出的分布，不改变索引持有什么」。

**覆盖与缺口**：已覆盖：复制/目录更新/索引字节计费；准备失败与单键成功语义一致。未覆盖：「峰值空间」没有测量——E26 只给索引字节与溢出字节两个端点值，无峰值记录；「常驻内存不等于块内可用容量」这一句在本阶段未见显式表述。

**关闭标准**：补峰值空间的计费或明确说明为何不计。关闭标准：峰值有数或有不计的显式理由。

**未能核实**：我在 RESULTS_E26.md、E26_TAGS.md、RESULTS_LONG.md 中检索「峰值/peak」无命中；不能排除在 check_e26_c2.cpp 输出里有我未读的峰值字段。

> **反驳复核**：维持原判（stands）。

### [C-long-26] 第三阶段 L104：用活跃记录数约为 N 的长期替换序列，补 50%/100%/200% 累计周期检查点；报告完整负载耗时、维护事件、溢出、目标空间分项与观测暂停；若做单操作尾延迟须用独立采样模式并说明打点影响；样本不足时不声称可靠 P99.9。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：bench_long（干净）+ diag_long；run_long/run_long_same；analyze_long.py → E26_LONG.md；RESULTS_LONG.md。

**证据**：RESULTS_LONG.md §0/§2/§3 与 E26_LONG.md（检查点 25/50/100/200%，活跃恒定；报告累计 ms、溢出条数、索引 MB、压缩次数/重放记录数/复制量/促成放置）。

**覆盖与缺口**：已覆盖：50/100/200% 检查点、完整负载耗时、维护事件、溢出、索引/空间。未覆盖：「目标空间分项」未按分项给出（只有索引 MB 与溢出条数两个总量）；「观测暂停」（尾延迟）在本阶段未测——尾延迟首次出现在 E32（冻结之后），且 E32 用独立采样模式并声明了打点影响与 p99.9 的样本限制。

**关闭标准**：补目标空间分项与观测暂停，或显式记录本阶段不做及其边界。关闭标准：分项与暂停有数，或有不做的显式理由。

**未能核实**：「观测暂停」是否在 RESULTS_LONG/E26_LONG 的某个字段里以其他名字出现，我未逐字段核对 jsonl。

> **反驳改判**：`已完成并采用` → **`部分完成`**（downgraded）
>
> 本条在**本轮内被实际违反过，且由项目自己的记录承认**：RESULTS_FREEZE.md §5/§7 写「本轮真的发生过一次(check_e8_hetero 缺 include 却『通过』)」——源码编译失败而 run 回退到上一次成功的二进制并报 passed，正是 L143 禁止的行为。另外 run_e25.py 至今仍无条件 `os.remove(results/main/e25_time.jsonl)`(本次核到该行仍在)，『不覆盖已经分析的运行』只在单次运行内成立；E26 的 e26_time.jsonl 正是被整体重跑替换(RESULTS_E26.md 的「表比数据旧」更正)。该行 coverage 自认这两点却仍判『已完成并采用』，内部不一致。tools_build.py:372-393 的 run_current/run_recorded 与 rc=2 拒绝确已实现。

### [C-long-27] 第三阶段 L106：判断维护是否回本——按同一序列比较包含维护的累计耗时，不能只在整理完之后测查询；反复无收益或代价超过收益时保留原策略并完成负结果报告。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：C2/C1 的比值即同一序列含维护（压缩、分配、复制）的累计耗时比。

**证据**：RESULTS_E26.md §0.3 耗时表（C2/C1 = 0.049–0.970，方向 5/5）；§1 判据表第一项「包含维护在内的完整负载收益：有」。E29_C3.md 是「代价超过收益」的负面版本（C3/C2 = 1.454–6.346，0/5），完整提交。

**覆盖与缺口**：已覆盖：回本判据按同一序列含维护比较。未覆盖：无。

**关闭标准**：无。

**未能核实**：无。

> **反驳改判**：`已完成并采用` → **`部分完成`**（downgraded）
>
> 串行与「只对改动/风险门加验」有据：run_e25.py docstring「Processes run serially on purpose」；TASK_STATE.md 阶段四「继承门 11/11、E25 写门与 E26 C2 门取值不变」。但「预计开销很大的测试先小规模验证程序和输出」**没有记录**：LOG.md/TASK_STATE.md 里找不到任何小规模预验证；相反 TASK_STATE.md「被排除的运行」第2条记**首轮 3600 条计时 sweep 整套作废**(cycles 被写成常量、检查点百分比按最后一个检查点归一)——若先做小规模输出验证，这种协议级错误不会等到全量 sweep 跑完才暴露。该行 coverage 亦自认「小规模预验证的具体记录在 LOG 中未逐条列出」。

### [C-long-28] 第三阶段 L108 完成条件：得到一个经验证的维护选择，或明确证明当前约束下不值得做；两者都算此阶段完成。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：得到并采用 C1+C2（维护选择），check_e26_c2 逐条验证内容与键序。

**证据**：RESULTS_E26.md §1 采用决定；check_e26_c2 每数据集 851,968–1,703,936 次元素比对一致。构建记录 logs/build_check_e26_c2.json。

**覆盖与缺口**：已覆盖：经验证的维护选择。未覆盖：check_e26_c2 属 DRIFT（构建于 2026-09-19，早于 index.hpp 末次改动），其证据绑定 E26 期源码。

**关闭标准**：无。

**未能核实**：无。

> **反驳复核**：维持原判（stands）。

### [C-long-29] 第四阶段 L112-116：先检查起点定位次数、空组/空子块访问、归并比较和结果复制；只选最直接的一项（空组访问多→非空位图；重复定位明显→一次 lower_bound 加持续游标；中间字符串复制明显→减少物化中间副本）。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：diag_scan 测四项 → 选结果物化一项：index.hpp:3867-3870（scan_reserve_result 分支，预留后原地追加）。

**证据**：E27_SCAN.md §1 四项表与结论表（起点定位 2.01–2.49、空组全 0、子块跳过 1.3–179.6、归并 0.23–0.99、结果分配 1.75–2.00 次/返回记录）；RESULTS_E27.md §0。results/main/e27_scan_diag.jsonl(27)。

**覆盖与缺口**：已覆盖：四项先测后选、只选一项、位图因无目标跳过。未覆盖：无。

**关闭标准**：无。

**未能核实**：无。

> **反驳复核**：维持原判（stands）。

### [C-long-30] 第四阶段 L118：沿用 E23 的状态和扫描语义，并从实际协议读取原扫描长度，不得凭记忆填写；与当前冻结版及有序数组同轮比较，复核点查和更新的附带影响；诊断性无物化访问不能与基线完整物化接口混排。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：三臂 S0/S1/ARR 同进程内配对；状态/长度/起点表取自 E23 协议。

**证据**：E27_SCAN.md 头部（语义、起点表从协议读取）；§3 同轮计时表；RESULTS_E27.md §5「附带影响」；RESULTS_E27.md §5 明示「门通过支持的是功能未回归，不是点查/更新性能未受影响的证据」。

**覆盖与缺口**：已覆盖：语义沿用、协议读取长度、与冻结版及有序数组同轮比较。未覆盖：「复核点查和更新的附带影响」只做到功能层（继承门取值不变），未做同轮计时；报告已如实限定，故不构成误报，但性能转嫁未被测量。

**关闭标准**：补点查/更新的同轮时间对照，或明确记录为「按功能层复核、不做计时」的边界。关闭标准：有时间数或显式边界。

**未能核实**：无。

> **反驳复核**：维持原判（stands）。

### [C-long-31] 第四阶段 L120：E11/E13 已做过子块范围定位，不将打开已有开关写成新机制；一次只改一项，无可识别浪费或收益不明确时保留现状；不为追求全面胜出复制完整有序索引，也不新增整棵 ART 路由。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：本轮只改结果物化；未动 E11/E13 子块范围定位开关（write_locate 保持 false）；ARR 是朴素有序数组，不是复制完整索引。

**证据**：RESULTS_E27.md §0「没有动空组位图，也没有动 E11/E13 已研究过的子块范围定位开关」、§3「改动只影响结果物化」；E27_SCAN.md §3 ARR 定义。

**覆盖与缺口**：已覆盖：一次只改一项、不动既有开关、不复制索引/ART 路由。未覆盖：无。

**关闭标准**：无。

**未能核实**：无。

> **反驳复核**：维持原判（stands）。

### [C-long-32] 第四阶段 L122 完成条件：一项扫描改动有同轮证据，或明确记录为何维持当前实现；不预设必须超过有序数组。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：S1（scan_reserve_result）采用，index.hpp:673 默认 true。

**证据**：E27_SCAN.md §3（S1/S0 = 0.758–0.937，27 格全部同向；S1/ARR = 1.30–2.23，明示仍慢）；check_scan 逐条比对 950 万–1432 万条。构建记录 logs/build_check_scan.json。

**覆盖与缺口**：已覆盖：同轮证据、不预设超过有序数组。未覆盖：check_scan 属 DRIFT（2026-09-19 构建）。

**关闭标准**：无。

**未能核实**：无。

> **反驳复核**：维持原判（stands）。

### [C-long-33] 第五阶段 L126：只组合已经分别有证据支持的组件，复核组合效果；与当前冻结版本对照；涉及对 ART 的最终新结论时必须同轮测量；构建/点查/更新/扫描/常驻空间/维护峰值分别报告，不能跨轮拼出「整体快多少」；没有重测的指标保留其历史轮次标识。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：check_freeze 断言 SHIPPED 默认入口 = CHOSEN（五个开关全开）且内容/结构统计相同；FINAL_RESULTS_TABLE.md 逐行标来源。

**证据**：RESULTS_FREEZE.md §2（三配置 OLD/CHOSEN/SHIPPED，三数据集全通过，851,968–1,703,936 次元素比对）；RESULTS_FREEZE.md §6 同轮 ART 与 ARR；FINAL_RESULTS_TABLE.md 头部「各项来自不同轮次，不是最终版本的一次统一重测」。构建记录 logs/build_check_freeze.json（LIVE）。

**覆盖与缺口**：已覆盖：默认入口断言、组合复核、同轮 ART/ARR、逐行标来源。未覆盖：「维护峰值」未在本阶段报告（峰值空间在 E25–E27 阶段整体缺测）；构建三行是历史值未随冻结重测，文档已如实标注。

**关闭标准**：补维护峰值（或明确标注未测）。关闭标准：峰值有数或被显式标为未测。

**未能核实**：维护峰值是否在其它轮次（E31/E32/E34）文档中给出，我未逐轮核对；本阶段（E25–E27+冻结）内确无。

> **反驳复核**：维持原判（stands）。

### [C-long-34] 第五阶段 L128：更新入口 README、总览和总报告，明确：最终构建仍采用 R1；写路径选了 V0/V1/V2 中哪个；维护/扫描哪些启用、哪些可选、哪些无收益；当前 ART 的具体优缺点及适用键域；结果表由脚本生成，历史实验原始目录不覆盖。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：README.md 更新至 2026-09-21；00_Overview 有项目总览；RESULTS_SUMMARY.md / FINAL_RESULTS_TABLE.md 为总结果表。

**证据**：README.md:3「内容更新：2026-09-18、2026-09-20（冻结，以及冻结之后的 E28–E31）」与 :62 冻结段（五开关、R1 不变、V0/V1/V2 选择）；README.md:66/68/70 记录「逐行标注…不是最终版本的一次统一重测」与「历史值未随冻结重测」；RESULTS_SUMMARY.md:3 与 FINAL_RESULTS_TABLE.md 头部。

**覆盖与缺口**：已覆盖：R1、写路径选择、维护/扫描启停、结果表脚本生成、不覆盖历史目录（README.md:116/130 快照按日期命名不覆盖）。未覆盖：ART 的「适用键域」未出现在 README/RESULTS_SUMMARY 的显著位置，仅在 RESULTS_E25.md §5.6 一行。

**关闭标准**：把 ART 的适用键域与优缺点补入 README/总览显著位置。关闭标准：入口文档含该条。

**未能核实**：00_Overview 目录下的项目总览文件我未逐字读；不能排除该键域说明写在那里。

### [C-long-35] 第五阶段 L130-135：最后汇报优先回答四问——①搜索优化与上下文复用各自带来什么可测收益？②更新后空间和性能能否维持，维护是否回本？③扫描是否改善，有没有转嫁到点查或更新上的代价？④最终版本与当前 ART、有序数组相比各自适用于什么场景？

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：四问分散在 RESULTS_E25.md §0.1、RESULTS_LONG.md §0/§5、RESULTS_E27.md §0/§5、RESULTS_FREEZE.md §6；没有一份「总报告」按四问编号统一作答。

**证据**：① RESULTS_E25.md §0.1①②（V1 只改值快 1.85–3.06×；V2 插入再快 3%–14%）。② RESULTS_LONG.md §0/§3（空间不趋稳、C1 与 C2 空间逐位相同）+ RESULTS_E26.md §0.3（C2/C1 含维护回本）。③ RESULTS_E27.md §0（扫描降 6%–24%）+ §5（仅功能层，明示不是性能证据）。④ RESULTS_FREEZE.md §6（ART/ARR 适用范围）。

**覆盖与缺口**：已覆盖：四问的答案要素均存在且可追溯到原始记录。未覆盖：无统一的「四问」编号回答块；③ 的「转嫁代价」只有功能层、无同轮计时；② 的「性能能否维持」在长期替换下给出的是「空间不趋稳」，性能维持只在时间比值上给出。

**关闭标准**：在总报告里按 1–4 编号集中回答，并补齐③的转嫁代价（同轮点查/更新时间）。关闭标准：交付文档存在编号四问块。

**未能核实**：我在全工作区 grep「优先回答/四个问题/搜索优化与上下文复用/维护是否回本/转嫁到点查/各自适用于什么场景」，命中只在任务书与两份无关文档（E21 LOG、Build_Cost_Analysis），故确认无集中回答块。

### [C-long-36] 第五阶段 L137：不得把 SIMD、位图、压缩本身称为原创，也不承诺全面超越 ART；研究贡献以实际完成的固定模型布局机制和适用边界为准。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：文档一律限定表述；无原创性宣称。

**证据**：RESULTS_FREEZE.md §6「本报告不声称全面超过 ART」；RESULTS_E26.md §0.3a「准确说法是查询侧未观察到可分辨的变化，而不是更快」；RESULTS_E27.md §2「本轮不假定这个差距必须消失」。

**覆盖与缺口**：已覆盖：无原创宣称、无全面超越承诺。未覆盖：无。

**关闭标准**：无。

**未能核实**：无。

### [C-long-37] 检查点 L141：每个阶段更新一个 TASK_STATE.md，仅记录：已完成、当前决策、下一条待执行、正式 run_id、源码/二进制标识、被排除运行及原因；保存后继续，不当作新的审批点。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：TASK_STATE.md 分阶段记录（阶段一至五 + 各阶段已完成块）；被排除运行单列。

**证据**：TASK_STATE.md 全文（阶段表、「阶段二已完成」「当前决策」「下一条待执行」「标识」「被排除的运行」1–5 条）。

**覆盖与缺口**：已覆盖：六项要素与被排除运行。未覆盖：run_id 在 TASK_STATE 中写作 P1–P5 与空 tag，粒度较粗。

**关闭标准**：无。

**未能核实**：无。

### [C-long-38] 运行纪律 L143：使用已有构建与运行闭环；编译失败不得启动旧二进制；JSON/CSV 写出后先解析、检查必需字段与记录数再分析；正式记录使用唯一 run_id，不覆盖已分析的运行。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：tools_build.py run/run_current/run_recorded（run_current 重算源哈希，失败拒绝 rc=2）；冻结期修复了「编译失败回退旧二进制」的洞。

**证据**：RESULTS_FREEZE.md §5（洞与修法：失败时一并删除 build/<target>.built.json 与 .exe）与 §7（改坏源码后 RUN REFUSED rc=2，二进制已删除的逐步骤验证）；tools_build.py:372/388/408-416；RESULTS_E35 记录 run_current 的引入。

**覆盖与缺口**：已覆盖：构建闭环、失败硬停、run_id 唯一性。未覆盖：该洞在本轮真的发生过一次（check_e8_hetero 缺 include 却报 passed，见 RESULTS_FREEZE §5/§7），即在修复前违反了本条；此外 run_e25.py 仍然无条件删除工作文件 e25_time.jsonl（`os.remove(out)`），与「不覆盖已分析的运行」只在单次运行内成立（E38 的驱动才改成只清当前 run_id）。

**关闭标准**：无（洞已修并验证）。可选的加固：把 run_e25.py 的无条件删除改为按 run_id 归档。

**未能核实**：无。

### [C-long-39] 运行纪律 L145：预计开销很大的测试先小规模验证程序和输出，正式协议确定后执行一次完整计划；基准测试进程串行运行；阶段结束核对清单，只对确有改动或具体风险的门增加验证，不反复重跑所有历史轮次。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | medium |

**实现**：正式运行串行（run_e25.py 注释明示）；阶段末只对改动/风险门加验证（E27 附带影响只跑继承门+写门+E26 C2 门）。

**证据**：run_e25.py 注释「Processes run serially on purpose」；TASK_STATE.md 阶段四「附带影响已核实：继承门 11/11 与 E24 逐字节相同；E25 写门与 E26 C2 门取值不变」；RESULTS_E27.md §5。

**覆盖与缺口**：已覆盖：串行、定向加验、不重跑全部。未覆盖：小规模预验证的具体记录在 LOG 中未逐条列出。

**关闭标准**：无。

**未能核实**：「先小规模验证」这一步是否有独立记录，我未在 LOG.md 中定位到明确条目。

### [C-long-40] 停止条件 L147：无源码/输入、正确性故障或平台资源不足时，先尝试已有约定内的可逆修复；仍阻塞则报告具体缺失项和可恢复状态；机器噪声、负收益或某分支不可行不是要求用户再次定方向的理由。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：遇到阻塞时按约定可逆修复并继续（如 V2 upsert 绕过 Index 控制流 → 修法写回控制流；宏重定义 → 修）；ART 无扫描适配 → 报告具体缺失并排除出扫描排名。

**证据**：LOG.md「E25-3 事故」1/2；TASK_STATE.md 被排除运行 1–3；RESULTS_E25.md §5.6 / RESULTS_SUMMARY.md §4「ART 不进排名（无语义匹配的适配器）」。

**覆盖与缺口**：已覆盖：可逆修复与阻塞报告。未覆盖：无。

**关闭标准**：无。

**未能核实**：无。

### [C-long-41] 总完成标准 L149①：E25 测量闭环完成。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：调用矩阵 + 正式同轮对照 + 点查回归 + 默认版本选择全部产出。

**证据**：e25_calls.jsonl(93) → E25_CALLS.md；e25_time.jsonl(3600) → E25_TIME.md；RESULTS_E25.md；TASK_STATE.md 阶段一/二=已完成。

**覆盖与缺口**：已覆盖：闭环五要件（见 §19）。未覆盖：门为 DRIFT 这一身份限制。

**关闭标准**：无。

**未能核实**：无。

### [C-long-42] 总完成标准 L149②：E26 有明确选择或否定结论。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：选择 C1+C2 并冻结；C3（E29）作为否定结论保留。

**证据**：RESULTS_E26.md §1（采用 C1+C2）；RESULTS_E29.md/E29_C3.md（C3/C2 = 1.454–6.346，0/5，不采用）；RESULTS_FREEZE.md §1。

**覆盖与缺口**：已覆盖：明确选择 + 明确否定项。未覆盖：无。

**关闭标准**：无。

**未能核实**：无。

### [C-long-43] 总完成标准 L149③：扫描有一次有依据的评估。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：S1（scan_reserve_result）采用并冻结。

**证据**：E27_SCAN.md §3；RESULTS_E27.md §3；RESULTS_FREEZE.md §1。

**覆盖与缺口**：已覆盖：有依据的评估（先测四项、逐条内容比对、27 格同向）。未覆盖：无。

**关闭标准**：无。

**未能核实**：无。

### [C-long-44] 总完成标准 L149④：最终组合与文档一致。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | high |

**实现**：check_freeze 断言 SHIPPED 默认入口的开关串 == CHOSEN，且内容与结构统计相同。

**证据**：RESULTS_FREEZE.md §2；check_freeze 实测 cfg_shipped = write_fast_locate=1,reuse_locate_ctx=1,compact_reuse_tag=1,compact_needs_garbage=1,scan_reserve_result=1；logs/build_check_freeze.json（LIVE，源哈希与当前磁盘一致）。index.hpp:638/643/659/666/673 五个默认值为 true。

**覆盖与缺口**：已覆盖：默认入口与选定组合经断言相等，且该门是 LIVE（唯一能在当前源码上作证的冻结门之一）。未覆盖：无。

**关闭标准**：无。

**未能核实**：无。

### [C-long-45] 总完成标准 L149⑤：用户能够从原始记录复现主要表格。

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **已完成并采用** |
| **置信度** | medium |

**实现**：每条表格都给出「原始 jsonl → analyze_*.py → 表格 md」的路径，并有 run_*.py 驱动与 tools_build.py 入口。

**证据**：RESULTS_E25.md §6、RESULTS_E26.md §3、RESULTS_E27.md §6、RESULTS_LONG.md §7、RESULTS_FREEZE.md §9 均列出原始数据文件名与脚本；results/main/*.jsonl 存在于磁盘；analyze_*.py 与 run_*.py 存在。

**覆盖与缺口**：已覆盖：从已归档 jsonl 重算表格的路径完整（脚本 + 原始记录均在）。未覆盖：重跑测量需要 E25/E26/E27 期的源码，而 bench_e25/bench_e26/bench_scan 均为 DRIFT（源哈希与当前磁盘不符），因此「复现表格」可从 jsonl 完成，但「复现运行」在漂移后不能再由当前源码得到同一数值。

**关闭标准**：无（若要求复现运行而非复算表格，需保留 E25 期源码快照或声明只支持复算）。

**未能核实**：我未逐个运行 analyze_*.py 验证其确实能由 jsonl 重建 md；判定依据是脚本与原始记录文件名、字段对得上。


---

## D-lit  —— 文献候选目录（工程优化方法检索）（26 条）

### [D-lit-00] §二 方法1（P0）局部重平衡、间隙与容量再分配（ALEX；LINE）：多块组及 Region 维护路径；先固定模型和预算做组内重排，再区分区域内跨组借容量

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：组内子块级重排已实现为候选开关：Config::subblock_reclaim（index.hpp:695，默认 false），Region::repack_subblock（index.hpp:1685）＋ first_candidate（index.hpp:1661，与 put_primary 共用枚举，防止漂移）。区域内跨组借容量（F3）与 ALEX 定长间隙未实现。

**证据**：源码身份：当前盘 index.hpp（2026-09-22 12:51）/switches.hpp（10:57）。门 check_e31（DRIFT，8/10 源哈希不符，见 results/main/g0_build_drift.txt）；san_check_e31（LIVE，14/14）。轮次报告 RESULTS_E31.md；原始数据 results/main/long_e31_same.jsonl、long_e31_grow.jsonl、e31_analysis.txt；入口 RESULTS_SUMMARY.md §3.4。

**覆盖与缺口**：覆盖：same/grow 长期替换、0/25/50/100/200% 检查点、三数据集（mixed/skew/tranco）、子块字节/槽位约束。未覆盖：F3 区域级跨组容量重分配（未实现）；E28/MODEL_AND_MAPPING.md:158 明示「跨组与区域级容量错配未被任何一层覆盖」，组内 ~3% 预算不足未分类。

**关闭标准**：需 A2 级证据后实现区域内容量重分配（MODEL_AND_MAPPING §5 §7.4 标注未做）；当前只有子块级候选且默认关闭。

**未能核实**：未找到任何轮次评估 ALEX 定长间隙对本变长后缀空间的适用性（文档 §二已自陈「不能直接当作变长后缀空间」，但无实测）。

> **反驳改判**：`已完成，负结果或条件性候选` → **`部分完成`**（downgraded）
>
> 实现那一半我逐字核过、成立：index.hpp:705 reclaim_hysteresis、index.hpp:712 reclaim_skip_repeat 都是真 Config 字段且默认 false；唯一读取点是 index.hpp:3226 的 reclaim_gated() 与 3234 的间隔判断，并且只在 config_.subblock_reclaim 为真时被 3262（place_with_ctx）和 3345（insert_slow）调用——开关确实被代码读，不是空声明；kMinReclaimGap=64 在 index.hpp:1324。但证据链撑不起“已完成”：(1) 它引作证据的门 check_e31 根本不覆盖这条分支。我在整个 51 个 .cpp 里 grep，reclaim_hysteresis / reclaim_skip_repeat 这两个标识符只出现在 bench_long_hyst.cpp（第 159/163/170/171 行）；check_e31.cpp 只打开 subblock_reclaim（第 146-147 行），从不打开迟滞开关。所以它的“通过”证明的是 E31，不是迟滞——正是纪律里点名的“门通过≠覆盖了该分支”。(2) 关闭这条结论的实测全部绑在已被取代的源码上。logs/build_bench_long_hyst.json 的 build_id 解出 2026-09-20 11:05:34 UTC，logs/build_diag_hyst.json 11:05:47，logs/build_check_e31.j…

### [D-lit-01] §二 方法2（P0/P1）增量区与周期性合并（SIndex；动态 PGM）：把溢出视为有维护周期的增量区，合并计入复制/分配/查询代价

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：溢出容器作为增量区已存在：Region::OverflowList（index.hpp:1039），合并原语 Region::compact（index.hpp:1870）；无基于周期/时间的触发，触发条件是「放置失败→原地收紧→重试」（E31）。无独立开关。

**证据**：RESULTS_LONG.md（空间不趋稳）、RESULTS_E26.md/E26_LONG.md、RESULTS_E28.md（溢出成因为子块内部字节碎片）。E26/E28 构建均 DRIFT；check_freeze（LIVE）。原始 long_replace_{same,grow}.jsonl、e26_time.jsonl。

**覆盖与缺口**：覆盖：插/替/删、50/100/200%、C1/C2 触发门。未覆盖：把溢出当「有维护周期的增量区」的周期合并（未实现）；复制/分配/查询代价已按 E26/E30 计入。

**关闭标准**：周期/收敛触发未做；E28/E31 的实际选择是惰性回收，需要在文档层面说明这是对「周期性合并」的替代而非实现。

**未能核实**：「周期性合并」是否作为独立条目被正式评估——未见单独轮次报告，只有 compact 的触发门 C2。

> **反驳复核**：维持原判（stands）。

### [D-lit-02] §二 方法3（P0）维护的收益判据与迟滞（ALEX；SALI）：设最小操作间隔、失败后记录布局版本，状态未变不反复尝试

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **已完成，负结果或条件性候选** |
| **置信度** | high |

**实现**：两半分别实现为候选开关：reclaim_hysteresis（index.hpp:705，默认 false＝E31H，间隔＋垃圾未增则跳过）、reclaim_skip_repeat（index.hpp:712，默认 false＝E31P，只看状态）。

**证据**：RESULTS_E32.md §2；门 bench_long_hyst、check_e31（均 DRIFT）；原始 results/main/long_hyst_same.jsonl、long_hyst_diag_{same,grow}.jsonl、e32_analysis.txt；构建 logs/build_bench_long_hyst.json。

**覆盖与缺口**：覆盖：same 模式、25/100/200%、三数据集、两个变体、结构量（压缩次数 1,801→43,026）。未覆盖：grow 模式（RESULTS_E32 §6 明确未补）。

**关闭标准**：RESULTS_E32 §2 已判定「该分支应从清单划掉」——抑制重试改变系统状态本身，关闭标准达成。

**未能核实**：无。

> **反驳复核**：维持原判（stands）。

### [D-lit-03] §二 方法4（P1）分步迁移、限制单次维护工作（LINE 2026）：先同步正确版本，有尾延迟问题再拆步骤

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：有界迁回已实现为候选：Config::overflow_migrate（index.hpp:745，默认 false），Region::drain_all_bounded（index.hpp:2522）、Region::diag_migration_scan（index.hpp:2400）。渐进状态机（旧布局有效/迁移中/新布局有效）未实现。

**证据**：RESULTS_E37.md；bench_migrate/check_migrate 均 DRIFT（index.hpp+switches.hpp changed）；原始 results/main/migr_{same,grow}.jsonl、e37_analysis.txt、e37_feasibility.txt；入口 RESULTS_SUMMARY.md §3.8。

**覆盖与缺口**：覆盖：200% 检查点、same/grow、三数据集、迁回条数/尾延迟/空间。未覆盖：§4.4 渐进模式的可见性规则、每步预算、跨版本扫描（未实现）。

**关闭标准**：渐进维护未做；当前为同步有界迁回，且「kMigrateBudget 限迁回条数不等于维护成本有界」的口径已随数字记录。

**未能核实**：无。

### [D-lit-04] §二 方法5（P1）稳定物理槽位＋逻辑顺序目录（Masstree）：移动小槽号而非大记录

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **已完成，负结果或条件性候选** |
| **置信度** | high |

**实现**：已实现为候选：Config::overflow_stable_store（index.hpp:738，默认 false），Region::OverflowList（index.hpp:1039）以有序 uint32_t 句柄＋稳定 store 承载记录。默认关闭。

**证据**：RESULTS_E34.md；check_overflow_store（LIVE，14/14）；bench_long_o1（DRIFT）；原始 results/main/long_o1_{same,grow}.jsonl、e34_analysis.txt。

**覆盖与缺口**：覆盖：erase 搬移字节 48B→4B（12.0×，六格逐一成立）、点查/扫描/尾延迟/常驻空间、same/grow、200%。未覆盖：进入默认（读路径一致受损 1.02–1.53，故不默认）。

**关闭标准**：非默认；下一步按审阅已转为「降低溢出记录数量」而非扩展该容器读路径。

**未能核实**：无。

### [D-lit-05] §二 方法6（已部分采用，P1 补强）密集控制元数据、SIMD 过滤、冷热分离（Swiss Tables；F14；ART）

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：SIMD 标签过滤已采用：Mode::simd/write_simd（index.hpp:551、650），批式 SSE2 标签过滤（index.hpp:2988）；标签/长度/偏移目录已用（offset_directory，index.hpp:617）。冷热分离未实现。

**证据**：E5（RESULTS_E5.md：SIMD tag 筛选为不稳定收益，留作可开关组件）；E26_TAGS.md（标签复用 C1）；check_freeze（LIVE）、check_tag_store（LIVE）。

**覆盖与缺口**：覆盖：SIMD tag 过滤、标签复用、点查/扫描。未覆盖：标签宽度 8/16 的独立对照（见 §六.1 行）、冷热分离、把哈希表换入（文档自陈须计范围扫描成本）。

**关闭标准**：8/16 位标签独立对照未做（§六.1）；冷热分离未评估。

**未能核实**：冷热分离是否在任何轮次被评估——未见记录。

### [D-lit-06] §二 方法7（P1）跳过已确认前缀的判别片段（B²-tree；Masstree；HOT）：仅在前缀已验证的上下文内跳过，等值仍完整比较

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **条件未满足，已按任务书关闭** |
| **置信度** | high |

**实现**：未实现。诊断在 E35 完成并给出关闭判据（bench_prefix.cpp 一个源两目标）。

**证据**：RESULTS_E35.md；bench_prefix（DRIFT）；原始 results/main/e35_mech.txt；入口 RESULTS_SUMMARY.md §3.6。

**覆盖与缺口**：覆盖：mixed/skew/tranco 的二分比较可判定率（4/8 字节窗口）与线性路径边际价值。未覆盖：实现（依判据停止）。

**关闭标准**：已关闭：mixed/skew 二分可判定率 0.0%；线性在 tag 之上边际 ≤0.41%；两窗口不嵌套不得相加。

**未能核实**：无。

### [D-lit-07] §二 方法8（P1）紧凑有序叶与持续扫描游标（B+ tree；Wormhole；Masstree）：起点只定位一次，接口仍返回有序完整键值

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：起点定位缓存已实现为候选：Config::scan_cache_start（index.hpp:720，默认 false，E27 S2）；结果物化改为预留追加：Config::scan_reserve_result（index.hpp:673，默认 true，E27 S1）。Region::scan 按组号升序遍历（index.hpp:2677）。跨组持续游标/归并状态复用、scan_into/回调、零拷贝视图未实现。

**证据**：RESULTS_E27.md、E27_SCAN.md、RESULTS_E32.md §1；bench_scan/bench_scan_s2（DRIFT）、check_scan（DRIFT）、check_scan_contract（LIVE）；原始 e27_scan_time.jsonl、e27s2_scan_time.jsonl。

**覆盖与缺口**：覆盖：S0/S1/S2/ARR 四臂、三状态×三长度、三数据集、起点定位缓存。未覆盖：游标复用、scan_into/回调、零拷贝（MODEL_AND_MAPPING §5 明示 §7.6「游标复用仍未做」）。

**关闭标准**：游标复用未做；接口仍返回完整物化键值（与文档 §十「不能把不构造结果的一方与完整物化的一方直接排名」一致，尚未出现该比较）。

**未能核实**：无。

### [D-lit-08] §二 方法9（P2）保序字符串编码（HOPE）：可与已有前缀压缩重叠，需测剩余后缀额外压缩率并计编码/解码成本

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **尚未完成** |
| **置信度** | high |

**实现**：未实现。

**证据**：RESULTS_E32.md §6 明示「§2 row 9/12/13/16、§8 HOPE/FSST、§10 B+ tree 基线：仍未动」。无构建、无门、无原始数据。

**覆盖与缺口**：未覆盖任何数据域或检查点。

**关闭标准**：需先测剩余后缀额外压缩率，再计查询编码与扫描解码；模型若仍收原串需明确两表示接口。

**未能核实**：是否有任何轮次尝试 HOPE/FSST——已在 workspace 内检索关键词，除 E32 列为未做外无其它记录。

### [D-lit-09] §二 方法10（P1/P2）批量查找与软件预取（AMAC；CARMI；Masstree）：为一批请求先路由再预取组元数据；预取要有提前量

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **尚未完成** |
| **置信度** | high |

**实现**：未实现批量接口（无 find_batch）。预取代码路径存在但恒关闭：enum class Prefetch{Off,Early,Late}（index.hpp:527）、Mode::prefetch（index.hpp:549）；构造处 index.hpp:2850 与 3322 恒设 Prefetch::Off，故 index.hpp:2924/2933 的 __builtin_prefetch 分支永不执行；无 Config 开关暴露。

**证据**：源码 index.hpp:527/549/2850/2924/2933/3322；未见任何预取计时的轮次报告或原始数据。

**覆盖与缺口**：未覆盖任何批量/预取测量（吞吐、每请求服务时间、P99、批处理等待均无）。

**关闭标准**：需 find_batch 与 1/4/8/16 候选对照、独立选阈值、失效协议；预取须先测缓存未命中。

**未能核实**：Prefetch::Early/Late 是否曾在 E5 前后被测过——E5 只讲 SIMD tag，未找到预取计时数据。

### [D-lit-10] §二 方法11（P2）按负载选择叶表示（ART；LITS/LIT+）：少数困难区域可退回成熟结构，要计切换成本

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：部分：模型后端可切为 LITS-HPT 适配器（model/lits_hpt_adapter.hpp；ModelBackend，index.hpp:613，默认 Context16），并按区域回退到 Context16（E38 P3）。但「learned/sorted 叶表示」的按区域切换未实现。

**证据**：RESULTS_E38.md、RESULTS_E38_P3.md；bench_lits_adapter（LIVE，15/15）、check_lits_model/index/fingerprint（LIVE，14/14/14/14）；原始 results/main/lits_adapter_p3a/b_{same,grow}.jsonl、e38_p3_analysis.txt。

**覆盖与缺口**：覆盖：mixed/skew/tranco、构建/静态点查/常驻空间；区域回退率 18.2%/17.0%/7.9%（正确性门口径）。未覆盖：叶表示（learned vs sorted）切换、「困难区域达阈转换＋迟滞」。

**关闭标准**：叶表示切换未做；当前回退是区域→Context16（模型层），不是叶表示层。

**未能核实**：无。

### [D-lit-11] §二 方法12（P2）块式有序增量容器（B-skiplist 2025；B+ tree）：很大的溢出区用多有序小块

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **尚未完成** |
| **置信度** | high |

**实现**：未实现（OverflowList 仍为单条有序向量，index.hpp:1039）。

**证据**：RESULTS_E32.md §6「仍未动」。

**覆盖与缺口**：未覆盖。

**关闭标准**：大溢出区块式组织未做。

**未能核实**：无。

### [D-lit-12] §二 方法13（P1/P2）内存池、稳定句柄与压缩偏移（F14；TPHT 2026）：先证实分配/元数据是大头

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **部分完成** |
| **置信度** | medium |

**实现**：部分：O1 的稳定句柄＋空闲链表（Region::OverflowList，index.hpp:1039）实现了「稳定句柄」思想；内存池/arena 未实现。

**证据**：RESULTS_E34.md；check_overflow_store（LIVE）。

**覆盖与缺口**：覆盖：句柄稳定化与搬移字节。未覆盖：分配成本是否为大头的隔离测量、内存池、分配器保留与 RSS 的区分。

**关闭标准**：「先证实分配/元数据是大头」这一步未单独完成；无内存池。

**未能核实**：E32 §4 测的是溢出向量搬移而非分配器成本，故「分配是否为大头」未被直接回答。

### [D-lit-13] §二 方法14（P2）更宽 SIMD 与小候选块（LiBox 2026）：须保留标量/现有 SIMD 路径

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **尚未完成** |
| **置信度** | high |

**实现**：未实现（仅 SSE2，index.hpp:25/486/2960；无 AVX2/AVX-512 平台构建）。

**证据**：RESULTS_E32.md §6「§6.3 宽 SIMD 仍未动」。

**覆盖与缺口**：未覆盖。

**关闭标准**：需独立平台构建，保留 x86-64-v2 基线，记录 CPU 支持/频率/工作集。

**未能核实**：无。

### [D-lit-14] §二 方法15（P1 末端工程项，特别标注）PGO、LTO、热路径拆分（GCC 官方文档）：独立训练负载、相同指令集、基线同等优化

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **尚未完成** |
| **置信度** | high |

**实现**：未实现。补丁脚本 item4_pgo_lto.py 已写好但未应用未测（RESULTS_E32.md §0 第 5 行、§6；RESULTS_E34.md:319「后置不变：PGO/LTO」）。

**证据**：RESULTS_E32.md §0/§6；RESULTS_E34.md:319。无构建、无门、无原始数据。

**覆盖与缺口**：未覆盖（未应用补丁，未产生任何 PGO/LTO 计时或二进制哈希）。

**关闭标准**：应用补丁→独立训练负载→同指令集→基线同等优化→记录参数与二进制哈希。

**未能核实**：item4_pgo_lto.py 的存在与内容——它在「会话工作目录」，不在只读工作区内，未能核实。

### [D-lit-15] §二 方法16（暂缓）按操作类型选择同步机制（OPAL 2026；ART-OLC）：当前串行瓶颈不是锁竞争，并发不属于本轮职责

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **条件未满足，已按任务书关闭** |
| **置信度** | high |

**实现**：未实现（单线程串行，无并发）。

**证据**：RESULTS_SUMMARY.md §6 跨轮边界「并发扫描、游标复用、反向扫描、并发更新均未测」。

**覆盖与缺口**：未覆盖并发。

**关闭标准**：依任务书「并发不属于你本轮职责」关闭。

**未能核实**：无。

### [D-lit-16] §四 动态容量再利用（4.1 借鉴/保持；4.2 F0–F3 快照诊断；4.3 最小在线方案 repack_group＋阈值迟滞；4.4 渐进维护）

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：F0/F1/F2 只读诊断已做（E28：diag_transfer、diag_overflow）；F3 未实现（MODEL_AND_MAPPING.md:158）。在线方案：组内子块级重排＝subblock_reclaim（index.hpp:695，默认 false）；阈值迟滞＝reclaim_hysteresis/reclaim_skip_repeat（index.hpp:705/712）负结果；渐进维护未实现。

**证据**：RESULTS_E28.md/E29/E30/E31/E32；MODEL_AND_MAPPING.md §5；源码 index.hpp:695/705/712/1685。E28-E31 构建均 DRIFT；check_e31（DRIFT）、san_check_e31（LIVE）。原始 diag_transfer、diag_overflow、ov_cost_diag.jsonl、long_c3_*.jsonl。

**覆盖与缺口**：覆盖：F0-F2、same/grow、0/25/50/100/200%、三数据集、卡字节 vs 卡槽位（208,689:18,527）。未覆盖：F3 区域级容量再分配、「空闲在无溢出组（98%–99.6%）」的转移、渐进维护。

**关闭标准**：F3/区域级重分配（A2 级证据未就绪）、渐进维护均未做；在线 repack 只到子块级，未做文档所称 repack_group(g) 的整组归并重放。

**未能核实**：「跨组与区域级容量错配」是否为主要成因——E28 只测到 F2 的必要条件（97%+ 满足），余 ~3% 未分类，文档本身也未据此下结论。

### [D-lit-17] §五 溢出区三种互斥原型 O1/O2/O3（先确认溢出向量移动/构造/分配占主要时间，再选型）

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **已完成，负结果或条件性候选** |
| **置信度** | high |

**实现**：前置诊断已做（E32 §4，diag_ov_cost）；O1 已实现为候选：overflow_stable_store（index.hpp:738）＋ overflow_inline_tag（index.hpp:742，E36 T8）。O2（小增量缓冲）、O3（块式有序容器）未实现。

**证据**：RESULTS_E32.md §4、RESULTS_E34.md、RESULTS_E36.md；check_overflow_store、check_tag_store（均 LIVE）；bench_long_o1/bench_tag（DRIFT）。原始 ov_cost_diag.jsonl、long_o1_*.jsonl、e36_scan_tag.jsonl。

**覆盖与缺口**：覆盖：O1（含 T8）在 same/grow、200% 的搬移字节/点查/扫描/空间/尾延迟；前置诊断（溢出占维护 12.2%/37.3%/37.6%，代价集中在 erase 搬移）。未覆盖：O2/O3。

**关闭标准**：O1/T8 均非默认；按审阅「转向降低溢出记录数量」，不再扩展该容器读路径。

**未能核实**：无。

### [D-lit-18] §六 6.1 标签 8 位与 16 位的独立对照：需同时报同条目容量与严格等预算两种实验

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **尚未完成** |
| **置信度** | high |

**实现**：未实现（无 tag width 配置项；标签位宽未参数化）。E36 的 T8 是内联标签位置，不是 8/16 位对照。

**证据**：RESULTS_E32.md §6「§6.1 标签 8/16 位…仍未动」；RESULTS_E36.md（T8/O8 是标签存放位置）。

**覆盖与缺口**：未覆盖。

**关闭标准**：需两种口径（同条目容量＋严格等预算）的独立对照。

**未能核实**：无。

### [D-lit-19] §六 6.2 用公共前缀之后的字节（判别片段）：在已验证前缀的局部目录缓存后缀前 4/8 字节大端整数

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **条件未满足，已按任务书关闭** |
| **置信度** | high |

**实现**：未实现；E35 诊断判定不实现（bench_prefix.cpp 一源两目标）。

**证据**：RESULTS_E35.md；bench_prefix（DRIFT）；原始 results/main/e35_mech.txt；RESULTS_SUMMARY.md §3.6。

**覆盖与缺口**：覆盖：二分可判定率（mixed/skew 0.0%，tranco 4B 13.5%–25.2%/8B 57.0%–74.8%，两窗口不嵌套）；线性路径在 tag 之上的边际。未覆盖：实现。

**关闭标准**：已关闭（与 §二 方法7 同源）。

**未能核实**：无。

### [D-lit-20] §六 6.3 宽 SIMD 的边界：单独设平台构建、保留 x86-64-v2 基线、尾部不越界、记录 CPU 支持

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **尚未完成** |
| **置信度** | high |

**实现**：未实现（无 AVX2/AVX-512 路径，见 §二 方法14）。

**证据**：RESULTS_E32.md §6「§6.3 宽 SIMD 仍未动」。

**覆盖与缺口**：未覆盖。

**关闭标准**：平台构建与边界记录未做。

**未能核实**：无。

### [D-lit-21] §七 7.1 扫描优先试持续游标：start_region=locate(start) 提到循环外，后续按 Region 顺序走；作为独立开关验证

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：部分：起点定位缓存 scan_cache_start（index.hpp:720，默认 false，E27 S2）已做；跨组持续游标、组内主块/溢出归并游标、scan_into/回调未实现（MODEL_AND_MAPPING §5 明示 §7.6 游标复用仍未做）。

**证据**：RESULTS_E27.md、RESULTS_E32.md §1；check_scan（DRIFT）、san_check_freeze（LIVE）；原始 e27s2_scan_time.jsonl。

**覆盖与缺口**：覆盖：S2÷S1 0.92–1.02（百万域名 0.932，5/5）；起点定位 2.01–2.49 次/扫描测了未改。未覆盖：游标复用、scan_into、零拷贝。

**关闭标准**：游标复用未做。

**未能核实**：无。

### [D-lit-22] §七 7.2 预取要有提前量：find_batch 先路由算组号再预取组元数据；批量 1/4/8/16 对照，阈值独立选定

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **尚未完成** |
| **置信度** | high |

**实现**：未实现（无 find_batch；Prefetch 路径恒 Off，见 §二 方法10）。

**证据**：index.hpp:527/549/2850/2924/2933/3322；无预取计时数据。

**覆盖与缺口**：未覆盖。

**关闭标准**：需批量接口与 1/4/8/16 对照、独立选阈值、失效协议；第一版只做固定索引上的只读批量接口。

**未能核实**：无。

### [D-lit-23] §八 保序压缩、混合叶与编译优化的取舍（HOPE/FSST；LITS/ART 自适应结构；PGO/LTO 放算法固定后）

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：HOPE/FSST 未实现；混合叶部分（E38 区域→Context16 回退，见 §二 方法11）；PGO/LTO 未实现（见 §二 方法15）。FSST 作为保序键比较的迁移方案在该轮未被尝试。

**证据**：RESULTS_E32.md §6、RESULTS_E38_P3.md §5.1、RESULTS_E34.md:319。

**覆盖与缺口**：覆盖：区域回退（模型层）。未覆盖：HOPE/FSST 编码、困难区域「达阈转换＋迟滞」、PGO/LTO。

**关闭标准**：HOPE/FSST、PGO/LTO 未做；混合叶只到区域回退，未到按负载选叶表示。

**未能核实**：无。

### [D-lit-24] §九 建议安排的六项后续实验（含最小对照与继续条件），以及每轮纪律与新增两个可控子集（真保持逻辑组/跨组漂移）

| | |
|---|---|
| **性质** | 有进入条件的候选 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：逐行：行1 F0–F3 快照＝E28（部分，F3 未实现）；行2 在线重排/区域重分配＝E31/E37（部分）；行3 大溢出区组织＝E34/E36（O1 已做，O2/O3 未做）；行4 扫描持续游标＝E32 S2（部分）；行5 判别元数据＝E35（诊断后关闭）；行6 硬件与编译补强＝未做。两个可控子集未实现（E28 只统计换组率 0.016%–0.046%）。

**证据**：RESULTS_E28/E31/E32/E34/E35/E36.md；RESULTS_SUMMARY.md §3.4–3.8。

**覆盖与缺口**：覆盖：行1–5；每轮纪律（同进程配对、轮换臂序、5 独立进程、方向计数、检查点不进计时）已在用（E32 §0、E30 §9.5）。未覆盖：行6（预取/batch/PGO/SIMD）、两个新子集、跨组漂移判定。

**关闭标准**：行6 与两个可控子集未做；§九 明确「不追加到已关闭的长程任务」，故尚未进入的项不等于违约。

**未能核实**：「真保持逻辑组/跨组漂移」两可控子集是否曾被生成——未见其原始数据或轮次报告。

### [D-lit-25] §十 基线与创新应如何表述（特别标注）：外部对照保留 ART；对动态有序维护应增加一个支持相同字符串/值所有权/扫描语义的成熟 B+ tree 候选；LITS/Wormhole/Masstree 先验收接口与正确性再排名

| | |
|---|---|
| **性质** | 必须完成 |
| **状态** | **部分完成** |
| **置信度** | high |

**实现**：ART 基线已贯穿（libart 适配，E20/E21/E25–E38）；B+ tree 外部基线未实现（workspace 内无任何 B+ tree 候选）。LITS 接口语义差异已在 E38 记录（LIT+ find() 要求起点存在≠任意 lower_bound；append() 不判重≠判重插入）。

**证据**：RESULTS_SUMMARY.md §1–3；RESULTS_E32.md §6「§10 B+ tree 基线仍未动」；RESULTS_E38.md（语义差异与域限定：字节≥128 与嵌入 NUL 一律拒绝）。

**覆盖与缺口**：覆盖：ART 同轮对照（构建/点查/更新/扫描/维护）；有序数组作为静态基线（E21/E23/E27）。未覆盖：B+ tree 动态有序维护对照、其借位/分裂/合并参照。

**关闭标准**：需增加支持相同字符串/值所有权/扫描语义的成熟 B+ tree 候选，并先验收接口与正确性再排名。

**未能核实**：是否有任何 B+ tree 实现被接口验收或计时——未见记录。


---

## 3. 横向发现（跨章节）

### 3.1 证据身份

- **计时全 DRIFT**：E25–E37 的所有 `bench_*` 构建记录都对不上当前源码。任何"当前版本更快／更慢"的句子，若引用这些数字，都必须写明它绑的是哪一期的源码。
- **早期门全 DRIFT**，但其中 8 个继承门与 6 个开关门在本次 G1 补测中已重建复跑（`RESULTS_G1_GATES.md`），且结果与历史日志逐字段一致。
- **唯一的 LIVE 冻结类门是 `check_freeze`**（断言默认入口的开关串等于选定组合，且内容与结构统计相同）。因此"最终组合与文档一致"这条**站得住**。
- **8 个 `san_*` 目标 LIVE**，覆盖 E31 回收、E9 多块、E11 定位、冻结门与 LITS 三个门。

### 3.2 已实现但未进入默认的机制（不是"没做"）

| 机制 | 开关 | 默认 | 结论 |
|---|---|---|---|
| E31 子块内原地回收 | `subblock_reclaim` | false | 空间大降；`grow` 混合上一致慢 4%–8% |
| E29 放宽压缩触发 | `compact_repack_budget` | false | 负结果（1.45×–6.02× 更慢） |
| E31H 迟滞 ／ E31P 状态抑制 | `reclaim_hysteresis`／`reclaim_skip_repeat` | false | 负结果，明确不再盲调 |
| E27 S2 扫描起点缓存 | `scan_cache_start` | false | 小收益，仅百万域名方向稳定 |
| O1 稳定记录存储 | `overflow_stable_store` | false | 搬移 12×，但读路径受损 1.02–1.53 |
| T8 内联标签 | `overflow_inline_tag` | false | 场景候选，二分负载上仍有读代价 |
| E37 有界迁回 | `overflow_migrate` | false | 溢出 −46%–−91%、点查 0.571–0.714；但归因未分离、尾延迟六格全变差 |

### 3.3 原任务要求成立、但确实没有闭环的必做项

按优先级：

1. **I10 准备—验证—提交与可控失败注入**：无失败注入设施，「维护失败不丢记录／不半更新」没有断言。置信度 low。
2. **§4.4 独立区间账本**：`garbage_bytes` 是减法派生值，没有"按偏移排序记录区间 + 间隙合并"的独立计算，也没有声明它是派生值。
3. **I11 值更新前后状态断言**：现有门只比内容（`check_lits_index`）或比两配置（`check_freeze`），**没有**同一个索引在纯值更新前后比较偏移表／组几何／空间计数。
4. **I12 的 `n = 0`**：三个扫描门的 count 集都是 {1,2,16}，`n=0` 从未被测，只有代码可读。
5. **扫描改动对点查／更新的同轮代价**：E27 与 E32 的扫描对照都只报扫描；"复核点查与更新的附带影响"只做到功能层。
6. **E24／E26 阶段的空间分项与长期检查点**：`0/5/10/25%` 四个检查点未逐点采集空间分项；"维护峰值"在 E25–E27 阶段整体缺测。
7. **第三阶段第③问**：任务书要求"清除垃圾后能成功放入的记录数与所需复制字节"，报告以"不适用"替代。
8. **§9.5 的 A/A 同配置双实例控制**：未见专门运行，"未能区分"的判据因此缺一个基准。
9. **分析脚本的身份校验**（§12.2 的拒绝清单）：17 个 `analyze_*.py` 中 0 个读取构建身份；缺一臂时静默返回空表。
10. **运行记录缺身份字段**：`results/main/*.jsonl` 没有 `run_id`、配置全文、数据哈希、种子、时间单位、校验和（身份只在构建记录里）。

### 3.4 尚未进入的候选（进入条件成立与否须先证明）

- **A2 相邻子块重分配**（§7.2／A2 层）：入口证据"F1 失败而 A2 成功"不成立——F1 在 80%–95% 的压力事件上已成功，故按条件未满足关闭；但**A2 本身从未构造过**。
- **A4 区域内容量再分配**（§7.4／F3）：F2 判定 97%+ 预算够用，剩下的 ~3% 未分类；**跨组容量错配未被任何一层覆盖**。
- **A5 残余分类**（§5 L230 四情形）：未实现。
- **本块空洞复用**（§7.3）：**连入口诊断（洞尺寸匹配率）都没有做**。
- **分支 C 缩减溢出保留容量**：未实现，也没有"活跃数下降后触发"的路径。
- **O2 小增量缓冲 / O3 块式有序容器**：未实现。
- **持续扫描游标**：未实现（E32 的起点缓存与 E27 的结果构造都不等价于它）。
- **PGO/LTO**：**补丁脚本 `item4_pgo_lto.py` 在工作区与临时目录里都不存在了**（E32 说"已写好"，那份文件已经不在了）。要从头写。
- **B+ tree 外部基线**（§十）：未实现。
- **HOPE/FSST 保序编码、8/16 位标签对照、宽 SIMD、`find_batch` 与预取**：未实现（预取代码路径存在但恒为 `Prefetch::Off`，不可达）。
- **ART 使用实现的路径深度／节点类型／分配与更新行为**：只有 README 的静态描述，无实测。

### 3.5 本次核查期间修掉的一处洞

重建 14 个门时，`logs/build_<target>.json` 被就地覆盖，`RESULTS_FREEZE.md §7` 引用的 `check_e8_hetero` 的 `build_id` 与 exe 哈希因此只剩报告里的文字。已给 `tools_build.py` 加 `archive_stale_record()`：写新记录前把将被替换的那条按 `build_id` 归档到 `logs/archive/`，幂等、不删任何东西。已用重建 `check` 验证（旧记录归档、新旧 exe 哈希相同）。

---

## 4. 给 G1／G2 的输入

**G1（补完欠账）按这个顺序做**，与总计划 §2.4 的优先级一致：

| 序 | 动作 | 依据 |
|---|---|---|
| 1 | ✅ 已做：14 个门的重建复跑 | `RESULTS_G1_GATES.md` |
| 2 | ✅ 已做：构建记录归档守卫 | `tools_build.py` |
| 3 | 把 `MODEL_AND_MAPPING.md`／`LITS_AUDIT.md` 的 file:line 重绑到当前 `index.hpp` | §3.2，本条命中 12 行 |
| 4 | 补 I10 的失败注入门与 I11 的前后状态断言 | §3.3 第 1、3 项 |
| 5 | 补 `n = 0` 与"缺席新键组号一致"两个用例 | §3.3 第 4 项、A-3 I1 |
| 6 | 分析脚本加身份校验（至少：缺臂、操作数不一致、旧可执行身份三项硬停） | §3.3 第 9 项 |
| 7 | 写 `INVARIANTS.md`（I1–I12 与对应门）与 `CODE_AUDIT.md` | A-10-13 |

**G2（定稿实验计划）必须包含**：

- 当前结构 S0 的显式配置快照（含 `offset_directory`／`write_simd`／`write_locate` 这三个**既不在白名单也不在相等断言里**的 bool）；
- 一次**当前源码**上的基线重测——E38 改了默认查询路径与每区域 136 字节的常驻开销，这是所有历史比值失效的原因；
- 首选架构包按压力分层选择；Engineering 包以 PGO/LTO 为首（它是唯一被反复点名的旧欠账，且补丁已不存在）。

**不得把本文的"部分完成"读成"没做"**：它多数时候的意思是"做了、测了、但证据绑在已被取代的源码上，或交付形态缺一件"。同样，**"已完成并采用"的 80 条也不是全部绑在当前源码上**——它们只是通过了独立反驳的核查，其中性能类结论仍需按 §0.2 的身份限定引用。

