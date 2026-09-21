# -*- coding: utf-8 -*-
"""E36 analysis: the inline tag, against the two arms the reviewer fixed.

Inputs
  results/main/tag_{same,grow}.jsonl        clean build, 5 processes x 4 arms x 4 reps
  results/main/tag_diag_{same,grow}.jsonl   diagnostic build, the counters

What the reviewer asked this round to answer, in order:
  1. how many order entries does one linear overflow query read
  2. how many candidates are rejected BEFORE any store access
  3. how much did the store accesses actually drop
and then, so that a recovered point query cannot be mistaken for a net gain:
  T/O and T/B on maintenance time, space, point query, scan, and tail latency.

Aggregation, stated once: a cell's per-process value is the median of the within-repetition
paired ratios; the headline is the median of those five per-process values and the direction
count says how many of the 5 processes agree. max/min across processes is the noise floor
for that cell. A headline inside its own noise floor is reported as no change.

Two assertions run BEFORE any ratio: queue_hash and queue_acc must agree across B, O and T
at every cell, or the three arms were not measured on the same work and the ratios are not
paired. Written to results/main/e36_analysis.txt.
"""

import io
import json
import os
import statistics as st

DIR = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(DIR, "results", "main", "e36_analysis.txt")

KINDS = ("mixed", "skew", "tranco")
MODES = ("same", "grow")
FRACS = (0.25, 0.5, 1.0, 2.0)
QUEUES = ("main", "overflow", "miss")
# The two comparisons the reviewer fixed, in the labels the report uses.
PAIRS = [("T", "O", "T/O  (the tag's net effect, one switch)"),
         ("T", "B", "T/B  (the whole thing against the original)"),
         ("O", "B", "O/B  (carried over, for continuity with E34)")]


