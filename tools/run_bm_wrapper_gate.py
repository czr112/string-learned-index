"""Paired wrapper-cost gate for make_bm_split_experiment_index (Linux/WSL, timing).

Preregistered analysis (corrected 2026-10-25: the first version's AA condition was
vacuous - median<=1.05 implied it - so the verdict below re-binds to fresh data and
the earlier median-only claim is retracted):
  unit  = one process; per process the six paired repeats are averaged in log space;
  W     = per-process log(adapter/core) values -> geometric mean with t CI (df=4);
  AA    = per-process log(adapter_AA/adapter) and log(core_AA/core), same CI each;
  AA quality pass = both AA CIs inside [1/1.05, 1.05];
  verdict: pass if CI(W) high <= 1.05; regression if CI(W) low > 1.05;
           undecided if the CI straddles 1.05;
           measurement_unresolved if the AA quality check fails (no claim either way).
This mirrors the statistics of tools/run_bm_query_recheck.py (ci95/aa_summary/comparison).
The gate binary also re-checks answers, structure bytes and zero-allocation find.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shlex
import statistics
import subprocess

ROOT = Path(__file__).resolve().parents[1]
DIAG = ROOT / "07_Eval_Integration/diagnostics"
PINS = {"benchmark": "003a55bf674b43350fbb70242070fb74f33a56ce",
        "model": "3894c767338a865d2920ba5e21283827fea1d803"}
CORPORA = {"words": "english_words", "urls": "memetracker_urls", "phrases": "memetracker_phrases"}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def save(path, value):
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def ci95(log_values):
    n = len(log_values)
    critical = {4: 2.7764451052, 15: 2.1314495456}.get(n - 1)  # t(0.975) for 5 or 16 processes
    if critical is None:
        raise RuntimeError(f"unregistered number of independent processes: {n}")
    mean = statistics.fmean(log_values)
    half = critical * statistics.stdev(log_values) / math.sqrt(n)
    return {"n": n, "df": n - 1, "ratio": math.exp(mean), "ci95": [math.exp(mean - half), math.exp(mean + half)]}


def aa_summary(log_values):
    value = ci95(log_values)
    value["equivalent_within_5pct"] = value["ci95"][0] >= 1 / 1.05 and value["ci95"][1] <= 1.05
    return value


def verdict(paired, aa_adapter, aa_core, limit=1.05):
    value = ci95(paired)
    value.update(limit=limit, aa_adapter=aa_adapter, aa_core=aa_core,
                 aa_quality_pass=aa_adapter["equivalent_within_5pct"] and aa_core["equivalent_within_5pct"])
    low, high = value["ci95"]
    value["interval_verdict"] = "pass" if high <= limit else "regression" if low > limit else "undecided"
    value["verdict"] = value["interval_verdict"] if value["aa_quality_pass"] else "measurement_unresolved"
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dependency-root", required=True, type=Path)
    parser.add_argument("--corpus-root", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--processes", type=int, default=16,
                        help="independent processes per corpus (default 16, the repo's block convention; "
                             "5-process runs measured AA noise beyond +/-5% and stayed unresolved)")
    args = parser.parse_args()
    if os.name == "nt":
        parser.error("Run with Linux Python inside WSL.")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    commands, binaries = [], {}
    report = {"status": "running", "purpose": "paired wrapper-cost gate for make_bm_split_experiment_index",
              "preregistered_verdict": "process-level t CI vs 1.05 with independent AA quality check; "
                                       "corrected 2026-10-25 after the first version's vacuous AA condition (see docstring)",
              "processes_per_corpus": args.processes, "binaries": {}, "corpora": {}}

    def execute(command, name, *, environment=None):
        command = [str(x) for x in command]
        for token in command:
            if token in binaries and sha(token) != binaries[token]:
                raise RuntimeError(f"built binary changed before run: {token}")
        commands.append(command)
        with (output / name).open("w", encoding="utf-8") as stream:
            result = subprocess.run(command, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT,
                                    env=environment, text=True, check=False)
        if result.returncode:
            raise RuntimeError(f"{name}: exit {result.returncode}; see retained log")
        return (output / name).read_text(encoding="utf-8")

    def checkpoint(message):
        save(output / "report.json", report)
        save(output / "commands.json", commands)
        print(message, flush=True)

    try:
        deps = {name: (args.dependency_root / f"{name}_{commit[:8]}").resolve()
                for name, commit in PINS.items()}
        for name, directory in deps.items():
            if execute(["git", "-C", directory, "rev-parse", "HEAD"], f"{name}_commit.log").strip() != PINS[name]:
                raise RuntimeError(f"{name}: unexpected commit")
            execute(["git", "-C", directory, "diff", "--exit-code", "HEAD", "--", "src"], f"{name}_diff.log")
            if execute(["git", "-C", directory, "ls-files", "--others", "--exclude-standard", "--", "src"],
                       f"{name}_untracked.log").strip():
                raise RuntimeError(f"{name}: untracked source files")
        includes = [DIAG, ROOT / "02_Code_and_Experiments/validation_e25_20260918",
                    deps["benchmark"] / "src", deps["model"] / "src"]
        common = ["-std=c++17", "-pthread", "-march=x86-64-v3", *["-I" + str(p) for p in includes]]
        definitions = {"BM_MODEL_COMMIT": PINS["model"], "BM_CORE_BUNDLE_SHA256": "dependency-scan",
                       "BM_SPLIT_CORE_SHA256": "dependency-scan",
                       "BM_EXPERIMENT_ADAPTER_SHA256": sha(DIAG / "bm_experiment_adapter.hpp"),
                       "BM_SPLIT_ADAPTER_SHA256": sha(DIAG / "bm_split_adapter.hpp")}
        macros = lambda: [f'-D{key}="{value}"' for key, value in definitions.items()]
        gate_source = DIAG / "check_bm_split_wrapper.cpp"
        model_source = deps["model"] / "src/block_model.cpp"
        runner_source = deps["benchmark"] / "src/unified/runner.cpp"
        closure = {}
        for name, source in (("gate", gate_source), ("model", model_source), ("runner", runner_source)):
            depfile = output / (name + "_closure.d")
            execute(["g++", *common, *macros(), "-MM", "-MF", depfile, source], name + "_closure.log")
            body = depfile.read_text().replace("\\\n", " ").split(":", 1)[1]
            for value in shlex.split(body):
                path = Path(value).resolve()
                for label, base in (("benchmark", deps["benchmark"]), ("model", deps["model"]), ("project", ROOT)):
                    if path.is_relative_to(base):
                        closure[label + "/" + path.relative_to(base).as_posix()] = {"path": str(path), "sha256": sha(path)}
                        break
                else:
                    raise RuntimeError(f"Unclassified dependency: {path}")
        bundle = hashlib.sha256(json.dumps({k: v["sha256"] for k, v in closure.items()}, sort_keys=True).encode()).hexdigest()
        definitions.update(BM_CORE_BUNDLE_SHA256=bundle, BM_SPLIT_CORE_SHA256=bundle)
        git = ["git"]
        git_file = ROOT / ".git"
        if git_file.is_file():
            match = re.fullmatch(r"gitdir: ([A-Za-z]):[/\\](.*)", git_file.read_text().strip())
            if match:
                git_dir = "/mnt/" + match[1].lower() + "/" + match[2].replace("\\", "/")
                git += ["--git-dir=" + git_dir, "--work-tree=" + str(ROOT)]
        identity = {"base_commit": execute([*git, "rev-parse", "HEAD"], "project_commit.log").strip(),
                    "dependency_commits": PINS, "closure": closure, "source_bundle_sha256": bundle,
                    "runner_sha256": sha(Path(__file__)), "definitions": definitions,
                    "compiler": execute(["g++", "--version"], "compiler.log").splitlines()[0]}
        save(output / "identity.json", identity)
        checkpoint(f"Sources bound to {bundle[:12]}")

        objects = []
        for name, source in (("gate", gate_source), ("model", model_source), ("runner", runner_source)):
            obj = output / (name + ".o")
            execute(["g++", *common, *macros(), "-O2", "-MMD", "-MF", output / (name + ".d"), "-c", source, "-o", obj],
                    name + "_build.log")
            objects.append(obj)
        binary = output / "wrapper_gate"
        staged = output / "wrapper_gate.partial"
        execute(["g++", *common, *macros(), "-O2", *objects, "-o", staged], "link.log")
        os.replace(staged, binary)
        binaries[str(binary)] = sha(binary)
        report["binaries"]["wrapper_gate"] = binaries[str(binary)]
        checkpoint("Wrapper gate built and published atomically")

        for label, stem in CORPORA.items():
            rows = args.corpus_root / f"{stem}_rows.bin"
            queries = args.corpus_root / f"{stem}_queries.bin"
            report.setdefault("inputs", {})[label] = {"rows_sha256": sha(rows), "queries_sha256": sha(queries)}
            per_process = {}
            for process in range(args.processes):
                text = execute(["taskset", "-c", "0", binary, rows, queries, str(process)], f"{label}_{process}.log")
                lines = [json.loads(line) for line in text.splitlines() if line.strip()]
                gate = [item for item in lines if item.get("phase") == "gate"]
                if len(gate) != 1 or gate[0]["mismatches"] or gate[0]["find_allocations"]:
                    raise RuntimeError(f"{label}/{process}: functional part of the gate failed")
                times = {}
                for item in lines:
                    if item.get("phase") == "pass":
                        times.setdefault(item["repeat"], {})[item["arm"]] = item["ns_per_operation"]
                wrapper, aa_adapter, aa_core = [], [], []
                for repeat, arms in sorted(times.items()):
                    if set(arms) != {"adapter", "adapter_AA", "core", "core_AA"}:
                        raise RuntimeError(f"{label}/{process}: incomplete arm set in repeat {repeat}")
                    wrapper.append(math.log(arms["adapter"] / arms["core"]))
                    aa_adapter.append(math.log(arms["adapter_AA"] / arms["adapter"]))
                    aa_core.append(math.log(arms["core_AA"] / arms["core"]))
                if len(wrapper) != 8:
                    raise RuntimeError(f"{label}/{process}: expected eight repeats")
                per_process[process] = {"wrapper": statistics.fmean(wrapper),
                                        "aa_adapter": statistics.fmean(aa_adapter),
                                        "aa_core": statistics.fmean(aa_core)}
            result = verdict([item["wrapper"] for item in per_process.values()],
                             aa_summary([item["aa_adapter"] for item in per_process.values()]),
                             aa_summary([item["aa_core"] for item in per_process.values()]))
            result["processes"] = args.processes
            result["per_process_geometric_ratios"] = [round(math.exp(item["wrapper"]), 4)
                                                      for item in per_process.values()]
            report["corpora"][label] = result
            checkpoint(f"{label}: ratio {result['ratio']:.4f} CI [{result['ci95'][0]:.4f},"
                       f"{result['ci95'][1]:.4f}] AA {'ok' if result['aa_quality_pass'] else 'FAILED'}"
                       f" -> {result['verdict']}")

        for item in closure.values():
            if sha(item["path"]) != item["sha256"]:
                raise RuntimeError("Source changed during wrapper gate run")
        if sha(Path(__file__)) != identity["runner_sha256"]:
            raise RuntimeError("Runner changed during wrapper gate run")
        for executable, digest in binaries.items():
            if sha(executable) != digest:
                raise RuntimeError("Built binary changed during wrapper gate run")
        report["status"] = "complete"
        report["all_pass"] = all(item["verdict"] == "pass" for item in report["corpora"].values())
        report["finished_at"] = datetime.now(timezone.utc).isoformat()
        checkpoint("Wrapper gate finished")
    except Exception as error:
        report.update(status="failed", error=str(error), finished_at=datetime.now(timezone.utc).isoformat())
        checkpoint("Wrapper gate stopped: " + str(error))
        raise


if __name__ == "__main__":
    main()
