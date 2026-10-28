"""Read-only reproducibility check for the committed 100k v3 traces (Linux/WSL).

Regenerates the eight 100k cells with the parameterised audited generator into a NEW
directory (never touching bm_audited_100k_v3_20261025) and compares every cell's
rows/operations SHA-256 plus the audit numbers against the committed manifest.
Exit 0 with verification.json written on full agreement; exit 1 on any mismatch.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EV = ROOT / "07_Eval_Integration" / "results"
ORIGINAL = EV / "bm_audited_100k_v3_20261025"
REPRO = EV / "bm_audited_100k_v3_repro_20261025"
GENERATOR = ROOT / "07_Eval_Integration" / "diagnostics" / "prepare_bm_audited_v2.py"
DATASET_ARG = ("words_100k=07_Eval_Integration/results/bm_space_gen_inputs_20261019/interchange/"
               "english_words_100000_s20261019_rows.bin")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args()
    if sys.platform == "win32":
        parser.error("Run in Linux/WSL")
    original = json.loads((ORIGINAL / "manifest.json").read_text(encoding="utf-8"))
    if REPRO.exists():
        parser.error(f"refusing to overwrite an existing repro directory: {REPRO}")
    result = subprocess.run([sys.executable, "-B", str(GENERATOR), "--dataset", DATASET_ARG,
                             "--out", REPRO.name], cwd=ROOT)
    if result.returncode:
        print("generator failed", file=sys.stderr)
        raise SystemExit(1)
    reproduced = json.loads((REPRO / "manifest.json").read_text(encoding="utf-8"))

    report = {"status": "running", "original_manifest": str((ORIGINAL / "manifest.json").relative_to(ROOT)),
              "generator": str(GENERATOR.relative_to(ROOT)), "cells": {},
              "generator_sha256": hashlib.sha256(GENERATOR.read_bytes()).hexdigest(),
              "checked_at": datetime.now(timezone.utc).isoformat()}
    mismatches = []
    if set(original) != set(reproduced):
        mismatches.append(f"cell sets differ: only-original={sorted(set(original) - set(reproduced))}, "
                          f"only-repro={sorted(set(reproduced) - set(original))}")
    for cell in sorted(set(original) & set(reproduced)):
        a, b = original[cell], reproduced[cell]
        fields = {}
        for key in ("rows_sha256", "operations_sha256", "live_before", "live_after", "net_growth",
                    "find_hit_rate_measured", "hot_write_fraction_measured"):
            same = a.get(key) == b.get(key)
            fields[key] = {"original": a.get(key), "reproduced": b.get(key), "match": same}
            if not same:
                mismatches.append(f"{cell}: {key} {a.get(key)!r} != {b.get(key)!r}")
        for key in ("successful", "kinds"):
            same = a.get(key) == b.get(key)
            fields[key] = {"match": same}
            if not same:
                mismatches.append(f"{cell}: {key} differs")
        report["cells"][cell] = fields
    report["mismatches"] = mismatches
    report["all_match"] = not mismatches
    report["status"] = "complete"
    (REPRO / "verification.json").write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n",
                                             encoding="utf-8")
    print(f"cells compared: {len(report['cells'])}; mismatches: {len(mismatches)}")
    for cell in sorted(report["cells"]):
        row = report["cells"][cell]
        print(f"  {cell:46s} rows={row['rows_sha256']['match']} ops={row['operations_sha256']['match']}")
    raise SystemExit(0 if not mismatches else 1)


if __name__ == "__main__":
    main()
