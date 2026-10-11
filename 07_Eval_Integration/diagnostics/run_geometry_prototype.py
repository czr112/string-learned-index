"""GD prototype: sanitizer gates first, then the fixed A/B the plan pre-registered.

Pre-registered before any number was seen (plan section 4):
  * main pair arm2/arm0 (G0/B0) on Context16/tranco -- the descriptor with the shipped
    query mode against the production geometry with the same mode;
  * the spread floor of a cell is max/min over the five process medians of the CONTROL
    pairing arm1/arm0 (two BASELINE arms differing only in `locate`), exactly as the plan
    writes it;
  * main target: median(G0/B0) <= 0.90, at least 4/5 processes faster, and 1/median above
    the floor;
  * risk cells (Context16 mixed/skew): a failure needs median > 1.05, at least 4/5 slower
    AND above the floor; anything else is reported as unresolved, not as a regression;
  * LITS cells are replayed and reported; they are never substituted for the main pair;
  * build: descriptor preparation is part of the load, and its share must stay under 10%
    and inside the load A/A spread (the second identical build of the same process);
  * space: descriptor resident bytes within 5% of the index accounting.
The analyzer is exercised on fixed small records first: it must reject a missing main
metric, a missing cell, a duplicated pairing and a missing pairing before it is pointed
at a real run. It refuses to overwrite an existing output directory.
"""
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from make_geometry_prototype_v2 import generate, ROOT, UPSTREAM, PIN_INDEX, PIN_SWITCHES

CORPUS_ROWS = {"mixed": 131072, "skew": 131072, "tranco": 262144}
QUERIES = 10000
ARMS = 4
MAIN_TARGET = ("tranco", "context16")
RISK_CELLS = {("mixed", "context16"), ("skew", "context16")}
MAIN_MAX = 0.90
RISK_MIN = 1.05
BUILD_MAX = 0.10
SPACE_MAX = 20  # resident * 20 <= index_bytes


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def analyse(records):
    """The whole verdict, from raw paired/build records only.

    `records` is a list of dicts: {"cell", "process", "phase", "repeat", "arm",
    "elapsed_ns", ...}. Every requirement the plan fixed is re-derived here, so a
    record set that lacks the main metric, a cell or a pairing cannot produce a pass.
    """
    cells = sorted({r["cell"] for r in records if r["phase"] == "paired"})
    expected = sorted("%s/%s" % (c, b) for c in CORPUS_ROWS for b in ("context16", "lits"))
    if cells != expected:
        raise RuntimeError("cells missing or extra: %r" % (set(expected) ^ set(cells)))
    out = {"cells": {}, "problems": []}
    for cell in cells:
        rows = [r for r in records if r["cell"] == cell and r["phase"] == "paired"]
        processes = sorted({r["process"] for r in rows})
        if len(processes) != 5:
            raise RuntimeError("%s: expected 5 processes, found %d" % (cell, len(processes)))
        per_process = {}
        for p in processes:
            slots = {}
            for r in rows:
                if r["process"] != p:
                    continue
                key = (r["repeat"], r["arm"])
                if key in slots:
                    raise RuntimeError("%s: duplicate pairing %r in process %d" % (cell, key, p))
                slots[key] = r
            repeats = sorted({k[0] for k in slots})
            if sorted({k[1] for k in slots}) != list(range(ARMS)):
                raise RuntimeError("%s: process %d lacks an arm" % (cell, p))
            if len(slots) != len(repeats) * ARMS:
                raise RuntimeError("%s: process %d has a missing pairing" % (cell, p))
            def ratio(num, den):
                values = []
                for rep in repeats:
                    a = slots[(rep, num)]["elapsed_ns"]
                    b = slots[(rep, den)]["elapsed_ns"]
                    if a <= 0 or b <= 0 or slots[(rep, num)]["operations"] != slots[(rep, den)]["operations"]:
                        raise RuntimeError("%s: bad pairing on repeat %d" % (cell, rep))
                    values.append(a / b)
                return statistics.median(values)
            per_process[p] = {"g0_over_b0": ratio(2, 0), "g1_over_b1": ratio(3, 1),
                              "b1_over_b0": ratio(1, 0)}
        def collect(field):
            return [per_process[p][field] for p in processes]
        g0 = collect("g0_over_b0")
        floor = max(collect("b1_over_b0")) / min(collect("b1_over_b0"))
        row = {
            "g0_over_b0": g0, "g1_over_b1": collect("g1_over_b1"),
            "b1_over_b0": collect("b1_over_b0"), "floor": floor,
            "g0_median": statistics.median(g0),
            "g0_faster": sum(v < 1 for v in g0),
            "g0_slower": sum(v > 1 for v in g0),
            "main_pass": None, "risk_fail": None,
        }
        if cell == "%s/%s" % MAIN_TARGET:
            row["main_pass"] = (row["g0_median"] <= MAIN_MAX and row["g0_faster"] >= 4
                                and 1.0 / row["g0_median"] > floor)
        if tuple(cell.split("/")) in RISK_CELLS:
            row["risk_fail"] = (row["g0_median"] > RISK_MIN and row["g0_slower"] >= 4
                                and row["g0_median"] > floor)
        out["cells"][cell] = row
    if out["cells"]["%s/%s" % MAIN_TARGET]["main_pass"] is None:
        raise RuntimeError("main metric missing")
    return out


