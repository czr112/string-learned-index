# -*- coding: utf-8 -*-
"""E37 step 1: the bounded-migration feasibility table.

Reads results/main/migrate_diag_{same,grow}.jsonl (run_migrate.py). Two Index arms and ART;
ART takes no part in the scan (no sub-blocks, no container) so it is not printed.

The three columns the reader has to keep apart:

  NOW      what the layout accepts as it stands. E31's reclaim runs LAZILY, on a failed
           insert, for the block the placement picks first -- so these blocks have not been
           reclaimed yet and this column is what the base captures today.
  AFTER    the same records judged against the post-reclaim watermark, each against the
           SAME recovered state. This is an UPPER BOUND: it ignores that placing one record
           consumes the slot the next one's test assumed.
  GREEDY   a dry run that decrements a virtual copy of the target block's slots and bytes as
           it accepts, walking the container in its own order. This is the opportunity under
           a stated policy, and the gap to AFTER is what mutual exclusion costs.

Written to results/main/e37_feasibility.txt.
"""

import io
import json
import os

DIR = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(DIR, "results", "main", "e37_feasibility.txt")

KINDS = ("mixed", "skew", "tranco")
MODES = ("same", "grow")
FRACS = (0.25, 0.5, 1.0, 2.0)
ARMS = ("B", "S")


def load():
    rows = []

    for mode in MODES:
        path = os.path.join(DIR, "results", "main", "migrate_diag_%s.jsonl" % mode)

        if not os.path.exists(path):
            raise SystemExit("missing %s -- run run_migrate.py" % path)

        with io.open(path, encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if line:
                    rows.append(json.loads(line))

    return [r for r in rows if r.get("arm") in ARMS]


def main():
    rows = load()
    out = io.open(OUT, "w", encoding="utf-8")
    w = out.write

    w("=" * 122 + "\n")
    w("E37 第一步：有界迁回的可行性（只读扫描，不加机制）\n")
    w("=" * 122 + "\n")
    w("  %d 条记录。B ＝ 冻结版本；S ＝ 冻结 ＋ `subblock_reclaim`（**E37 的底座**，即 E31）。\n" % len(rows))
    w("  底座固定为「E31 子块整理 ＋ 原有溢出容器」，**不叠加 T8**。\n")

    w("\n" + "#" * 122 + "\n## 1. 溢出里有多少条能回主块区\n" + "#" * 122 + "\n")
    w("  %-7s %-5s %-2s %-5s %9s %9s %10s %10s %10s\n"
      % ("data", "mode", "arm", "frac", "溢出记录", "NOW可迁", "AFTER可迁", "GREEDY可迁", "GREEDY占比"))

    for kind in KINDS:
        for mode in MODES:
            for arm in ARMS:
                for frac in FRACS:
                    for r in rows:
                        if (r["kind"] == kind and r["mode"] == mode and r["arm"] == arm
                                and abs(r["frac"] - frac) < 1e-9):
                            ov = max(1, r["mig_records"])
                            w("  %-7s %-5s %-2s %-5.2f %9d %9d %10d %10d %9.1f%%\n"
                              % (kind, mode, arm, frac, r["mig_records"], r["mig_migratable"],
                                 r["mig_migratable_after"], r["mig_greedy"],
                                 100.0 * r["mig_greedy"] / ov))
                            break

    w("\n" + "#" * 122 + "\n## 2. 约束卡在哪一条（200% 检查点，GREEDY 的对照）\n" + "#" * 122 + "\n")
    w("  三个条件是审阅列的那三个：**键范围（有目标子块）／空槽／字节**。\n")
    w("  %-7s %-5s %-2s %10s %10s %10s %12s %12s\n"
      % ("data", "mode", "arm", "无目标", "NOW无槽", "NOW缺字节", "AFTER无槽", "AFTER缺字节"))

    for kind in KINDS:
        for mode in MODES:
            for arm in ARMS:
                for r in rows:
                    if (r["kind"] == kind and r["mode"] == mode and r["arm"] == arm
                            and abs(r["frac"] - 2.0) < 1e-9):
                        w("  %-7s %-5s %-2s %10d %10d %10d %12d %12d\n"
                          % (kind, mode, arm, r["mig_no_target"], r["mig_fail_slots"],
                             r["mig_fail_bytes"], r["mig_fail_slots_after"],
                             r["mig_fail_bytes_after"]))
                        break

    w("\n" + "#" * 122 + "\n## 3. 字节量，与迁出溢出的搬移代价\n" + "#" * 122 + "\n")
    w("  `逐条删除` ＝ 按升序一条条 erase 的总搬移（每次 erase 走剩下的尾部）；\n")
    w("  `单趟压缩` ＝ 一趟过完、每个幸存条目最多移动一次。**两者相差约 26–31 倍**，\n")
    w("  采用哪个是设计决定而不是测量结果，所以两个都报。\n")
    w("  %-7s %-5s %-2s %12s %12s %14s %14s %8s\n"
      % ("data", "mode", "arm", "GREEDY字节", "AFTER字节", "逐条删除搬移", "单趟压缩搬移", "倍数"))

    for kind in KINDS:
        for mode in MODES:
            for arm in ARMS:
                for r in rows:
                    if (r["kind"] == kind and r["mode"] == mode and r["arm"] == arm
                            and abs(r["frac"] - 2.0) < 1e-9):
                        comp = max(1, r["mig_shift_compaction"])
                        w("  %-7s %-5s %-2s %12d %12d %14d %14d %7.1fx\n"
                          % (kind, mode, arm, r["mig_greedy_bytes"], r["mig_bytes_after"],
                             r["mig_shift_one_by_one"], r["mig_shift_compaction"],
                             r["mig_shift_one_by_one"] / float(comp)))
                        break

    out.close()
    print("written: %s" % OUT)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
