"""M2 entry: six counted cells on the cross-sub-block contiguous tag panel.

Serialises the plan's section 2 conditions (docs/NEXT_ACTION_PLAN_20261011_CONTIG.md):
  heads : at least 60% of the original queries touched more than four 64 B header lines
  lines : the panel cuts the query-weighted filtering-metadata line count by >=40%
  space : panel resident bytes within 5% of the index accounting
  diff  : per-query simulation counters equal the production Result item by item, every
          corpus key and query answer equals the interchange oracle, and the candidate
          sequence agrees between the panel copy and a recomputation from the stored suffix
The analyzer re-derives every gate from the raw fields and refuses records whose fields are
missing or whose device booleans disagree. It refuses to overwrite an existing output
directory. No timing is taken and no count here converts to a speedup.
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from make_contig_probe import generate, ROOT, UPSTREAM, PIN_INDEX, PIN_SWITCHES

CORPUS_ROWS = {"mixed": 131072, "skew": 131072, "tranco": 262144}
QUERIES = 10000
TARGET = ("mixed", "context16")
FIELDS = ("queries", "queries_gt4", "orig_lines", "panel_lines", "cold_blocks", "cold_lines",
          "anchor_lines", "sim_blocks", "sim_batches", "sim_candidates", "sim_candidate_blocks",
          "sim_primary_cmps", "counter_mismatch", "answer_mismatch", "sequence_mismatch",
          "corpus_keys", "corpus_hits", "corpus_misses", "corpus_mismatch", "panel_records",
          "align_violations", "prod_blocks", "prod_batches", "prod_candidates",
          "prod_candidate_blocks", "prod_cmps", "index_bytes", "panel_planned_bytes",
          "panel_capacity_bytes", "panel_object_bytes", "panel_resident_bytes")


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def gates_of(row):
    gates = {
        "heads": row["queries_gt4"] * 10 >= row["queries"] * 6,
        "lines": row["panel_lines"] * 10 <= row["orig_lines"] * 6,
        "space": row["panel_resident_bytes"] * 20 <= row["index_bytes"],
        "diff": (row["counter_mismatch"] == 0 and row["answer_mismatch"] == 0
                 and row["sequence_mismatch"] == 0 and row["corpus_mismatch"] == 0
                 and row["queries"] == QUERIES and row["corpus_keys"] == row["rows"]
                 and row["align_violations"] == 0
                 and row["sim_blocks"] == row["prod_blocks"]
                 and row["sim_batches"] == row["prod_batches"]
                 and row["sim_candidates"] == row["prod_candidates"]
                 and row["sim_candidate_blocks"] == row["prod_candidate_blocks"]
                 and row["sim_primary_cmps"] == row["prod_cmps"]),
    }
    gates["pass"] = all(gates.values())
    if row["panel_resident_bytes"] != row["panel_capacity_bytes"] + row["panel_object_bytes"]:
        raise RuntimeError("resident accounting does not close")
    if row["panel_planned_bytes"] != 32 * row["panel_records"]:
        raise RuntimeError("planned bytes are not 32 B per record")
    return gates


def main():
    if digest(UPSTREAM / "index.hpp") != PIN_INDEX:
        raise SystemExit("index.hpp drift before the M2 entry")
    if digest(UPSTREAM / "switches.hpp") != PIN_SWITCHES:
        raise SystemExit("switches.hpp drift before the M2 entry")
    out = ROOT / "results/contig_entry_20261011"
    out.mkdir(exist_ok=False)   # refuse to overwrite
    header = generate(out / "probe")
    sources = [Path(__file__), ROOT / "diagnostics/contig_entry.cpp",
               ROOT / "diagnostics/make_contig_probe.py", header,
               ROOT / "diagnostics/lookup_structure.cpp",
               ROOT.parent / "docs/NEXT_ACTION_PLAN_20261011_CONTIG.md",
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

    includes = ["-I" + str(header.parent), "-I" + str(UPSTREAM), "-Ilearned-index/src"]
    binary = out / "contig_entry"
    run(["g++", "-O3", "-std=c++17", "-march=x86-64-v3", "-DE12_COUNT", "-DE25_DIAG"] + includes
        + ["diagnostics/contig_entry.cpp", "learned-index/src/unified/runner.cpp",
           "-o", str(binary)])
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
            missing = [f for f in FIELDS if f not in row]
            if missing:
                raise RuntimeError("missing fields: %r" % missing)
            if row["rows"] != CORPUS_ROWS[corpus] or row["queries"] != QUERIES:
                raise RuntimeError("row/query count differs from the interchange pin")
            gates = gates_of(row)
            for key in ("heads", "lines", "space", "diff"):
                if bool(row["gate_" + key]) != gates[key]:
                    raise RuntimeError("device boolean %s disagrees with the recomputation" % key)
            if bool(row["entry_pass"]) != gates["pass"]:
                raise RuntimeError("entry_pass disagrees with the recomputation")
            row["gates_recomputed"] = gates
            records.append(row)
            (out / "work.json").write_text(json.dumps(records, indent=2) + "\n")
            summaries.append({"corpus": corpus, "backend": backend, "gates": gates,
                              "queries_gt4": row["queries_gt4"],
                              "reduction": 1.0 - row["panel_lines"] / row["orig_lines"],
                              "resident_ratio": row["panel_resident_bytes"] / row["index_bytes"],
                              "head_hist": row["head_hist"]})
            print(corpus, backend, "gt4=%d/%d" % (row["queries_gt4"], row["queries"]),
                  "reduction=%.4f" % (1.0 - row["panel_lines"] / row["orig_lines"]),
                  "resident=%.4f%%" % (100.0 * row["panel_resident_bytes"] / row["index_bytes"]),
                  gates, flush=True)
    assert len(records) == 6
    for corpus in CORPUS_ROWS:
        if len({r["checksum"] for r in records if r["corpus"] == corpus}) != 1:
            raise RuntimeError("backends disagree on the answer sequence for " + corpus)
    target = next(s for s in summaries
                  if s["corpus"] == TARGET[0] and s["backend"] == TARGET[1])
    summary = {"target": "%s/%s" % TARGET, "cells": summaries,
               "target_pass": target["gates"]["pass"],
               "target_conditions": target["gates"],
               "entry_all_diff_clean": all(s["gates"]["diff"] for s in summaries)}
    (out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    for p, h in identity.items():
        if digest(ROOT.parent / p) != h:
            raise SystemExit("source/input drift after the M2 entry: " + p)
    if digest(UPSTREAM / "index.hpp") != PIN_INDEX:
        raise SystemExit("index.hpp drift after the M2 entry")
    (out / "identity.json").write_text(json.dumps(
        {"sources_sha256": identity, "cpu": cpu, "commands": commands,
         "binary_sha256": digest(binary),
         "compiler": run(["g++", "--version"]).stdout.splitlines()[0],
         "probe_from": PIN_INDEX,
         "scope": "M2 entry counters only; C on, W/S/H off, pinned layout (1024 B, 16 slots); "
                  "panel laid out for real at 32 B per block with a 16 B aligned tag start; "
                  "address-layout proxy, not a cache-miss measurement; no speed claim"},
        indent=2) + "\n")
    log.close()
    print("target", summary["target"], "pass", summary["target_pass"],
          "all_diff_clean", summary["entry_all_diff_clean"], flush=True)


if __name__ == "__main__":
    main()