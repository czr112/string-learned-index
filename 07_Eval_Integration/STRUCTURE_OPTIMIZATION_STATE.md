# 索引结构优化：当前动作与证据

更新：2026-10-11（**16:20 重建版**）。用户明确主任务为索引结构优化。
**主线：最小诊断 → 单个结构候选 → 正确性 → 同状态 A/B → 集成与外部复测。**
正式协议扩建、外部扫描适配和报告措辞清理不再作为结构优化的前置工程。
本文件替代 `PLAN_REVIEW_20261009.md` 的当前优先顺序；保留其中与具体实验有关的身份和正确性约束。

> **重建说明（2026-10-11）**：本文件在 16:06 的一次编辑事故（工具写入空集合）中被清空为 0 字节，
> 原始文本没有另存副本；快照只保留哈希（`MANIFEST_workspace_20261011m` = 8edac68e1810c705f6c95089d777bffafe915d1eb2859813035cffa1675694c1）。
> 本版保留原文档结构与全部"当前决策"段落，早期轮次（E/G/I10/P2/A2/A3 等）压缩为指针台账；
> 一切数字与判定**以各轮 RESULTS 文件为准**，不从本文件二次引用。

## 指令更新（2026-10-11，最高优先）

**下一阶段计划（P0–P2 已于同日执行，结论见下方「BlockLinearRank 收口」节）**：以
[模型级接入计划](../docs/NEXT_ACTION_PLAN_20261011_BLOCK_MODEL.md) 为当前执行顺序：
接口契约 → 单一 BlockLinearRank 后端 → 正确性与结构入口 → 有限配对 A/B → 通过后集成。
上一轮 matrix4 是完整索引对照，不是 RegionModel 替换；matrix4s 只增加各臂内部重复到15，
未实现臂间轮转配对，不能作为采用级排名。首轮不移植残差标签/delta，不做 LITS 定向优化。
M4 顺延至该接入轮收口；下文旧“下一步待定”及 M3/M4 顺序保留为历史记录，以此计划为准。

1. **停止一切针对 LITS 模型适配的优化**：LITS 只是当前临时使用的模型，后续会被替换；围绕它的适配优化在替换后不再有收益。
   已完成的 LITS 查询域守卫属于内存安全修复（保留），但不新开任何 LITS 定向优化项；M3 也仅以"默认关闭的空间候选"存在，
   不为其做新的适配工作。
2. **主线切换：接入其他成员设计的新模型并做对照**。模型由用户随后提供；到位后按既有纪律执行：
   接口契约与身份冻结 → 正确性门（含 ASan/UBSan）→ 同状态对照（同一数据、同一查询、同一结构条件，
   与现有后端对照）→ 结论与采用建议。接入前不预设其接口形状，先读交付源码与说明。
3. M4 归并入口（P1）顺延：它不是 LITS 定向工作，保留在册；排在新模型对照就绪之后执行。
## BlockLinearRank 收口（2026-10-11，P0–P2 已执行）：正确但入口回归

结果：[RESULTS_BLOCK_RANK_ENTRY_20261011.md](RESULTS_BLOCK_RANK_ENTRY_20261011.md)；独立审查：
[REVIEW_BLOCK_MODEL_CONTRACT_20261011.md](REVIEW_BLOCK_MODEL_CONTRACT_20261011.md)。

- **做了什么**：新增 `model/block_linear_rank.hpp`（队友 @ 60837a88 的 9-bit 编码 + 分段线性拟合，
  固定 block_size=1 / 7 字节 / 8 段，无参数搜索；只存前缀长度不存视图），在 `index.hpp` 接成第三个
  `ModelBackend`（默认仍是 `Context16`），并修掉 `switches.hpp` 里对三值枚举仍按二值比较的缺陷
  （该缺陷会让「Context16 vs BlockLinearRank」被判为无差异，正是差异表要防的空洞通过）。
- **正确性**：`encode` 12/12 与上游逐位一致；`fit` 分段表逐字段一致（唯一有意差异是 n=1 边界）；
  新门 `check_block_rank` 三语料 131072 全绿（324/315/325 项检查，0 失败），
  三个 sanitizer 单元全绿（204/199/201 项，ASan/UBSan 无诊断）。
- **入口读数**（入口自身的查询模式，两后端逐区域配对）：主块访问中位比值 mixed 2.84、skew 3.79、
  tranco 2.05；变差区域 62/64、64/64、64/64；skew 完整键比较 205,280→438,279。
- **机制**（独立审查的两族反例）：①坐标饱和——区域前缀之后第 8 个字节不进模型，C3 反例
  2000 个不同键塌成 1 组；②分箱不均匀——C6 反例 32/64 组空置、最大组为平均的 8.5 倍。
