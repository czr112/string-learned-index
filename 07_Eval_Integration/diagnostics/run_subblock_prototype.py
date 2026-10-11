"""Q static A/B: oracle + sanitizer first, then 30 sequential process blocks."""
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
from make_subblock_prototype import generate, ROOT, UPSTREAM, PIN

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    entry = json.loads((ROOT / "results/subblock_entry_20261011/work.json").read_text())
    assert all(next(r for r in entry if r["corpus"] == "tranco" and r["backend"] == b)["entry_pass"]
               for b in ("context16", "lits")), "target entry did not establish both backends"
    assert digest(UPSTREAM / "index.hpp") == PIN
    out = ROOT / "results/subblock_prototype_20261011"
    out.mkdir(exist_ok=False)
    header = generate(out / "prototype")
    sources = [Path(__file__), ROOT / "diagnostics/make_subblock_prototype.py", header,
               ROOT / "diagnostics/lookup_subblock.cpp", ROOT / "diagnostics/lookup_structure.cpp",
               ROOT.parent / "docs/NEXT_ACTION_PLAN_20261011_Q.md",
               ROOT / "learned-index/src/unified/runner.cpp"]
    sources += sorted(UPSTREAM.rglob("*.hpp"))
    sources += sorted((ROOT / "learned-index/src/unified").rglob("*.h"))
    identity = {str(p.relative_to(ROOT.parent)): digest(p) for p in sources}
    for corpus in ("mixed", "skew", "tranco"):
        for name in ("rows.bin", "queries.bin"):
            p = ROOT / ("results/matrix2_" + corpus) / "interchange" / name
            identity[str(p.relative_to(ROOT.parent))] = digest(p)
    commands = []
    log = (out / "commands.log").open("x")
    def run(cmd):
        commands.append(cmd); log.write("$ " + " ".join(cmd) + "\n"); log.flush()
        result = subprocess.run(cmd, cwd=ROOT, text=True, capture_output=True, timeout=300)
        log.write(result.stdout + result.stderr + f"rc={result.returncode}\n"); log.flush()
        if result.returncode:
            raise RuntimeError(result.stderr or result.stdout or "command failed")
        return result
    includes = ["-I" + str(header.parent), "-I" + str(UPSTREAM), "-Ilearned-index/src"]
    common = ["-std=c++17", "-march=x86-64-v3"] + includes + ["diagnostics/lookup_subblock.cpp", "learned-index/src/unified/runner.cpp"]
    clean, san = out / "lookup_subblock", out / "san_lookup_subblock"
    print("building static prototype and sanitizer", flush=True)
    run(["g++", "-O3"] + common + ["-o", str(clean)])
    run(["g++", "-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"] + common + ["-o", str(san)])
    def arguments(corpus, backend, repeats):
        p = ROOT / ("results/matrix2_" + corpus) / "interchange"
        return [str(p / "rows.bin"), str(p / "queries.bin"), backend, str(repeats)]
    gates = []
    for backend in ("context16", "lits"):
        # Both ASCII corpora and the binary domain gate run with the sanitizer.
        for corpus in ("mixed", "skew", "tranco"):
            result = run([str(san)] + arguments(corpus, backend, 0))
            if result.stderr:
                raise RuntimeError("sanitizer stderr is not empty")
            row = json.loads(result.stdout); row.update(corpus=corpus, backend=backend)
            assert row["row_query_checks"] == (131072 if corpus != "tranco" else 262144) + 40000
            assert row["edge_checks"] > 3000
            gates.append(row)
            (out / "sanitizer.json").write_text(json.dumps(gates, indent=2) + "\n")
            print("sanitizer", corpus, backend, "passed", flush=True)
    cpu = min(os.sched_getaffinity(0)); raw = []; summaries = []
    print("timing starts: no concurrent experiments", flush=True)
    for corpus in ("mixed", "skew", "tranco"):
        for backend in ("context16", "lits"):
            medians = {1: [], 2: [], 3: []}
            for process in range(5):
                result = run(["taskset", "-c", str(cpu), str(clean)] + arguments(corpus, backend, 4))
                if result.stderr:
                    raise RuntimeError("unexpected timing stderr")
                rows = [json.loads(line) for line in result.stdout.splitlines()]
                assert len(rows) == 17 and rows[0]["phase"] == "gate"
                measured = rows[1:]; pairs = {(r["repeat"], r["arm"]): r for r in measured}
                assert len(pairs) == 16 and len({r["checksum"] for r in measured}) == 1
                for arm in range(4):
                    assert sorted(r["position"] for r in measured if r["arm"] == arm) == list(range(4))
                for arm in (1, 2, 3):
                    medians[arm].append(statistics.median(pairs[rep, arm]["elapsed_ns"] / pairs[rep, 0]["elapsed_ns"] for rep in range(4)))
                for row in rows:
                    row.update(corpus=corpus, backend=backend, process=process); raw.append(row)
                (out / "raw.json").write_text(json.dumps(raw, indent=2) + "\n")
                print(corpus, backend, "P" + str(process + 1), "Q/S", round(medians[3][-1], 4), flush=True)
            floor = max(medians[1]) / min(medians[1])
            summary = {"corpus": corpus, "backend": backend, "noise_floor": floor,
                       "extra_bytes": rows[0]["extra_bytes"], "baseline_bytes": rows[0]["baseline_bytes"]}
            for arm in (1, 2, 3):
                values = medians[arm]
                summary[str(arm)] = {"process_medians": values, "median": statistics.median(values),
                    "min": min(values), "max": max(values), "faster": sum(v < 1 for v in values)}
            q = summary["3"]
            summary["clear_gain"] = q["faster"] >= 4 and 1 / q["median"] > floor
            summary["clear_regression"] = q["faster"] <= 1 and q["median"] > floor
            summaries.append(summary)
            (out / "summary.json").write_text(json.dumps(summaries, indent=2) + "\n")
            print("cell", corpus, backend, "Q/S", round(q["median"], 4), "floor", round(floor, 4), flush=True)
    decisions = {}
    for backend in ("context16", "lits"):
        target = next(s for s in summaries if s["corpus"] == "tranco" and s["backend"] == backend)
        risks = [s for s in summaries if s["corpus"] != "tranco" and s["backend"] == backend]
        decisions[backend] = {"target_clear_gain": target["clear_gain"],
            "risk_clear_regression": any(s["clear_regression"] for s in risks),
            "integrate": target["clear_gain"] and not any(s["clear_regression"] for s in risks)}
    assert len(summaries) == 6 and len(raw) == 510
    assert all(digest(ROOT.parent / p) == h for p, h in identity.items()), "source/input drift"
    (out / "decision.json").write_text(json.dumps(decisions, indent=2) + "\n")
    (out / "identity.json").write_text(json.dumps({"sources_sha256": identity, "cpu": cpu, "commands": commands,
        "binary_sha256": digest(clean), "sanitizer_sha256": digest(san),
        "compiler": run(["g++", "--version"]).stdout.splitlines()[0], "prototype_from": PIN,
        "scope": "one immutable index; C on; W/S/H off; Q static-only; no production changes"}, indent=2) + "\n")
    log.close(); print("decision", json.dumps(decisions), flush=True)

if __name__ == "__main__":
    main()
