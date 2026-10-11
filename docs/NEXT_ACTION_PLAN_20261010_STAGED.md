# 下一轮行动计划：阶段 D（写路径残余成本入口诊断）；候选 S 已收口

日期：2026-10-10（第四版）。**候选 S 已完成并收口**：实现、门（手检门扩展段、新门
`check_sround` 12 格、两个 sanitizer 版、默认关闭时既有组合门回归）、定向+扩展 A/B、
未决格逐调用判定全部完成，结果见
`../02_Code_and_Experiments/validation_e25_20260918/RESULTS_SROUND_20261010.md`。
S 保持**默认关闭**（走行长的负载上 scan16/scan256 收益 −22%～−26%，5/5 方向；其余列不退化）；
默认翻转另作决定。**当前首选 = 阶段 D（本文第 3 节）。**

（历史注记：S 之所以被选为首选，依据是独立核验件 `RESULTS_SCAN_ENTRY_VERIFY_20261010.md`
与主结果 `RESULTS_SCAN_ENTRY_20261010.md` 第 1/4 节——中位起点只走 1 步，极端起点一次调用
线性走 136–492 条记录，墙钟上两臂都贵；S 是两臂共同的扫描优化，不是 W 回归的修复。）

## 1. 候选 S 的定义（已执行完成；保留供引用）

**缩短 `Region::scan` 的起点推进**：把"从组头逐条比较推进到 start"改为"用已排序的先验结构
定位到最近位置"，两侧分开评估、先做证据最充分的一侧：

- 主块侧：子块之间与子块内都是 rank-ordered（`check_scan_contract` 已验证该前提），
  可用"先按子块序二分到子块、再在子块内二分/线性"的两级搜索；**必须**实测比较次数
  （计数器已在 `adv_p_cmps/adv_p_skipped/adv_p_subblocks`，本候选复用）；
- 溢出侧：容器已有有序表示与既有查找（`lower_bound_pos` 一族），只在该侧被选为对象时复用；
- 首版**不新增持久目录、不缓存额外键、不改模型映射、不改区域划分、不改主块/回收预算、
  不改输出 API**；用默认关闭的开关隔离，在 Config、基线、冻结配置、开关摘要与差异白名单
  五处完整登记（`switches.hpp` 四张表 + `require_only_switches_differ` 差集断言）。

接受判据（先定向、后扩面）：
1. 正确性：扩展 `check_scan_contract`（排序负控制）与 `check_scan_entry`（手算核验）；
   边界集：空键/NUL/高位字节/4096 长键/共享前缀、空子块位于组中间、repack 后 offset 变化、
   删除重插、区域分裂与重建、起点在首键前/恰命中/两键间/末键后、limit=0、空索引；
2. 计数：目标格子（下节）的 `adv_*` 显著下降；`after_last` 探针从"整组线性"降到对数级；
3. 墙钟：`(B+S)/B` 与 `(W+S)/W` 在同状态、同队列、位置轮转的 5 进程 × 4 重复下可分辨；
   目标格子 = lits/same/mixed 的 frac≥1.0（极端起点集中）与 ctx16/grow/tranco 200%；
4. 不得以任何其它指标的退化换扫描收益；若不可分辨或回归 → 关闭候选，不扩大参数搜索。

## 2. 阶段 S 的执行顺序（已完成）

1. 干净实现 + 开关登记 → 官方门（`check_scan_contract`、`check_scan_entry`、组合门 four-arm
   差异断言、sanitizer 两个）全跑；
2. 定向 A/B：B、W、S、W+S 四臂，lits/same/mixed（131,072，same）与 ctx16/grow/tranco
   （262,144，grow），检查点 0/25/50/100/200%，报告 scan16/256/4096 + 维护/点查/空间四列；
3. 可分辨收益后扩到两后端 × same/grow × 三语料 × 五检查点；
4. 收口：结果文档、状态文档、4 份 manifest + 适配器版本串同步、新快照。

## 3. 阶段 D（当前首选）

以保留的 W（导航关闭）为被诊断配置，在**更新后**状态上指认写路径里"仍有可省工作的一个环节"
（残余主块检索/放置、落溢出、放置重试、回收与重建）。网格先 same/mixed（131k）与
grow/tranco（262k）× 25%/200%，1 进程 × 4 重复计数 + 最小计时抽查；进入候选实现的判据：
计数指认一个具体环节且存在有界改动面。入口不成立则记负结果、转候选清单下一项。

本轮详细执行口径以 [NEXT_ACTION_PLAN_20261010_D.md](NEXT_ACTION_PLAN_20261010_D.md) 为准：
显式固定两后端；区间增量与检查点末尾 8,192 周期窗口分开；计数、阶段计时、无插桩 A/B
分开；旧 E3 脚本不直接运行；入口成立后只实现一个候选。S 未决格已经判定，不再作为前置补跑。

## 4. 前置补跑清单（全部已完成，2026-10-10；S 轮新增门/探针亦已完成）

- 组合门 same+grow 12 格 rc=0：`logs/combo_gate_20261010_se3_{same,grow}.log`；
- sanitizer 组合门两后端 rc=0：`logs/san_combo_20261010_se2.log`；
- `check_scan_entry` 41/41 通过；`san_check_scan_entry` 曾因漏登记 `SAN_TARGETS` 而实为空跑，
  修正后重跑通过；
- `bench_firstscan` 两格 × 2 进程 × 2 重复：`results/firstscan_20261010/`；
- 快照 `MANIFEST_workspace_20261010d.sha256`（3,018 项，逐项重算 0 失配）。

## 5. 起始身份

- `index.hpp` 6,124 行 / SHA-256 `2bbcc23b4e02aed96eec16a1e482d07a503a34042c86e3ce70157d0d193efcc6`
- `switches.hpp` `1e0bccc0800dcd975e841fe6ad4f38fe2e370f1db5efeb5ddc8455a41040efe3`
- 装置身份随每轮构建记录（`logs/build_*.json`），引用时以记录为准。
