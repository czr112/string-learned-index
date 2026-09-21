# -*- coding: utf-8 -*-
"""E35 mechanism table: can an inline comparison prefix pay for the O1 read path?

Reads results/main/prefix_diag_{same,grow}.jsonl (run_prefix.py --phases mech), phase
"queue" records.

Three questions, in the order that decides the outcome:

  1. WHICH PATH RUNS. The overflow search is Hybrid: containers at or above the frozen
     cutoff take the binary search, smaller ones take the linear scan. If the linear scan
     dominates, then a prefix that only helps the binary search helps the minority case.

  2. ON THE BINARY PATH, would a W-byte prefix decide? decidability is counted on the
     STORED SUFFIX, and short4 is its own bucket because no window W can settle a
     comparison where one side has fewer than W bytes.

  3. ON THE LINEAR PATH, what would a prefix add ON TOP OF THE TAG? The scan reads every
     record for its tag before comparing anything, so the prefix's marginal value is only
     the tag survivors it could settle. This is the number that decides the prototype.

Written to results/main/e35_mech.txt.
"""

import io
import json
import os

DIR = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(DIR, "results", "main", "e35_mech.txt")

KINDS = ("mixed", "skew", "tranco")
MODES = ("same", "grow")
FRACS = (0.25, 0.5, 1.0, 2.0)
QUEUES = ("main", "overflow", "miss")


