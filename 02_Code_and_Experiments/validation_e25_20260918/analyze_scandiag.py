import collections
import glob
import json
import statistics

FIELDS = ["scan_regions", "scan_groups", "scan_groups_empty", "scan_blocks_skipped",
          "scan_merge_cmps", "scan_result_allocs", "scan_start_locates",
          "scan_cursor_starts", "scan_ov_cursor_starts", "scan_canonical"]

by = collections.defaultdict(lambda: collections.defaultdict(list))

for path in sorted(glob.glob("results/main/combo_scandiag_*.jsonl")):
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()

            if not line:
                continue

            r = json.loads(line)
            key = (r["backend"], r["mode"], r["kind"])

            if abs(r["frac"] - 2.0) < 1e-9:
                by[key][r["arm"]].append(r)

for key in sorted(by):
    print("=" * 110)
    print("%s %s %s  (cumulative scan counters at 200%%, median over 4 reps)" % key)
    print("=" * 110)
    print("  %-24s %12s %12s %8s %12s %8s" % ("counter", "B", "W", "W/B", "C", "WC/B"))
    print("  " + "-" * 84)

    for f in FIELDS:
        rows_b = by[key].get("B", [])
        rows_w = by[key].get("W", [])
        rows_c = by[key].get("C", [])
        rows_wc = by[key].get("WC", [])

        if not rows_b or not rows_w:
            continue

        b = statistics.median([r[f] for r in rows_b])
        w = statistics.median([r[f] for r in rows_w])
        c = statistics.median([r[f] for r in rows_c]) if rows_c else 0
        wc = statistics.median([r[f] for r in rows_wc]) if rows_wc else 0
        ratio = (w / b) if b else float("nan")
        ratio_wc = (wc / c) if c else float("nan")
        print("  %-24s %12.0f %12.0f %8.3f %12.0f %8.3f" % (f, b, w, ratio, c, ratio_wc))

    print()