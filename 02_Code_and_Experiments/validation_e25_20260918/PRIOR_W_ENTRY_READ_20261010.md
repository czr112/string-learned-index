# 阶段 D 入口先验：既有 W 配置计数把候选指向回收/扩宽路径（2026-10-10）

**性质**：这是对**已有** W 配置计数行（组合轮 `diag_combo`，WSL，2026-10-10）的只读再读，
**不是** 阶段 D 入口诊断本身，**不满足** [NEXT_ACTION_PLAN_20261010_D.md](../../docs/NEXT_ACTION_PLAN_20261010_D.md)
第 5 节的入口判据。它只回答一个更小的问题：**新入口应该优先盯哪一节**。

## 1. 口径（先写清，否则数会读错）

- 来源：`results/main/combo_diag_{ctx16,lits}_{same_mixed,grow_tranco}.jsonl`，`arm="W"` 行。
- `subblock_reclaims` / `reclaim_widened` 是 `E25_DIAG` 计数器，**每个 arm-run 只 reset 一次**
  （`bench_combo.cpp:979`，在 `bulk_load_with_plan` 之前），所以 frac=0.25 那一行**已经含有建索引
  阶段的回收**，而 frac=2.0 行是"载入 + 0→25% + 25%→200%"的累计。
  下表因此给**增量**：`incr = 值(2.0) − 值(0.25)`，分母为 `N × 1.75` 个替换周期
  （mixed 131,072 → 229,376；tranco 262,144 → 458,752）。中位数取 4 次重复。
- 这些行只有 `overflow` / `compactions` / `subblock_reclaims` / `reclaim_widened` 四个计数；
  **`subblock_reclaim_helped`、`reclaim_widened_helped`、拒绝原因、重试次数都不在其中**——
  这正是新入口要补的，也是本先验不能独立成立为入口证据的原因。

## 2. 读到的数

W 臂，25% → 200% 增量：

| 后端 | 模式 / 语料 | 回收 pass 数 | pass / 替换周期 | 其中扩宽 (widened) | widened / pass | 同期仍落溢出 |
|---|---|---:|---:|---:|---:|---:|
| Context16 | same / mixed | 120,759 | 0.527 | 2,005 | 1.7% | 358 |
| Context16 | grow / tranco | 219,792 | 0.479 | 3,929 | 1.8% | 34,629 |
| LitsHpt | same / mixed | 128,569 | 0.561 | 2,386 | 1.9% | 1,623 |
| LitsHpt | grow / tranco | 140,097 | 0.305 | 2,517 | 1.8% | 21,104 |

对照（同轮 B 臂，开关关闭，故 pass 恒为 0）：同期落 Overflow 的替换周期占比 B 为
0.51–0.98（same/mixed）与 0.37–0.74（grow/tranco）；W 把它压到 0.16–0.7%（same/mixed）
与 4.6–7.5%（grow/tranco）。**W 确实把记录搬回了主块**——"W 没生效"不是这些数支持的读法。

## 3. 先验指向什么

两点合起来才是候选：

1. **回收路径在主块侧被高频进入**：same/mixed 上大约每 1.8–3.3 个替换周期就有一次 in-place
   sub-block pass；grow/tranco 上每 3.3–4.2 个周期一次。这不是边角分支，是主路径上的一等公民。
2. **第二阶段（widened）几乎不触发**：只有 1.7–1.9% 的 pass 会走到扩宽，即"第一次 pass 没让它放下"
   的比例很小；那么**真正的未知量是第一阶段 pass 的"帮到了没有"**：
   - 若 `subblock_reclaim_helped / subblock_reclaims` 高（pass 通常就是需要的），回收是有效工作，
     残余成本应到别处找；
   - 若该比值低，则存在**有界候选**：用拒绝原因（`place_fail_slots` vs `place_fail_bytes`）+ 已定位
     的位置上下文，在**跑 pass 之前**判断这次 pass 是否可能让重试成功，不该跑就不跑；
     这正落在计划 §5 的第二条候选（"放置失败后重复遍历：只有拒绝条件或位置上下文可安全复用…"）。
   注意 grow/tranco 上**单靠回收并没有把溢出压到很低**（仍有 21k–35k 条落溢出，占周期 4.6–7.5%），
   所以这条线在 tranco 上更值得先看：那里的 pass/isn't-helping 比例最可能被读出来。

**不用这个先验做决定**：它只把新入口的第一批读数顺序定下来（先看 `helped/pass` 与拒绝原因，
再看重建/分裂与溢出搬移），判定仍按计划走 8 格 + 阈值。
