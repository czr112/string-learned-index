"""M3 lifecycle gate runner -- P0 of docs/NEXT_ACTION_PLAN_20261011_MERGE.md.

Pre-registered pass rules (plan section 2): OFF, ON and an independent unsigned-byte
ordered reference run the same update program; splits and prefix rebuilds must
positively trigger (Index::splits / Index::rebuilds deltas); LITS regions must not hold
the reference table while fallback regions keep it; group_of() and a VALID preset must
be refused on ON, with OFF as the accepting control (bulk_load_with_plan is never used
as the preset test); key-domain probes must behave identically on both arms. The
ASan/UBSan build must be green on all six cells with empty stderr.

This round changes only diagnostics: index.hpp must still be the M3 integration pin.
"""
import copy
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = ROOT.parent / "02_Code_and_Experiments/validation_e25_20260918"
CORPORA = ("mixed", "skew", "tranco")
BACKENDS = ("context16", "lits")
PIN_INDEX = "61592294a0b0a58ee9064c28fd7583941934eaa33d7c37e644dc0b649f765016"
PIN_SWITCHES = "9f8a55cc2f6ec52d0eec1e69793e9c9c27c67e5a26da264bd2a3221d9e030620"
MANIFESTS = ("our_index", "our_index_lits", "our_index_routing", "our_index_lits_routing")


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def evaluate(records):
    """Return the list of pre-registered violations; empty list = pass."""
    fails = []
    cells = {(r.get("corpus"), r.get("backend")): r for r in records}
    want = {(c, b) for c in CORPORA for b in BACKENDS}
    for key in sorted(want - set(cells)):
        fails.append("missing cell %s/%s" % key)
    for key in sorted(set(cells) - want):
        fails.append("unexpected cell %s/%s" % key)
    for key, r in sorted(cells.items()):
        corpus, backend = key
        if r.get("phase") != "lifecycle":
            fails.append("%s/%s phase is not lifecycle" % key)
        for sub in ("gate_full", "gate_size", "gate_absent", "gate_scan", "gate_ops",
                    "gate_split", "gate_ownership", "gate_prefix", "gate_groupof",
                    "gate_domain", "gate_query", "gate_preset", "gate_pass"):
            if not r.get(sub):
                fails.append("%s/%s %s false" % (corpus, backend, sub))
        if r.get("splits_on_delta", 0) <= 0:
            fails.append("%s/%s no split positively triggered" % key)
        if r.get("splits_on_delta") != r.get("splits_off_delta"):
            fails.append("%s/%s split delta differs between arms" % key)
        if r.get("own_violations") != 0:
            fails.append("%s/%s ownership violations %s" % (corpus, backend, r.get("own_violations")))
        own_on, own_off = r.get("own_final_on", {}), r.get("own_final_off", {})
        if own_on.get("lits_with_table"):
            fails.append("%s/%s LITS region kept a reference table" % key)
        if own_on.get("fallback_without_table"):
            fails.append("%s/%s fallback region lost its reference table" % key)
        if own_off.get("lits_without_table") or own_off.get("fallback_without_table"):
            fails.append("%s/%s OFF arm lost a reference table" % key)
        if backend == "lits":
            if own_on.get("lits", 0) <= 0:
                fails.append("%s/lits has no LITS region" % corpus)
            preset = r.get("preset", {})
            if not (preset.get("off_ok") and preset.get("on_threw") and preset.get("message_ok")):
                fails.append("%s/lits valid-preset refusal missing" % corpus)
        else:
            if not r.get("preset", {}).get("on_ok"):
                fails.append("%s/context16 ON arm must accept the valid preset" % corpus)
    if not any(r.get("backend") == "lits" and
               r.get("own_final_on", {}).get("fallback_with_table", 0) > 0
               for r in records):
        fails.append("no fallback region positively triggered in any cell")
    for backend in BACKENDS:
        first = None
        for r in records:
            if r.get("backend") != backend:
                continue
            cur = json.dumps({"prefix": r.get("prefix"), "domain": r.get("domain"),
                              "preset": r.get("preset")}, sort_keys=True)
            if first is None:
                first = cur
            elif first != cur:
                fails.append("%s domain/prefix/preset sections differ across corpora" % backend)
    return fails


