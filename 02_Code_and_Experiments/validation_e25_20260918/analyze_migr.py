# -*- coding: utf-8 -*-
"""E37 step 2 analysis: the R0 / R1 acceptance table, the scan arm, and the repair.

Inputs
  results/main/migr_{same,grow}.jsonl        clean build, 5 processes x 4 arms x 4 reps
  results/main/migr_diag_{same,grow}.jsonl   diagnostic build: the migration volume
  results/main/e37_scan_migr.jsonl           the scan arm (S2 / S2R0 / S2R1 / ARR)
  results/main/repair_diag_{same,grow}.jsonl the repair of a degraded snapshot

Aggregation as everywhere in this project: a cell's per-process value is the median of the
within-repetition paired ratios; the headline is the median of those five per-process values
and the direction count says how many of the 5 agree. max/min across processes is the noise
floor for that cell.

The two comparisons the reviewer fixed: R1/R0 isolates the drain (one switch), R1/B measures
the whole change from the frozen version. B is context, not a control for the drain.

Written to results/main/e37_analysis.txt.
"""

import io
import json
import os
import statistics as st

DIR = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(DIR, "results", "main", "e37_analysis.txt")

KINDS = ("mixed", "skew", "tranco")
MODES = ("same", "grow")
FRACS = (0.25, 0.5, 1.0, 2.0)
QUEUES = ("main", "overflow", "miss")
PAIRS = [("R1", "R0", "R1/R0  (the drain, one switch)"),
         ("R1", "B", "R1/B   (the whole change from the frozen version)")]


