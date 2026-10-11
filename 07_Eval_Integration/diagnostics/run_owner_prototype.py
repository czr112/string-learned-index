"""M3 prototype: sanitizer gates first, then the pre-registered A/B.

Pre-registered (docs/NEXT_ACTION_PLAN_20261011_OWNER.md section 3):
  * space   : the measured resident saving must be the whole theoretical table bytes of
              LITS-mapped regions, and at least 90% of it on the target cell;
  * control : Context16 cells must save exactly zero and answer bit-identically;
  * layout  : the prototype device's production arm must reproduce the production-header
              entry device's numbers for the same cell (regions, per-class table bytes,
              checksum) -- "the answers are still right" does not catch a degenerate mapping;
  * timing  : build / query / update / scan, five serial processes x six balanced repeats,
              ratios formed inside one process and repeat, the A/A control (two production
              builds) defining each phase's floor; a phase FAILS on a resolvable >5% regression
              (median > 1.05, at least 4/5 slower, and above the floor).
The analyzer refuses a record set that lacks a cell, a phase or a pairing. No counter build
takes timings. Refuses to overwrite an existing output directory.
"""
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from make_owner_prototype import generate, ROOT, UPSTREAM, PIN_INDEX, PIN_SWITCHES

CORPUS_ROWS = {"mixed": 131072, "skew": 131072, "tranco": 262144}
QUERIES = 10000
KINDS = ("build", "query", "update", "scan")
REGRESSION = 1.05
ARMS = 3


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def analyse(records):
    """Per cell and phase: arm2/arm0 (prototype/production) against arm1/arm0 (A/A control)."""
    cells = sorted({r["cell"] for r in records})
    expected = sorted("%s/%s" % (c, b) for c in CORPUS_ROWS for b in ("context16", "lits"))
    if cells != expected:
        raise RuntimeError("cells missing or extra: %r" % (set(expected) ^ set(cells)))
    out = {}
    for cell in cells:
        rows = [r for r in records if r["cell"] == cell]
        processes = sorted({r["process"] for r in rows})
        if len(processes) != 5:
            raise RuntimeError("%s: expected 5 processes, found %d" % (cell, len(processes)))
        out[cell] = {}
        for kind in KINDS:
            per_process = {}
            for p in processes:
                slots = {}
                for r in rows:
                    if r["process"] != p or r["kind"] != kind:
                        continue
                    key = (r["repeat"], r["arm"])
                    if key in slots:
                        raise RuntimeError("%s/%s: duplicate pairing %r" % (cell, kind, key))
                    if r["elapsed_ns"] <= 0:
                        raise RuntimeError("%s/%s: no main metric" % (cell, kind))
                    slots[key] = r
                repeats = sorted({k[0] for k in slots})
                if not repeats:
                    raise RuntimeError("%s/%s: missing phase" % (cell, kind))
                for rep in repeats:
                    if sorted(k[1] for k in slots if k[0] == rep) != list(range(ARMS)):
                        raise RuntimeError("%s/%s: repeat %d lacks an arm" % (cell, kind, rep))
                def ratio(num, den):
                    return statistics.median(slots[(rep, num)]["elapsed_ns"] /
                                             slots[(rep, den)]["elapsed_ns"] for rep in repeats)
                per_process[p] = {"s_over_p": ratio(2, 0), "a_over_p": ratio(1, 0)}
            proto = [per_process[p]["s_over_p"] for p in processes]
            control = [per_process[p]["a_over_p"] for p in processes]
            floor = max(control) / min(control)
            median = statistics.median(proto)
            out[cell][kind] = {
                "prototype_over_production": proto,
                "control_over_production": control,
                "median": median, "floor": floor,
                "slower": sum(v > 1 for v in proto), "faster": sum(v < 1 for v in proto),
                "regression_fail": median > REGRESSION and sum(v > 1 for v in proto) >= 4
                                   and median > floor,
                "improvement_resolvable": 1.0 / median > floor and median < 1.0,
            }
    return {"cells": out}