def make_cell(corpus, backend):
    return {
        "phase": "lifecycle", "corpus": corpus, "backend": backend,
        "rows": 100, "checkpoints": 7, "splits_on_delta": 2, "splits_off_delta": 2,
        "own_violations": 0,
        "own_final_on": {"regions": 10, "lits": 9 if backend == "lits" else 0,
                         "lits_with_table": 0, "fallback_without_table": 0,
                         "fallback_with_table": 3 if (backend == "lits" and corpus == "mixed") else 0},
        "own_final_off": {"lits_without_table": 0, "fallback_without_table": 0},
        "prefix": [{"name": "prefix-break-late", "mismatch": 0}],
        "domain": [{"name": "domain-empty-nul-high", "mismatch": 0}],
        "preset": {"off_ok": True, "on_threw": backend == "lits", "message_ok": True,
                   "on_ok": backend == "context16", "scan_equal": True},
        "gate_full": True, "gate_size": True, "gate_absent": True, "gate_scan": True,
        "gate_ops": True, "gate_split": True, "gate_ownership": True,
        "gate_prefix": True, "gate_groupof": True, "gate_domain": True, "gate_query": True,
        "gate_preset": True,
        "gate_pass": True,
    }


def selftest_analyzer():
    base = [make_cell(c, b) for c in CORPORA for b in BACKENDS]
    if evaluate(copy.deepcopy(base)):
        raise RuntimeError("analyzer selftest rejected a passing record set")
    mutations = []

    def mut(name, fn):
        mutations.append((name, fn))

    mut("gate_pass false", lambda rs: rs[0].update(gate_pass=False))
    mut("missing cell", lambda rs: rs.pop(3))
    mut("no split", lambda rs: rs[2].update(splits_on_delta=0))
    mut("lits region kept its table", lambda rs: rs[1]["own_final_on"].update(lits_with_table=1))
    mut("find mismatch", lambda rs: rs[0].update(full_find_mismatch=1, gate_full=False))
    mut("fallback never triggered",
        lambda rs: [r["own_final_on"].update(fallback_with_table=0) for r in rs])
    mut("prefix mismatch", lambda rs: rs[0]["prefix"][0].update(mismatch=1))
    mut("preset not refused",
        lambda rs: rs[1]["preset"].update(on_threw=False, message_ok=False))
    mut("context16 preset rejected", lambda rs: rs[0]["preset"].update(on_ok=False))
    mut("group_of not refused", lambda rs: rs[1].update(gate_groupof=False))
    mut("query domain not refused", lambda rs: rs[1].update(gate_query=False))
    rejected = 0
    for name, fn in mutations:
        rows = copy.deepcopy(base)
        fn(rows)
        if evaluate(rows):
            rejected += 1
        else:
            raise RuntimeError("analyzer selftest accepted: " + name)
    return rejected

