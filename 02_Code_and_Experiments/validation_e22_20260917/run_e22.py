#!/usr/bin/env python3
"""E22 sweep driver.

Three independent processes on the clean build, then one diagnostic pass per
dataset for ART's running footprint.

Each dataset runs all three workloads, and each workload starts from its own fresh
initial state, so nothing carries over between them.

The CSV slot is passed EXPLICITLY even when empty: omitting it shifts the run tag
into the CSV argument and the tag is silently lost.

Usage:  python3 run_e22.py [--processes 3]
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

CLEAN = "bench_e22"
MEM = "bench_e22_mem"


def invoke(kind, n, csv, tag, log, target=CLEAN):
    args = [sys.executable, "tools_build.py", "run", target, kind, n, csv, tag]
    t0 = time.time()
    r = subprocess.run(args, cwd=HERE, capture_output=True, text=True)
    dt = time.time() - t0
    line = "%s %s %s tag=%s rc=%d wall=%.0fs" % (target, kind, n, tag, r.returncode, dt)
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

    out = os.path.join(HERE, "results", "main", "e22_dynamic.jsonl")
    if os.path.exists(out):
        os.remove(out)

    with open(os.path.join(HERE, "logs", "run_e22.log"), "w", encoding="utf-8") as log:
        log.write("sweep start " + time.strftime("%Y-%m-%d %H:%M:%S") + "\n")
        invoke(WARMUP[0], WARMUP[1], WARMUP[2], "warmup-P0", log)
        for p in range(1, a.processes + 1):
            log.write("--- process P%d, clean build ---\n" % p)
            for kind, n, csv in DATASETS:
                invoke(kind, n, csv, "P%d" % p, log)
        # ART's RUNNING footprint, one pass per dataset. The timings in these records
        # are inflated by the wrappers and are not used; only the space readings are.
        log.write("--- diagnostic build, ART space over time ---\n")
        for kind, n, csv in DATASETS:
            invoke(kind, n, csv, "MEM", log, target=MEM)
        log.write("sweep end " + time.strftime("%Y-%m-%d %H:%M:%S") + "\n")
    print("SWEEP COMPLETE")


if __name__ == "__main__":
    main()
