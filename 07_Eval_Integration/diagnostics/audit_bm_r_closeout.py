"""Recompute the saved R-series evidence; never run or modify an index."""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EVAL = ROOT / "07_Eval_Integration"


def read_json(path: Path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def read_trace(rows_path: Path, ops_path: Path):
    rows = rows_path.read_bytes()
    ops = ops_path.read_bytes()
    assert rows[:8] == b"SIBROW1\0" and ops[:8] == b"SIBOPS1\0"
    initial_count = struct.unpack_from("<Q", rows, 8)[0]
    pos, reference = 16, {}
    for _ in range(initial_count):
        size = struct.unpack_from("<Q", rows, pos)[0]
        pos += 8
        key = rows[pos:pos + size]
        pos += size
        value = struct.unpack_from("<Q", rows, pos)[0]
        pos += 8
        assert key not in reference
        reference[key] = value
    assert pos == len(rows)
    counts = dict(initial_rows=initial_count, operations=0, find_ops=0,
                  insert_ops=0, find_hits=0, insert_new=0, insert_dup=0)
    pos = 16
    for _ in range(struct.unpack_from("<Q", ops, 8)[0]):
        kind = ops[pos]
        value, limit, size = struct.unpack_from("<QQQ", ops, pos + 1)
        pos += 25
        key = ops[pos:pos + size]
        pos += size
        assert limit == 0 and kind in (1, 2)
        counts["operations"] += 1
        if kind == 1:
            counts["find_ops"] += 1
            counts["find_hits"] += key in reference
        else:
            counts["insert_ops"] += 1
            if key in reference:
                counts["insert_dup"] += 1
            else:
                counts["insert_new"] += 1
                reference[key] = value
    assert pos == len(ops)
    counts.update(net_growth=len(reference) - initial_count, scan_rows=len(reference))
    digest = hashlib.sha256()
    for key, value in sorted(reference.items()):
        digest.update(struct.pack("<Q", len(key)))
        digest.update(key)
        digest.update(struct.pack("<Q", value))
    return counts, digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    assert output.is_relative_to(ROOT), "output must stay in the workspace"
    assert not output.exists(), "preserve earlier audit outputs"
    result_root = EVAL / "results"
    r1 = result_root / "bm_r1_20261020"
    r2 = result_root / "bm_r2_20261020"
    r3 = result_root / "bm_r3_sizing_20261020"
    traces = result_root / "bm_p3_dynamic_20261020" / "traces"
    sources = []
    records = []
    for r2_path in sorted(r2.glob("*_growth.json")):
        cell = r2_path.stem
        stage = read_json(r2_path)
        copy_path = r3 / f"{cell}.json"
        copying = read_json(copy_path)
        arms = {}
        for arm in ("bm", "art", "ctx16"):
            path = r1 / f"{cell}_{arm}.json"
            arms[arm] = read_json(path)
            sources.append(path)
        rows_path = traces / f"{cell}_rows.bin"
        ops_path = traces / f"{cell}_ops.bin"
        expected, final_map_sha = read_trace(rows_path, ops_path)
        for arm in arms.values():
            assert arm["mismatches"] == 0
            assert all(arm[key] == value for key, value in expected.items())
        assert stage["mismatches"] == 0
        assert stage["inserted"] == expected["insert_new"]
        assert stage["rebuilds"] == copying["rebuilds"]
        assert stage["write_ns"] == stage["batch_ns"] - stage["find_ns"]
        assert stage["inplace"] + stage["rebuilds"] == stage["inserted"]
        # Source invariant: insert-only, no coalescing; a rebuild yields 1 or 2 pages.
        splits = stage["pages_after"] - stage["pages_before"]
        single_page = stage["rebuilds"] - splits
        write_ratio = arms["bm"]["write_us"] / arms["art"]["write_us"]
        fraction = stage["page_ns"] / stage["write_ns"]
        records.append({
            "cell": cell, "expected_counts": expected,
            "oracle_final_map_sha256": final_map_sha,
            "saved_find_bm_over_art": arms["bm"]["find_us"] / arms["art"]["find_us"],
            "saved_write_bm_over_art": write_ratio,
            "saved_write_bm_over_context16": arms["bm"]["write_us"] / arms["ctx16"]["write_us"],
            "illustrative_reduction_to_art_parity": 1 - 1 / write_ratio,
            "rebuilds": stage["rebuilds"], "derived_two_page_splits": splits,
            "derived_single_page_rebuilds": single_page,
            "derived_single_page_fraction": single_page / stage["rebuilds"],
            "single_page_cause": "not recorded; cannot distinguish block/arena/prefix/empty",
            "instrumented_page_fraction_of_batch_minus_find": fraction,
            "copy_microbenchmark_over_other_run_write_time": copying["copy_ns"] / stage["write_ns"],
            "illustrative_speedup_if_timed_page_region_were_free": 1 / (1 - fraction),
        })
        sources.extend((r2_path, copy_path, rows_path, ops_path))
    assert len(records) == 4
    for name in ("bench_bm_r1_growth.cpp", "bench_bm_r2_split.cpp", "bench_bm_r3_sizing.cpp",
                 "bm_timed_index.hpp", "bm_bounded_index.hpp", "bm_page_core.hpp",
                 "bm_page_root.hpp", "bm_experiment_adapter.hpp", "check_bm_r0_closing.cpp"):
        sources.append(EVAL / "diagnostics" / name)
    sources.extend((result_root / "bm_r0_20261020" / "manifest.json",
                    result_root / "bm_r0_closing_20261020" / "summary.json"))
    source_hashes = {path.relative_to(ROOT).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
                     for path in sorted(set(sources))}
    result = {
        "schema": "bm-r-closeout-readonly-audit-v1",
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "performance_runs_executed": 0,
        "records": records,
        "scope": [
            "counts independently reconstructed from frozen byte traces",
            "ratios recomputed from saved per-cell records, not cross-cell ranges",
            "split counts derived from the current insert-only source invariant, not historical reason counters",
            "current source hashes are observations, not proof of historical binary identity",
            "saved timings include oracle work; Amdahl and parity calculations are illustrative only",
            "copy microbenchmark is neither an upper nor a lower bound on removable index cost",
        ],
        "source_sha256": source_hashes,
    }
    output.mkdir(parents=True)
    (output / "summary.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    for record in records:
        print(f"{record['cell']}: write/ART={record['saved_write_bm_over_art']:.3f}, "
              f"find/ART={record['saved_find_bm_over_art']:.3f}, "
              f"single-page={record['derived_single_page_rebuilds']}/{record['rebuilds']}")
    print(f"Verified {len(records)} cells, {len(source_hashes)} input hashes; no performance run.")


if __name__ == "__main__":
    main()
