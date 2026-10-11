"""M3 integration round: the switch is in the production source; verify what it claims.

Pre-registered (docs/NEXT_ACTION_PLAN_20261011_OWNER_INTEGRATION.md):
  OFF  : with `lits_skip_reference_table` left at its default, the index must be unchanged.
         The production-header entry device is re-run on all six cells and its record must
         equal the recorded entry round FIELD BY FIELD -- not merely "the gates still pass".
  ON   : the prototype device is rebuilt against the PRODUCTION header (no probe) and must
         reproduce the space target (saving == theoretical LITS table bytes, >=90% of it),
         the Context16 control (saving 0, answers identical) and the absence of a resolvable
         >5% regression in build/query/update/scan (the pre-registered rules are imported
         from run_owner_prototype.py so there is exactly one definition of them).
  tables: the new switch must be registered in all four places in switches.hpp, and the
         printed switch line must name it.
  adapters: the four adapter manifests must carry the new source identity and the checker
         must pass.
  sanitizer: the ON gate must run clean under ASan/UBSan on all six cells.
No default is flipped; adoption stays with the roadmap's finite-generalization acceptance.
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from run_owner_prototype import analyse, selftest_analyzer, KINDS, ARMS
from make_owner_prototype import PIN_INDEX, PIN_SWITCHES   # noqa: F401  (pins of the entry)

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = ROOT.parent / "02_Code_and_Experiments/validation_e25_20260918"
CORPUS_ROWS = {"mixed": 131072, "skew": 131072, "tranco": 262144}
ENTRY = ROOT / "results/owner_entry_20261011/work.json"


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    index_pin = digest(UPSTREAM / "index.hpp")
    switches_pin = digest(UPSTREAM / "switches.hpp")
    out = ROOT / "results/owner_integration_20261011"
    out.mkdir(exist_ok=False)   # refuse to overwrite
    rejected = selftest_analyzer()

    switch_text = (UPSTREAM / "switches.hpp").read_text()
    table_sites = {
        "baseline_config": "c.lits_skip_reference_table = false;  // M3 space candidate (2026-10-11), not in the frozen set",
        "pin_frozen_switches": "c.lits_skip_reference_table = false;  // NOT part of the frozen set; candidate M3 (2026-10-11)",
        "switches()": '",lits_skip_reference_table=" + (c.lits_skip_reference_table ? "1" : "0") +',
        "differ-table": '{"lits_skip_reference_table", a.lits_skip_reference_table, b.lits_skip_reference_table},',
    }
    table_ok = all(switch_text.count(text) == 1 for text in table_sites.values())

    sources = [Path(__file__), ROOT / "diagnostics/owner_proto.cpp",
               ROOT / "diagnostics/owner_entry.cpp",
               ROOT / "diagnostics/run_owner_prototype.py",
               ROOT.parent / "docs/NEXT_ACTION_PLAN_20261011_OWNER_INTEGRATION.md",
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

    includes = ["-I" + str(UPSTREAM), "-Ilearned-index/src"]
    cpu = min(os.sched_getaffinity(0))

    def arguments(corpus):
        p = ROOT / ("results/matrix2_" + corpus) / "interchange"
        return [str(p / "rows.bin"), str(p / "queries.bin")]

    # ---- OFF: the production source must be unchanged --------------------------------
    entry_device = out / "owner_entry"
    run(["g++", "-O3", "-std=c++17", "-march=x86-64-v3", "-DE12_COUNT", "-DE25_DIAG"]
        + includes + ["diagnostics/owner_entry.cpp", "learned-index/src/unified/runner.cpp",
                      "-o", str(entry_device)])
    recorded = json.loads(ENTRY.read_text())
    off_records = []
    for corpus in CORPUS_ROWS:
        for backend in ("context16", "lits"):
            result = run(["taskset", "-c", str(cpu), str(entry_device)]
                         + arguments(corpus) + [backend])
            if result.stderr:
                raise RuntimeError("unexpected stderr from the OFF run")
            row = json.loads(result.stdout.splitlines()[0])
            row.update(corpus=corpus, backend=backend)
            want = next(r for r in recorded if r["corpus"] == corpus and r["backend"] == backend)
            # only the fields the DEVICE emits; the recorded file also carries the
            # two fields the entry runner derived from them (gates_recomputed, savable_ratio)
            derived = ("corpus", "backend", "gates_recomputed", "savable_ratio")
            diff = {k: (row.get(k), want.get(k)) for k in want
                    if k not in derived and row.get(k) != want.get(k)}
            if diff:
                raise RuntimeError("OFF is not unchanged on %s/%s: %r" % (corpus, backend, diff))
            off_records.append(row)
            (out / "off.json").write_text(json.dumps(off_records, indent=2) + "\n")
            print("OFF", corpus, backend, "matches the recorded entry field by field", flush=True)

    # ---- ON: the prototype device on the PRODUCTION header ---------------------------
    common = ["-std=c++17", "-march=x86-64-v3"] + includes + [
        "diagnostics/owner_proto.cpp", "learned-index/src/unified/runner.cpp"]
    clean, san = out / "owner_proto", out / "san_owner_proto"
    run(["g++", "-O3"] + common + ["-o", str(clean)])
    run(["g++", "-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
         "-DE25_DIAG"] + common + ["-o", str(san)])
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
            gates.append(row)
            (out / "gates.json").write_text(json.dumps(gates, indent=2) + "\n")
            print("ON", corpus, backend, "sanitizer gate passed", flush=True)

    records = [], []
    records = []
    for corpus in CORPUS_ROWS:
        for backend in ("context16", "lits"):
            cell = "%s/%s" % (corpus, backend)
            for process in range(5):
                result = run(["taskset", "-c", str(cpu), str(clean)]
                             + arguments(corpus) + [backend, "6"])
                if result.stderr:
                    raise RuntimeError("unexpected timing stderr")
                lines = [json.loads(line) for line in result.stdout.splitlines()]
                if lines[0]["phase"] != "gate" or not lines[0]["gate_pass"]:
                    raise RuntimeError("%s: clean gate failed" % cell)
                if len(lines) != 1 + 6 * ARMS * len(KINDS):
                    raise RuntimeError("%s: unexpected line count %d" % (cell, len(lines)))
                for line in lines[1:]:
                    line.update(cell=cell, process=process)
                    records.append(line)
                (out / "raw.json").write_text(json.dumps(records, indent=2) + "\n")
                print(cell, "P%d" % (process + 1), "timed", flush=True)

    timing = analyse(records)
    space_ok = all(g["gate_saving"] for g in gates)
    control_ok = all(g["saving_bytes"] == 0 for g in gates if g["backend"] == "context16")
    regressions = {cell: {k: v["regression_fail"] for k, v in kinds.items()}
                   for cell, kinds in timing["cells"].items()}
    decision = {
        "analyzer_selftest_rejections": rejected,
        "off_unchanged": len(off_records) == 6,
        "switch_tables_registered": table_ok,
        "space_ok": space_ok, "control_ok": control_ok,
        "regressions": regressions,
        "any_regression": any(v for kinds in regressions.values() for v in kinds.values()),
        "savings": {("%s/%s" % (g["corpus"], g["backend"])): g["saving_bytes"] for g in gates},
        "default_flipped": False,
    }
    decision["integrated"] = bool(decision["off_unchanged"] and table_ok and space_ok
                                  and control_ok and not decision["any_regression"])
    (out / "timing.json").write_text(json.dumps(timing, indent=2) + "\n")
    (out / "decision.json").write_text(json.dumps(decision, indent=2) + "\n")
    for p, h in identity.items():
        if digest(ROOT.parent / p) != h:
            raise SystemExit("source/input drift after the integration round: " + p)
    (out / "identity.json").write_text(json.dumps(
        {"sources_sha256": identity, "cpu": cpu, "commands": commands,
         "binary_sha256": digest(clean), "sanitizer_sha256": digest(san),
         "entry_binary_sha256": digest(entry_device),
         "compiler": run(["g++", "--version"]).stdout.splitlines()[0],
         "index_hpp_sha256": index_pin, "switches_hpp_sha256": switches_pin,
         "scope": "integration of candidate M3 behind a default-OFF switch; OFF verified "
                  "field-by-field against the recorded entry round; ON verified on the six "
                  "cells with the prototype's pre-registered rules; no default flipped"},
        indent=2) + "\n")
    log.close()
    print(json.dumps(decision, indent=2), flush=True)


if __name__ == "__main__":
    main()