#!/usr/bin/env python3
"""O1 mechanism pass: the movement counters, one process per dataset.

The counters exist only under -DE25_DIAG, and the diagnostic build pays its counting cost
in the hot path -- asymmetrically, because the C2 arm never enters the stable-store branch
and so never executes those increments. That makes the diagnostic build unfit to supply the
headline timings; bench_long_o1 (the clean build) does that.

The counts themselves are deterministic functions of the workload, not measurements with
noise, so one process per dataset is enough to populate the acceptance table's mechanism
columns. The high-water figures are in here too: they are the space half of the mechanism.

Usage:  python3 run_o1_mech.py
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

MODES = ["same", "grow"]
TARGET = "diag_o1"


def main():
    # The benchmark appends; removing the files first is what keeps a re-run from mixing
    # two passes.
    for mode in MODES:
        out = os.path.join(HERE, "results", "main", "long_o1_diag_%s.jsonl" % mode)
        if os.path.exists(out):
            os.remove(out)

    with open(os.path.join(HERE, "logs", "run_o1_mech.log"), "w", encoding="utf-8") as log:
        for mode in MODES:
            for kind, n, csv in DATASETS:
                args = [sys.executable, "tools_build.py", "run", TARGET, kind, n, "1", csv, "MECH", mode]

                started = time.time()
                result = subprocess.run(args, cwd=HERE, capture_output=True, text=True)

                line = "%s %s %s reps=1 tag=MECH rc=%d wall=%.0fs" % (
                    TARGET, kind, mode, result.returncode, time.time() - started)
                print(line, flush=True)
                log.write(line + "\n")

                if result.returncode != 0:
                    log.write("  stderr: " + result.stderr.strip()[-1500:] + "\n")
                    log.flush()
                    raise SystemExit("FAILED: " + line)

    print("MECHANISM PASS COMPLETE")


if __name__ == "__main__":
    main()
