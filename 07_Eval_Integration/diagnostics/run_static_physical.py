"""Owning static blocks: independent gates, then balanced 7-arm full-lookup comparisons.

Run in WSL from the workspace: python3 07_Eval_Integration/diagnostics/run_static_physical.py
Outputs are never overwritten. No LITS benchmark or production-default mutation.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import statistics
import struct
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = ROOT.parent / "02_Code_and_Experiments/validation_e25_20260918"
ARMS = ["release", "C", "C_AA", "packed_directory", "packed_predictor", "ART", "HOT"]
CORPORA = ["tranco", "mixed", "skew"]
PIN = "c8a1ae3c7373714842f724887a6f45eea7ce5ab7b6e9cdbd677ab87fed0e7265"
SWITCH_PIN = "9f8a55cc2f6ec52d0eec1e69793e9c9c27c67e5a26da264bd2a3221d9e030620"


def digest(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def save(p, data):
    p.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n")


def read_rows(path):
    data = path.read_bytes()
    assert data[:8] == b"SIBROW1\0"
    count, = struct.unpack_from("<Q", data, 8)
    pos, rows = 16, []
    for _ in range(count):
        length, = struct.unpack_from("<Q", data, pos)
        pos += 8
        key = data[pos:pos + length]
        pos += length
        value, = struct.unpack_from("<Q", data, pos)
        pos += 8
        rows.append((key, value))
    assert pos == len(data) and all(rows[i - 1][0] < rows[i][0] for i in range(1, len(rows)))
    return rows


def heldout(rows_path, path):
    rows = read_rows(rows_path)
    reference = dict(rows)
    rng = random.Random(2026101201)
    queries = []
    for i in range(10000):
        key, value = rows[rng.randrange(len(rows))]
        if i % 10 == 0:
            key += b"~static-heldout/" + str(rng.getrandbits(64)).encode("ascii")
            while key in reference:
                key += b"~"
            queries.append((key, False, 0))
        else:
            queries.append((key, True, value))
    rng.shuffle(queries)
    with path.open("xb") as out:
        out.write(b"SIBQRY1\0" + struct.pack("<Q", len(queries)))
        for key, present, value in queries:
            assert (key in reference) == present and (not present or reference[key] == value)
            out.write(struct.pack("<BQQ", present, value, len(key)) + key)
    return {"seed": 2026101201, "queries": len(queries), "hit": 9000, "miss": 1000,
            "sha256": digest(path), "miss_rule": "append frozen ASCII suffix, independently verify absent",
            "generator": "Python random.Random; Python version recorded"}


def summarize(raw, gates):
    cells = []
    for corpus in CORPORA:
        for queue in ("historical", "heldout"):
            records = [r for r in raw if r["corpus"] == corpus and r["queue"] == queue and r["phase"] == "paired"]
            if not records:
                continue
            assert len(records) == 5 * 14 * 7
            values = {}
            comparisons = [(a, "C") for a in ARMS if a != "C"]
            comparisons += [("packed_predictor", "packed_directory"), ("C", "release")]
            comparisons += [(a, base) for a in ("release", "C", "packed_directory", "packed_predictor") for base in ("ART", "HOT")]
            for numerator, denominator in comparisons:
                medians = []
                for process in range(5):
                    pairs = {(r["repeat"], r["arm"]): r for r in records if r["process"] == process}
                    assert len(pairs) == 98
                    medians.append(statistics.median(pairs[rep, numerator]["elapsed_ns"] / pairs[rep, denominator]["elapsed_ns"] for rep in range(14)))
                values[numerator + "/" + denominator] = {"process_medians": medians, "median": statistics.median(medians),
                    "faster": sum(x < 1 for x in medians), "slower": sum(x > 1 for x in medians), "min": min(medians), "max": max(medians)}
            floor = max(max(x, 1 / x) for x in values["C_AA/C"]["process_medians"])
            pp, increment = values["packed_predictor/C"], values["packed_predictor/packed_directory"]
            memory = next(g for g in gates if g["corpus"] == corpus and g["queue"] == queue)
            primary = pp["median"] <= .85 and pp["faster"] >= 4 and 1 / pp["median"] > floor
            model_gain = increment["median"] <= .95 and increment["faster"] >= 4 and 1 / increment["median"] > floor
            regression = pp["median"] > 1.05 and pp["slower"] >= 4 and pp["median"] > floor
            cell = {"corpus": corpus, "queue": queue, "aa_observed_band": floor, "ratios": values,
                "space_pass": memory["memory"]["total"] <= memory["baseline_bytes"], "primary_speed_pass": primary,
                "model_increment_pass": model_gain, "clear_regression": regression,
                "arm_ns_per_op": {a: statistics.median(r["elapsed_ns"] / r["operations"] for r in records if r["arm"] == a) for a in ARMS}}
            cells.append(cell)
    return cells


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default="static_physical_20261012b")
    args = parser.parse_args()
    if Path(args.output).name != args.output:
        raise ValueError("output must be a directory name")
    assert digest(UPSTREAM / "index.hpp") == PIN
    assert digest(UPSTREAM / "switches.hpp") == SWITCH_PIN
    out = ROOT / "results" / args.output
    out.mkdir(exist_ok=False)
    build = out / "build"
    build.mkdir()
    sources = [Path(__file__), ROOT.parent / "docs/NEXT_ACTION_PLAN_20261012_STATIC.md"]
    sources += [ROOT / "diagnostics" / n for n in ("static_physical_blocks.hpp", "static_input.hpp", "check_static_physical.cpp", "bench_static_physical.cpp", "verify_static_physical.py")]
    sources += sorted(UPSTREAM.rglob("*.hpp"))
    sources += sorted((ROOT / "learned-index/src").rglob("*.h"))
    sources += [ROOT / "learned-index/src/unified/runner.cpp"]
    for adapter in ("our_index", "art", "hot"):
        sources += sorted((ROOT / "adapters" / adapter).glob("*.cpp")) + sorted((ROOT / "adapters" / adapter).glob("*.h"))
    vendor = UPSTREAM / "third_party/vendor/libart/src"
    sources += [vendor / "art.c", vendor / "art.h"]
    sources += sorted((ROOT / "hot-src/libs").rglob("*.hpp")) + sorted((ROOT / "hot-src/libs").rglob("*.h"))
    sources = sorted(set(sources))
    identity = {str(p.relative_to(ROOT.parent)): digest(p) for p in sources}
    cpu = min(os.sched_getaffinity(0))
    meta = {"source_sha256": identity, "cpu": cpu, "commands": [], "inputs": {},
        "base_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "scope": "immutable owning prototype; no production-default or LITS changes",
        "protocol": "5 processes x 14 Williams position-and-predecessor-balanced repeats x 20 passes; equal immediate per-arm warm-up; identical C/C_AA instance; all arms v1 virtual find",
        "adapter_identity_note": "Existing ours descriptor/manifest declares an older index hash; actual compiled dependencies are independently rehashed here; old results are not relabeled",
        "configs": {"release": "baseline_config(128)+pin_frozen_switches; Context16; C/W/S off",
                    "C": "same, only compact_region_routing on", "C_AA": "identical C object and call"}}
    log = (out / "commands.log").open("x")

    def run(command, timeout=900):
        meta["commands"].append(command)
        log.write("$ " + " ".join(map(str, command)) + "\n")
        log.flush()
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=timeout)
        log.write(result.stdout + result.stderr + f"rc={result.returncode}\n")
        log.flush()
        if result.returncode:
            raise RuntimeError(result.stderr or result.stdout or "experiment command failed")
        return result

    meta["compiler"] = run(["g++", "--version"]).stdout.splitlines()[0]
    meta["python"] = run(["python3", "--version"]).stdout.strip()
    for corpus in CORPORA:
        inp = ROOT / ("results/matrix2_" + corpus) / "interchange"
        held = out / (corpus + "_heldout.bin")
        meta["inputs"][corpus] = {"rows": digest(inp / "rows.bin"), "historical": digest(inp / "queries.bin"),
                                  "heldout": heldout(inp / "rows.bin", held)}
    save(out / "identity.json", meta)
    common = ["-std=c++17", "-march=x86-64-v3", "-I" + str(UPSTREAM), "-Ilearned-index/src"]
    check_source = ["diagnostics/check_static_physical.cpp", "learned-index/src/unified/runner.cpp"]
    print("building correctness and sanitizer gates", flush=True)
    check, san = build / "check", build / "san_check"
    run(["g++", "-O2", "-DE12_COUNT"] + common + check_source + ["-o", str(check)])
    run(["g++", "-O1", "-g", "-DE12_COUNT", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"] + common + check_source + ["-o", str(san)])
    gates, sanitized = [], []
    for corpus in CORPORA:
        inp = ROOT / ("results/matrix2_" + corpus) / "interchange"
        for queue in ("historical", "heldout"):
            queries = inp / "queries.bin" if queue == "historical" else out / (corpus + "_heldout.bin")
            clean = run([str(check), str(inp / "rows.bin"), str(queries)])
            row = json.loads(clean.stdout)
            row.update(corpus=corpus, queue=queue)
            gates.append(row)
            san_result = run([str(san), str(inp / "rows.bin"), str(queries)])
            if san_result.stderr:
                raise RuntimeError("sanitizer stderr is not empty")
            assert json.loads(san_result.stdout) == {k: v for k, v in row.items() if k not in ("corpus", "queue")}
            sanitized.append({"corpus": corpus, "queue": queue, "rc": 0, "stderr_bytes": 0})
            save(out / "gates.json", gates)
            save(out / "sanitizer.json", sanitized)
            print("gate", corpus, queue, "checks", row["checks"], "window p50/p99", row["window_p50"], row["window_p99"], flush=True)
    if any(g["memory"]["total"] > g["baseline_bytes"] for g in gates if g["corpus"] == "tranco"):
        save(out / "decision.json", {"stage": "S1", "continue": False, "reason": "target resident-byte budget failed"})
        return
    print("gates passed; building the 7-arm clean benchmark", flush=True)
    objs = []

    def compile_obj(compiler, name, source, options):
        obj = build / (name + ".o")
        run([compiler, "-O3", "-march=x86-64-v3"] + options + ["-c", str(source), "-o", str(obj)])
        objs.append(str(obj))

    compile_obj("g++", "ours", ROOT / "adapters/our_index/our_index_adapter.cpp", common + ["-DOUR_INDEX_BACKEND=0"])
    compile_obj("g++", "ours_c", ROOT / "adapters/our_index/our_index_adapter.cpp", common + ["-DOUR_INDEX_BACKEND=0", "-DOUR_INDEX_ROUTING_ENABLED=1", "-Dmake_our_index_index=make_our_index_routing_index"])
    compile_obj("gcc", "art", vendor / "art.c", ["-std=c99", "-w"])
    compile_obj("g++", "art_adapter", ROOT / "adapters/art/art_adapter.cpp", common + ["-I" + str(vendor)])
    hot_roots = ["hot/single-threaded", "hot/commons", "hot/rowex", "idx/content-helpers", "idx/map-helpers"]
    hot_options = common + ["-I" + str(ROOT / "hot-src/libs" / p / "include") for p in hot_roots]
    hot_options += ["-mbmi2", "-mlzcnt", "-faligned-new", "-fno-builtin-malloc", "-fno-builtin-calloc", "-fno-builtin-realloc", "-fno-builtin-free", "-Wno-deprecated-declarations"]
    compile_obj("g++", "hot_adapter", ROOT / "adapters/hot/hot_adapter.cpp", hot_options)
    bench = build / "bench"
    run(["g++", "-O3"] + common + ["diagnostics/bench_static_physical.cpp", "learned-index/src/unified/runner.cpp"] + objs + ["-o", str(bench)])
    meta["binaries"] = {"check": digest(check), "san_check": digest(san), "bench": digest(bench)}
    save(out / "identity.json", meta)
    raw, build_samples = [], []
    print("timing begins; all cells sequential on CPU", cpu, flush=True)
    for corpus in CORPORA:
        inp = ROOT / ("results/matrix2_" + corpus) / "interchange"
        for queue in ("historical", "heldout"):
            queries = inp / "queries.bin" if queue == "historical" else out / (corpus + "_heldout.bin")
            for process in range(5):
                result = run(["taskset", "-c", str(cpu), str(bench), str(inp / "rows.bin"), str(queries), "14", str(process)])
                if result.stderr:
                    raise RuntimeError("benchmark emitted unexpected stderr")
                records = [json.loads(line) for line in result.stdout.splitlines()]
                assert len(records) == 105
                samples = [r for r in records if r["phase"] == "paired"]
                assert len(samples) == 98 and len({r["checksum"] for r in samples}) == 1
                for arm in ARMS:
                    assert sorted(r["position"] for r in samples if r["arm"] == arm) == sorted(list(range(7))*2)
                predecessors = {(a,b):0 for a in ARMS for b in ARMS if a!=b}
                for rep in range(14):
                    sequence=sorted((r for r in samples if r["repeat"]==rep),key=lambda r:r["position"])
                    for a,b in zip(sequence,sequence[1:]):predecessors[a["arm"],b["arm"]]+=1
                assert set(predecessors.values())=={2}, "unbalanced directed carryover"
                for row in records:
                    row.update(corpus=corpus, queue=queue, process=process)
                raw.extend(records)
                save(out / "raw.json", raw)
                print("timing", corpus, queue, "P" + str(process + 1), "done", flush=True)
            save(out / "summary.json", summarize(raw, gates))
    summary = summarize(raw, gates)
    targets = [c for c in summary if c["corpus"] == "tranco"]
    risks = [c for c in summary if c["corpus"] != "tranco"]
    decision = {"stage": "S2", "target_speed_pass": all(c["primary_speed_pass"] for c in targets),
        "target_model_increment_pass": all(c["model_increment_pass"] for c in targets),
        "target_space_pass": all(c["space_pass"] for c in targets),
        "risk_regression": any(c["clear_regression"] for c in risks),
        "production_default_changed": False, "dynamic_support": False}
    decision["advance_S3"] = decision["target_speed_pass"] and decision["target_model_increment_pass"] and decision["target_space_pass"]
    decision["static_scenario_candidate_eligible"] = decision["advance_S3"] and not decision["risk_regression"]
    decision["generic_default_eligible"] = False  # immutable prototype is never the dynamic default
    assert all(digest(ROOT.parent / p) == h for p, h in identity.items()), "source drift"
    for corpus in CORPORA:
        inp = ROOT / ("results/matrix2_" + corpus) / "interchange"
        assert digest(inp / "rows.bin") == meta["inputs"][corpus]["rows"]
        assert digest(inp / "queries.bin") == meta["inputs"][corpus]["historical"]
        assert digest(out / (corpus + "_heldout.bin")) == meta["inputs"][corpus]["heldout"]["sha256"]
    save(out / "summary.json", summary)
    save(out / "decision.json", decision)
    save(out / "identity.json", meta)
    log.close()
    print("completed", json.dumps(decision), flush=True)


if __name__ == "__main__":
    main()