- **处置**：按计划 §6「映射造成明显集中 → 先报告瓶颈，不启动大矩阵」，**不开 P3 A/B**；后端保持
  默认关闭。身份已同步：index.hpp `f553630d`…、switches.hpp `f09dc3f6…`、
  model/block_linear_rank.hpp `17d8714d…`、门 `b5cf16fc`…、四个适配包 manifest、`diagnostics/run_owner_lifecycle.py`。
  并发写入事故：一个审查代理中途改写了 index.hpp / check_block_rank.cpp / tools_build.py；前者（`#ifdef E25_DIAG` 拟合计时，从未执行）已回退并使 index.hpp 由 `46fdf520…` 变为 `f553630d…`，门的草稿保留在 `scratch_blockaudit/agent_draft_check_block_model_backend.cpp`，恢复后全部格子逐位复现（见 RESULTS §8）。
- **方向①的入口诊断已完成（2026-10-11）**：[RESULTS_THRESHOLD_ENTRY_20261011.md](RESULTS_THRESHOLD_ENTRY_20261011.md)。
  等深阈值目录的**分隔符形式**第一次真正消除组负载集中：最大组负载 1366/824/546 → 12/12/12、空组
  14278/19178/5993 → 0/0/0，压缩分隔符常驻 0.6–5.0 KB/区域（被替换的 Context16 参考表是
  16,512 B/区域）；查找 7.5–8.3 步/查询、比较 21–127 B/查询（按 memcmp 首个不同字节计费）。
  **63-bit 坐标形式不成立**（skew 64/64 区域阈值重复，最大组负载 1431 > Context16 的 824）。
  子块数没有改善（+13%/+35%/+1%），所以收益的机制是「把模型预测 + 组内目录两次搜索合成一次等深二分」，
  不是「更少的子块」——这一条写进了结果文档的边界段。下一轮预登记为「最小差分实现」：
  第四个 ModelBackend（默认关闭）+ 正确性门 + 同状态入口诊断，未通过不进 A/B。
- **方向①已执行完实现轮（2026-10-11）**：[RESULTS_DEPTH_DIR_20261011.md](RESULTS_DEPTH_DIR_20261011.md)。
  第四个后端 `ModelBackend::EqualDepthDir`（`model/equal_depth_dir.hpp`，默认关闭）＋ 门
  `check_depth_dir`（三语料 131,626 项检查 0 失败，含独立重算、精确等深模式 12/13、重复键与域套件、
  旋转负对照）＋ sanitizer 三格全绿 ＋ A/B `bench_depth_dir`（5 进程 × 4 重复、重复内配对、20 对/语料、
  0 完整性问题）。**判定分裂**：skew 净收益（静态 0.903、更新后 0.779，5/5），mixed 静态回归
  （1.142，0/5，噪声底 1.128），tranco 未分辨（1.257，噪声底 1.321）；构建与维护三语料全快
  （0.87–0.95 / 0.43–0.71，5/5）。常驻 mixed +2.21 MB（≈+5.6%，超 ≤5% 预算）。按计划 §6 采用规则
  （主目标 mixed ≤0.90）**不设默认**，保留为默认关闭候选＋场景结论（skew 可用）。
  机制面：主块访问降到 ≈1.00 次/查询、溢出归零；机制假设（未验）为未截断 arena 的缓存不友好，
  下一轮若继续，预登记只做「截断分隔符」且必须先解决截断下的正确比较语义。
  身份：index.hpp `8688225e…`、switches.hpp `12a497a2…`、model/equal_depth_dir.hpp `07495f52…`，
  四个适配包 manifest 与 `diagnostics/run_owner_lifecycle.py` 已重钉；既有五个门/基准重建全绿。
- **下一步三选一**（一轮只做一项）：①结构侧**等深阈值目录**（用区域内键的 63-bit 坐标直接构造
  G 个组阈值，按构造既保序又均衡，模型无关，≈2 KB/区域）；②把两族反例交回模型负责人后再接一次；
  ③按计划回到 M4 有序段归并。

## 新模型接入对照（2026-10-11，第一轮已完成）：BlockLearn × 我方索引

结果：[RESULTS_BLOCKLEARN_MATRIX4_20261011.md](RESULTS_BLOCKLEARN_MATRIX4_20261011.md)。队友仓库
`Zephyr761/Learned-Index-Model` @ `60837a88…` 的四个消融臂 ＋ 我方两臂 ＋ ART/HOT，在统一入口（`403de52`，未改动）
与三份语料上跑了两轮：`matrix4`（入口默认协议，3 语料 × 8 臂）与 `matrix4s`（mixed，15 重复确认轮）。
接入验收：其 Debug+ASan `ctest` 4/4；矩阵两轮八臂全部 `passed`；身份（commit + 逐文件哈希）见
`results/blocklearn_identity_20261011.json`。

