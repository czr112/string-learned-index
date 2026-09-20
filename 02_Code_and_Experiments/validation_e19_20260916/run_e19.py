#!/usr/bin/env python3
"""E19 sweep driver.

Five independent processes on the CLEAN build (headline timings), then three
diagnostic passes per dataset, tagged DIAG so the analysis keeps them out of every
timing table.

The clean build has no counters and no allocator accounting. The diagnostic build
has both, and exists to report prediction counts, allocation counts and the
transient peak per variant.

Usage:  python3 run_e19.py [--processes 5] [--diag 3]
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

WARMUP = ("mixed", "1024", "")

CLEAN = "bench_e19"
DIAG = "bench_e19_diag"


def invoke(target, kind, n, csv, tag, log):
    # The CSV slot is always passed EXPLICITLY, even when empty: omitting it shifts
    # the run tag into the CSV argument and the tag is silently lost.
    args = [sys.executable, "tools_build.py", "run", target, kind, n, csv, tag]
    t0 = time.time()
    r = subprocess.run(args, cwd=HERE, capture_output=True, text=True)
    dt = time.time() - t0
    line = f"{target} {kind} {n} tag={tag} rc={r.returncode} wall={dt:.1f}s"
    print(line, flush=True)
    log.write(line + "\n")
    if r.stdout.strip():
        log.write("  stdout: " + r.stdout.strip() + "\n")
    if r.stderr.strip():
        log.write("  stderr: " + r.stderr.strip().replace("\n", " | ")[-1500:] + "\n")
    if r.returncode != 0:
        log.flush()
        raise SystemExit(f"FAILED: {line}")
    log.flush()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--processes", type=int, default=5)
    ap.add_argument("--diag", type=int, default=3)
    a = ap.parse_args()

    with open(os.path.join(HERE, "logs", "run_e19.log"), "w", encoding="utf-8") as log:
        log.write(f"sweep start {time.strftime('%Y-%m-%d %H:%M:%S')}\n")
        for p in range(1, a.processes + 1):
            log.write(f"--- process P{p}, clean build ---\n")
            log.flush()
            invoke(CLEAN, WARMUP[0], WARMUP[1], WARMUP[2], f"warmup-P{p}", log)
            for kind, n, csv in DATASETS:
                invoke(CLEAN, kind, n, csv, f"P{p}", log)
        log.write(f"--- diagnostic build, {a.diag} passes per dataset ---\n")
        for _ in range(a.diag):
            for kind, n, csv in DATASETS:
                invoke(DIAG, kind, n, csv, "DIAG", log)
        log.write(f"sweep end {time.strftime('%Y-%m-%d %H:%M:%S')}\n")
    print("SWEEP COMPLETE")


if __name__ == "__main__":
    main()
