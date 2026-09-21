#!/usr/bin/env python3
"""E37 repair pass: repairing an already-degraded snapshot.

One process, all datasets and modes. The repair's own millisecond span is reported (that is
the measurement); everything else in this program is a counting diagnostic.

Usage:  python3 run_repair.py
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
TARGET = "diag_repair"


def main():
    for mode in MODES:
        out = os.path.join(HERE, "results", "main", "repair_diag_%s.jsonl" % mode)

        if os.path.exists(out):
            os.remove(out)

    with open(os.path.join(HERE, "logs", "run_repair.log"), "w", encoding="utf-8") as log:
        log.write("repair pass %s\n" % time.strftime("%Y-%m-%d %H:%M:%S"))

        for mode in MODES:
            for kind, n, csv in DATASETS:
                args = [sys.executable, "tools_build.py", "run_current", TARGET,
                        kind, n, "1", csv, "REPAIR", mode]

                started = time.time()
                result = subprocess.run(args, cwd=HERE, capture_output=True, text=True)

                line = "%s %s %s rc=%d wall=%.0fs" % (TARGET, kind, mode, result.returncode,
                                                      time.time() - started)

                print(line, flush=True)
                log.write(line + "\n")

                if result.returncode != 0:
                    log.write("  stderr: " + result.stderr.strip()[-2500:] + "\n")
                    log.flush()
                    raise SystemExit("FAILED: " + line)

    print("REPAIR PASS COMPLETE")


if __name__ == "__main__":
    main()
