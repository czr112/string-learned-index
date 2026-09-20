#!/usr/bin/env python3
"""E23 sweep driver: three independent processes on the clean build.

Each dataset runs all three states, and each state is built from its own fresh
initial one.

Usage:  python3 run_e23.py [--processes 3]
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

WARMUP = ("mixed", "16384", "")


def invoke(kind, n, csv, tag, log):
    # The CSV slot is passed EXPLICITLY even when empty: omitting it shifts the run
    # tag into the CSV argument and the tag is silently lost.
    args = [sys.executable, "tools_build.py", "run", "bench_e23", kind, n, csv, tag]
    t0 = time.time()
    r = subprocess.run(args, cwd=HERE, capture_output=True, text=True)
    dt = time.time() - t0
    line = "bench_e23 %s %s tag=%s rc=%d wall=%.0fs" % (kind, n, tag, r.returncode, dt)
    print(line, flush=True)
    log.write(line + "\n")
    if r.returncode != 0:
        log.write("  stderr: " + r.stderr.strip()[-800:] + "\n")
        log.flush()
        raise SystemExit("FAILED: " + line)
    log.flush()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--processes", type=int, default=3)
    a = ap.parse_args()

    out = os.path.join(HERE, "results", "main", "e23_scan.jsonl")
    if os.path.exists(out):
        os.remove(out)

    with open(os.path.join(HERE, "logs", "run_e23.log"), "w", encoding="utf-8") as log:
        log.write("sweep start " + time.strftime("%Y-%m-%d %H:%M:%S") + "\n")
        invoke(WARMUP[0], WARMUP[1], WARMUP[2], "warmup-P0", log)
        for p in range(1, a.processes + 1):
            log.write("--- process P%d ---\n" % p)
            for kind, n, csv in DATASETS:
                invoke(kind, n, csv, "P%d" % p, log)
        log.write("sweep end " + time.strftime("%Y-%m-%d %H:%M:%S") + "\n")
    print("SWEEP COMPLETE")


if __name__ == "__main__":
    main()