def selftest_analyzer():
    def good(cell, p, rep, arm, kind):
        return {"cell": cell, "process": p, "repeat": rep, "arm": arm, "kind": kind,
                "elapsed_ns": 1000 + (5 if arm == 2 else 0)}
    cells = ["%s/%s" % (c, b) for c in CORPUS_ROWS for b in ("context16", "lits")]
    records = [good(cell, p, rep, arm, kind) for cell in cells for p in range(5)
               for rep in range(6) for arm in range(ARMS) for kind in KINDS]
    analyse(records)
    broken = [
        ("missing cell", [r for r in records if r["cell"] != "tranco/lits"]),
        ("missing phase", [r for r in records if not (r["cell"] == "mixed/lits"
                                                       and r["kind"] == "scan")]),
        ("missing pairing", [r for r in records
                             if not (r["cell"] == "mixed/context16" and r["process"] == 2
                                     and r["kind"] == "build" and r["repeat"] == 1
                                     and r["arm"] == 2)]),
        ("duplicate pairing", records + [good("mixed/context16", 0, 0, 0, "build")]),
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
        raise SystemExit("index.hpp drift before the M3 prototype")
    if digest(UPSTREAM / "switches.hpp") != PIN_SWITCHES:
        raise SystemExit("switches.hpp drift before the M3 prototype")
    entry = json.loads((ROOT / "results/owner_entry_20261011/work.json").read_text())
    entry_cells = {"%s/%s" % (r["corpus"], r["backend"]): r for r in entry}
    rejected = selftest_analyzer()
    out = ROOT / "results/owner_prototype_20261011"
    out.mkdir(exist_ok=False)   # refuse to overwrite
    header = generate(out / "probe")
    sources = [Path(__file__), ROOT / "diagnostics/owner_proto.cpp",
               ROOT / "diagnostics/make_owner_prototype.py", header,
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

    def run(cmd, timeout=7200):
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
        "diagnostics/owner_proto.cpp", "learned-index/src/unified/runner.cpp"]
    clean, san = out / "owner_proto", out / "san_owner_proto"
    print("building prototype and sanitizer", flush=True)
    run(["g++", "-O3"] + common + ["-o", str(clean)])
    run(["g++", "-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
         "-DE25_DIAG"] + common + ["-o", str(san)])
    cpu = min(os.sched_getaffinity(0))

    def arguments(corpus):
        p = ROOT / ("results/matrix2_" + corpus) / "interchange"
        return [str(p / "rows.bin"), str(p / "queries.bin")]

    gates = []
    for corpus in CORPUS_ROWS:
        for backend in ("context16", "lits"):
            result = run([str(san)] + arguments(corpus) + [backend, "0"])
            if result.stderr:
                raise RuntimeError("sanitizer stderr is not empty")
            row = json.loads(result.stdout.splitlines()[0])
            row.update(corpus=corpus, backend=backend)
            if not row["gate_pass"]:
                raise RuntimeError("sanitizer gate failed for %s/%s" % (corpus, backend))
            want = entry_cells["%s/%s" % (corpus, backend)]
            for key, src in (("regions_lits", "regions_lits"),
                             ("regions_fallback", "regions_fallback"),
                             ("model_bytes_lits_production", "model_bytes_lits"),
                             ("model_bytes_fallback_production", "model_bytes_fallback"),
                             ("checksum", "checksum")):
                if row[key] != want[src]:
                    raise RuntimeError("%s/%s: prototype layout differs from production on %s "
                                       "(%r vs %r)" % (corpus, backend, key, row[key], want[src]))
            gates.append(row)
            (out / "gates.json").write_text(json.dumps(gates, indent=2) + "\n")
            print("sanitizer", corpus, backend, "gate passed; production arm matches entry",
                  flush=True)

    records, gates_clean = [], []
    for corpus in CORPUS_ROWS:
        for backend in ("context16", "lits"):
            cell = "%s/%s" % (corpus, backend)
            for process in range(5):
                result = run(["taskset", "-c", str(cpu), str(clean)]
                             + arguments(corpus) + [backend, "6"])
                if result.stderr:
                    raise RuntimeError("unexpected timing stderr")
                lines = [json.loads(line) for line in result.stdout.splitlines()]
                gate = lines[0]
                if gate["phase"] != "gate" or not gate["gate_pass"]:
                    raise RuntimeError("%s: clean gate failed" % cell)
                expect = 1 + 6 * ARMS * len(KINDS)
                if len(lines) != expect:
                    raise RuntimeError("%s: expected %d lines, found %d" % (cell, expect, len(lines)))
                gates_clean.append(gate)
                for line in lines[1:]:
                    line.update(cell=cell, process=process)
                    records.append(line)
                (out / "raw.json").write_text(json.dumps(records, indent=2) + "\n")
                print(cell, "P%d" % (process + 1), "done", flush=True)

    timing = analyse(records)
    space_ok = all(g["gate_saving"] for g in gates)
    control_ok = all(g["gate_saving"] for g in gates
                     if g["backend"] == "context16") and \
                 all(g["saving_bytes"] == 0 for g in gates if g["backend"] == "context16")
    regressions = {cell: {k: v["regression_fail"] for k, v in kinds.items()}
                   for cell, kinds in timing["cells"].items()}
    target = timing["cells"]["tranco/lits"]
    decision = {
        "analyzer_selftest_rejections": rejected,
        "space_ok": space_ok, "control_ok": control_ok,
        "regressions": regressions,
        "any_regression": any(v for kinds in regressions.values() for v in kinds.values()),
        "target_build_resolvable": target["build"]["improvement_resolvable"],
        "target_saving_bytes": next(g["saving_bytes"] for g in gates
                                    if g["corpus"] == "tranco" and g["backend"] == "lits"),
    }
    decision["integrate"] = bool(space_ok and control_ok and not decision["any_regression"])
    (out / "timing.json").write_text(json.dumps(timing, indent=2) + "\n")
    (out / "decision.json").write_text(json.dumps(decision, indent=2) + "\n")

    for p, h in identity.items():
        if digest(ROOT.parent / p) != h:
            raise SystemExit("source/input drift after the M3 prototype: " + p)
    if digest(UPSTREAM / "index.hpp") != PIN_INDEX:
        raise SystemExit("index.hpp drift after the M3 prototype")
    (out / "identity.json").write_text(json.dumps(
        {"sources_sha256": identity, "cpu": cpu, "commands": commands,
         "binary_sha256": digest(clean), "sanitizer_sha256": digest(san),
         "compiler": run(["g++", "--version"]).stdout.splitlines()[0],
         "prototype_from": PIN_INDEX,
         "preregistered": {"space": "saving == theoretical LITS table bytes, and >=90% of it",
                           "control": "Context16 cells save exactly 0 and answer identically",
                           "regression": "median > 1.05 and >=4/5 slower and above the A/A floor",
                           "arms": "P production, A production again (A/A), S prototype"},
         "scope": "one isolated probe; production source untouched; queries/updates/scans "
                  "identical between arms; only the construction of LITS-mapped regions differs"},
        indent=2) + "\n")
    log.close()
    print(json.dumps(decision, indent=2), flush=True)


if __name__ == "__main__":
    main()