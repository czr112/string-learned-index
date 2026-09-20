#!/usr/bin/env python3
"""E31 item 3: the overflow-container share-of-cost sweep.

The checklist's section 5 precondition asks whether the ordered overflow vector's
movement / construction / allocation is a large share of maintenance time before any of
O1/O2/O3 is chosen. diag_ov_cost measures it; this driver runs it.

Five independent processes, three datasets, both replacement modes, run SERIALLY -- these
are CPU- and memory-sensitive measurements and concurrent runs would contend for the same
core. Each process appends one record per (kind, mode, frac) per repetition to
results/main/ov_cost_diag.jsonl.

The program is a DIAGNOSTIC build (-DE25_DIAG -DE30_TIME): the counters and clocks are
compiled in. The number it produces is a SHARE of maintenance time, not a speedup.

Usage:  python3 run_ov_cost.py [--processes 5] [--reps 3] [--modes same,grow]
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
TARGET = "diag_ov_cost"
OUT = "ov_cost_diag.jsonl"


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

    # The program appends, because five processes share the file. Removing it here is what
    # keeps a re-run from mixing two sweeps -- and what makes the record count a
    # meaningful check afterwards.
    out = os.path.join(HERE, "results", "main", OUT)
    if os.path.exists(out):
        os.remove(out)

    with open(os.path.join(HERE, "logs", "run_ov_cost.log"), "w", encoding="utf-8") as log:
        for mode in modes:
            log.write("E31 item 3 overflow sweep mode=%s %s\n" % (mode, time.strftime("%Y-%m-%d %H:%M:%S")))
            print("--- mode %s ---" % mode, flush=True)
            for p in range(1, a.processes + 1):
                log.write("--- process P%d ---\n" % p)
                for kind, n, csv in DATASETS:
                    invoke(kind, n, a.reps, csv, "P%d" % p, mode, log)
        log.write("sweep end %s\n" % time.strftime("%Y-%m-%d %H:%M:%S"))
    print("SWEEP COMPLETE")


if __name__ == "__main__":
    main()
