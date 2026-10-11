import io, json, os

root = r"D:\StringLearnedIndex_Workspace"
note_add = (" P1/M4 diagnostic notes (2026-10-11): index.hpp gained, inside #ifdef E25_DIAG only, "
            "(a) the read-path entry counters for diag_depth_dir_read_path (primary/overflow split compare "
            "sites E25_CMP_P/O, Index::locate boundary counting, fingerprint fused byte counting) and (b) the "
            "M4 ordered-segment merge shadow MergeShadowCounts + Region/Index::diag_scan_merge_shadow. "
            "Shipped builds are unchanged; the affected gates were rebuilt and re-run green with the current "
            "sources (check_scan_contract pass; order gates 132/0 and 773/0), see "
            "RESULTS_DEPTH_DIR_READ_PATH_ENTRY_20261011.md and RESULTS_MERGE_ENTRY_20261011.md. index.hpp is now "
            "c8a1ae3c7373714842f724887a6f45eea7ce5ab7b6e9cdbd677ab87fed0e7265, diag_merge_entry.cpp "
            "a71be7c59f4aee5026639bf06730c0bce6dd762793ea5b364b3f24e37edba172, tools_build.py "
            "d86e95a1e0b0abb24b521c0406a1b3df550308e2b5a1509be6c6bc883eee0360.")

for name in ("our_index", "our_index_lits", "our_index_routing", "our_index_lits_routing"):
    p = os.path.join(root, "07_Eval_Integration", "adapters", name, "benchmark-adapter.json")
    s = io.open(p, encoding="utf-8", newline="").read()
    anchor = "RESULTS_DEPTH_DIR_ORDER_FIX_20261011.md."
    i = s.rfind(anchor)
    assert i > 0, name
    j = i + len(anchor)
    s2 = s[:j] + note_add + s[j:]
    io.open(p, "w", encoding="utf-8", newline="\n").write(s2)
    print(name, "note appended,", len(s), "->", len(s2))

# state doc: record the M4 entry closure after the P1 sentence
p = os.path.join(root, "07_Eval_Integration", "STRUCTURE_OPTIMIZATION_STATE.md")
s = io.open(p, encoding="utf-8", newline="").read()
a = "下一步：M4 有序段归并入口（计划 §6）。"
b = ("下一步：M4 有序段归并入口（计划 §6）。**M4 入口已执行收口（2026-10-11 深夜）**："
     "`diag_merge_entry` 在冻结状态（C/W/S、grow、tranco、N=262,144、scan256×17 起点）上测得"
     "成功段输出占比 0%（门槛 ≥50%）、比较净减少 −3.31%（门槛 ≥30%），两个目标检查点均不通过；"
     "按计划 §4 关闭该机制（不建原型、不新增开关），转入 M5 配置收口。见 "
     "[RESULTS_MERGE_ENTRY_20261011.md](RESULTS_MERGE_ENTRY_20261011.md)。")
assert s.count(a) == 1, "state anchor"
io.open(p, "w", encoding="utf-8", newline="\n").write(s.replace(a, b))
print("state doc updated")

# 07 README pointer
p = os.path.join(root, "07_Eval_Integration", "README.md")
s = io.open(p, encoding="utf-8", newline="").read()
a = "- [读路径入口诊断（P1）](RESULTS_DEPTH_DIR_READ_PATH_ENTRY_20261011.md)：目录额外工作来源；目录读路径收口、转 M4。\n"
b = a + "- [M4 归并入口](RESULTS_MERGE_ENTRY_20261011.md)：有序段归并的部署性工作减少未通过；机制收口、转 M5。\n"
assert s.count(a) == 1, "07 README anchor"
io.open(p, "w", encoding="utf-8", newline="\n").write(s.replace(a, b))
print("07 README updated")

# root README pointer
p = os.path.join(root, "README.md")
s = io.open(p, encoding="utf-8", newline="").read()
a = "- **读路径入口诊断（P1）**：[目录额外工作来源与收口判定](07_Eval_Integration/RESULTS_DEPTH_DIR_READ_PATH_ENTRY_20261011.md)。\n"
b = a + "- **M4 归并入口**：[有序段归并未通过、机制收口](07_Eval_Integration/RESULTS_MERGE_ENTRY_20261011.md)。\n"
assert s.count(a) == 1, "root README anchor"
io.open(p, "w", encoding="utf-8", newline="\n").write(s.replace(a, b))
print("root README updated")
