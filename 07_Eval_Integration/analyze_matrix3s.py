#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""matrix3s roll-up: paired within-repetition comparison at the strengthened protocol.

The entry's default protocol gives three repeats per arm, and the per-repeat spread of a
single arm is about +-20% -- wider than any candidate effect measured in this project. The
strengthened run raises the repeat count to 15 through the entry's own --workload override,
and this script does what the project's own analyzer does: ratios are formed WITHIN one
repeat (same seed, same workload, same process) and the reported figure is the median of
those ratios, with a direction count.
"""

import collections
import csv
import os
import statistics
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def samples(matrix, corpus):
    path = os.path.join(HERE, "results", "%s_%s" % (matrix, corpus), "samples.csv")
    per_arm = collections.defaultdict(dict)

    with open(path, encoding="utf-8") as f:
        for row in csv.DictReader(f):
            if row.get("sample_type") != "throughput":
                continue

            try:
                v = float(row["lookup_ns"])
            except (TypeError, ValueError):
                continue

            per_arm[row["implementation_id"]][int(row["repeat"])] = v

    return per_arm


def report(matrix, corpus, pairs):
    data = samples(matrix, corpus)
    print("=" * 100)
    print("%s / %s  (throughput-instance lookup_ns per repeat; medians and paired ratios)"
          % (matrix, corpus))
    print("=" * 100)

    for off, on, label in pairs:
        a = data.get(off, {})
        b = data.get(on, {})

        if not a or not b:
            print("  %-12s MISSING" % label)
            continue

        reps = sorted(set(a) & set(b))
        ratios = [b[r] / a[r] for r in reps if a[r] > 0]
        med_off = statistics.median(a.values())
        med_on = statistics.median(b.values())
        faster = sum(1 for r in ratios if r < 1.0)
        print("  %-12s off median %10.0f ns (n=%d, range %.0f-%.0f)"
              % (label, med_off, len(a), min(a.values()), max(a.values())))
        print("  %-12s on  median %10.0f ns (n=%d, range %.0f-%.0f)"
              % ("", med_on, len(b), min(b.values()), max(b.values())))
        print("  %-12s PAIRED ratio median %.3f  direction %d/%d  range %.3f-%.3f"
              % ("", statistics.median(ratios), faster, len(ratios),
                 min(ratios), max(ratios)))
        print()


def main():
    matrix = sys.argv[1] if len(sys.argv) > 1 else "matrix3s"
    corpora = sys.argv[2:] if len(sys.argv) > 2 else ["tranco"]
    pairs = [("ours-context16", "ours-context16-routing", "Context16"),
             ("ours-lits", "ours-lits-routing", "LitsHpt")]

    for c in corpora:
        report(matrix, c, pairs)


if __name__ == "__main__":
    main()