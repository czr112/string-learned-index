"""PFX2 entry: six counted cells on the pinned source. Counters only, no timing inference.

Serializes the plan section 3 entry conditions that can be expressed on counters alone:
  cover  : at least 60% of the original queries served with the model-identical answer
  steps  : at least 50% of the model interval steps avoidable
  space  : planned resident table accounting within 5% of the index accounting
  diff   : full-domain table entries, corpus rows and queries all differentially checked
The analyzer re-derives every gate from the raw fields and refuses records whose fields are
missing, whose cross-field sums do not close, or whose device booleans disagree with the
recomputation. It does not overwrite an existing output directory.
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = ROOT.parent / "02_Code_and_Experiments/validation_e25_20260918"
PIN_INDEX = "640660a32f1fd1476d0d889d541c007883e5597d70e476524e3bcf2556ec8aa1"
PIN_SWITCHES = "5486909fffc61c1eef940c058eae3fe036e970f0db21c988b3b41800803f0d4e"
CORPUS_ROWS = {"mixed": 131072, "skew": 131072, "tranco": 262144}
QUERIES = 10000
COVER_MIN = (6, 10)   # hits * 10 >= queries * 6
STEPS_MIN = (1, 2)    # avoided * 2 >= total
SPACE_MAX = 20        # resident_bytes * 20 <= index_bytes


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def gates_of(row):
    if not row["enrolled"]:
        return None   # control arm: the entry conditions are not applicable
    queries = row["queries"]
    hits = row["hit_l1"] + row["hit_l2"]
    cover = hits * COVER_MIN[1] >= queries * COVER_MIN[0]
    steps = row["avoided_steps"] * STEPS_MIN[1] >= row["total_steps"] * STEPS_MIN[0]
    space = row["resident_bytes"] * SPACE_MAX <= row["index_bytes"]
    diff = bool(row["gate_diff"])
    return {"cover": cover, "steps": steps, "space": space, "diff": diff,
            "pass": cover and steps and space and diff}


def main():
    if digest(UPSTREAM / "index.hpp") != PIN_INDEX:
        raise SystemExit("index.hpp drift before PFX2 entry")
    if digest(UPSTREAM / "switches.hpp") != PIN_SWITCHES:
        raise SystemExit("switches.hpp drift before PFX2 entry")
    out = ROOT / "results/prefix2_entry_20261011"
    out.mkdir(exist_ok=False)  # refuse to overwrite
    sources = [Path(__file__), ROOT / "diagnostics/prefix2_entry.cpp",
               ROOT / "diagnostics/lookup_structure.cpp",
               ROOT.parent / "docs/NEXT_ACTION_PLAN_20261011_MODEL.md",
               ROOT / "learned-index/src/unified/runner.cpp",
               ROOT / "learned-index/src/unified/runner.h"]
    sources += sorted(UPSTREAM.rglob("*.hpp"))
    identity = {str(p.relative_to(ROOT.parent)): digest(p) for p in sources}
    log = (out / "commands.log").open("x")
    commands = []

    def run(cmd, timeout=3600):
        commands.append(cmd)
        log.write("$ " + " ".join(cmd) + "\n")
        log.flush()
        result = subprocess.run(cmd, cwd=ROOT, text=True, capture_output=True, timeout=timeout)
        log.write(result.stdout + result.stderr + f"rc={result.returncode}\n")
        log.flush()
        if result.returncode:
            raise RuntimeError(result.stderr or result.stdout or "command failed")
        return result

    binary = out / "prefix2_entry"
    run(["g++", "-O3", "-std=c++17", "-march=x86-64-v3", "-DE12_COUNT", "-DE25_DIAG",
         "-I" + str(UPSTREAM), "-Ilearned-index/src", "diagnostics/prefix2_entry.cpp",
         "learned-index/src/unified/runner.cpp", "-o", str(binary)])
    cpu = min(os.sched_getaffinity(0))
    records = []
    summaries = []
    for corpus in ("mixed", "skew", "tranco"):
        inputs = ROOT / ("results/matrix2_" + corpus) / "interchange"
        for name in ("rows.bin", "queries.bin"):
            identity[str((inputs / name).relative_to(ROOT.parent))] = digest(inputs / name)
        for backend in ("context16", "lits"):
            result = run(["taskset", "-c", str(cpu), str(binary),
                          str(inputs / "rows.bin"), str(inputs / "queries.bin"), backend])
            lines = result.stdout.splitlines()
            if len(lines) != 1 or result.stderr:
                raise RuntimeError("unexpected diagnostic output")
            row = json.loads(lines[0])
            row["corpus"] = corpus
            assert row["queries"] == QUERIES and row["rows"] == CORPUS_ROWS[corpus]
            assert sum(row["step_hist"]) == QUERIES
            assert sum(row["step_hist_found"]) + sum(row["step_hist_miss"]) == QUERIES
            if backend == "context16":
                # PFX2 served set must be exactly the early-stopping set, and the
                # cross-field sums must close; a device that silently drops queries
                # or counts them twice fails here instead of biasing a gate.
                assert row["enrolled"] is True
                assert row["hit_l1"] + row["hit_l2"] + row["fallback"] == QUERIES
                assert row["fallback"] == (row["fb_prefix"] + row["fb_empty"] + row["fb_len1"]
                                           + row["fb_row_missing"] + row["fb_row_entry"])
                assert row["hit_l1"] == row["step_hist"][1]
                assert row["hit_l2"] == row["step_hist"][2]
                assert row["avoided_steps"] == row["hit_l1"] + 2 * row["hit_l2"]
                assert row["queries_checked"] == QUERIES and row["rows_checked"] == CORPUS_ROWS[corpus]
                assert row["regions_enrolled"] == row["regions"]
                assert row["l1_probes"] == row["regions"] * 256
                assert row["l1_verified"] == row["l1_determined"]
                assert row["l2_verified"] == row["l2_determined"]
                assert row["rows_l1"] + row["rows_l2"] + row["rows_fallback"] == CORPUS_ROWS[corpus]
            else:
                # Control arm: no PFX2 involvement at all, but the work sheet must still close.
                assert row["enrolled"] is False
                assert row["regions_enrolled"] == 0 and row["l1_probes"] == 0
                assert row["planned_bytes"] == 0 and row["resident_bytes"] == 0
                assert row["queries_checked"] == QUERIES and row["rows_checked"] == CORPUS_ROWS[corpus]
            if backend == "context16":
                recomputed = gates_of(row)
                for key in ("cover", "steps", "space", "diff"):
                    assert (row["gate_" + key] is True) == recomputed[key], (corpus, backend, key)
                assert (row["entry_pass"] is True) == recomputed["pass"]
            else:
                assert row["gate_cover"] is None and row["gate_steps"] is None
                assert row["gate_space"] is None and row["gate_diff"] is True
                assert row["entry_pass"] is False
                recomputed = None
            row["gates_recomputed"] = recomputed
            records.append(row)
            (out / "work.json").write_text(json.dumps(records, indent=2) + "\n")
            summaries.append((corpus, backend, row, recomputed))
            print(corpus, backend,
                  "steps[0/1/2/3+]=", row["step_hist"][0], row["step_hist"][1], row["step_hist"][2],
                  sum(row["step_hist"][3:]),
                  "hits=", row["hit_l1"] + row["hit_l2"], "fallback=", row["fallback"],
                  "avoided=", row["avoided_steps"], "total=", row["total_steps"],
                  "rows=", row["l2_rows"], "resident=", row["resident_bytes"],
                  "gates=", recomputed, flush=True)
    assert len(records) == 6
    for corpus in ("mixed", "skew", "tranco"):
        assert len({r["checksum"] for r in records if r["corpus"] == corpus}) == 1
    assert all(digest(ROOT.parent / p) == h for p, h in identity.items()), "source/input drift"
    (out / "summary.json").write_text(json.dumps(
        [{"corpus": c, "backend": b, "gates": g,
          "hits": r["hit_l1"] + r["hit_l2"], "queries": r["queries"],
          "avoided_steps": r["avoided_steps"], "total_steps": r["total_steps"],
          "resident_bytes": r["resident_bytes"], "index_bytes": r["index_bytes"],
          "l2_rows": r["l2_rows"], "region_count": r["regions"]}
         for c, b, r, g in summaries], indent=2) + "\n")
    (out / "identity.json").write_text(json.dumps(
        {"sources_sha256": identity, "cpu": cpu, "commands": commands,
         "binary_sha256": digest(binary),
         "compiler": run(["g++", "--version"]).stdout.splitlines()[0],
         "scope": "PFX2 entry counters only; C enabled, W/S/H off, frozen switches pinned; "
                  "no speed claim; entry conditions cover>=6/10, steps>=1/2, space<=5%, diff complete"},
        indent=2) + "\n")
    log.close()
    target = next(r for c, b, r, g in summaries if c == "tranco" and b == "context16")
    print("target cell Context16/tranco:", target["gates_recomputed"], flush=True)


if __name__ == "__main__":
    main()