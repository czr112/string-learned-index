"""Q: six counted cells, pinned source, strict oracle checks; no timing inference."""
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = ROOT.parent / "02_Code_and_Experiments/validation_e25_20260918"
PIN = "640660a32f1fd1476d0d889d541c007883e5597d70e476524e3bcf2556ec8aa1"

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    if digest(UPSTREAM / "index.hpp") != PIN:
        raise RuntimeError("index.hpp drift before entry")
    out = ROOT / "results/subblock_entry_20261011"
    out.mkdir(exist_ok=False)
    sources = [Path(__file__), ROOT / "diagnostics/subblock_entry.cpp",
               ROOT / "diagnostics/lookup_structure.cpp", ROOT.parent / "docs/NEXT_ACTION_PLAN_20261011_Q.md",
               ROOT / "learned-index/src/unified/runner.cpp", ROOT / "learned-index/src/unified/runner.h"]
    sources += sorted(UPSTREAM.rglob("*.hpp"))
    identity = {str(p.relative_to(ROOT.parent)): digest(p) for p in sources}
    log = (out / "commands.log").open("x")
    commands = []
    def run(cmd):
        commands.append(cmd)
        log.write("$ " + " ".join(cmd) + "\n"); log.flush()
        result = subprocess.run(cmd, cwd=ROOT, text=True, capture_output=True, timeout=300)
        log.write(result.stdout + result.stderr + f"rc={result.returncode}\n"); log.flush()
        if result.returncode:
            raise RuntimeError(result.stderr or result.stdout or "command failed")
        return result
    binary = out / "subblock_entry"
    compile_cmd = ["g++", "-O3", "-std=c++17", "-march=x86-64-v3", "-DE12_COUNT", "-DE25_DIAG",
                   "-I" + str(UPSTREAM), "-Ilearned-index/src", "diagnostics/subblock_entry.cpp",
                   "learned-index/src/unified/runner.cpp", "-o", str(binary)]
    run(compile_cmd)
    cpu = min(os.sched_getaffinity(0))
    records = []
    for corpus in ("mixed", "skew", "tranco"):
        inputs = ROOT / ("results/matrix2_" + corpus) / "interchange"
        for name in ("rows.bin", "queries.bin"):
            identity[str((inputs / name).relative_to(ROOT.parent))] = digest(inputs / name)
        for backend in ("context16", "lits"):
            args = [str(inputs / "rows.bin"), str(inputs / "queries.bin"), backend]
            result = run(["taskset", "-c", str(cpu), str(binary)] + args)
            lines = result.stdout.splitlines()
            if len(lines) != 1 or result.stderr:
                raise RuntimeError("unexpected diagnostic output")
            row = json.loads(lines[0]); row["corpus"] = corpus
            seq, ordinary = row["sequential"], row["ordinary_directory"]
            assert seq["queries"] == ordinary["queries"] == row["queries"] == 10000
            assert row["route_checks"] == row["rows"] + row["queries"]
            assert row["routing_probes"] == ordinary["directory_probes"]
            assert 0 <= row["full_fallbacks"] <= row["routing_probes"]
            row["avg_sequential_blocks"] = seq["primary_blocks"] / row["queries"]
            row["fallback_fraction"] = row["full_fallbacks"] / row["routing_probes"] if row["routing_probes"] else 1
            row["hot_space_fraction"] = row["planned_hot_bytes"] / row["index_bytes"]
            row["entry_pass"] = (row["avg_sequential_blocks"] > 2 and
                                 row["fallback_fraction"] <= 0.5 and row["hot_space_fraction"] <= 0.05)
            records.append(row)
            (out / "work.json").write_text(json.dumps(records, indent=2) + "\n")
            print(corpus, backend, "blocks", round(row["avg_sequential_blocks"], 3),
                  "fallback", round(row["fallback_fraction"], 4), "space", round(row["hot_space_fraction"], 4),
                  "entry", row["entry_pass"], flush=True)
    assert len(records) == 6
    for corpus in ("mixed", "skew", "tranco"):
        assert len({r["checksum"] for r in records if r["corpus"] == corpus}) == 1
    for backend in ("context16", "lits"):
        if next(r for r in records if r["corpus"] == "tranco" and r["backend"] == backend)["entry_pass"]:
            print(backend, "static prototype entry established", flush=True)
    assert all(digest(ROOT.parent / p) == h for p, h in identity.items()), "source/input drift"
    (out / "identity.json").write_text(json.dumps({"sources_sha256": identity, "cpu": cpu,
        "commands": commands, "binary_sha256": digest(binary), "compiler": run(["g++", "--version"]).stdout.splitlines()[0],
        "scope": "counter-only Q entry; no new speed claim; C enabled W/S/H disabled"}, indent=2) + "\n")
    log.close()

if __name__ == "__main__":
    main()
