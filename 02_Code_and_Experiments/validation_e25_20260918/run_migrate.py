#!/usr/bin/env python3
"""E37 feasibility pass: the migration scan on all datasets and modes.

One process. The scan is deterministic given the state, and this program makes no timing
claims -- it counts -- so a second process would reproduce every column exactly.

Usage:  python3 run_migrate.py
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
TARGET = "diag_migrate"


def main():
    for mode in MODES:
        out = os.path.join(HERE, "results", "main", "migrate_diag_%s.jsonl" % mode)

        if os.path.exists(out):
            os.remove(out)

    with open(os.path.join(HERE, "logs", "run_migrate.log"), "w", encoding="utf-8") as log:
        log.write("migration feasibility pass %s\n" % time.strftime("%Y-%m-%d %H:%M:%S"))

        for mode in MODES:
            for kind, n, csv in DATASETS:
                args = [sys.executable, "tools_build.py", "run_current", TARGET,
                        kind, n, "1", csv, "MIG", mode]

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

    print("MIGRATION FEASIBILITY PASS COMPLETE")


if __name__ == "__main__":
    main()
