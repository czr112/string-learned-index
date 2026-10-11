"""Independent re-derivation of the GD entry gates, from the recorded raw fields.

This script shares no code with run_geometry_entry.py: it reads the published
work.json / clean.json / shape.json / identity.json and recomputes every entry
condition from the raw numbers, so a gate that only exists as a device boolean
cannot pass by assertion. It re-hashes every file the identity record names.
Read-only; prints one JSON verdict.
"""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "results/geometry_entry_20261011"
COUNTERS = ("steps", "subblocks_visited", "candidate_blocks", "simd_batches", "tag_candidates",
            "str_cmps_primary", "str_cmps_overflow", "overflow_checks", "overflow_entered",
            "locate_probes", "locate_cmp_bytes", "source", "found", "value")


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    work = json.loads((OUT / "work.json").read_text())
    clean = json.loads((OUT / "clean.json").read_text())
    shape = json.loads((OUT / "shape.json").read_text())
    identity = json.loads((OUT / "identity.json").read_text())
    verdict = {"cells": [], "problems": [], "identity_mismatches": []}

    for row in work:
        tag = "%s/%s" % (row["corpus"], row["backend"])
        checks = {
            "slot_bytes": row["descriptor_slot_bytes"] == 24,
            "slot_align": row["descriptor_slot_align"] == 8,
            "planned": row["descriptor_planned_bytes"] == 24 * row["groups_total"],
            "resident": row["descriptor_resident_bytes"]
                        == row["descriptor_capacity_bytes"] + row["descriptor_object_bytes"],
            "scratch": row["descriptor_generation_scratch_bytes"] == 0,
            "peak": row["descriptor_generation_peak_bytes"] == row["descriptor_resident_bytes"],
            "space_gate": row["gate_space"]
                          == (row["descriptor_resident_bytes"] * 20 <= row["index_bytes"]),
            "nb_sum": sum(row["nb_hist"]) == row["groups_total"],
            "groups_checked": row["groups_checked"] == row["groups_total"],
            "fields": row["field_mismatches"] == 0,
            "addresses": row["address_pairs_checked"] == row["blocks_total"],
            "address_mismatches": row["address_mismatches"] == 0,
            "queries": row["queries_checked"] == 10000,
            "rows": row["rows_checked"] == row["rows"],
            "row_values": row["row_value_mismatches"] == 0,
            "counter_diffs": all(row["counter_diffs"][f] == 0 for f in COUNTERS),
            "row_counter_diffs": row["row_counter_diffs"] == 0,
            "arms_equal": len({row["arm_checksums"][k] for k in ("b0", "b1", "g0", "g1")}) == 1,
            "checksum": row["checksum"] == row["arm_checksums"]["g0"],
            "hot_reads": row["hot_reads"] > 0 and row["hot_reads_baseline"] == 0,
            "gate_fields": row["gate_fields"] is True and row["field_mismatches"] == 0,
            "gate_address": row["gate_address"] is True and row["address_mismatches"] == 0,
            "gate_diff": row["gate_diff"] is True and row["row_value_mismatches"] == 0,
            "gate_counters": row["gate_counters"] is True,
            "gate_hot_reads": row["gate_hot_reads"] is True,
            "entry_pass": row["entry_pass"] is True,
        }
        if not all(checks.values()):
            verdict["problems"].append({"cell": tag,
                                        "failed": [k for k, v in checks.items() if not v]})
        verdict["cells"].append({"cell": tag, "checks": checks,
                                 "resident_ratio": row["descriptor_resident_bytes"]
                                                   / row["index_bytes"]})

    for row in clean:
        tag = "%s/%s" % (row["corpus"], row["backend"])
        counted = next(r for r in work if r["corpus"] == row["corpus"]
                       and r["backend"] == row["backend"])
        if row["arm_checksums"] != counted["arm_checksums"] or row["checksum"] != counted["checksum"]:
            verdict["problems"].append({"cell": tag, "clean_build": "checksum mismatch"})

    pairs = shape["pairs"]
    verdict["shape"] = {"pairs": [{"baseline_div": p["baseline"]["div_total"],
                                   "candidate_div": p["candidate"]["div_total"],
                                   "baseline_insns": p["baseline"]["instructions"],
                                   "candidate_insns": p["candidate"]["instructions"],
                                   "eliminated_any_div": p["eliminated_any_div"]} for p in pairs]}
    if not pairs or not all(p["candidate"]["div_total"] < p["baseline"]["div_total"] for p in pairs):
        verdict["problems"].append({"shape": "no pair eliminated an integer division"})

    for name, want in identity["sources_sha256"].items():
        if digest(ROOT.parent / name) != want:
            verdict["identity_mismatches"].append(name)
    for key, name in (("binary_sha256", "geometry_entry"),
                      ("shape_binary_sha256", "geometry_entry_clean")):
        if digest(OUT / name) != identity[key]:
            verdict["identity_mismatches"].append(str(OUT / name))

    verdict["all_pass"] = (not verdict["problems"]) and (not verdict["identity_mismatches"]) \
        and all(c["checks"]["entry_pass"] for c in verdict["cells"]) and len(work) == 6
    print(json.dumps(verdict, indent=2))
    return 0 if verdict["all_pass"] else 1


if __name__ == "__main__":
    import sys
    sys.exit(main())