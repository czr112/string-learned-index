#!/usr/bin/env python3
"""E37 scan sweep: S2 / S2R0 / S2R1 / ARR, five independent processes.

Answers the acceptance row for scan: does draining overflow change what a range scan costs.
Migration moves records between the primary area and the container, and a scan merges the
container in as it walks groups, so this is one of the places the change could show up.

The pair that carries the answer is S2R1 against S2R0, one switch apart. ARR is the standing
baseline, for scale only.

Usage:  python3 run_scan_migr.py [--processes 5] [--reps 5]
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

TARGET = "bench_scan_s5"
OUTPUT = os.path.join(HERE, "results", "main", "e37_scan_migr.jsonl")
LOG = os.path.join(HERE, "logs", "run_scan_migr.log")


def invoke(kind, n, reps, csv, tag, log):
    args = [sys.executable, "tools_build.py", "run_current", TARGET, kind, n, str(reps), csv, tag]

    started = time.time()
    result = subprocess.run(args, cwd=HERE, capture_output=True, text=True)
    elapsed = time.time() - started

    line = "%s %s reps=%d tag=%s rc=%d wall=%.0fs" % (TARGET, kind, reps, tag, result.returncode, elapsed)

    print(line, flush=True)
    log.write(line + "\n")

    if result.returncode != 0:
        log.write("  stderr: " + result.stderr.strip()[-2500:] + "\n")
        log.flush()
        raise SystemExit("FAILED: " + line)

    log.flush()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--processes", type=int, default=5)
    parser.add_argument("--reps", type=int, default=5)
    opts = parser.parse_args()

    if os.path.exists(OUTPUT):
        os.remove(OUTPUT)

    with open(LOG, "w", encoding="utf-8") as log:
        log.write("scan migration sweep %s\n" % time.strftime("%Y-%m-%d %H:%M:%S"))

        for p in range(1, opts.processes + 1):
            log.write("--- process P%d ---\n" % p)

            for kind, n, csv in DATASETS:
                invoke(kind, n, opts.reps, csv, "P%d" % p, log)

        log.write("sweep end %s\n" % time.strftime("%Y-%m-%d %H:%M:%S"))

    print("SCAN MIGRATION SWEEP COMPLETE")


if __name__ == "__main__":
    main()
