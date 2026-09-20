#!/usr/bin/env python3
"""E17 sweep driver.

Five independent processes, each warming the binary on a small dataset (tagged so
those records are excluded -- the first execution of a fresh binary pays its own
page-in cost) and then running every measured dataset.

One target, one build: the CLEAN one. No allocator wrapping and no accounting
anywhere, because the probe path allocates far more than the statistics path and
bookkeeping inside the timed region would inflate the path under test.

The comparison that matters -- B, A and C against each other, with ratios formed
within a repetition -- happens INSIDE each run; this driver only supplies the
repetitions across processes.

Usage:  python3 run_e17.py [--processes 5]
"""
import argparse
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))

# Dataset sizes are E13's, unchanged, so the three paths describe the same three
# builds the earlier rounds reported on.
DATASETS = [
    ("mixed", "131072", ""),
    ("skew", "131072", ""),
    ("tranco", "262144", "../validation_e5_20260913/data/tranco_XN6XN.csv"),
]

WARMUP = ("mixed", "1024", "")

TARGET = "bench_e17"


def invoke(kind, n, csv, tag, log):
    # The CSV slot is always passed EXPLICITLY, even when empty: omitting it shifts
    # the run tag into the CSV argument and the tag is silently lost.
    args = [sys.executable, "tools_build.py", "run", TARGET, kind, n, csv, tag]
    t0 = time.time()
    r = subprocess.run(args, cwd=HERE, capture_output=True, text=True)
    dt = time.time() - t0
    line = f"{TARGET} {kind} {n} tag={tag} rc={r.returncode} wall={dt:.1f}s"
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
    a = ap.parse_args()

    with open(os.path.join(HERE, "logs", "run_e17.log"), "w", encoding="utf-8") as log:
        log.write(f"sweep start {time.strftime('%Y-%m-%d %H:%M:%S')}\n")
        for p in range(1, a.processes + 1):
            log.write(f"--- process P{p} ---\n")
            log.flush()
            invoke(WARMUP[0], WARMUP[1], WARMUP[2], f"warmup-P{p}", log)
            for kind, n, csv in DATASETS:
                invoke(kind, n, csv, f"P{p}", log)
        log.write(f"sweep end {time.strftime('%Y-%m-%d %H:%M:%S')}\n")
    print("SWEEP COMPLETE")


if __name__ == "__main__":
    main()
