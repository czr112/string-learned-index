#!/usr/bin/env python3
"""E21 sweep driver.

Five independent processes on the CLEAN build (all query timings), then one
diagnostic pass per dataset for ART's footprint.

The CSV is appended to, so the driver writes the header and clears the file once
before the sweep rather than per run. Each row carries the run tag.

Usage:  python3 run_e21.py [--processes 5]
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

CLEAN = "bench_e21"
MEM = "bench_e21_mem"

HEADER = "kind,keys,nq,seed,round,class,method,ns_per_query,bytes,bytes_known,build_ms,run_tag"


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
    if r.stderr.strip():
        log.write("  stderr: " + r.stderr.strip().replace("\n", " | ")[-800:] + "\n")
    if r.returncode != 0:
        log.flush()
        raise SystemExit(f"FAILED: {line}")
    log.flush()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--processes", type=int, default=5)
    a = ap.parse_args()

    timings = os.path.join(HERE, "results", "main", "e21_timings.csv")
    with open(timings, "w", encoding="utf-8", newline="") as f:
        f.write(HEADER + "\n")
    buildrec = os.path.join(HERE, "results", "main", "e21_build.jsonl")
    if os.path.exists(buildrec):
        os.remove(buildrec)

    with open(os.path.join(HERE, "logs", "run_e21.log"), "w", encoding="utf-8") as log:
        log.write(f"sweep start {time.strftime('%Y-%m-%d %H:%M:%S')}\n")
        for p in range(1, a.processes + 1):
            log.write(f"--- process P{p}, clean build ---\n")
            log.flush()
            invoke(CLEAN, WARMUP[0], WARMUP[1], WARMUP[2], f"warmup-P{p}", log)
            for kind, n, csv in DATASETS:
                invoke(CLEAN, kind, n, csv, f"P{p}", log)
        log.write("--- diagnostic build, ART footprint ---\n")
        for kind, n, csv in DATASETS:
            invoke(MEM, kind, n, csv, "MEM", log)
        log.write(f"sweep end {time.strftime('%Y-%m-%d %H:%M:%S')}\n")
    print("SWEEP COMPLETE")


if __name__ == "__main__":
    main()
