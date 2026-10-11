import collections
import glob
import json

by = collections.defaultdict(list)

for path in sorted(glob.glob("results/main/combo_diag_*.jsonl")):
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()

            if line:
                by[(json.loads(line)["backend"], json.loads(line)["mode"],
                    json.loads(line)["kind"])].append(json.loads(line))

print("DIAG COUNTERS AT THE 200% CHECKPOINT (per arm-run; counters reset per arm)")
print("  %-9s %-5s %-7s %14s %10s %12s %8s" %
      ("backend", "mode", "corpus", "W subblock_rc", "W widened", "WC widened", "routing=regions"))

problems = 0

for key in sorted(by):
    rows = by[key]
    w = [r for r in rows if r["arm"] == "W" and abs(r["frac"] - 2.0) < 1e-9]
    wc = [r for r in rows if r["arm"] == "WC" and abs(r["frac"] - 2.0) < 1e-9]
    c = [r for r in rows if r["arm"] == "C"]

    if len(w) != 4 or len(wc) != 4:
        print("  %-9s %-5s %-7s  MISSING REPS (%d, %d)" % (key[0], key[1], key[2], len(w), len(wc)))
        problems += 1
        continue

    sub_min = min(r["subblock_reclaims"] for r in w)
    wid_min = min(r["reclaim_widened"] for r in w)
    wid_wc = min(r["reclaim_widened"] for r in wc)
    routing_ok = all(r["routing_regions"] == r["regions"] for r in c)

    ok = sub_min > 0 and wid_min > 0 and wid_wc > 0 and routing_ok

    if not ok:
        problems += 1

    print("  %-9s %-5s %-7s %14d %10d %12d %8s  %s" %
          (key[0], key[1], key[2], sub_min, wid_min, wid_wc, routing_ok,
           "ok" if ok else "*** PROBLEM ***"))

print()
print("  'W subblock_rc' and 'W widened' are the MINIMUM over the 4 repetitions of the W arm.")
print("  Both must be > 0 so the timing round is not certifying an arm that never ran its")
print("  branch. routing=regions checks the C arm rebuilt the digest directory after the")
print("  last structural change (True means routing_regions == region_count on every row).")
print()
print("%d problem group(s)" % problems)