关键读数（mixed 15 重复，中位点查 ms）：ART 3.88 < ours-c16 5.12 < ours-lits 5.67 < HOT 6.19 <
block-residual-delta 6.97 < block-residual 7.36 < block-hash 7.59 < block-plain 9.67。
消融梯子里 E1→E2（哈希标签）大胜；**E3（残差感知标签）未稳定超过 E2**（tranco 明显更差）；
E4 在 skew 与 mixed 加强轮里是他们四臂最优。构建成本是他们 0.58–1.05 s vs 我方 49–76 ms；
"索引＋保留输入"口径内存约为我方的 38%–67%。**本轮证据不支持"已超过我方/ART/HOT"**，
其核心残差标签点需要更多语料与混合负载复核（这与其 README 自述一致）。

下一步待定：① 把上述反馈给队友；② 若要"模型级替换"（把 `block_model.*` + `residual_tags.*` 接进我们
`Index` 的 RegionModel 位，做新 `ModelBackend`，同轮对照 Context16/LitsHpt），作为独立一轮执行；
③ 需要更硬的表就对 skew/tranco 补 15 重复、并另立混合读写轮。
## 当前决策（H、Q、PFX2、GD、M2 已收口；M3 已集成；P0 补验完成，2026-10-11）

**P0 生命周期补验已执行；ASan 抓到并修复一处真实缺陷（2026-10-11）**：
结果 [RESULTS_OWNER_LIFECYCLE_20261011.md](RESULTS_OWNER_LIFECYCLE_20261011.md)。新门对 OFF/ON/独立参考跑同一更新串
（6,501 插入 / 952 擦除 / 618 覆盖更新、8 个全键值检查点；mixed/skew 各 1,062,425 次 find、tranco 2,111,001 次），
真实分裂（splits 增量 2/2，左右子区各有落点）、前缀重建（3 例各 +1）、LITS/fallback 所有权（63/3、65/1、129/1，违规 0）、
`group_of()` 与**有效 preset** 拒绝（OFF 对照接受）、域拒绝（规定消息、OFF/ON 对称）全部正触发；
六格计数与 ASan/UBSan 双构建全绿，两套记录逐字节一致。期间 ASan 在 LITS 查询路径抓到 `predict_group`
的堆越界读（域外查询键字节 ≥0x80；构建侧拒绝从未覆盖查询路径），已在 `model/lits_hpt_adapter.hpp`
补同域守卫：97195dd7… → **51c52d68…**（`index.hpp` b20c83ca…、`switches.hpp` 72856bdc… 未动）。
M3 的空间/时间数字仍绑定旧适配器，采用（M5）前必须在新身份上重验。
下一步按 [M4 详细计划](../docs/NEXT_ACTION_PLAN_20261011_MERGE.md)：M4 主块有序段归并入口在新身份上执行 →
条件通过才做单个原型与完整 scan256 A/B → 集成 → M5 采用（含 M3 新身份重验）及双外部基线。
M3 默认继续 OFF，M4 双方 M3 也固定 OFF；既有负结果不重开。

**Q 紧凑子块边界导航已完成入口、静态原型和六格同状态 A/B，两个后端均不集成。**
结果：[RESULTS_SUBBLOCK_ROUTING_20261011.md](RESULTS_SUBBLOCK_ROUTING_20261011.md)。
在 C 开启、W/S/H 关闭、普通 bulk_load 和原 matrix2 查询下，tranco 摘要回退率为
5.05%/6.06%，新增空间约 0.6%；但完整点查 Q/顺序 = 1.015/1.073（2/5、1/5），
没有超过本轮散布的净收益。mixed/skew 的 LITS 格相对顺序路径偏快，相对已有普通目录
没有稳定增量，不能将收益归给新摘要。六格 sanitizer 全绿，30 个计时进程全部通过。
Q 只在隔离副本中，生产 index.hpp 未动；不试宽度或阈值挽救。

**H 口径更正**：取回先前独立复核后确认，原 A/B 只有整段 seg_ns，未实现计划要求的
最后 8,192 周期主窗口。−8.7%/−18.2% 属于 mixed 的 100%→200% 整段，原"主指标通过"撤回。
新严格只读审核对四份各 400 行的配对/身份/验值全通过，风险/早期整段回归仍违反必要条件，
故 H 继续关闭，无需为已失败的必要条件重跑。见 RESULTS_HOLE_REUSE_20261011.md §6 和
`../04_Consolidation_and_Verification/AUDIT_H_PROTOCOL_20261011.json`。

