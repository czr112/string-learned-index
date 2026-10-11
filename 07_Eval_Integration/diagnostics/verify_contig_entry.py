"""Independent re-derivation of the M2 entry gates from the recorded raw fields.

Shares no code with run_contig_entry.py: reads the published work.json / summary.json /
identity.json, recomputes every condition from the raw numbers, and re-hashes every file the
identity record names. Read-only; prints one JSON verdict.
"""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "results/contig_entry_20261011"


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    work = json.loads((OUT / "work.json").read_text())
    summary = json.loads((OUT / "summary.json").read_text())
    identity = json.loads((OUT / "identity.json").read_text())
    verdict = {"cells": [], "problems": [], "identity_mismatches": []}
    for row in work:
        tag = "%s/%s" % (row["corpus"], row["backend"])
        checks = {
            "planned_is_32_per_record": row["panel_planned_bytes"] == 32 * row["panel_records"],
            "resident": row["panel_resident_bytes"]
                        == row["panel_capacity_bytes"] + row["panel_object_bytes"],
            "heads": row["gate_heads"] == (row["queries_gt4"] * 10 >= row["queries"] * 6),
            "lines": row["gate_lines"] == (row["panel_lines"] * 10 <= row["orig_lines"] * 6),
            "space": row["gate_space"]
                     == (row["panel_resident_bytes"] * 20 <= row["index_bytes"]),
            "diff": row["gate_diff"] == (row["counter_mismatch"] == 0
                                         and row["answer_mismatch"] == 0
                                         and row["sequence_mismatch"] == 0
                                         and row["corpus_mismatch"] == 0
                                         and row["queries"] == 10000
                                         and row["corpus_keys"] == row["rows"]
                                         and row["align_violations"] == 0),
            "counters_agree": (row["sim_blocks"] == row["prod_blocks"]
                               and row["sim_batches"] == row["prod_batches"]
                               and row["sim_candidates"] == row["prod_candidates"]
                               and row["sim_candidate_blocks"] == row["prod_candidate_blocks"]
                               and row["sim_primary_cmps"] == row["prod_cmps"]),
            "hist_sums": sum(row["head_hist"]) == row["queries"],
            "entry_pass": row["entry_pass"] == all(row["gate_" + k] for k in
                                                   ("heads", "lines", "space", "diff")),
        }
        if not all(checks.values()):
            verdict["problems"].append({"cell": tag,
                                        "failed": [k for k, v in checks.items() if not v]})
        verdict["cells"].append({"cell": tag, "checks": checks,
                                 "reduction": 1.0 - row["panel_lines"] / row["orig_lines"],
                                 "gt4_fraction": row["queries_gt4"] / row["queries"]})
    target = summary["target"]
    target_row = next(r for r in work if "%s/%s" % (r["corpus"], r["backend"]) == target)
    verdict["target"] = target
    verdict["target_pass_recomputed"] = bool(target_row["gate_heads"] and target_row["gate_lines"]
                                             and target_row["gate_space"] and target_row["gate_diff"])
    if verdict["target_pass_recomputed"] != summary["target_pass"]:
        verdict["problems"].append({"target_pass": "summary disagrees with recomputation"})
    verdict["all_diff_clean"] = all(c["checks"]["diff"] for c in verdict["cells"])
    if verdict["all_diff_clean"] != summary["entry_all_diff_clean"]:
        verdict["problems"].append({"diff_clean": "summary disagrees with recomputation"})
    for name, want in identity["sources_sha256"].items():
        if digest(ROOT.parent / name) != want:
            verdict["identity_mismatches"].append(name)
    if digest(OUT / "contig_entry") != identity["binary_sha256"]:
        verdict["identity_mismatches"].append(str(OUT / "contig_entry"))
    verdict["all_checks_ok"] = (not verdict["problems"]) and (not verdict["identity_mismatches"])
    print(json.dumps(verdict, indent=2))
    return 0 if verdict["all_checks_ok"] else 1


if __name__ == "__main__":
    import sys
    sys.exit(main())