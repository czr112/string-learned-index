# -*- coding: utf-8 -*-
"""Reduce the per-event hole logs to a joint size distribution, then drop the raw logs.

WHY A REDUCTION RATHER THAN KEEPING THE RAW. At full scale the three per-event logs are
~227 MB, and the round's decision does not rest on them: it rests on the counters, which
are exact and live in the 3.5 KB summary files. What the rows add is one thing -- the joint
distribution of (requested suffix length, best available dead range) inside the failing
block, which is what lets the matching question be re-cut. That distribution is a few KB, so
the raw logs are derived into it and then deleted.

THE DELETION IS RECORDED, not silent: RESULTS_HOLES_ENTRY.md names the command that
regenerates the raw logs, so the reduction is reversible rather than destructive.

Usage:  python3 derive_hole_dist.py [--delete]
Without --delete it only reports what it would write.
"""

import collections
import io
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
MAIN = os.path.join(HERE, "results", "main")

KINDS = ["mixed", "skew", "tranco"]

# Suffix lengths run ~14-160 bytes on these corpora, so 16-byte buckets to 256 keep the
# resolution where it matters and the last bucket absorbs the tail. Dead ranges run from 0
# to a few thousand bytes and are strongly skewed, so they get log2 buckets.
LEN_BUCKETS = [0, 16, 32, 48, 64, 96, 128, 192, 256, 1 << 30]


def len_bucket(v):
    for b in LEN_BUCKETS:
        if v <= b:
            return b

    return LEN_BUCKETS[-1]


def hole_bucket(v):
    if v == 0:
        return 0

    b = 1

    while b < v and b < (1 << 20):
        b <<= 1

    return b


def main():
    delete = "--delete" in sys.argv
    plan = []

    for kind in KINDS:
        raw = os.path.join(MAIN, "holes_%s.jsonl" % kind)
        dist = os.path.join(MAIN, "holes_%s.dist.jsonl" % kind)

        hist = collections.Counter()
        structure = collections.Counter()
        n = 0
        best_eq_garbage = 0

        with io.open(raw, encoding="utf-8") as f:
            for line in f:
                line = line.strip()

                if not line:
                    continue

                r = json.loads(line)
                n += 1
                hist[(r["workload"], len_bucket(r["len"]), hole_bucket(r["best_hole"]))] += 1
                structure[(r["workload"], r["nholes"], r["slots_free"])] += 1

                if r["best_hole"] == r["garbage"]:
                    best_eq_garbage += 1

        with io.open(dist, "w", encoding="utf-8", newline="\n") as f:
            for (workload, lb, hb), c in sorted(hist.items()):
                f.write(json.dumps({"kind": kind, "workload": workload,
                                    "len_bucket": lb, "hole_bucket": hb, "n": c},
                                   sort_keys=True) + "\n")

            for (workload, nh, sf), c in sorted(structure.items()):
                f.write(json.dumps({"kind": kind, "workload": workload, "record": "structure",
                                    "nholes": nh, "slots_free": sf, "n": c},
                                   sort_keys=True) + "\n")

        plan.append((raw, dist, n, len(hist), best_eq_garbage))

    for raw, dist, n, cells, eq in plan:
        print("%-46s rows=%-9d dist_cells=%-6d best_eq_garbage=%.4f%%"
              % (os.path.basename(dist), n, cells, 100.0 * eq / (n or 1)))

        if delete:
            os.remove(raw)
            print("    deleted %s" % os.path.basename(raw))

    if not delete:
        print("\n(dry run: pass --delete to write the reductions and remove the raw logs)")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
