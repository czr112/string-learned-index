import io
def patch(path, old, new):
    s = io.open(path, encoding="utf-8", newline="").read()
    assert s.count(old) == 1, (path, "anchor not unique")
    io.open(path, "w", encoding="utf-8", newline="\n").write(s.replace(old, new))

# state doc: record P1 closure after the P0 sentence
patch(r"07_Eval_Integration\STRUCTURE_OPTIMIZATION_STATE.md",
      "下一步为 P1 读路径入口诊断（不接计时）。",
      "**P1 已执行收口（2026-10-11 深夜）**：`diag_depth_dir_read_path` 三语料计数（断言与对账 0 违例）"
      "表明额外工作来自目录搜索自身（每查询 ~8 步、+17.8~+101.1 逻辑字节），指纹总量两臂相等、外层导航相同，"
      "不存在单一可部署改动；判定为收口目录读路径、恢复 M4。见 "
      "[RESULTS_DEPTH_DIR_READ_PATH_ENTRY_20261011.md](RESULTS_DEPTH_DIR_READ_PATH_ENTRY_20261011.md)。"
      "下一步：M4 有序段归并入口（计划 §6）。")

# 07 README: add the new results entry
patch(r"07_Eval_Integration\README.md",
      "- [下一阶段详细计划](../docs/NEXT_ACTION_PLAN_20261011_POST_TRUNCDIR.md)：目录字节序修复、有限读路径诊断及 M4；\n  前置发现见 [最小反例核查](AUDIT_DEPTH_DIR_ORDER_20261011.md)。\n",
      "- [下一阶段详细计划](../docs/NEXT_ACTION_PLAN_20261011_POST_TRUNCDIR.md)：目录字节序修复、有限读路径诊断及 M4；\n  前置发现见 [最小反例核查](AUDIT_DEPTH_DIR_ORDER_20261011.md)。\n"
      "- [读路径入口诊断（P1）](RESULTS_DEPTH_DIR_READ_PATH_ENTRY_20261011.md)：目录额外工作来源；目录读路径收口、转 M4。\n")

# root README: add pointer under the plan bullet
patch(r"README.md",
      "- **下一阶段行动计划**：[字节序修复、读路径诊断与 M4](docs/NEXT_ACTION_PLAN_20261011_POST_TRUNCDIR.md)；\n  目录候选的前置缺陷见 [字节序核查](07_Eval_Integration/AUDIT_DEPTH_DIR_ORDER_20261011.md)。\n",
      "- **下一阶段行动计划**：[字节序修复、读路径诊断与 M4](docs/NEXT_ACTION_PLAN_20261011_POST_TRUNCDIR.md)；\n  目录候选的前置缺陷见 [字节序核查](07_Eval_Integration/AUDIT_DEPTH_DIR_ORDER_20261011.md)。\n"
      "- **读路径入口诊断（P1）**：[目录额外工作来源与收口判定](07_Eval_Integration/RESULTS_DEPTH_DIR_READ_PATH_ENTRY_20261011.md)。\n")
print("docs updated")
