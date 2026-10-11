import io
p = "README.md"
t = io.open(p, encoding="utf-8", newline="").read()
old = "- **等深目录结构候选**：[正确性与 A/B 结果](07_Eval_Integration/RESULTS_DEPTH_DIR_20261011.md)。\n"
new = (old +
       "- **截断分隔符等深目录**：[正确性、规模与配对 A/B](07_Eval_Integration/RESULTS_DEPTH_DIR_TRUNC_20261011.md)。\n")
assert t.count(old) == 1
t = t.replace(old, new)

old2 = ("默认后端仍为 Context16。BlockLinearRank 正确性通过但结构入口回归；EqualDepthDir 在 skew 上有收益，\n"
        "mixed 主目标未通过，两者均未设为默认。")
new2 = ("默认后端仍为 Context16。BlockLinearRank 正确性通过但结构入口回归；EqualDepthDir 在 skew 上有收益，\n"
        "mixed 主目标未通过；其截断形态（EqualDepthDirTrunc）证实了截断机制（mixed/skew 更快、目录内存降到 1/9–1/2），\n"
        "但对 C16 的主目标仍未通过。以上候选均未设为默认。")
assert t.count(old2) == 1
t = t.replace(old2, new2)
io.open(p, "w", encoding="utf-8", newline="").write(t)
print("root README updated")
