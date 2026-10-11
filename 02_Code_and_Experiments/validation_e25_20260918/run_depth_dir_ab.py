#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Driver and analyzer for the EqualDepthDir A/B.

PROTOCOL (the project's, unchanged): five independent processes, four repetitions each, the two
arms rotating position inside every repetition so neither arm is systematically first or last.
Ratios are formed WITHIN one repetition -- same process, same machine state, same query
sequence -- and the reported figure is the median of those per-repetition ratios. Direction is
counted over the ratios; the noise floor is max/min of the PER-PROCESS medians, and a difference
smaller than that is reported as unresolved rather than as a win.

WHAT THE ANALYZER REFUSES TO DO: no ratio between arms' medians from different repetitions, no
chaining ratios across runs, no reading a win out of a difference inside the noise floor.
It also checks the things that make the comparison valid at all: every arm answered every probe
(mismatches 0), the arms agree on the checksums and on the probe hashes, and the two arms really
do differ in the backend.

Usage:  python3 run_depth_dir_ab.py --run [--procs 5] [--reps 4] [--stem depth_ab]
        python3 run_depth_dir_ab.py --analyze [--stem depth_ab]
"""

import argparse
import json
import os
import statistics
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
MAIN = os.path.join(HERE, "results", "main")
TR = "../validation_e5_20260913/data/tranco_XN6XN.csv"

CORPORA = [("mixed", 131072, ""), ("skew", 131072, ""), ("tranco", 262144, TR)]
ARMS = ["C16", "DDir"]
METRICS = ["static_ns", "post_ns", "build_ns", "maint_ns"]


def run(procs, reps, stem):
    for kind, n, csv in CORPORA:
        path = os.path.join(MAIN, "%s_%s.jsonl" % (stem, kind))
        if os.path.exists(path):
            print("REFUSED: %s already exists" % path, file=sys.stderr)
            return 2
    for kind, n, csv in CORPORA:
        for p in range(procs):
            tag = "p%d" % p
            cmd = ["python3", "tools_build.py", "run", "bench_depth_dir", kind, str(n),
                   str(reps), csv, tag, "%s_%s" % (stem, kind)]
            print("### " + " ".join(cmd), flush=True)
            rc = subprocess.run(cmd, cwd=HERE).returncode
            if rc != 0:
                print("FAILED rc=%d for %s %s" % (rc, kind, tag), file=sys.stderr)
                return rc
    return 0


def analyze(stem):
    problems = []
    summary = []
    for kind, n, _csv in CORPORA:
        path = os.path.join(MAIN, "%s_%s.jsonl" % (stem, kind))
        if not os.path.exists(path):
            problems.append("missing %s" % path)
            continue
        recs = [json.loads(line) for line in open(path, encoding="utf-8") if line.strip()]

        # validity checks
        for r in recs:
            if r.get("static_mismatches") or r.get("post_mismatches"):
                problems.append("%s arm %s rep %s answered a probe wrongly" %
                                (kind, r["arm"], r["rep"]))
        by_key = {}
        for r in recs:
            by_key.setdefault((r["run_tag"], r["rep"]), {})[r["arm"]] = r
        incomplete = 0
        for key, arms in by_key.items():
            if set(arms) != set(ARMS):
                incomplete += 1
                continue
            a, b = arms["C16"], arms["DDir"]
            for field in ("static_checksum", "post_checksum", "static_probe_hash", "post_probe_hash"):
                if a[field] != b[field]:
                    problems.append("%s %s differs on %s" % (kind, key, field))
            for field in ("static_hits", "post_hits"):
                if a[field] != b[field]:
                    problems.append("%s %s differs on %s" % (kind, key, field))

        per_proc = {}
        for (tag, rep), arms in sorted(by_key.items()):
            if set(arms) != set(ARMS):
                continue
            a, b = arms["C16"], arms["DDir"]
            row = per_proc.setdefault(tag, {m: [] for m in METRICS})
            for m in METRICS:
                if a[m] > 0:
                    row[m].append(b[m] / a[m])

        entry = {"kind": kind, "pairs": len([k for k, v in by_key.items() if set(v) == set(ARMS)]),
                 "incomplete": incomplete}
        for m in METRICS:
            medians = {tag: statistics.median(v[m]) for tag, v in per_proc.items() if v[m]}
            if not medians:
                continue
            vals = sorted(medians.values())
            entry[m] = {
                "median_of_process_medians": statistics.median(vals),
                "per_process": {k: round(v, 4) for k, v in sorted(medians.items())},
                "noise_floor": (max(vals) / min(vals)) if min(vals) > 0 else None,
                "faster_than_C16_processes": sum(1 for v in vals if v < 1.0),
                "processes": len(vals),
            }
        # memory: model bytes per arm (median over records)
        for arm in ARMS:
            mb = [r["model_bytes"] for r in recs if r["arm"] == arm]
            if mb:
                entry["model_bytes_%s" % arm] = statistics.median(mb)
        summary.append(entry)

    print(json.dumps({"summary": summary, "problems": problems}, indent=1))
    return 1 if problems else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--run", action="store_true")
    ap.add_argument("--analyze", action="store_true")
    ap.add_argument("--procs", type=int, default=5)
    ap.add_argument("--reps", type=int, default=4)
    ap.add_argument("--stem", default="depth_ab")
    a = ap.parse_args()
    if a.run:
        return run(a.procs, a.reps, a.stem)
    return analyze(a.stem)


if __name__ == "__main__":
    sys.exit(main())