**PFX2 入口已执行，负结果收口**：结果
[RESULTS_PREFIX2_ENTRY_20261011.md](RESULTS_PREFIX2_ENTRY_20261011.md)。六格入口（C 开、W/S/H 关、
matrix2 原 rows/queries）跑满；目标格 Context16/tranco 三项定量条件全部不成立——覆盖 16.02%（要求 ≥60%）、
可免模型步 9.95%（要求 ≥50%）、常驻记账 12.02%（要求 ≤5%）；逐项差分全过（全部语料键与查询重放
found/组号/处理长度/CRC/tag 一致，空后缀与单字节后缀规则逐区域复核）。按计划 §3 不建原型、不做 A/B、
不试宽度或阈值挽救；`index.hpp` 未动，适配 manifest 无需同步。步数分布一并落盘：
两字节内早停占 mixed 2.98%/skew 0%/tranco 16.02%，即 PFX2 类（≤2 字节前缀表）的覆盖上界。

**GD 已收口：入口成立、原型 A/B 主目标失败、候选关闭（2026-10-11）**：结果
[入口](RESULTS_GEOMETRY_ENTRY_20261011.md) 与 [原型](RESULTS_GEOMETRY_PROTOTYPE_20261011.md)。
入口六格全过（字段/地址零不符、四臂答案与 14 项逐查询计数全零差异、常驻 1.20%–1.51%、
1 条整数除法 → 0 条）。原型：ASan/UBSan 六格门全绿，并在门里抓到「插入触发重建 → 新 Region 无描述符」
这一真实缺陷；固定 A/B 主目标 Context16/tranco 的 G0/B0 中位 1.014、2/5 更快、控制底 1.161，
三条子条件全部失败 → 按计划关闭（不集成、不加默认开关、不试宽度或增加重复）。
关闭的是「三张几何表 → 一张 24 B 连续描述符」这一机制，不证明主块访问无法通过其他组织方式减少成本。

**M2 连续标签面板入口不成立，候选在入口关闭（2026-10-11）**：结果
[RESULTS_CONTIG_ENTRY_20261011.md](RESULTS_CONTIG_ENTRY_20261011.md)。六格计数入口差分全等；目标格
Context16/mixed 的「>4 头部查询占比 ≥60%」不成立（31.3%，重尾而非普遍），减少率 43.8%、常驻 2.95%、
差分三条均成立。按计划「不满足即停」：不建原型、不做 A/B；生产 `index.hpp` 未动。

## 更早一轮：低活跃回收、分支 C-2、E3（三轮入口已收口）

低活跃子块回收（`reclaim_low_activity`）的**入口诊断轮已收口：负结果，不进入计时轮**。
结果见 `../02_Code_and_Experiments/validation_e25_20260918/RESULTS_LOW_ACTIVITY_ENTRY.md`。
一句话：分支够得着的机会只把溢出插入改写了 0.03%–0.84%（最好的格子是 mixed/E31 的 −346 次，占该段 131,072 次插入的 0.26%），
而**真正大的那批拒绝——85% 的字节拒绝落在空块上——根本不在这个入口的候选范围里**：`first_candidate()` 不选空块。
分支 C-2（缩到历史峰值）与 E3（写路径分配）的入口轮亦已收口，判定与适用范围见
`RESULTS_C2_PEAK_ENTRY.md`、`RESULTS_E3_ENTRY.md` 与 `RESULTS_BRANCHC_ENTRY.md`。
## 结构诊断的结论（历史快照，2026-10-09 轮）

| 当前普通构建 | Context16 溢出记录 | LITS 溢出记录 | 每查询主块访问：Context16 / LITS |
|---|---:|---:|---:|
| mixed | 2,547 | 3,144 | 12.04 / 16.24 |
| skew | 5,033 | 4,653 | 18.11 / 27.62 |
| tranco | 0 | 0 | 2.91 / 2.53 |

每格是当时查询队列 10,000 次的计数。mixed/skew 的 1,000 个未命中查询中，分别有 298/341、697/632 次进入溢出。
因此"静态溢出少，所以差距全部发生在主块路径"不能跨语料一概成立。

子块目录把主块访问降到每次约一个，但新增边界比较抵消了部分收益：tranco 两后端的净耗时分别为基线的 1.156/1.094。
**不采用统一开启目录作为本轮优化。**
预知区域编号的诊断对照在 tranco 上耗时为 0.595/0.517。
它是无法直接部署的诊断对照；不把这个比值称为真实结构改进，也不把差额解释为精确分段耗时。
这项证据支持试一次外层区域导航布局。

