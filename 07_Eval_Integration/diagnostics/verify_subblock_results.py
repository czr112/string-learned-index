"""Independent read-only recheck of Q's raw pairing, noise rule and current identity."""
import hashlib
import json
from pathlib import Path
import statistics
from collections import defaultdict

ROOT = Path(__file__).resolve().parents[1]
def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    out = ROOT / "results/subblock_prototype_20261011"
    entry = json.loads((ROOT / "results/subblock_entry_20261011/work.json").read_text())
    raw = json.loads((out / "raw.json").read_text())
    recorded = json.loads((out / "summary.json").read_text())
    decisions = json.loads((out / "decision.json").read_text())
    summary = {(s["corpus"], s["backend"]): s for s in recorded}
    expected = {(c, b) for c in ("mixed", "skew", "tranco") for b in ("context16", "lits")}
    assert len(summary) == 6 and summary.keys() == expected
    gates = {}; measurements = defaultdict(dict)
    for row in raw:
        case = (row["corpus"], row["backend"]); process = row["process"]
        assert case in expected and process in range(5)
        if row["phase"] == "gate":
            assert (case, process) not in gates; gates[case, process] = row
        else:
            assert row["phase"] == "paired"
            rep, arm = row["repeat"], row["arm"]
            assert rep in range(4) and arm in range(4)
            assert row["arm"] == (row["repeat"] + row["position"]) % 4
            assert row["operations"] == 1000000 and row["elapsed_ns"] > 0
            m = measurements[case, process]
            assert (rep, arm) not in m; m[rep, arm] = row
    assert len(gates) == len(measurements) == 30 and len(raw) == 510
    cross = []; checked_pairs = 0
    for case in sorted(expected):
        medians = {a: [] for a in (1, 2, 3)}; qd = []
        for process in range(5):
            m = measurements[case, process]
            assert len(m) == 16 and len({r["checksum"] for r in m.values()}) == 1
            for arm in range(4):
                assert sorted(m[r, arm]["position"] for r in range(4)) == list(range(4))
            for arm in (1, 2, 3):
                medians[arm].append(statistics.median(m[r, arm]["elapsed_ns"] / m[r, 0]["elapsed_ns"] for r in range(4)))
            qd.append(statistics.median(m[r, 3]["elapsed_ns"] / m[r, 2]["elapsed_ns"] for r in range(4)))
            checked_pairs += 16
        s = summary[case]
        for arm in (1, 2, 3):
            assert s[str(arm)]["process_medians"] == medians[arm]
        floor = max(medians[1]) / min(medians[1]); assert s["noise_floor"] == floor
        q = s["3"]
        assert s["clear_gain"] == (q["faster"] >= 4 and 1 / q["median"] > floor)
        assert s["clear_regression"] == (q["faster"] <= 1 and q["median"] > floor)
        e = next(e for e in entry if (e["corpus"], e["backend"]) == case)
        assert all(gates[case, p]["extra_bytes"] == e["planned_hot_bytes"] for p in range(5))
        cross.append({"corpus": case[0], "backend": case[1], "process_medians": qd,
                      "median": statistics.median(qd), "faster": sum(v < 1 for v in qd)})
    for b in ("context16", "lits"):
        accept = summary["tranco", b]["clear_gain"] and not any(summary[c, b]["clear_regression"] for c in ("mixed", "skew"))
        assert decisions[b]["integrate"] == accept == False
    for directory in (out, ROOT / "results/subblock_entry_20261011"):
        identity = json.loads((directory / "identity.json").read_text())
        assert all(digest(ROOT.parent / p) == h for p, h in identity["sources_sha256"].items()
                   if not p.endswith("NEXT_ACTION_PLAN_20261011_Q.md")), "source/input drift"
        # The Q plan gained an explicit timing-floor formula between entry and A/B,
        # before timing started. The A/B copy must match the current predeclared plan.
        if directory == out:
            p = ROOT.parent / "docs/NEXT_ACTION_PLAN_20261011_Q.md"
            assert digest(p) == identity["sources_sha256"][str(p.relative_to(ROOT.parent))]
    san = json.loads((out / "sanitizer.json").read_text())
    assert len(san) == 6 and {(s["corpus"], s["backend"]) for s in san} == expected
    report = {"paired_rows_checked": checked_pairs, "processes": 30, "cells": 6,
              "decisions_reproduced": True, "identity_checked": True, "q_vs_ordinary": cross,
              "scope": "read-only post-run audit; no change to predeclared selection metrics"}
    with (out / "verification.json").open("x") as f:
        json.dump(report, f, indent=2); f.write("\n")
    print("Q independently rechecked: 480 paired rows, 30 gates, 6 sanitizer cells; both stop decisions reproduced")

if __name__ == "__main__":
    main()
