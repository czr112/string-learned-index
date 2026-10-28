"""Recompute N-series report claims from saved records; no performance runs."""
from __future__ import annotations

import argparse
import hashlib
import json
import statistics
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RESULTS = ROOT / "07_Eval_Integration/results"


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    assert output.is_relative_to(ROOT) and not output.exists()
    inputs = []
    manifest_path = RESULTS / "bm_n_series_manifest_20261023/manifest.json"
    manifest = read(manifest_path)
    for name, expected in manifest["sources"].items():
        source = ROOT / name
        assert hashlib.sha256(source.read_bytes()).hexdigest() == expected, name
        inputs.append(source)
    inputs.append(manifest_path)
    paired = []
    for path in sorted((RESULTS / "bm_n3_paired_20261022").glob("*.jsonl")):
        rows = [json.loads(line) for line in path.read_text(encoding="utf-8-sig").splitlines() if line.strip()]
        by = {arm: [row for row in rows if row["arm"] == arm]
              for arm in ("bm", "bm_split", "art", "ctx16")}
        assert all(len(records) == 5 for records in by.values())
        assert all(row["mismatches"] == 0 for row in rows)
        med = {arm: {field: statistics.median(row[field] for row in records)
                     for field in ("write_us", "find_us", "index_bytes")}
               for arm, records in by.items()}
        b, s, art = (med[arm] for arm in ("bm", "bm_split", "art"))
        paired.append({
            "cell": path.stem, "medians": med,
            "candidate_over_baseline_write": s["write_us"] / b["write_us"],
            "baseline_over_art_write": b["write_us"] / art["write_us"],
            "candidate_over_art_write": s["write_us"] / art["write_us"],
            "candidate_over_baseline_find": s["find_us"] / b["find_us"],
            "self_report_memory_increase_fraction": s["index_bytes"] / b["index_bytes"] - 1,
        })
        inputs.append(path)
    matrices = {}
    for phase, folder in (("N4", "bm_n4_matrix_20261022"), ("N5", "bm_n5_100k_20261023")):
        path = RESULTS / folder / "records.json"
        grouped = {}
        for record in read(path):
            assert record["mismatches"] == 0
            grouped.setdefault(record["cell"], {})[record["arm"]] = record
        computed = []
        for cell, by in grouped.items():
            b, s, art = (by[arm] for arm in ("bm", "bm_split", "art"))
            counts = ("initial_rows", "find_ops", "find_hits", "insert_ops", "insert_new",
                      "insert_dup", "update_ops", "update_hits", "erase_ops", "erase_hits", "net_growth")
            assert all(all(row[field] == s[field] for field in counts) for row in by.values())
            computed.append({
                "cell": cell,
                "candidate_over_baseline_write": s["write_us"] / b["write_us"],
                "candidate_over_art_write": s["write_us"] / art["write_us"],
                "self_report_memory_increase_fraction": s["index_bytes"] / b["index_bytes"] - 1,
                "candidate_self_bytes": s["index_bytes"], "baseline_self_bytes": b["index_bytes"],
                "operation_counts": {field: s[field] for field in counts},
                "find_hit_fraction": s["find_hits"] / s["find_ops"],
                "duplicate_insert_fraction": s["insert_dup"] / s["insert_ops"],
            })
        matrices[phase] = computed
        inputs.extend((path, RESULTS / folder / "identity.json"))
    probe_dir = RESULTS / "bm_n_series_review_20261023"
    probe = read(probe_dir / "alloc_failure_probe.json")
    inputs.extend(probe_dir / name for name in ("alloc_failure_probe.cpp", "alloc_failure_probe", "alloc_failure_probe.json"))
    inputs.extend(ROOT / name for name in (
        "07_Eval_Integration/diagnostics/bench_bm_r1_growth.cpp",
        "07_Eval_Integration/diagnostics/gate_bm_n3.cpp",
        "07_Eval_Integration/diagnostics/gate_bm_n3_space.cpp",
        "07_Eval_Integration/diagnostics/run_bm_n4.py",
        "07_Eval_Integration/diagnostics/run_bm_n5.py",
        "tools/n3_paired.sh",
    ))
    result = {
        "review_date": "2026-10-23", "reviewed_commit": "0d1a7de",
        "candidate_manifest_sources_match": True,
        "new_performance_runs": 0, "new_semantic_fault_probes": 1,
        "N3": paired, "matrices": matrices, "allocation_failure_probe": probe,
        "interpretation": [
            "timings include oracle work, no A/A or Williams rotation",
            "index_bytes is the implementation self-report, not an allocation ledger",
            "replacement write_us combines insert, update and erase attempts",
            "saved zero mismatches validate the actual traces, not the intended workload specification",
            "probe exit zero means reproduced rollback defect, not successful rollback",
        ],
        "source_sha256": {path.relative_to(ROOT).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
                          for path in sorted(set(inputs))},
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Recomputed {len(paired)} N3 cells, {len(matrices['N4'])} N4 cells, {len(matrices['N5'])} N5 cells.")
    print(f"Candidate manifest matches; {len(result['source_sha256'])} input/artifact hashes saved.")
    print(f"Rollback preserved: {probe['rollback_preserved']}; old keys missing: {probe['missing_old_rows_after_failure']}.")


if __name__ == "__main__":
    main()