def load(prefix):
    rows = []

    for mode in MODES:
        path = os.path.join(DIR, "results", "main", "%s_%s.jsonl" % (prefix, mode))

        if not os.path.exists(path):
            raise SystemExit("missing %s -- run the matching driver" % path)

        with io.open(path, encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if line:
                    rows.append(json.loads(line))

    return rows


def ratio_block(rows, field, num, den, label, out, fracs=None):
    tags = sorted({r["run_tag"] for r in rows})
    reps = sorted({r["rep"] for r in rows})
    idx = {(r["kind"], r["mode"], r["frac"], r["rep"], r["run_tag"], r["arm"]): r
           for r in rows if r.get("phase", "maint") == "maint"}

    out.write("\n-- %s : %s / %s --\n" % (label, num, den))
    out.write("  %-7s %-6s %-6s %9s %9s %7s   %s\n"
              % ("data", "mode", "frac", "procMed", "max/min", "dir", "per-process medians"))

    for kind in KINDS:
        for mode in MODES:
            for frac in (fracs if fracs else FRACS):
                per_process = []

                for tag in tags:
                    ratios = []

                    for rep in reps:
                        a = idx.get((kind, mode, frac, rep, tag, num))
                        b = idx.get((kind, mode, frac, rep, tag, den))

                        if a and b and b.get(field):
                            ratios.append(a[field] / b[field])

                    if ratios:
                        per_process.append(st.median(ratios))

                if not per_process:
                    continue

                out.write("  %-7s %-6s %-6.2f %9.3f %9.3f %5d/%d   %s\n"
                          % (kind, mode, frac, st.median(per_process),
                             max(per_process) / min(per_process),
                             sum(1 for v in per_process if v < 1.0), len(per_process),
                             " ".join("%.3f" % v for v in per_process)))


def queue_block(rows, num, den, out):
    tags = sorted({r["run_tag"] for r in rows})
    reps = sorted({r["rep"] for r in rows})
    idx = {(r["kind"], r["mode"], r["frac"], r["queue"], r["rep"], r["run_tag"], r["arm"]): r
           for r in rows if r.get("phase") == "queue"}

    out.write("\n-- point query by queue : %s / %s --\n" % (num, den))
    out.write("  %-7s %-6s %-6s %-9s %9s %9s %7s   %s\n"
              % ("data", "mode", "frac", "queue", "procMed", "max/min", "dir", "per-process medians"))

    for kind in KINDS:
        for mode in MODES:
            for frac in (1.0, 2.0):
                for queue in QUEUES:
                    per_process = []

                    for tag in tags:
                        ratios = []

                        for rep in reps:
                            a = idx.get((kind, mode, frac, queue, rep, tag, num))
                            b = idx.get((kind, mode, frac, queue, rep, tag, den))

                            if a and b and b.get("ns_per_query"):
                                ratios.append(a["ns_per_query"] / b["ns_per_query"])

                        if ratios:
                            per_process.append(st.median(ratios))

                    if not per_process:
                        continue

                    out.write("  %-7s %-6s %-6.2f %-9s %9.3f %9.3f %5d/%d   %s\n"
                              % (kind, mode, frac, queue, st.median(per_process),
                                 max(per_process) / min(per_process),
                                 sum(1 for v in per_process if v < 1.0), len(per_process),
                                 " ".join("%.3f" % v for v in per_process)))


def scan_section(out):
    path = os.path.join(DIR, "results", "main", "e37_scan_migr.jsonl")

    out.write("\n" + "#" * 118 + "\n## 3. 扫描：S2R1 / S2R0（以及 S2R1 / S2）\n" + "#" * 118 + "\n")

    if not os.path.exists(path):
        out.write("  (absent -- run run_scan_migr.py)\n")
        return

    with io.open(path, encoding="utf-8") as f:
        rows = [json.loads(line) for line in f if line.strip()]

    tags = sorted({r["run_tag"] for r in rows})
    reps = sorted({r["rep"] for r in rows})
    idx = {(r["kind"], r["state"], r["n"], r["rep"], r["run_tag"], r["arm"]): r for r in rows}

    for num, den in (("S2R1", "S2R0"), ("S2R1", "S2")):
        out.write("\n-- scan : %s / %s --\n" % (num, den))
        out.write("  %-7s %-9s %5s %9s %9s %7s   %s\n"
                  % ("data", "state", "n", "procMed", "max/min", "dir", "per-process medians"))

        for kind in KINDS:
            for state in ("initial", "insert25", "replace25"):
                for n in (16, 128, 1024):
                    per_process = []

                    for tag in tags:
                        ratios = []

                        for rep in reps:
                            a = idx.get((kind, state, n, rep, tag, num))
                            b = idx.get((kind, state, n, rep, tag, den))

                            if a and b and b["ms"]:
                                ratios.append(a["ms"] / b["ms"])

                        if ratios:
                            per_process.append(st.median(ratios))

                    if not per_process:
                        continue

                    out.write("  %-7s %-9s %5d %9.3f %9.3f %5d/%d   %s\n"
                              % (kind, state, n, st.median(per_process),
                                 max(per_process) / min(per_process),
                                 sum(1 for v in per_process if v < 1.0), len(per_process),
                                 " ".join("%.3f" % v for v in per_process)))


def repair_section(out):
    path_same = os.path.join(DIR, "results", "main", "repair_diag_same.jsonl")

    out.write("\n" + "#" * 118 + "\n## 4. 修复已退化快照（从 200% 状态开始迁回）\n" + "#" * 118 + "\n")

    if not os.path.exists(path_same):
        out.write("  (absent -- run run_repair.py)\n")
        return

    rows = []

    for mode in MODES:
        p = os.path.join(DIR, "results", "main", "repair_diag_%s.jsonl" % mode)

        if os.path.exists(p):
            with io.open(p, encoding="utf-8") as f:
                rows += [json.loads(line) for line in f if line.strip()]

    reps = sorted({r["kind"] for r in rows if r.get("phase") == "repair"})
    out.write("  %-7s %-6s %11s %11s %9s %9s %9s %6s %12s\n"
              % ("data", "mode", "修复前溢出", "修复后溢出", "取出", "放置成功", "被退回", "轮数", "修复耗时ms"))

    for r in rows:
        if r.get("phase") != "repair":
            continue

        out.write("  %-7s %-6s %11d %11d %9d %9d %9d %6d %12.2f\n"
                  % (r["kind"], r["mode"], r["overflow_before"], r["overflow_after"],
                     r.get("taken", -1), r.get("placed", -1), r.get("rejected", -1),
                     r["rounds"], r["ms"]))


def main():
    diag = load("migr_diag")
    rows = load("migr")

    out = io.open(OUT, "w", encoding="utf-8")
    w = out.write

    w("=" * 118 + "\n")
    w("E37 第二步：有界迁回 R0 / R1\n")
    w("=" * 118 + "\n")
    w("  %d 条计时记录（干净构建），%d 条诊断记录\n" % (len(rows), len(diag)))
    w("  B ＝ 冻结版本｜R0 ＝ B ＋ `subblock_reclaim`（**E37 底座**）｜R1 ＝ R0 ＋ `overflow_migrate`\n")
    w("  `kMigrateBudget` ＝ 64 条／次调用。**这是迁回条数的上限，不是维护成本的上界**（见 §5）。\n")

    w("\n" + "#" * 118 + "\n## 1. 迁回量与溢出持有量（诊断构建，累计到该检查点）\n" + "#" * 118 + "\n")
    w("  %-7s %-5s %-3s %-5s %9s %9s %9s %9s %11s %11s\n"
      % ("data", "mode", "arm", "frac", "溢出", "迁回条数", "被退回", "促成放置", "子块回收", "回收促成"))

    for r in diag:
        if r.get("phase", "maint") != "maint" or r["arm"] == "ART":
            continue

        w("  %-7s %-5s %-3s %-5.2f %9d %9d %9d %9d %11d %11d\n"
          % (r["kind"], r["mode"], r["arm"], r["frac"], r["overflow"],
             r["migrated_records"], r.get("migrate_rejected", -1), r["migrate_helped"],
             r.get("subblock_reclaims", -1), r.get("subblock_reclaim_helped", -1)))

    w("\n  读法：`迁回条数` 是离开容器的条数，`被退回` 是其中放不进主块区、又回到容器的条数。\n")
    w("  `回收促成` 是**子块回收**直接让一次放置成功的次数——**这一列才是 R1 溢出大降的主因**，\n")
    w("  因为 drain 会顺手回收它选中的那些子块，而插入路径本来不会走到它们。\n")

    w("\n" + "#" * 118 + "\n## 2. 验收表（200% 检查点；比值 <1 表示分子那臂更快/更省）\n" + "#" * 118 + "\n")

    for num, den, label in PAIRS:
        ratio_block(rows, "cum_ms", num, den, label + " 完整维护耗时", out)

    for num, den, label in PAIRS:
        ratio_block(rows, "query_ms", num, den, label + " 点查（混合队列）", out)

    ratio_block(rows, "memory", "R1", "R0", "R1/R0 常驻空间", out)
    ratio_block(rows, "ov_bytes", "R1", "R0", "R1/R0 溢出字节", out)
    ratio_block(rows, "op_p99_ns", "R1", "R0", "R1/R0 尾延迟 p99", out)
    ratio_block(rows, "op_p999_ns", "R1", "R0", "R1/R0 尾延迟 p99.9", out)

    queue_block(rows, "R1", "R0", out)

    scan_section(out)
    repair_section(out)

    out.close()
    print("written: %s" % OUT)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
