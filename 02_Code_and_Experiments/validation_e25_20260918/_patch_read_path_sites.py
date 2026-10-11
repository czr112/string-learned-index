import io
p = r"D:\StringLearnedIndex_Workspace\02_Code_and_Experiments\validation_e25_20260918\index.hpp"
lines = io.open(p, encoding="utf-8", newline="").read().split("\n")
# 1-based targets: (line, must_contain, old_sub, new_sub)
targets = [
    (4365, "E25_CMP(suffix(g, blk, es[i]), s) == 0", "E25_CMP(", "E25_CMP_P("),
    (4468, "E25_CMP((*o)[mid].suffix, s) < 0", "E25_CMP(", "E25_CMP_O("),
    (4474, "E25_CMP((*o)[lo].suffix, s) == 0", "E25_CMP(", "E25_CMP_O("),
    (4514, "E25_CMP(e.suffix, s) == 0", "E25_CMP(", "E25_CMP_O("),
    (4554, "E25_CMP(e.suffix, s) == 0", "E25_CMP(", "E25_CMP_O("),
]
for ln, must, old, new in targets:
    text = lines[ln - 1]
    assert must in text and old in text, "line %d does not match: %r" % (ln, text)
    lines[ln - 1] = text.replace(old, new, 1)
io.open(p, "w", encoding="utf-8", newline="\n").write("\n".join(lines))
print("5 find-path sites split into primary/overflow")
