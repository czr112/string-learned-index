"""M3 entry: six counted cells on "is the per-region reference table ever read?".

Serialises the plan's section 2 conditions (docs/NEXT_ACTION_PLAN_20261011_OWNER.md):
  identity : Delta(Model::predict) equals the number of lookups that landed in a fallback
             region -- for the query replay, the corpus replay and the construction itself
  zero     : on a cell with no fallback region that delta is exactly 0
  diff     : every corpus key and query still answers as the interchange oracle says
  space    : the theoretical savable bytes (the reference tables of LITS-mapped regions)
             are reported against the cell's accounting
The analyzer re-derives every gate from the raw fields; it refuses records whose fields are
missing or whose device booleans disagree, and refuses to overwrite an existing directory.
No timing is taken and no count here converts to a speedup.
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = ROOT.parent / "02_Code_and_Experiments/validation_e25_20260918"
PIN_INDEX = "640660a32f1fd1476d0d889d541c007883e5597d70e476524e3bcf2556ec8aa1"
PIN_SWITCHES = "5486909fffc61c1eef940c058eae3fe036e970f0db21c988b3b41800803f0d4e"
CORPUS_ROWS = {"mixed": 131072, "skew": 131072, "tranco": 262144}
QUERIES = 10000
TARGET = ("tranco", "lits")
MODEL_BYTES_PER_REGION = 16 * 258 * 4   # 16 rows x 258 u32, the trained table


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def gates_of(row):
    fallback = row["regions_fallback"]
    gates = {
        "identity": (row["query_predict_calls"] == row["query_lookups_fallback"]
                     and row["row_predict_calls"] == row["row_lookups_fallback"]),
        "zero": (fallback != 0) or (row["query_predict_calls"] == 0 and row["row_predict_calls"] == 0),
        "diff": (row["oracle_mismatch"] == 0 and row["corpus_mismatch"] == 0
                 and row["query_lookups"] == QUERIES and row["row_lookups"] == row["rows"]),
        "space": row["model_bytes_lits"] == MODEL_BYTES_PER_REGION * row["regions_lits"],
    }
    gates["pass"] = all(gates.values())
    if row["model_bytes_lits"] + row["model_bytes_fallback"] != row["model_bytes_total"]:
        raise RuntimeError("model bytes do not close")
    return gates


def main():
    if digest(UPSTREAM / "index.hpp") != PIN_INDEX:
        raise SystemExit("index.hpp drift before the M3 entry")
    if digest(UPSTREAM / "switches.hpp") != PIN_SWITCHES:
        raise SystemExit("switches.hpp drift before the M3 entry")
    out = ROOT / "results/owner_entry_20261011"
    out.mkdir(exist_ok=False)   # refuse to overwrite
    sources = [Path(__file__), ROOT / "diagnostics/owner_entry.cpp",
               ROOT / "diagnostics/lookup_structure.cpp",
               ROOT.parent / "docs/NEXT_ACTION_PLAN_20261011_OWNER.md",
               ROOT / "learned-index/src/unified/runner.cpp",
               ROOT / "learned-index/src/unified/runner.h", UPSTREAM / "index.hpp",
               UPSTREAM / "switches.hpp"]
    identity = {str(p.relative_to(ROOT.parent)): digest(p) for p in sources}
    for corpus in CORPUS_ROWS:
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

    binary = out / "owner_entry"
    run(["g++", "-O3", "-std=c++17", "-march=x86-64-v3", "-DE12_COUNT", "-DE25_DIAG",
         "-I" + str(UPSTREAM), "-Ilearned-index/src", "diagnostics/owner_entry.cpp",
         "learned-index/src/unified/runner.cpp", "-o", str(binary)])
    cpu = min(os.sched_getaffinity(0))
    records, summaries = [], []
    for corpus in ("mixed", "skew", "tranco"):
        p = ROOT / ("results/matrix2_" + corpus) / "interchange"
        for backend in ("context16", "lits"):
            result = run(["taskset", "-c", str(cpu), str(binary), str(p / "rows.bin"),
                          str(p / "queries.bin"), backend])
            if result.stderr:
                raise RuntimeError("unexpected diagnostic stderr")
            lines = result.stdout.splitlines()
            if len(lines) != 1:
                raise RuntimeError("unexpected diagnostic output: %d lines" % len(lines))
            row = json.loads(lines[0])
            row.update(corpus=corpus, backend=backend)
            if row["rows"] != CORPUS_ROWS[corpus] or row["queries"] != QUERIES:
                raise RuntimeError("row/query count differs from the interchange pin")
            gates = gates_of(row)
            for key in ("identity", "zero", "diff"):
                if bool(row["gate_" + key]) != gates[key]:
                    raise RuntimeError("device boolean %s disagrees with the recomputation" % key)
            if bool(row["entry_pass"]) != gates["pass"]:
                raise RuntimeError("entry_pass disagrees with the recomputation")
            row["gates_recomputed"] = gates
            row["savable_ratio"] = row["model_bytes_lits"] / row["index_bytes"]
            records.append(row)
            (out / "work.json").write_text(json.dumps(records, indent=2) + "\n")
            summaries.append({"corpus": corpus, "backend": backend, "gates": gates,
                              "regions_lits": row["regions_lits"],
                              "regions_fallback": row["regions_fallback"],
                              "model_bytes_lits": row["model_bytes_lits"],
                              "savable_ratio": row["savable_ratio"],
                              "checksum": row["checksum"]})
            print(corpus, backend, "lits=%d/%d" % (row["regions_lits"], row["regions"]),
                  "savable=%d B (%.3f%%)" % (row["model_bytes_lits"],
                                             100.0 * row["savable_ratio"]),
                  "identity=Q%d/%d R%d/%d B%d" % (row["query_predict_calls"],
                                                   row["query_lookups_fallback"],
                                                   row["row_predict_calls"],
                                                   row["row_lookups_fallback"],
                                                   row["build_predict_calls"]),
                  gates, flush=True)
    assert len(records) == 6
    for corpus in CORPUS_ROWS:
        if len({r["checksum"] for r in records if r["corpus"] == corpus}) != 1:
            raise RuntimeError("backends disagree on the answer sequence for " + corpus)
    target = next(s for s in summaries if s["corpus"] == TARGET[0] and s["backend"] == TARGET[1])
    summary = {"target": "%s/%s" % TARGET, "cells": summaries,
               "target_pass": target["gates"]["pass"],
               "target_savable_bytes": target["model_bytes_lits"],
               "target_savable_ratio": target["savable_ratio"],
               "all_cells_pass": all(s["gates"]["pass"] for s in summaries)}
    (out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    for p, h in identity.items():
        if digest(ROOT.parent / p) != h:
            raise SystemExit("source/input drift after the M3 entry: " + p)
    if digest(UPSTREAM / "index.hpp") != PIN_INDEX:
        raise SystemExit("index.hpp drift after the M3 entry")
    (out / "identity.json").write_text(json.dumps(
        {"sources_sha256": identity, "cpu": cpu, "commands": commands,
         "binary_sha256": digest(binary),
         "compiler": run(["g++", "--version"]).stdout.splitlines()[0],
         "entry_from": PIN_INDEX,
         "scope": "M3 entry counters only; C on, W/S/H off, ordinary bulk_load, standard query "
                  "mode; the identity Delta(Model::predict) == fallback-region lookups is the "
                  "falsifiable claim; no speed claim and no source change"},
        indent=2) + "\n")
    log.close()
    print("target", summary["target"], "pass", summary["target_pass"],
          "savable", summary["target_savable_bytes"], "bytes",
          "all_cells_pass", summary["all_cells_pass"], flush=True)


if __name__ == "__main__":
    main()