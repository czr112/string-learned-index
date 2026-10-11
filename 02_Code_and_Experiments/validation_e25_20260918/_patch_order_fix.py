# -*- coding: utf-8 -*-
"""P0: make the directory search compare bytes as UNSIGNED, explicitly.

The defect (07_Eval_Integration/AUDIT_DEPTH_DIR_ORDER_20261011.md): `sep[i] < q[i]` compares
char, which is signed on x86, so every byte >= 0x80 sorts below every byte < 0x80. Placement and
lookup share the wrong map, so find-only checks cannot see it and the ASCII corpora cannot
either. The fix is the two casts below -- not -funsigned-char, which would change the semantics of
the whole program.
"""
import io, os
HERE = os.path.dirname(os.path.abspath(__file__))
p = os.path.join(HERE, "model", "equal_depth_dir.hpp")
t = io.open(p, encoding="utf-8", newline="").read()

old = ("            const bool sep_le_q = (i == n) ? (sep.size() <= q.size()) : (sep[i] < q[i]);\n")
new = ("            // UNSIGNED bytes, and explicitly so. A plain `sep[i] < q[i]` compares char,\n"
       "            // which is signed on x86: every byte >= 0x80 would sort below every byte <\n"
       "            // 0x80, so the count is wrong for stored keys and for probes alike (128/256\n"
       "            // mismatches and one group inversion on the four-key fixture in\n"
       "            // check_depth_dir_order). compare() below uses memcmp, which is unsigned by\n"
       "            // the standard, which is why the fit and the duplicate-threshold check never\n"
       "            // showed this.\n"
       "            const bool sep_le_q =\n"
       "                (i == n) ? (sep.size() <= q.size())\n"
       "                         : (static_cast<unsigned char>(sep[i]) <\n"
       "                            static_cast<unsigned char>(q[i]));\n")
if t.count(old) != 1:
    raise SystemExit("FATAL: search comparison not found exactly once (%d)" % t.count(old))
t = t.replace(old, new)

old2 = ("//   1. Compare is the index's own unsigned-byte order on the SUFFIX (region prefix stripped),\n"
        "//      which agrees with full-key order because every key the region accepts shares the prefix.\n")
new2 = ("//   1. Compare is the index's own unsigned-byte order on the SUFFIX (region prefix stripped),\n"
        "//      which agrees with full-key order because every key the region accepts shares the prefix.\n"
        "//      The SEARCH must obey the same order, byte by byte: it compares through\n"
        "//      `static_cast<unsigned char>` on purpose, because a plain char comparison would be\n"
        "//      signed on x86 and would order every byte >= 0x80 below every byte < 0x80.\n")
if t.count(old2) != 1:
    raise SystemExit("FATAL: correctness comment not found exactly once (%d)" % t.count(old2))
t = t.replace(old2, new2)
io.open(p, "w", encoding="utf-8", newline="\n").write(t)
print("patched", p)
