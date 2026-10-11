"""Independent re-derivation of the M3 entry gates from the recorded raw fields.

Shares no code with run_owner_entry.py: recomputes every condition from the raw numbers and
re-hashes everything the identity record names. Read-only; prints one JSON verdict.
"""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "results/owner_entry_20261011"
MODEL_BYTES_PER_REGION = 16 * 258 * 4


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
            "identity_queries": row["query_predict_calls"] == row["query_lookups_fallback"],
            "identity_rows": row["row_predict_calls"] == row["row_lookups_fallback"],
            "identity_erase": row["erase_predict_calls"] == row["erase_ops_fallback"],
            "scan_zero_or_fallback": (row["regions_fallback"] != 0
                                      or row["scan_predict_calls"] == 0),
            "zero_on_lits_only": (row["regions_fallback"] != 0
                                  or (row["query_predict_calls"] == 0
                                      and row["row_predict_calls"] == 0
                                      and row["build_predict_calls"] == 0
                                      and row["erase_predict_calls"] == 0
                                      and row["scan_predict_calls"] == 0)),
            "diff": (row["oracle_mismatch"] == 0 and row["corpus_mismatch"] == 0
                     and row["query_lookups"] == 10000
                     and row["row_lookups"] == row["rows"]),
            "bytes_close": (row["model_bytes_lits"] + row["model_bytes_fallback"]
                            == row["model_bytes_total"]),
            "bytes_per_lits_region": row["model_bytes_lits"]
                                     == MODEL_BYTES_PER_REGION * row["regions_lits"],
            "regions_close": row["regions_lits"] + row["regions_fallback"] == row["regions"],
            "entry_pass": row["entry_pass"] == (row["gate_identity"] and row["gate_zero"]
                                                and row["gate_diff"]),
        }
        if not all(checks.values()):
            verdict["problems"].append({"cell": tag,
                                        "failed": [k for k, v in checks.items() if not v]})
        verdict["cells"].append({"cell": tag, "checks": checks,
                                 "savable_ratio": row["model_bytes_lits"] / row["index_bytes"]})
    target = summary["target"]
    row = next(r for r in work if "%s/%s" % (r["corpus"], r["backend"]) == target)
    verdict["target"] = target
    verdict["target_savable_bytes"] = row["model_bytes_lits"]
    verdict["target_pass_recomputed"] = bool(
        row["gate_identity"] and row["gate_zero"] and row["gate_diff"]
        and row["model_bytes_lits"] == MODEL_BYTES_PER_REGION * row["regions_lits"])
    if verdict["target_pass_recomputed"] != summary["target_pass"]:
        verdict["problems"].append({"target_pass": "summary disagrees with recomputation"})
    if verdict["target_savable_bytes"] != summary["target_savable_bytes"]:
        verdict["problems"].append({"savable": "summary disagrees with recomputation"})
    if summary["all_cells_pass"] != all(c["checks"]["entry_pass"] for c in verdict["cells"]):
        verdict["problems"].append({"all_cells_pass": "summary disagrees with recomputation"})
    for name, want in identity["sources_sha256"].items():
        if digest(ROOT.parent / name) != want:
            verdict["identity_mismatches"].append(name)
    if digest(OUT / "owner_entry") != identity["binary_sha256"]:
        verdict["identity_mismatches"].append(str(OUT / "owner_entry"))
    verdict["all_checks_ok"] = (not verdict["problems"]) and (not verdict["identity_mismatches"])
    print(json.dumps(verdict, indent=2))
    return 0 if verdict["all_checks_ok"] else 1


if __name__ == "__main__":
    import sys
    sys.exit(main())