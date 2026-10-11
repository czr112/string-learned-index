"""Read-only audit of H's existing observations; no replacement timing runs."""
import hashlib
import json
from pathlib import Path
import statistics

ROOT = Path(__file__).resolve().parents[2]
EXP = ROOT / "02_Code_and_Experiments/validation_e25_20260918"
GRIDS = {"hab_ctx16_same_mixed": ("Context16", "same", "mixed", 131072),
         "hab_lits_same_mixed": ("LitsHpt", "same", "mixed", 131072),
         "hab_ctx16_grow_tranco": ("Context16", "grow", "tranco", 262144),
         "hab_lits_grow_tranco": ("LitsHpt", "grow", "tranco", 262144)}
METRICS = ("seg_ns", "cum_ns", "hit_ns", "miss_ns", "scan16_ns", "scan256_ns", "scan4096_ns", "index_bytes", "overflow")
FRACS = (0, 0.25, 0.5, 1, 2)
def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    output = ROOT / "04_Consolidation_and_Verification/AUDIT_H_PROTOCOL_20261011.json"
    if output.exists():
        raise RuntimeError("audit output already exists")
    cells = []
    identity = {str(p.relative_to(ROOT)): digest(p) for p in
                (Path(__file__), EXP / "bench_combo.cpp", EXP / "analyze_h_ab.py")}
    for stem, (backend, mode, kind, n) in GRIDS.items():
        path = EXP / "results/main" / (stem + ".jsonl")
        identity[str(path.relative_to(ROOT))] = digest(path)
        rows = [json.loads(line) for line in path.read_text().splitlines() if line.strip()]
        assert len(rows) == 400, (stem, "expected 400 rows")
        required = set(METRICS) | {"backend", "mode", "kind", "n", "run_tag", "rep", "position", "arm", "frac",
            "cycles", "live", "hit_hash", "miss_hash", "hit_checksum", "miss_checksum", "hit_expected",
            "hit_observed", "hit_probes", "miss_observed", "miss_probes", "hit_mismatch", "miss_mismatch",
            "scan16_mismatch", "scan256_mismatch", "scan4096_mismatch", "scan16_rows", "scan256_rows", "scan4096_rows"}
        grid = {}
        for row in rows:
            assert required <= row.keys(), (stem, "missing field")
            assert (row["backend"], row["mode"], row["kind"], row["n"]) == (backend, mode, kind, n)
            assert row["run_tag"] in {"hp1", "hp2", "hp3", "hp4", "hp5"}
            assert row["rep"] in range(4) and row["position"] in range(4)
            assert row["arm"] in {"W", "WH"} and row["frac"] in FRACS
            i = FRACS.index(row["frac"])
            previous = FRACS[i - 1] if i else 0
            assert row["live"] == n and row["cycles"] == int((row["frac"] - previous) * n)
            assert row["arm"] == ("W" if (row["position"] + row["rep"]) % 2 == 0 else "WH")
            assert row["hit_observed"] == row["hit_expected"] == row["hit_probes"] == 4096
            assert row["miss_observed"] == 0 and row["miss_probes"] == 4096
            assert all(row[key] == 0 for key in required if key.endswith("_mismatch"))
            assert all(isinstance(row[m], (int, float)) and row[m] >= 0 for m in METRICS)
            key = (row["run_tag"], row["rep"], row["frac"], row["position"])
            assert key not in grid, (stem, "duplicate cell")
            grid[key] = row
        for tag in ("hp1", "hp2", "hp3", "hp4", "hp5"):
            for rep in range(4):
                for frac in FRACS:
                    group = [grid[tag, rep, frac, pos] for pos in range(4)]
                    assert [r["arm"] for r in group].count("W") == 2
                    assert [r["arm"] for r in group].count("WH") == 2
                    for field in ("hit_hash", "miss_hash", "hit_checksum", "miss_checksum", "live",
                                  "scan16_rows", "scan256_rows", "scan4096_rows"):
                        assert len({r[field] for r in group}) == 1, (stem, field, "cross-arm drift")
        values = []
        for frac in FRACS:
            for metric in METRICS:
                medians, floors = [], []
                for tag in ("hp1", "hp2", "hp3", "hp4", "hp5"):
                    pairs, duplicates = [], []
                    for rep in range(4):
                        group = [grid[tag, rep, frac, pos] for pos in range(4)]
                        w = [r[metric] for r in group if r["arm"] == "W"]
                        h = [r[metric] for r in group if r["arm"] == "WH"]
                        if min(w) == 0:
                            continue  # zero overflow is a count, never a timing success
                        pairs.append(statistics.median(h) / statistics.median(w))
                        duplicates.append(max(w) / min(w))
                    if len(pairs) == 4:
                        medians.append(statistics.median(pairs)); floors.append(statistics.median(duplicates))
                if len(medians) == 5:
                    values.append({"frac": frac, "metric": metric, "ratio": statistics.median(medians),
                        "faster": sum(x < 1 for x in medians), "floor": statistics.median(floors),
                        "process_medians": medians})
        assert all("win_ns" not in r and "win_cycles" not in r for r in rows), "review conclusion stale"
        cells.append({"cell": stem, "rows": len(rows), "strict_evidence_checks": "passed",
                      "planned_window_measured": False, "ratios": values})
        target = next(v for v in values if v["frac"] == 2 and v["metric"] == "seg_ns")
        print(stem, "strict checks passed; whole-segment", round(target["ratio"], 4),
              "faster", target["faster"], "floor", round(target["floor"], 4), flush=True)
    output.write_text(json.dumps({"identity": identity, "cells": cells,
        "decision_scope": "H remains closed because predeclared risk/early segment conditions failed; primary last-8192 window was NOT measured"}, indent=2) + "\n")

if __name__ == "__main__":
    main()
