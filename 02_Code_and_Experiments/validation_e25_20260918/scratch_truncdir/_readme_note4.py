import io
p = "README.md"
t = io.open(p, encoding="utf-8", newline="").read()
old = "- [等深目录结果](RESULTS_DEPTH_DIR_20261011.md)：场景收益与主目标未通过的结论。\n"
new = (old +
       "- [截断分隔符等深目录](RESULTS_DEPTH_DIR_TRUNC_20261011.md)：截断机制成立（mixed/skew 更快、\n"
       "  arena −57%…−89%），但对 C16 的主目标仍未通过的结论。\n")
assert t.count(old) == 1, t.count(old)
io.open(p, "w", encoding="utf-8", newline="").write(t.replace(old, new))
print("README updated")