## 已实现的最小结构候选：紧凑 fence 摘要导航（C，默认关闭；集成与六格重测已完成）

代码（原型）：`diagnostics/compact_fence_router.h` 与 `diagnostics/lookup_routing.cpp`；集成：`index.hpp` 的
`Config::compact_region_routing`（四张开关表全登记，默认 OFF）。

候选将区域 fence 的 8 字节保序摘要放进连续热数组，把完整 fence 留作冷访问。
摘要前可跳过所有 fence 的共同前缀；摘要相同始终回退到完整 unsigned-byte 比较。
每次查询真实执行二分导航，区域划分、模型、组几何、主块、记录和值全部不变。
边界条件覆盖短键、空键、内嵌 NUL、高位字节、超过 8 字节的公共前缀和摘要碰撞。

**查询耗时 新/旧（越小越好）**：5 独立进程 × 4 平衡重复；同一个索引状态；每臂重放原查询序列 100 遍。

| 语料 | Context16：中位比值 / 更快进程 | LITS：中位比值 / 更快进程 |
|---|---|---|
| mixed | 0.923 / 5/5 | 0.993 / 3/5 |
| skew | 1.023 / 1/5 | 1.003 / 2/5 |
| tranco | 0.713 / 5/5 | 0.732 / 5/5 |

tranco 两格跨进程范围为 0.693–0.771 与 0.639–0.813，观察到约 29%/27% 的耗时下降。
mixed/Context16 方向一致；其余三格无一致收益，skew/Context16 有回归风险。
**结论：保留为静态短键导航候选，不进入统一默认。** 集成轮的六格同状态 A/B 复核见
`../02_Code_and_Experiments/validation_e25_20260918/RESULTS_COMPACT_ROUTING.md`（tranco 通过、其余未分辨，
C 保持默认关闭、不做宽度搜索）。

每个进程逐条验证完整语料键与原路由一致、查询答案与 interchange oracle 一致，并验证各臂计时 checksum 相等。
内存与二进制键域检查已完成：独立 unsigned-byte 参考对照 569,063 次通过，六份语料/后端路由与查询检查通过；
ASan/UBSan 运行均 rc=0，逐次核查 stderr 为空。记录见 `results/compact_routing_20261009/sanitizer.json`。
这不是与 ART/HOT 的新排名，也没有验证一次全新随机查询流、尾延迟或动态更新代价。

## 早期轮次台账（E25–E38 / G 系列 / I10 / P2-A4 / A2 / A3 / LONG / FREEZE）

这些轮次的结论**只在其 RESULTS 文件中**（本重建版不复述，避免二手转述误差）：

- E25、E26、E27、E28、E29、E30、E31、E32、E33、E34、E35、E36、E37、E38（+E38_P3）：`02_Code_and_Experiments/validation_e25_20260918/RESULTS_E*.md`；
- 冻结与长期替换：`RESULTS_FREEZE.md`、`RESULTS_LONG.md`；
- 门/契约/组合轮：`RESULTS_G1_GATES.md`、`RESULTS_G1_CONTRACT.md`、`RESULTS_G3_WIDEST.md`、`RESULTS_G4_PGO.md`、`RESULTS_G5_BTREE.md`、`RESULTS_G5_X2BACKEND.md`、`RESULTS_I10_COMMIT.md`；
- 诊断轮：`RESULTS_P2_SCAN_NAV.md`、`RESULTS_A2_ENTRY.md`、`RESULTS_A3_ENTRY.md`、`RESULTS_HOLES_ENTRY.md`；
- 2026-10 各轮（W/C/S、扫描起点、残余写、拆分裂、候选 H、组合、几何、连续标签、前缀2、低活跃、C-2、E3、LITS/owner、生命周期）：
  见 `RESULTS_*.md` 全表与 `docs/NEXT_ACTION_PLAN_*.md` 系列。

## 可复现入口

在 WSL 的 `07_Eval_Integration` 目录运行：

```bash
python3 -B diagnostics/run_lookup_structure.py
python3 -B diagnostics/run_lookup_routing.py
python3 -B diagnostics/validate_compact_routing.py
python3 -B diagnostics/run_owner_lifecycle.py     # P0：M3 生命周期补门（2026-10-11）
```

前两个脚本只接受尚不存在的对应结果目录，复测需显式使用新的输出名，不能覆盖已有证据。
本轮未修改原索引 `index.hpp`、现有适配器默认值或远程评测仓库。
