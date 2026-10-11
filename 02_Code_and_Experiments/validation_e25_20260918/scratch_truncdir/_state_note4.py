import io
p = "STRUCTURE_OPTIMIZATION_STATE.md"
t = io.open(p, encoding="utf-8", newline="").read()

anchor = "- **下一步三选一**（一轮只做一项）："
assert t.count(anchor) == 1, t.count(anchor)

new_bullet = (
"- **方向①的截断形态已收口（2026-10-11）**：预登记 [TRUNCDIR_ENTRY_DESIGN.md]"
"(../02_Code_and_Experiments/validation_e25_20260918/TRUNCDIR_ENTRY_DESIGN.md) → 实现\n"
"  `ModelBackend::EqualDepthDirTrunc`（`model/equal_depth_dir.hpp` 的 `fit(..., truncate=true)`，\n"
"  默认关闭）＋门 `check_depth_dir_trunc`（三语料 131,072 各 1,731 项检查 0 失败；sanitizer 三格\n"
"  771/723/739 全绿；逐键路由 differ=0；区间不变量 0 违例；两类负对照各 3 次命中）＋继承四门\n"
"  131,072 口径复核 rc=0。结果：[RESULTS_DEPTH_DIR_TRUNC_20261011.md]"
"(RESULTS_DEPTH_DIR_TRUNC_20261011.md)。\n"
"  目录 arena mixed/skew/tranco 2,121,557→**225,478**（−89.4%）／1,799,334→**332,242**（−81.5%）／\n"
"  264,219→**113,421**（−57.1%）。机制对照 DDir vs DDirT：静态 **0.856（5/5）／0.894（5/5）／\n"
"  1.006（未分辨）**——截断在 mixed/skew 有效，tranco 中性（其 arena 本来就小）。\n"
"  对 C16 主对照：mixed 静态 **1.037（1/5，未分辨）**、tranco 静态 **1.240（0/5，可分辨回归）**\n"
"  → **主目标未过，保持默认关闭**；skew 可分辨收益（静态 0.830、更新后 0.697、维护 0.409，均 5/5），\n"
"  维护三语料全快（0.491/0.409/0.708）。模型成分增量降到 +0.5%–0.7%（未截断版本 mixed +5.6%），\n"
"  常驻预算通过；全索引同装置小 22%–35%（含布局效应，不当纯空间收益宣传）。\n"
"  身份：index.hpp `61592294…`、switches.hpp `9f8a55cc…`、model/equal_depth_dir.hpp `6755f3ca…`、\n"
"  门 `8f39222b…`、bench `49a8722a…`＋runner `30300f49…`，四个适配包 manifest 与\n"
"  `run_owner_lifecycle.py` 已重钉。\n"
)

old_tail = ("- **下一步三选一**（一轮只做一项）：①结构侧**等深阈值目录**（用区域内键的 63-bit 坐标直接构造\n"
"  G 个组阈值，按构造既保序又均衡，模型无关，≈2 KB/区域）；②把两族反例交回模型负责人后再接一次；\n"
"  ③按计划回到 M4 有序段归并。\n")
assert t.count(old_tail) == 1, t.count(old_tail)

new_tail = ("- **下一步三选一（2026-10-11 更新，一轮只做一项）**：①**tranco 入口诊断**——arena 已缩到\n"
"  113 KB，回归仍在，先量“一次等深二分 vs 模型预测＋子块目录”的步骤/分支成本，再决定是否动代码；\n"
"  ②**M4 有序段归并**（模型无关，按 [计划](../docs/NEXT_ACTION_PLAN_20261011_BLOCK_MODEL.md) §7.4\n"
"  在第一轮接入收口后恢复）；③**参考表释放**——把 M3 的“非 Context16 后端释放参考表”从 LITS 扩到\n"
"  目录后端，纯空间取舍，另立一轮。\n")

t = t.replace(old_tail, new_bullet + new_tail)
io.open(p, "w", encoding="utf-8", newline="").write(t)
print("state doc updated")
