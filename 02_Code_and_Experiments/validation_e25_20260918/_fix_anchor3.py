import io
p = r"D:\StringLearnedIndex_Workspace\02_Code_and_Experiments\validation_e25_20260918\_patch_merge_shadow.py"
s = io.open(p, encoding="utf-8").read()
old = '     "    // The consume-mode mirror of scan(), region for region.\\n")'
new = '     "    // The consume-mode mirror of scan(), region for region.")'
n = s.count(old)
assert n == 2, n
io.open(p, "w", encoding="utf-8", newline="\n").write(s.replace(old, new))
print("anchor 3 fixed in both a and b")
