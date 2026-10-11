"""GD entry: six counted cells on the pinned source, plus the code-shape check.

Serialises the plan's section 2 entry conditions:
  fields  : every descriptor field equals the live geometry it was derived from
  address : every group/sub-block address the descriptor computes is the production one
  diff    : four arms on one immutable state -- answers and every per-query counter agree
  space   : descriptor resident bytes within 5% of the index accounting
  shape   : the no-counter build keeps one integer division in the production
            instantiation and none in the descriptor instantiation
The analyzer re-derives every gate from the raw fields and refuses records whose
fields are missing or whose device booleans disagree with the recomputation. It
refuses to overwrite an existing output directory.
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from make_geometry_prototype import generate, ROOT, UPSTREAM, PIN_INDEX, PIN_SWITCHES

CORPUS_ROWS = {"mixed": 131072, "skew": 131072, "tranco": 262144}
QUERIES = 10000
COUNTER_FIELDS = ("steps", "subblocks_visited", "candidate_blocks", "simd_batches",
                  "tag_candidates", "str_cmps_primary", "str_cmps_overflow",
                  "overflow_checks", "overflow_entered", "locate_probes",
                  "locate_cmp_bytes", "source", "found", "value")
CLEAN_BOOLS = ("gate_space", "gate_fields", "gate_address", "gate_diff",
               "gate_counters", "gate_hot_reads", "entry_pass")
COUNTERS = ("descriptor_slot_bytes", "descriptor_slot_align", "groups_total", "groups_checked", "field_mismatches", "blocks_total",
            "address_pairs_checked", "address_mismatches", "multi_block_groups",
            "regions_with_multiblock", "max_groups_per_region", "min_groups_per_region",
            "max_blocks_per_group", "queries_checked", "rows_checked",
            "row_value_mismatches", "row_checksum", "descriptor_planned_bytes",
            "descriptor_capacity_bytes", "descriptor_object_bytes",
            "descriptor_resident_bytes", "descriptor_generation_scratch_bytes", "descriptor_generation_peak_bytes",
            "index_bytes", "overflow_records", "regions", "prefix_rejected",
            "lits_regions", "lits_queries")


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def gates_of(row):
    """Every entry condition, recomputed from the raw fields the device emitted."""
    groups_total = row["groups_total"]
    gates = {
        "space": row["descriptor_resident_bytes"] * 20 <= row["index_bytes"],
        "fields": row["groups_checked"] == groups_total and row["field_mismatches"] == 0,
        "address": row["address_pairs_checked"] == row["blocks_total"]
                   and row["address_mismatches"] == 0,
        "diff": row["queries_checked"] == QUERIES and row["rows_checked"] == row["rows"]
                and row["row_value_mismatches"] == 0,
        "counters": all(row["counter_diffs"][f] == 0 for f in COUNTER_FIELDS)
                    and row["row_counter_diffs"] == 0,
        "hot_reads": row["hot_reads"] > 0 and row["hot_reads_baseline"] == 0,
    }
    sum_nb = sum(row["nb_hist"])
    consistent = (
        sum_nb == groups_total
        and row["descriptor_slot_bytes"] == 24 and row["descriptor_slot_align"] == 8
        and row["descriptor_planned_bytes"] == row["descriptor_slot_bytes"] * groups_total
        and row["descriptor_generation_peak_bytes"] == row["descriptor_resident_bytes"]
        and row["descriptor_resident_bytes"]
            == row["descriptor_capacity_bytes"] + row["descriptor_object_bytes"]
        and len({row["arm_checksums"][k] for k in ("b0", "b1", "g0", "g1")}) == 1
        and row["checksum"] == row["arm_checksums"]["g0"]
    )
    if not consistent:
        raise RuntimeError("cross-field sums do not close for this record")
    for key in CLEAN_BOOLS:
        if key == "entry_pass":
            continue
        expected = gates[key[5:]] if key != "entry_pass" else False
        if bool(row[key]) != bool(expected):
            raise RuntimeError("device boolean %s disagrees with the recomputation" % key)
    gates["pass"] = all(gates.values())
    if bool(row["entry_pass"]) != gates["pass"]:
        raise RuntimeError("entry_pass disagrees with the recomputation")
    return gates


def main():
    if digest(UPSTREAM / "index.hpp") != PIN_INDEX:
        raise SystemExit("index.hpp drift before the GD entry")
    if digest(UPSTREAM / "switches.hpp") != PIN_SWITCHES:
        raise SystemExit("switches.hpp drift before the GD entry")
    out = ROOT / "results/geometry_entry_20261011"
    out.mkdir(exist_ok=False)  # refuse to overwrite

    header = generate(out / "probe")
    device = ROOT / "diagnostics/geometry_entry.cpp"
    generator = ROOT / "diagnostics/make_geometry_prototype.py"
    shape_tool = ROOT / "diagnostics/check_geometry_shape.py"
    sources = [Path(__file__), device, generator, shape_tool, header,
               ROOT / "diagnostics/lookup_structure.cpp",
               ROOT.parent / "docs/NEXT_ACTION_PLAN_20261011_GEOMETRY.md",
               ROOT / "learned-index/src/unified/runner.cpp",
               ROOT / "learned-index/src/unified/runner.h", UPSTREAM / "index.hpp",
               UPSTREAM / "switches.hpp"]
    identity = {str(p.relative_to(ROOT.parent)): digest(p) for p in sources}
    for corpus in ("mixed", "skew", "tranco"):
        for name in ("rows.bin", "queries.bin"):
            p = ROOT / ("results/matrix2_" + corpus) / "interchange" / name
            identity[str(p.relative_to(ROOT.parent))] = digest(p)

    log = (out / "commands.log").open("x")
    commands = []

    def run(cmd, timeout=3600):
        commands.append(cmd)
        log.write("$ " + " ".join(cmd) + "\n")
        log.flush()
        result = subprocess.run(cmd, cwd=ROOT, text=True, capture_output=True, timeout=timeout)
        log.write(result.stdout + result.stderr + "rc=%d\n" % result.returncode)
        log.flush()
        if result.returncode:
            raise RuntimeError(result.stderr or result.stdout or "command failed")
        return result

    includes = ["-I" + str(header.parent), "-I" + str(UPSTREAM), "-Ilearned-index/src"]
    common = ["-std=c++17", "-march=x86-64-v3"] + includes + [
        "diagnostics/geometry_entry.cpp", "learned-index/src/unified/runner.cpp"]
    counted, clean = out / "geometry_entry", out / "geometry_entry_clean"
    print("building counted device", flush=True)
    run(["g++", "-O3", "-DE12_COUNT", "-DE25_DIAG"] + common + ["-o", str(counted)])
    print("building shape device", flush=True)
    run(["g++", "-O3", "-DGD_SHAPE_PROBE"] + common + ["-o", str(clean)])

    cpu = min(os.sched_getaffinity(0))

    def arguments(corpus):
        p = ROOT / ("results/matrix2_" + corpus) / "interchange"
        return [str(p / "rows.bin"), str(p / "queries.bin")]

    def one_cell(binary, corpus, backend):
        result = run(["taskset", "-c", str(cpu), str(binary)] + arguments(corpus) + [backend])
        lines = result.stdout.splitlines()
        if len(lines) != 1:
            raise RuntimeError("unexpected diagnostic output: %d lines" % len(lines))
        if result.stderr:
            raise RuntimeError("diagnostic stderr is not empty")
        row = json.loads(lines[0])
        row["corpus"] = corpus
        row["backend"] = backend
        return row

    counted_records, summaries = [], []
    for corpus in ("mixed", "skew", "tranco"):
        for backend in ("context16", "lits"):
            row = one_cell(counted, corpus, backend)
            missing = [f for f in list(COUNTERS) + list(CLEAN_BOOLS) if f not in row]
            if missing or "arm_checksums" not in row or "counter_diffs" not in row:
                raise RuntimeError("missing fields: %r" % missing)
            if row["rows"] != CORPUS_ROWS[corpus] or row["queries"] != QUERIES:
                raise RuntimeError("row/query count differs from the interchange pin")
            for f in COUNTER_FIELDS:
                if f not in row["counter_diffs"]:
                    raise RuntimeError("counter_diffs lacks " + f)
            row["gates_recomputed"] = gates_of(row)
            counted_records.append(row)
            (out / "work.json").write_text(json.dumps(counted_records, indent=2) + "\n")
            print(corpus, backend, "gates", row["gates_recomputed"], flush=True)

    clean_records = []
    for corpus in ("mixed", "skew", "tranco"):
        for backend in ("context16", "lits"):
            row = one_cell(clean, corpus, backend)
            counted_row = next(r for r in counted_records
                               if r["corpus"] == corpus and r["backend"] == backend)
            if row["arm_checksums"] != counted_row["arm_checksums"]:
                raise RuntimeError("clean build arm checksums differ from the counted build")
            if row["checksum"] != counted_row["checksum"]:
                raise RuntimeError("clean build checksum differs from the counted build")
            if row["row_checksum"] != counted_row["row_checksum"]:
                raise RuntimeError("clean build corpus checksum differs from the counted build")
            if len({row["arm_checksums"][k] for k in ("b0", "b1", "g0", "g1")}) != 1:
                raise RuntimeError("clean build arms disagree")
            clean_records.append(row)
            (out / "clean.json").write_text(json.dumps(clean_records, indent=2) + "\n")
            print(corpus, backend, "clean build agrees", flush=True)

    shape = run(["python3", "-B", "diagnostics/check_geometry_shape.py", str(clean),
                 "--pair", "find_impl<true, false>", "find_impl<true, true>"])
    shape_json = json.loads(shape.stdout)
    (out / "shape.json").write_text(json.dumps(shape_json, indent=2) + "\n")
    for pair in shape_json["pairs"]:
        print("shape", pair["baseline_substr"], "div", pair["baseline"]["div_total"],
              "->", pair["candidate_substr"], "div", pair["candidate"]["div_total"], flush=True)

    entries = []
    for row in counted_records:
        entries.append({
            "corpus": row["corpus"], "backend": row["backend"],
            "gates": row["gates_recomputed"],
            "descriptor_resident_bytes": row["descriptor_resident_bytes"],
            "index_bytes": row["index_bytes"],
            "resident_ratio": row["descriptor_resident_bytes"] / row["index_bytes"],
            "groups_total": row["groups_total"], "blocks_total": row["blocks_total"],
            "nb_hist": row["nb_hist"], "multi_block_groups": row["multi_block_groups"],
            "max_blocks_per_group": row["max_blocks_per_group"],
            "hot_reads": row["hot_reads"], "checksum": row["checksum"],
        })
    summary = {
        "entry_all_pass": all(e["gates"]["pass"] for e in entries),
        "cells": entries,
        "shape": {"pairs": shape_json["pairs"],
                  "eliminated_any_div": all(p["eliminated_any_div"] for p in shape_json["pairs"])},
    }
    (out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")

    if digest(UPSTREAM / "index.hpp") != PIN_INDEX:
        raise SystemExit("index.hpp drift after the GD entry")
    if digest(UPSTREAM / "switches.hpp") != PIN_SWITCHES:
        raise SystemExit("switches.hpp drift after the GD entry")
    for p, h in identity.items():
        if digest(ROOT.parent / p) != h:
            raise SystemExit("source/input drift after the GD entry: " + p)
    (out / "identity.json").write_text(json.dumps(
        {"sources_sha256": identity, "cpu": cpu, "commands": commands,
         "binary_sha256": digest(counted), "shape_binary_sha256": digest(clean),
         "compiler": run(["g++", "--version"]).stdout.splitlines()[0],
         "prototype_from": PIN_INDEX,
         "scope": "GD entry counters only; C on, W/S/H off, frozen switches pinned; "
                  "no speed claim; gates fields/address/diff/counters/space/hot_reads, "
                  "plus one division in the baseline instantiation and none in the "
                  "descriptor instantiation"},
        indent=2) + "\n")
    log.close()
    print("entry_all_pass", summary["entry_all_pass"], flush=True)


if __name__ == "__main__":
    main()