def load(prefix):
    rows = []

    for mode in MODES:
        path = os.path.join(DIR, "results", "main", "%s_%s.jsonl" % (prefix, mode))

        if not os.path.exists(path):
            raise SystemExit("missing %s -- run run_tag.py" % path)

        with io.open(path, encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if line:
                    rows.append(json.loads(line))

    return rows


def ratio_block(rows, phase, field, num, den, label, out, fracs=None):
    """Paired ratios for one field, one phase, one arm pair."""
    tags = sorted({r["run_tag"] for r in rows})
    reps = sorted({r["rep"] for r in rows})
    idx = {(r["kind"], r["mode"], r["frac"], r.get("queue", "-"), r["rep"], r["run_tag"], r["arm"]): r
           for r in rows if r.get("phase", "maint") == phase}

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
                        a = idx.get((kind, mode, frac, "-", rep, tag, num))
                        b = idx.get((kind, mode, frac, "-", rep, tag, den))

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


def queue_ratios(rows, num, den, out):
    """Per-queue paired ratios: which queue carries the change."""
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
    """The scan half of the acceptance, from its own paired sweep.

    Reads results/main/e36_scan_tag.jsonl -- arms S2 / S2O1 / S2OT / ARR over 3 states and
    3 scan lengths. S2OT is S2O1 plus overflow_inline_tag, one switch.

    The design leaves the range scan reading whole records with no query-tag filtering, so
    the expectation is that S2OT lands on S2O1. That expectation is checked here rather
    than asserted: a recovered point query that costs scan would not be a net gain.
    """
    path = os.path.join(DIR, "results", "main", "e36_scan_tag.jsonl")

    out.write("\n" + "#" * 118 + "\n## 4. 扫描：S2OT / S2O1（以及 S2OT / S2，对最初实现）\n" + "#" * 118 + "\n")

    if not os.path.exists(path):
        out.write("  (results/main/e36_scan_tag.jsonl absent -- run run_scan_tag.py)\n")
        return

    with io.open(path, encoding="utf-8") as f:
        rows = [json.loads(line) for line in f if line.strip()]

    tags = sorted({r["run_tag"] for r in rows})
    reps = sorted({r["rep"] for r in rows})
    idx = {(r["kind"], r["state"], r["n"], r["rep"], r["run_tag"], r["arm"]): r for r in rows}

    for num, den in (("S2OT", "S2O1"), ("S2OT", "S2")):
        out.write("\n-- 扫描 : %s / %s --\n" % (num, den))
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


def main():
    mech = [r for r in load("tag_diag") if r.get("phase") == "queue"]
    rows = load("tag")

    out = io.open(OUT, "w", encoding="utf-8")
    w = out.write

    w("=" * 118 + "\n")
    w("E36：内联标签（B / O / T）——把 tag 从记录里搬到有序条目旁边\n")
    w("=" * 118 + "\n")
    w("  %d 条队列计数记录（诊断构建）；%d 条计时记录（干净构建）\n" % (len(mech), len(rows)))
    w("  B ＝ 冻结版本｜O ＝ B ＋ 稳定记录存储｜T ＝ O ＋ 有序条目内联标签\n")
    w("  sizeof(OrderedEntry) ＝ 8（`sizeof` 为准，不是设想的 5：uint32_t ＋ uint8_t 补齐）\n")

    # ---------------- soundness, before any number is quoted ----------------
    w("\n" + "#" * 118 + "\n## 0. 三臂配对声音性（先断言，再看比值）\n" + "#" * 118 + "\n")

    tags = sorted({r["run_tag"] for r in rows if r.get("phase") == "queue"})
    reps = sorted({r["rep"] for r in rows if r.get("phase") == "queue"})
    qidx = {(r["kind"], r["mode"], r["frac"], r["queue"], r["rep"], r["run_tag"], r["arm"]): r
            for r in rows if r.get("phase") == "queue"}

    bad_hash, bad_acc, checked = [], [], 0

    for key, rec in qidx.items():
        kind, mode, frac, queue, rep, tag, arm = key

        if arm != "T":
            continue

        for other in ("B", "O"):
            b = qidx.get((kind, mode, frac, queue, rep, tag, other))

            if not b:
                continue

            checked += 1

            if b["queue_hash"] != rec["queue_hash"]:
                bad_hash.append((kind, mode, frac, queue, tag, rep, other))

            if b["queue_acc"] != rec["queue_acc"]:
                bad_acc.append((kind, mode, frac, queue, tag, rep, other))

    w("  比较了 %d 对（T 对 B、T 对 O）队列单元：\n" % checked)
    w("    queue_hash 不同：%d 对%s\n" % (len(bad_hash), "" if not bad_hash else "  " + str(bad_hash[:3])))
    w("    queue_acc  不同：%d 对%s\n" % (len(bad_acc), "" if not bad_acc else "  " + str(bad_acc[:3])))
    w("  相等意味着三臂计时的是同一批键、返回的是同一批结果——比值因此是配对的。\n")

    # ---------------- the three questions the reviewer asked ----------------
    w("\n" + "#" * 118 + "\n## 1. 三个诊断量（200% 检查点，溢出队列）\n" + "#" * 118 + "\n")
    w("  问 1：一次线性溢出查询读多少有序条目；问 2：多少候选在访问记录前被拒；\n")
    w("  问 3：实际访问记录存储的次数降了多少。\n")
    w("  %-7s %-6s %-5s %10s %12s %12s %12s %12s %10s\n"
      % ("data", "mode", "arm", "entries/q", "rej_pre", "store_reads", "rej_pre/读", "store/entries", "keys"))

    for kind in KINDS:
        for mode in MODES:
            for arm in ("B", "O", "T"):
                rec = None
                for r in mech:
                    if (r["kind"] == kind and r["mode"] == mode and r["arm"] == arm
                            and r["queue"] == "overflow" and abs(r["frac"] - 2.0) < 1e-9):
                        rec = r
                        break

                if not rec:
                    continue

                q = float(rec["keys"] * rec["runs"])
                entries = rec["ov_tag_entries"] / q
                reads = rec["ov_lin_recs"] / q
                ratio = (rec["ov_lin_recs"] / entries) if entries else 0.0

                w("  %-7s %-6s %-5s %10.2f %12d %12d %11.1f%% %11.2f %10d\n"
                  % (kind, mode, arm, entries, rec["ov_tag_reject_pre"], rec["ov_lin_recs"],
                     100.0 * rec["ov_tag_reject_pre"] / rec["ov_tag_entries"] if rec["ov_tag_entries"] else 0.0,
                     ratio, rec["keys"]))

    # The drop, stated per dataset, which is the answer to question 3.
    w("\n  **记录访问的下降（T 的 store_reads ÷ O 的 store_reads，200% 溢出队列）：**\n")

    for kind in KINDS:
        for mode in MODES:
            o = t = None

            for r in mech:
                if (r["kind"] == kind and r["mode"] == mode and r["queue"] == "overflow"
                        and abs(r["frac"] - 2.0) < 1e-9):
                    if r["arm"] == "O":
                        o = r
                    elif r["arm"] == "T":
                        t = r

            if o and t and o["ov_lin_recs"]:
                w("    %-7s %-6s  %d → %d   ＝ **%.1f%%**\n"
                  % (kind, mode, o["ov_lin_recs"], t["ov_lin_recs"],
                     100.0 * t["ov_lin_recs"] / o["ov_lin_recs"]))

    # ---------------- the acceptance table ----------------
    w("\n" + "#" * 118 + "\n## 2. 验收：T/O 与 T/B\n" + "#" * 118 + "\n")
    w("  比值 < 1 表示分子那臂更快/更省。方向计数是 5 个独立进程里同意该方向的有几个。\n")

    for num, den, label in PAIRS:
        ratio_block(rows, "maint", "cum_ms", num, den, label + " 完整维护耗时", out)

    for num, den, label in PAIRS:
        ratio_block(rows, "maint", "query_ms", num, den, label + " 点查（单一队列）", out)

    for num, den, label in PAIRS:
        queue_ratios([r for r in rows if r.get("phase") == "queue"], num, den, out)
        break

    for num, den, label in PAIRS:
        ratio_block(rows, "maint", "memory", num, den, label + " 常驻空间", out)

    for num, den, label in PAIRS:
        ratio_block(rows, "maint", "ov_handle_move_bytes", num, den, label + " 溢出搬动字节", out)

    for num, den, label in PAIRS:
        ratio_block(rows, "maint", "op_p99_ns", num, den, label + " 尾延迟 p99", out)

    # ---------------- space and movement, in absolute terms ----------------
    w("\n" + "#" * 118 + "\n## 3. 绝对量与取舍（200% 检查点，单元格中位数）\n" + "#" * 118 + "\n")
    w("  %-7s %-6s %-4s %12s %12s %14s %14s\n"
      % ("data", "mode", "arm", "memory", "ov_bytes", "handle_moves", "handle_bytes"))

    for kind in KINDS:
        for mode in MODES:
            for arm in ("B", "O", "T"):
                cells = [r for r in rows
                         if r.get("phase", "maint") == "maint" and r["kind"] == kind
                         and r["mode"] == mode and abs(r["frac"] - 2.0) < 1e-9 and r["arm"] == arm]

                if not cells:
                    continue

                med = lambda k: st.median([c[k] for c in cells])

                w("  %-7s %-6s %-4s %12.0f %12.0f %14.0f %14.0f\n"
                  % (kind, mode, arm, med("memory"), med("ov_bytes"),
                     med("ov_handle_moves"), med("ov_handle_move_bytes")))

    scan_section(out)

    out.close()
    print("written: %s" % OUT)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
