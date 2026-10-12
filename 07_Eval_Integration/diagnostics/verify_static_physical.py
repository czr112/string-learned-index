"""Independent integrity/ratio audit of the static round; no imports from its runner."""
import argparse
import hashlib
import json
from pathlib import Path
import statistics


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    path = args.directory.resolve()
    root = Path(__file__).resolve().parents[2]
    raw = json.loads((path / "raw.json").read_text())
    summary = json.loads((path / "summary.json").read_text())
    gates = json.loads((path / "gates.json").read_text())
    sanitizer = json.loads((path / "sanitizer.json").read_text())
    identity = json.loads((path / "identity.json").read_text())
    decision = json.loads((path / "decision.json").read_text())
    balanced = "Williams" in identity["protocol"]
    repeats = 14 if balanced else 7
    arms = {"release", "C", "C_AA", "packed_directory", "packed_predictor", "ART", "HOT"}
    assert len(summary) == len(gates) == len(sanitizer) == 6
    assert len(raw) == 6 * 5 * (7 + repeats * 7)
    pairs_checked = 0
    for cell in summary:
        key = cell["corpus"], cell["queue"]
        timings = [r for r in raw if r["phase"] == "paired" and (r["corpus"], r["queue"]) == key]
        observed = {}
        for process in range(5):
            records = [r for r in timings if r["process"] == process]
            assert len(records) == repeats * 7 and {r["arm"] for r in records} == arms
            assert len({r["checksum"] for r in records}) == 1
            assert {r["operations"] for r in records} == {200000}
            by_key = {(r["repeat"], r["arm"]): r for r in records}
            assert len(by_key) == repeats * 7
            for arm in arms:
                assert sorted(r["position"] for r in records if r["arm"] == arm) == sorted(list(range(7)) * (repeats // 7))
            if balanced:
                predecessors = {(a, b): 0 for a in arms for b in arms if a != b}
                for rep in range(repeats):
                    sequence = sorted((r for r in records if r["repeat"] == rep), key=lambda r: r["position"])
                    for a, b in zip(sequence, sequence[1:]):
                        predecessors[a["arm"], b["arm"]] += 1
                assert set(predecessors.values()) == {2}
            for ratio in cell["ratios"]:
                numerator, denominator = ratio.split("/")
                values = [by_key[rep, numerator]["elapsed_ns"] / by_key[rep, denominator]["elapsed_ns"] for rep in range(repeats)]
                observed.setdefault(ratio, []).append(statistics.median(values))
                pairs_checked += len(values)
        for ratio, values in observed.items():
            recorded = cell["ratios"][ratio]
            assert recorded["process_medians"] == values
            assert recorded["median"] == statistics.median(values)
            assert recorded["faster"] == sum(x < 1 for x in values)
            assert recorded["slower"] == sum(x > 1 for x in values)
            assert recorded["min"] == min(values) and recorded["max"] == max(values)
        floor = max(max(x, 1 / x) for x in observed["C_AA/C"])
        assert floor == cell["aa_observed_band"]
        speed, increment = cell["ratios"]["packed_predictor/C"], cell["ratios"]["packed_predictor/packed_directory"]
        assert cell["primary_speed_pass"] == (speed["median"] <= .85 and speed["faster"] >= 4 and 1 / speed["median"] > floor)
        assert cell["model_increment_pass"] == (increment["median"] <= .95 and increment["faster"] >= 4 and 1 / increment["median"] > floor)
        assert cell["clear_regression"] == (speed["median"] > 1.05 and speed["slower"] >= 4 and speed["median"] > floor)
        gate = next(g for g in gates if (g["corpus"], g["queue"]) == key)
        assert sum(v for k, v in gate["memory"].items() if k != "total") == gate["memory"]["total"]
        assert cell["space_pass"] == (gate["memory"]["total"] <= gate["baseline_bytes"])
        for row in raw:
            if row["phase"] == "gate" and (row["corpus"], row["queue"]) == key:
                if row["arm"] in ("C", "C_AA"):
                    assert row["index_bytes"] == gate["baseline_bytes"]
                if row["arm"] in ("packed_directory", "packed_predictor"):
                    assert row["index_bytes"] == gate["memory"]["total"]
                if row["arm"] == "ART":
                    assert row["index_bytes"] is None  # unknown is not zero
    assert all(s["rc"] == 0 and s["stderr_bytes"] == 0 for s in sanitizer)
    targets = [c for c in summary if c["corpus"] == "tranco"]
    assert len(targets) == 2
    expected = balanced and all(c["primary_speed_pass"] and c["model_increment_pass"] and c["space_pass"] for c in targets)
    assert decision["advance_S3"] == expected
    assert decision["generic_default_eligible"] is False
    if balanced:
        assert decision["static_scenario_candidate_eligible"] == (expected and not any(c["clear_regression"] for c in summary if c["corpus"] != "tranco"))
    for p, h in identity["source_sha256"].items():
        actual = identity.get("archived_sources", {}).get(p, p)
        assert hashlib.sha256((root / actual).read_bytes()).hexdigest() == h, p
    for corpus, inp in identity["inputs"].items():
        directory = root / "07_Eval_Integration/results" / ("matrix2_" + corpus) / "interchange"
        assert hashlib.sha256((directory / "rows.bin").read_bytes()).hexdigest() == inp["rows"]
        assert hashlib.sha256((directory / "queries.bin").read_bytes()).hexdigest() == inp["historical"]
        assert hashlib.sha256((path / (corpus + "_heldout.bin")).read_bytes()).hexdigest() == inp["heldout"]["sha256"]
    output = {"all_checks_ok": True, "cells": 6, "timing_rows": 6 * 5 * repeats * 7, "ratio_pairs_checked": pairs_checked,
              "sanitizer_cells": 6, "advance_S3": expected, "carryover_balanced": balanced,
              "verifier_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
    (path / "verification.json").write_text(json.dumps(output, indent=2) + "\n")
    print(json.dumps(output))


if __name__ == "__main__":
    main()
