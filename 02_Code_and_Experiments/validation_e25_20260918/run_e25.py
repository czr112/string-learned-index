#!/usr/bin/env python3
"""E25 formal timing sweep: independent processes, each running paired repetitions.

The benchmark itself runs the arms in a rotated order inside each repetition, so
the pairing is at the repetition level: V1/V0, V2/V1, V2/V0 and V2/ART are formed
INSIDE one repetition, never by dividing two aggregate medians.

Processes run serially on purpose: these are CPU- and memory-sensitive timings and
running them concurrently would make them contend for the same core.

Usage:  python3 run_e25.py [--processes 5] [--reps 5]
"""
import argparse
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))

DATASETS = [
    ("mixed", "131072", ""),
    ("skew", "131072", ""),
    ("tranco", "262144", "../validation_e5_20260913/data/tranco_XN6XN.csv"),
]


def invoke(kind, n, reps, csv, tag, log):
    args = [sys.executable, "tools_build.py", "run", "bench_e25", kind, n, str(reps), csv, tag]
    t0 = time.time()
    r = subprocess.run(args, cwd=HERE, capture_output=True, text=True)
    dt = time.time() - t0
    line = "bench_e25 %s %s reps=%d tag=%s rc=%d wall=%.0fs" % (kind, n, reps, tag, r.returncode, dt)
    print(line, flush=True)
    log.write(line + "\n")
    if r.returncode != 0:
        log.write("  stderr: " + r.stderr.strip()[-1500:] + "\n")
        log.flush()
        raise SystemExit("FAILED: " + line)
    log.flush()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--processes", type=int, default=5)
    ap.add_argument("--reps", type=int, default=5)
    a = ap.parse_args()

    out = os.path.join(HERE, "results", "main", "e25_time.jsonl")
    if os.path.exists(out):
        os.remove(out)

    with open(os.path.join(HERE, "logs", "run_e25.log"), "w", encoding="utf-8") as log:
        log.write("timing sweep start " + time.strftime("%Y-%m-%d %H:%M:%S") + "\n")
        for p in range(1, a.processes + 1):
            log.write("--- process P%d ---\n" % p)
            for kind, n, csv in DATASETS:
                invoke(kind, n, a.reps, csv, "P%d" % p, log)
        log.write("timing sweep end " + time.strftime("%Y-%m-%d %H:%M:%S") + "\n")
    print("TIMING SWEEP COMPLETE")


if __name__ == "__main__":
    main()