def analyse_build(builds):
    out = {}
    for cell, rows in builds.items():
        increments = [r["prep_ns"] / r["load2_ns"] for r in rows]
        aa = [r["load2_ns"] / r["load1_ns"] for r in rows]
        out[cell] = {"increment_median": statistics.median(increments),
                     "aa_floor": max(aa) / min(aa),
                     "descriptor_ratio": statistics.median(
                         [r["descriptor_bytes"] / r["index_bytes"] for r in rows]),
                     "fail": (statistics.median(increments) > BUILD_MAX
                              and statistics.median(increments) > max(aa) / min(aa))}
    return out


def selftest_analyzer():
    def good(cell, process, repeat, arm):
        return {"cell": cell, "process": process, "phase": "paired", "repeat": repeat,
                "arm": arm, "operations": 100 * QUERIES,
                "elapsed_ns": 1000 + (10 if arm == 2 else 0), "checksum": 1}
    cells = ["%s/%s" % (c, b) for c in CORPUS_ROWS for b in ("context16", "lits")]
    records = [good(cell, p, rep, arm) for cell in cells for p in range(5)
               for rep in range(4) for arm in range(ARMS)]
    analyse(records)                      # must not raise
    broken = [
        ("missing main metric", [r for r in records if r["cell"] != "tranco/context16"]),
        ("missing cell", [r for r in records if r["cell"] != "mixed/lits"]),
        ("duplicate pairing", records + [good("tranco/context16", 0, 0, 0)]),
        ("missing pairing", [r for r in records
                             if not (r["cell"] == "tranco/context16" and r["process"] == 1
                                     and r["repeat"] == 2 and r["arm"] == 3)]),
    ]
    for name, data in broken:
        try:
            analyse(data)
        except RuntimeError:
            continue
        raise RuntimeError("analyzer self-test: %s was NOT rejected" % name)
    return [name for name, _ in broken]


