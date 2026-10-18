# 短目录完整读门（阶段1单元3，登记）

2026-10-18，起点`ff225d7`工作树。依据[最新长期计划](../docs/LONG_TERM_BM_PLAN_20261018.md)§5.3：
给短目录建立**完整读门**的独立候选，替换页内诊断口径。

## 1. 候选与差分

`bm_sdindex::Index`（新，只读）：自持有按序页、自持有路由向量（`upper_bound`），每页
`bm_sdpage::Page`＝BM叶页 + 上游`blocklearn::ShortDirectory`；`find` 先走63位码有序路由，
码相等才回读真实fence，括号不成立回退完整BM路径，进入同一个完整键校验。

- **不含**入口轮的计数副本与本地码数组（上一版只用于诊断）。
- 新增常驻：`short_directory`（8B/块）与稳定的`fence_views`（每块一个`string_view`，按capacity计）**都计入**候选容量。
- 与上一轮页内消融的区别：这次是**完整find**，含真实路由层，并同时对比
  `bm-page-window`、`bm-page-base`、原生`-shortdir-first`、`F5`、`ART`。

## 2. 输入

真实三类键（english_words / memetracker_urls / memetracker_phrases，各65,536键）为**主判据**；
tranco/64k为机制阳性对照。每十条查询替换为已证明缺席的后缀键（~90%命中）。

## 3. 判据（登记）

- 全域门：空键、NUL、高位字节、超长键、前缀/后继/缺口缺席、全部行、全部查询、采样与全库scan，
  与`std::map`参照逐条相同，零失配；普通与两格ASan/UBSan一致。
- 完整读门：5进程×6次Williams行×8臂，同repeat同置换，八臂校验和相等；比值口径同前。
- 采用判据：三类真实键的`shortdir/ART`**均≤1.10**，且相对被替换的`bm-page-window`无清晰回归；
  tranco只作阳性对照，不得用它单独满足目标。
- 元数据预算：新增`short_directory+fence_views`占候选总量≤5%（长期计划元数据门），超出即记录并停止该形态。
- 不做动态操作、不改默认、不扩cost/adaptive；达不到门则明确收窄为场景并保留原结构门失败记录。

## 4. 结果摘要（执行后填写）

全域门四格各134,164项、失配0；完整find的shortdir/ART=1.362/1.032/0.975/1.573，shortdir/BM页=0.832–0.887（四格清晰更快），shortdir/原生sdf=0.725–0.883。**三类真实键≤1.10门未过**（words 1.362），**元数据5%门也未过**（7.87%/7.07%）；机制有效但不足以补上模型侧差距。详见[执行结果](../RESULTS_BM_SHORTDIR_READ_20261018.md)。