def main():
    index_pin = digest(UPSTREAM / "index.hpp")
    switches_pin = digest(UPSTREAM / "switches.hpp")
    if index_pin != PIN_INDEX:
        raise SystemExit("index.hpp moved off the M3 integration pin; P0 must not run")
    if switches_pin != PIN_SWITCHES:
        raise SystemExit("switches.hpp moved off the M3 integration pin")
    for name in MANIFESTS:
        manifest = json.loads((ROOT / "adapters" / name / "benchmark-adapter.json").read_text())
        identity = manifest.get("source_identity", {})
        if (identity.get("index_hpp_sha256") != PIN_INDEX or
                identity.get("switches_hpp_sha256") != PIN_SWITCHES):
            raise SystemExit("adapter manifest out of sync: " + name)

    out = ROOT / "results/owner_lifecycle_20261011"
    out.mkdir(exist_ok=False)   # refuse to overwrite an existing run
    rejected = selftest_analyzer()

    log = (out / "commands.log").open("x")
    commands = []

    def run(cmd, timeout=7200, allowed=(0,)):
        commands.append(cmd)
        log.write("$ " + " ".join(cmd) + "\n")
        log.flush()
        result = subprocess.run(cmd, cwd=ROOT, text=True, capture_output=True, timeout=timeout)
        log.write(result.stdout + result.stderr + "rc=%d\n" % result.returncode)
        log.flush()
        if result.returncode not in allowed:
            raise RuntimeError(result.stderr or result.stdout or "command failed")
        return result

    includes = ["-I" + str(UPSTREAM), "-Ilearned-index/src"]
    sources = ["diagnostics/check_owner_lifecycle.cpp", "learned-index/src/unified/runner.cpp"]
    cpu = min(os.sched_getaffinity(0))
    gate, san = out / "check_owner_lifecycle", out / "san_check_owner_lifecycle"
    run(["g++", "-O3", "-std=c++17", "-march=x86-64-v3", "-DE25_DIAG"]
        + includes + sources + ["-o", str(gate)])
    run(["g++", "-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
         "-DE25_DIAG"] + includes + sources + ["-o", str(san)])

    records, san_records = [], []
    for corpus in CORPORA:
        rows = ROOT / ("results/matrix2_" + corpus) / "interchange" / "rows.bin"
        for backend in BACKENDS:
            cell = "%s/%s" % (corpus, backend)
            sres = run(["taskset", "-c", str(cpu), str(san), str(rows), backend])
            if sres.stderr:
                raise RuntimeError("sanitizer stderr is not empty on " + cell)
            srow = json.loads(sres.stdout.splitlines()[0])
            srow.update(corpus=corpus, backend=backend)
            if not srow["gate_pass"]:
                raise RuntimeError("sanitizer gate failed on " + cell)
            san_records.append(srow)
            (out / "san.json").write_text(json.dumps(san_records, indent=2) + "\n")
            cru = run(["taskset", "-c", str(cpu), str(gate), str(rows), backend],
                      allowed=(0, 1))
            if cru.stderr:
                raise RuntimeError("unexpected stderr on " + cell)
            row = json.loads(cru.stdout.splitlines()[0])
            row.update(corpus=corpus, backend=backend)
            records.append(row)
            (out / "raw.json").write_text(json.dumps(records, indent=2) + "\n")
            print("%s gate_pass=%s splits_on=%d lits=%d fallback_with_table=%d "
                  "full_finds=%d" % (cell, row["gate_pass"], row["splits_on_delta"],
                                     row["own_final_on"]["lits"],
                                     row["own_final_on"]["fallback_with_table"],
                                     row["full_finds"]), flush=True)

    violations = evaluate(records)
    decision = {
        "analyzer_selftest_rejections": rejected,
        "cells": len(records),
        "sanitizer_cells_green": len(san_records),
        "fallback_positive_cells": [r["corpus"] for r in records
                                    if r["backend"] == "lits"
                                    and r["own_final_on"]["fallback_with_table"] > 0],
        "violations": violations,
        "passed": not violations,
        "scope": "M3 lifecycle correctness re-validation only; no timings, no default flip",
    }
    (out / "decision.json").write_text(json.dumps(decision, indent=2) + "\n")

    sources_to_hash = [Path(__file__), ROOT / "diagnostics/check_owner_lifecycle.cpp",
                       UPSTREAM / "index.hpp", UPSTREAM / "switches.hpp",
                       UPSTREAM / "model/lits_hpt_adapter.hpp"]
    for corpus in CORPORA:
        sources_to_hash.append(ROOT / ("results/matrix2_" + corpus) / "interchange" / "rows.bin")
    identity = {str(p.relative_to(ROOT.parent)): digest(p) for p in sources_to_hash}
    (out / "identity.json").write_text(json.dumps({
        "sources_sha256": identity,
        "binary_sha256": digest(gate),
        "lits_hpt_adapter_sha256": digest(UPSTREAM / "model/lits_hpt_adapter.hpp"),
        "sanitizer_binary_sha256": digest(san),
        "compiler": run(["g++", "--version"]).stdout.splitlines()[0],
        "cpu": cpu,
        "commands": commands,
        "scope": decision["scope"],
    }, indent=2) + "\n")
    for p, h in identity.items():
        if digest(ROOT.parent / p) != h:
            raise SystemExit("source/input drift after the lifecycle round: " + p)
    log.close()
    print(json.dumps(decision, indent=2), flush=True)
    if violations:
        raise SystemExit("lifecycle gate violations: " + "; ".join(violations))


if __name__ == "__main__":
    main()