def load():
    rows = []

    for mode in MODES:
        path = os.path.join(DIR, "results", "main", "prefix_diag_%s.jsonl" % mode)

        if not os.path.exists(path):
            raise SystemExit("missing %s -- run run_prefix.py --phases mech" % path)

        with io.open(path, encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if line:
                    rows.append(json.loads(line))

    return [r for r in rows if r.get("phase") == "queue"]


def cell(rows, kind, mode, frac, arm, queue):
    for r in rows:
        if (r["kind"] == kind and r["mode"] == mode and abs(r["frac"] - frac) < 1e-9
                and r["arm"] == arm and r["queue"] == queue):
            return r
    return None


def load_clean():
    rows = []

    for mode in MODES:
        path = os.path.join(DIR, "results", "main", "prefix_%s.jsonl" % mode)

        if not os.path.exists(path):
            raise SystemExit("missing %s -- run run_prefix.py --phases clean" % path)

        with io.open(path, encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if line:
                    rows.append(json.loads(line))

    return [r for r in rows if r.get("phase") == "queue"]


def timing_section(w):
    """Per-queue paired timings, from the clean build.

    Two assertions run BEFORE any ratio is printed, because a per-queue ratio is only
    meaningful if both arms were timed on the same keys and produced the same answers:

      queue_hash  equal between B and O  -> the same key list
      queue_acc   equal between B and O  -> the same query results
    """
    rows = load_clean()
    tags = sorted({r["run_tag"] for r in rows})
    reps = sorted({r["rep"] for r in rows})
    idx = {(r["kind"], r["mode"], r["frac"], r["queue"], r["rep"], r["run_tag"], r["arm"]): r
           for r in rows}

    w("\n" + "#" * 118 + "\n## 5. 各队列的绝对耗时与配对比值（干净构建，5 进程）\n" + "#" * 118 + "\n")
    w("  %d 条 queue 记录，tag %s，reps %s\n" % (len(rows), tags, reps))

    # --- the two soundness assertions ---
    bad_hash, bad_acc, checked = [], [], 0

    for key in list(idx):
        kind, mode, frac, queue, rep, tag, arm = key

        if arm != "O":
            continue

        b = idx.get((kind, mode, frac, queue, rep, tag, "B"))

        if not b:
            continue

        checked += 1

        if b["queue_hash"] != idx[key]["queue_hash"]:
            bad_hash.append((kind, mode, frac, queue, tag, rep))

        if b["queue_acc"] != idx[key]["queue_acc"]:
            bad_acc.append((kind, mode, frac, queue, tag, rep))

    w("\n  配对声音性：比较了 %d 对 B/O 单元。\n" % checked)
    w("    queue_hash 不同：%d 对%s\n" % (len(bad_hash), "" if not bad_hash else "  " + str(bad_hash[:5])))
    w("    queue_acc  不同：%d 对%s\n" % (len(bad_acc), "" if not bad_acc else "  " + str(bad_acc[:5])))

    # --- absolute times, then the paired ratio ---
    w("\n  %-7s %-6s %-6s %-9s %10s %10s %10s %8s\n"
      % ("data", "mode", "frac", "queue", "B ns/q", "O ns/q", "O/B", "dir"))

    for kind in KINDS:
        for mode in MODES:
            for frac in (0.25, 0.5, 1.0, 2.0):
                for queue in QUEUES:
                    per_process = []

                    for tag in tags:
                        ratios = []

                        for rep in reps:
                            b = idx.get((kind, mode, frac, queue, rep, tag, "B"))
                            o = idx.get((kind, mode, frac, queue, rep, tag, "O"))

                            if b and o and b["ns_per_query"] > 0:
                                ratios.append(o["ns_per_query"] / b["ns_per_query"])

                        if ratios:
                            per_process.append(sorted(ratios)[len(ratios) // 2])

                    if not per_process:
                        continue

                    # ART is absent from the queue phase by design -- libart has no overflow
                    # container, so main/overflow/miss is not a distinction it has -- so
                    # there is no ART column here rather than a column of not-applicables.
                    def med_of(arm):
                        vals = [r["ns_per_query"]
                                for r in rows
                                if r["kind"] == kind and r["mode"] == mode
                                and abs(r["frac"] - frac) < 1e-9
                                and r["queue"] == queue and r["arm"] == arm]
                        return sorted(vals)[len(vals) // 2] if vals else float("nan")

                    pm = sorted(per_process)[len(per_process) // 2]

                    w("  %-7s %-6s %-6.2f %-9s %10.1f %10.1f %10.3f %5d/%d\n"
                      % (kind, mode, frac, queue, med_of("B"), med_of("O"),
                         pm, sum(1 for v in per_process if v < 1.0), len(per_process)))


def main():
    rows = load()
    fout = io.open(OUT, "w", encoding="utf-8")
    w = fout.write

    w("=" * 118 + "\n")
    w("E35 机制表：内联比较前缀能不能值回 O1 的读路径\n")
    w("=" * 118 + "\n")
    w("  %d 条 queue 记录（诊断构建，-DE25_DIAG）\n" % len(rows))
    w("  计数器都是确定性量，不是计时；本表不报任何时间。\n")

    # ---- 1. which path runs -------------------------------------------------
    w("\n" + "#" * 118 + "\n## 1. 哪条路径在跑（Hybrid：容器 >= 冻结阈值走二分，否则走线性）\n" + "#" * 118 + "\n")
    w("  %-7s %-6s %-6s %-5s %10s %10s %10s   %s\n"
      % ("data", "mode", "frac", "arm", "bin_q", "linear_q", "linear%", "per-query overflow records"))

    for kind in KINDS:
        for mode in MODES:
            for frac in FRACS:
                for arm in ("B", "O"):
                    ov = cell(rows, kind, mode, frac, arm, "overflow")

                    if not ov or (ov["ov_bin_q"] + ov["ov_linear_q"]) == 0:
                        continue

                    tot = ov["ov_bin_q"] + ov["ov_linear_q"]
                    per_q = ov["ov_lin_recs"] / float(ov["keys"] * ov["runs"])

                    w("  %-7s %-6s %-6.2f %-5s %10d %10d %9.1f%%   %.1f\n"
                      % (kind, mode, frac, arm, ov["ov_bin_q"], ov["ov_linear_q"],
                         100.0 * ov["ov_linear_q"] / tot, per_q))

    # ---- 2. the binary path -------------------------------------------------
    w("\n" + "#" * 118 + "\n## 2. 二分路径上，4／8 字节前缀能决定多少比较\n" + "#" * 118 + "\n")
    w("  判据：两侧都 >= W 字节且前 W 字节不同。`短串` 单列——一侧不足 W 字节时任何窗口都定不了。\n")
    w("  %-7s %-6s %-6s %-5s %10s %10s %10s %10s %12s %10s\n"
      % ("data", "mode", "frac", "arm", "cmps", "decided4", "decided8", "短串", "平均LCP", "决定率"))

    for kind in KINDS:
        for mode in MODES:
            for frac in FRACS:
                for arm in ("B", "O"):
                    ov = cell(rows, kind, mode, frac, arm, "overflow")

                    if not ov or ov["ov_bin_cmps"] == 0:
                        continue

                    n = ov["ov_bin_cmps"]
                    lcp = ov["ov_lcp_bytes"] / float(n)
                    dec = (ov["ov_decided4"] + ov["ov_decided8"]) / float(n)

                    w("  %-7s %-6s %-6.2f %-5s %10d %10d %10d %10d %12.1f %9.1f%%\n"
                      % (kind, mode, frac, arm, n, ov["ov_decided4"], ov["ov_decided8"],
                         ov["ov_short4"], lcp, 100.0 * dec))

    # ---- 3. the linear path -------------------------------------------------
    w("\n" + "#" * 118 + "\n## 3. 线性路径上，前缀在「标签之上」还能省多少读\n" + "#" * 118 + "\n")
    w("  线性扫描为取 tag 会读每一条记录，所以前缀的**边际**价值只是它能定的 tag 幸存者。\n")
    w("  %-7s %-6s %-6s %-5s %11s %11s %11s %11s %10s %12s\n"
      % ("data", "mode", "frac", "arm", "读过记录", "tag拒", "tag过", "前缀可拒8", "占读过", "占tag过"))

    for kind in KINDS:
        for mode in MODES:
            for frac in FRACS:
                for arm in ("B", "O"):
                    ov = cell(rows, kind, mode, frac, arm, "overflow")

                    if not ov or ov["ov_lin_recs"] == 0:
                        continue

                    recs = ov["ov_lin_recs"]
                    passed = ov["ov_lin_tag_pass"]
                    rej8 = ov["ov_lin_reject8"]

                    w("  %-7s %-6s %-6.2f %-5s %11d %11d %11d %11d %9.2f%% %11.1f%%\n"
                      % (kind, mode, frac, arm, recs, ov["ov_lin_tag_rej"], passed, rej8,
                         100.0 * rej8 / recs, (100.0 * rej8 / passed) if passed else 0.0))

    # ---- 4. the verdict inputs, in one place --------------------------------
    w("\n" + "#" * 118 + "\n## 4. 判据汇总（只看 200% 检查点，溢出队列，O 臂）\n" + "#" * 118 + "\n")
    w("  %-7s %-6s %12s %12s %14s %16s\n"
      % ("data", "mode", "二分占比", "前缀可拒/读过", "二分决定率", "平均LCP(二分)"))

    for kind in KINDS:
        for mode in MODES:
            ov = cell(rows, kind, mode, 2.0, "O", "overflow")

            if not ov:
                continue

            tot = ov["ov_bin_q"] + ov["ov_linear_q"]
            bin_share = (100.0 * ov["ov_bin_q"] / tot) if tot else 0.0
            marg = (100.0 * ov["ov_lin_reject8"] / ov["ov_lin_recs"]) if ov["ov_lin_recs"] else 0.0

            if ov["ov_bin_cmps"]:
                dec = 100.0 * (ov["ov_decided4"] + ov["ov_decided8"]) / ov["ov_bin_cmps"]
                lcp = ov["ov_lcp_bytes"] / float(ov["ov_bin_cmps"])
            else:
                dec = float("nan")
                lcp = float("nan")

            w("  %-7s %-6s %11.1f%% %11.2f%% %13.1f%% %16.1f\n"
              % (kind, mode, bin_share, marg, dec, lcp))

    timing_section(w)

    fout.close()
    print("written: %s" % OUT)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
