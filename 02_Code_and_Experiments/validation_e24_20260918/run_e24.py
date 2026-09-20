#!/usr/bin/env python3
"""E24 diagnostic sweep: one process per dataset, three datasets.

E24 takes no timings, so there is no process repetition and no median: every
number is a count, and counts are deterministic given the fixed seeds. The
sweep exists to run the three datasets and keep a record, not to average away
noise.

Usage:  python3 run_e24.py
"""
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


def invoke(kind, n, csv, log):
    # The CSV slot is passed EXPLICITLY even when empty: omitting it shifts the
    # tag into the CSV argument and the tag is silently lost.
    args = [sys.executable, "tools_build.py", "run", "diag_e24", kind, n, csv, ""]
    t0 = time.time()
    r = subprocess.run(args, cwd=HERE, capture_output=True, text=True)
    dt = time.time() - t0
    line = "diag_e24 %s %s rc=%d wall=%.0fs" % (kind, n, r.returncode, dt)
    print(line, flush=True)
    log.write(line + "\n")
    if r.returncode != 0:
        log.write("  stderr: " + r.stderr.strip()[-1200:] + "\n")
        log.flush()
        raise SystemExit("FAILED: " + line)
    log.flush()


def main():
    out = os.path.join(HERE, "results", "main", "e24_diag.jsonl")
    if os.path.exists(out):
        os.remove(out)

    with open(os.path.join(HERE, "logs", "run_e24.log"), "w", encoding="utf-8") as log:
        log.write("sweep start " + time.strftime("%Y-%m-%d %H:%M:%S") + "\n")
        for kind, n, csv in DATASETS:
            invoke(kind, n, csv, log)
        log.write("sweep end " + time.strftime("%Y-%m-%d %H:%M:%S") + "\n")
    print("SWEEP COMPLETE")


if __name__ == "__main__":
    main()
