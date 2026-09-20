#!/usr/bin/env python3
"""E31 ART-arm sweep: five arms in one process, five independent processes.

Why this exists: RESULTS_E31.md compares E31 against the frozen version and against
E29's C3, but the round's own plan (RESULTS_E30.md section 6) required the end-to-end
comparison to include ART "in the same round". bench_long.cpp has four Index arms and
no ART arm -- deliberately, since it answers a different question -- so the fifth arm
is a separate program, bench_long_art.cpp, built as its own target from its own source.
The four-arm source and its build record are untouched.

Processes run serially: these are CPU- and memory-sensitive timings and running them
concurrently would make them contend for the same core.

Usage:  python3 run_e31art.py [--processes 5] [--reps 3] [--modes same,grow]
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
TARGET = "bench_long_art"


def invoke(kind, n, reps, csv, tag, mode, log):
    args = [sys.executable, "tools_build.py", "run", TARGET, kind, n, str(reps), csv, tag, mode]
    t0 = time.time()
    r = subprocess.run(args, cwd=HERE, capture_output=True, text=True)
    dt = time.time() - t0
    line = "%s %s %s reps=%d tag=%s rc=%d wall=%.0fs" % (TARGET, kind, mode, reps, tag, r.returncode, dt)
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
    ap.add_argument("--reps", type=int, default=3)
    ap.add_argument("--modes", default="same,grow")
    a = ap.parse_args()
    modes = [m.strip() for m in a.modes.split(",") if m.strip()]

    # The benchmark appends, because five processes share the file. Removing it here is
    # what keeps a re-run from mixing two sweeps -- and what makes the record count a
    # meaningful check afterwards.
    for mode in modes:
        out = os.path.join(HERE, "results", "main", "long_e31art_%s.jsonl" % mode)
        if os.path.exists(out):
            os.remove(out)

    with open(os.path.join(HERE, "logs", "run_e31art.log"), "w", encoding="utf-8") as log:
        for mode in modes:
            log.write("E31 ART sweep mode=%s %s\n" % (mode, time.strftime("%Y-%m-%d %H:%M:%S")))
            print("--- mode %s ---" % mode, flush=True)
            for p in range(1, a.processes + 1):
                log.write("--- process P%d ---\n" % p)
                for kind, n, csv in DATASETS:
                    invoke(kind, n, a.reps, csv, "P%d" % p, mode, log)
        log.write("sweep end %s\n" % time.strftime("%Y-%m-%d %H:%M:%S"))
    print("SWEEP COMPLETE")


if __name__ == "__main__":
    main()
