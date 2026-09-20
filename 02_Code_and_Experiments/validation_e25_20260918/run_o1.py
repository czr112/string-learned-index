#!/usr/bin/env python3
"""O1 sweep: three arms in one process, five independent processes.

Arms: C2 (the frozen version, the baseline) / O1 (C2 plus the stable overflow store) / ART.
The reviewer's rule for this round is that only the overflow organization changes, so the
baseline is the currently selected configuration and the E31 arms are deliberately absent
until O1 itself is shown to be worth anything.

Three arms and three repetitions means every arm occupies every position in the rotation
exactly once, so no arm is systematically first or last.

Processes run serially: these are CPU-sensitive timings and running them concurrently would
make them contend for the same core.

Usage:  python3 run_o1.py [--processes 5] [--reps 3] [--modes same,grow]
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

TARGET = "bench_long_o1"


def invoke(kind, n, reps, csv, tag, mode, log):
    args = [sys.executable, "tools_build.py", "run", TARGET, kind, n, str(reps), csv, tag, mode]

    started = time.time()
    result = subprocess.run(args, cwd=HERE, capture_output=True, text=True)
    elapsed = time.time() - started

    line = "%s %s %s reps=%d tag=%s rc=%d wall=%.0fs" % (TARGET, kind, mode, reps, tag, result.returncode, elapsed)

    print(line, flush=True)
    log.write(line + "\n")

    if result.returncode != 0:
        log.write("  stderr: " + result.stderr.strip()[-1500:] + "\n")
        log.flush()
        raise SystemExit("FAILED: " + line)

    log.flush()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--processes", type=int, default=5)
    parser.add_argument("--reps", type=int, default=3)
    parser.add_argument("--modes", default="same,grow")
    opts = parser.parse_args()

    modes = [m.strip() for m in opts.modes.split(",") if m.strip()]

    # The benchmark appends, because several processes share the file. Removing it here is
    # what keeps a re-run from mixing two sweeps.
    for mode in modes:
        out = os.path.join(HERE, "results", "main", "long_o1_%s.jsonl" % mode)
        if os.path.exists(out):
            os.remove(out)

    with open(os.path.join(HERE, "logs", "run_o1.log"), "w", encoding="utf-8") as log:
        for mode in modes:
            log.write("O1 sweep mode=%s %s\n" % (mode, time.strftime("%Y-%m-%d %H:%M:%S")))
            print("--- mode %s ---" % mode, flush=True)

            for p in range(1, opts.processes + 1):
                log.write("--- process P%d ---\n" % p)

                for kind, n, csv in DATASETS:
                    invoke(kind, n, opts.reps, csv, "P%d" % p, mode, log)

        log.write("sweep end %s\n" % time.strftime("%Y-%m-%d %H:%M:%S"))

    print("SWEEP COMPLETE")


if __name__ == "__main__":
    main()