def main():
    if digest(UPSTREAM / "index.hpp") != PIN_INDEX:
        raise SystemExit("index.hpp drift before the GD prototype")
    if digest(UPSTREAM / "switches.hpp") != PIN_SWITCHES:
        raise SystemExit("switches.hpp drift before the GD prototype")
    entry = json.loads((ROOT / "results/geometry_entry_20261011/summary.json").read_text())
    if entry.get("entry_all_pass") is not True:
        raise SystemExit("the GD entry did not establish its conditions")
    rejected = selftest_analyzer()
    out = ROOT / "results/geometry_prototype_20261011"
    out.mkdir(exist_ok=False)   # refuse to overwrite

    header = generate(out / "probe")
    device = ROOT / "diagnostics/lookup_geometry.cpp"
    sources = [Path(__file__), device, ROOT / "diagnostics/make_geometry_prototype_v2.py",
               ROOT / "diagnostics/make_geometry_prototype.py", header,
               ROOT / "diagnostics/lookup_structure.cpp",
               ROOT.parent / "docs/NEXT_ACTION_PLAN_20261011_GEOMETRY.md",
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
    common = ["-std=c++17", "-march=x86-64-v3"] + includes + [
        "diagnostics/lookup_geometry.cpp", "learned-index/src/unified/runner.cpp"]
    clean, san = out / "lookup_geometry", out / "san_lookup_geometry"
    print("building prototype and sanitizer", flush=True)
    run(["g++", "-O3"] + common + ["-o", str(clean)])
    run(["g++", "-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
         "-DE25_DIAG"] + common + ["-o", str(san)])
    cpu = min(os.sched_getaffinity(0))

    def arguments(corpus, repeats):
        p = ROOT / ("results/matrix2_" + corpus) / "interchange"
        return [str(p / "rows.bin"), str(p / "queries.bin")]

    gates = []
    for corpus in CORPUS_ROWS:
        for backend in ("context16", "lits"):
            result = run([str(san)] + arguments(corpus, 0) + [backend, "0"])
            if result.stderr:
                raise RuntimeError("sanitizer stderr is not empty")
            row = json.loads(result.stdout.splitlines()[0])
            row.update(corpus=corpus, backend=backend)
            if row["phase"] != "gate" or not row["gate_pass"]:
                raise RuntimeError("sanitizer gate failed for %s/%s: %r" % (corpus, backend, row))
            if row["corpus_keys"] != CORPUS_ROWS[corpus] or row["corpus_queries"] != QUERIES:
                raise RuntimeError("sanitizer gate did not cover the full corpus")
            gates.append(row)
            (out / "sanitizer.json").write_text(json.dumps(gates, indent=2) + "\n")
            print("sanitizer", corpus, backend, "gate passed", flush=True)

    records, builds = [], {}
    for corpus in CORPUS_ROWS:
        for backend in ("context16", "lits"):
            cell = "%s/%s" % (corpus, backend)
            for process in range(5):
                result = run(["taskset", "-c", str(cpu), str(clean)]
                             + arguments(corpus, 4) + [backend, "4"])
                if result.stderr:
                    raise RuntimeError("unexpected timing stderr")
                lines = [json.loads(line) for line in result.stdout.splitlines()]
                phases = [line["phase"] for line in lines]
                if phases.count("gate") != 1 or phases.count("build") != 1 or phases.count("paired") != 16:
                    raise RuntimeError("%s: unexpected phase mix" % cell)
                gate = lines[0]
                if gate["corpus_keys"] != CORPUS_ROWS[corpus] or gate["oracle_mismatch"] or not gate["gate_pass"]:
                    raise RuntimeError("%s: clean gate failed" % cell)
                build = lines[1]
                if build["prep_ns"] <= 0 or build["load2_ns"] <= 0:
                    raise RuntimeError("%s: build phase did not produce its metric" % cell)
                paired = lines[2:]
                if len({line["checksum"] for line in paired}) != 1:
                    raise RuntimeError("%s: arms disagree on the timed answer" % cell)
                builds.setdefault(cell, []).append(build)
                for line in paired:
                    line.update(cell=cell, process=process)
                    records.append(line)
                (out / "raw.json").write_text(json.dumps(records, indent=2) + "\n")
                (out / "build.json").write_text(json.dumps(builds, indent=2) + "\n")
                print(cell, "P%d" % (process + 1), "prep/load",
                      round(build["prep_ns"] / build["load2_ns"], 6), flush=True)

    timing = analyse(records)
    build_report = analyse_build(builds)
    decision = {
        "main_target": "%s/%s" % MAIN_TARGET,
        "main_pass": timing["cells"]["%s/%s" % MAIN_TARGET]["main_pass"],
        "risk_fail": {c: r["risk_fail"] for c, r in timing["cells"].items()
                      if r["risk_fail"] is not None},
        "build_fail": {c: r["fail"] for c, r in build_report.items()},
        "analyzer_selftest_rejections": rejected,
    }
    decision["integrate"] = bool(decision["main_pass"]
                                 and not any(decision["risk_fail"].values())
                                 and not any(decision["build_fail"].values()))
    (out / "timing.json").write_text(json.dumps(timing, indent=2) + "\n")

    if digest(UPSTREAM / "index.hpp") != PIN_INDEX:
        raise SystemExit("index.hpp drift after the GD prototype")
    for p, h in identity.items():
        if digest(ROOT.parent / p) != h:
            raise SystemExit("source/input drift after the GD prototype: " + p)
    (out / "identity.json").write_text(json.dumps(
        {"sources_sha256": identity, "cpu": cpu, "commands": commands,
         "binary_sha256": digest(clean), "sanitizer_sha256": digest(san),
         "compiler": run(["g++", "--version"]).stdout.splitlines()[0],
         "prototype_from": PIN_INDEX,
         "preregistered": {"main": "arm2/arm0 on Context16/tranco", "main_max": MAIN_MAX,
                           "risk_cells": sorted("%s/%s" % c for c in RISK_CELLS),
                           "risk_min": RISK_MIN, "build_max": BUILD_MAX,
                           "space_max": "resident*20 <= index_bytes",
                           "floor": "max/min of the five process medians of arm1/arm0"},
         "scope": "one immutable index; C on; W/S/H off; descriptor prepared after load "
                  "and counted in it; no production source change"},
        indent=2) + "\n")
    (out / "decision.json").write_text(json.dumps(decision, indent=2) + "\n")
    log.close()
    print(json.dumps(decision, indent=2), flush=True)


if __name__ == "__main__":
    main()