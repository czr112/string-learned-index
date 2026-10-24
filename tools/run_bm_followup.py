"""S0 acceptance for the CURRENT bounded BM public factories (Linux/WSL).

Uses pinned, already available dependencies and corpus/legacy traces. Does not time
operations, change defaults, fetch updates, stage files, or modify input directories.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]
DIAG = ROOT / "07_Eval_Integration/diagnostics"
PINS = {"benchmark": "003a55bf674b43350fbb70242070fb74f33a56ce",
        "model": "3894c767338a865d2920ba5e21283827fea1d803"}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save(path, value):
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dependency-root", required=True, type=Path)
    parser.add_argument("--trace-root", required=True, type=Path)
    parser.add_argument("--corpus-root", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    if os.name == "nt":
        parser.error("Run with Linux Python inside WSL.")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    commands, report = [], {"status": "running", "purpose": "S0 functional/space acceptance; no performance claim",
                            "contracts": {}, "fault": {}, "dense_factory": {}, "differential": {}, "space": {}, "sanitizers": {}}

    def execute(command, name, *, environment=None, expect_failure=False):
        command = [str(x) for x in command]
        commands.append(command)
        with (output / name).open("w", encoding="utf-8") as stream:
            result = subprocess.run(command, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT,
                                    env=environment, text=True, check=False)
        if bool(result.returncode) != expect_failure:
            raise RuntimeError(f"{name}: exit {result.returncode}; see retained log")
        return (output / name).read_text(encoding="utf-8")

    def records(command, name, *, environment=None):
        return [json.loads(line) for line in execute(command, name, environment=environment).splitlines() if line.strip()]

    def checkpoint(message):
        save(output / "report.json", report)
        save(output / "commands.json", commands)
        print(message, flush=True)

    try:
        deps = {name: (args.dependency_root / f"{name}_{commit[:8]}").resolve()
                for name, commit in PINS.items()}
        for name, directory in deps.items():
            actual = execute(["git", "-C", directory, "rev-parse", "HEAD"], f"{name}_commit.log").strip()
            if actual != PINS[name]:
                raise RuntimeError(f"{name}: unexpected commit {actual}")
            execute(["git", "-C", directory, "diff", "--exit-code", "HEAD", "--", "src"], f"{name}_diff.log")
            untracked = execute(["git", "-C", directory, "ls-files", "--others", "--exclude-standard", "--", "src"], f"{name}_untracked.log")
            if untracked.strip():
                raise RuntimeError(f"{name}: untracked source files")
        includes = [DIAG, ROOT / "02_Code_and_Experiments/validation_e25_20260918",
                    deps["benchmark"] / "src", deps["model"] / "src"]
        common = ["-std=c++17", "-pthread", "-march=x86-64-v3", *["-I" + str(p) for p in includes]]
        definitions = {"BM_MODEL_COMMIT": PINS["model"], "BM_CORE_BUNDLE_SHA256": "dependency-scan",
                       "BM_SPLIT_CORE_SHA256": "dependency-scan",
                       "BM_EXPERIMENT_ADAPTER_SHA256": sha(DIAG / "bm_experiment_adapter.hpp"),
                       "BM_SPLIT_ADAPTER_SHA256": sha(DIAG / "bm_split_adapter.hpp")}

        def macros():
            return [f'-D{key}="{value}"' for key, value in definitions.items()]

        gate_source = DIAG / "gate_bm_followup.cpp"
        model_source = deps["model"] / "src/block_model.cpp"
        closure = {}
        for name, source in (("gate", gate_source), ("model", model_source)):
            depfile = output / (name + "_closure.d")
            execute(["g++", *common, *macros(), "-MM", "-MF", depfile, source], name + "_closure.log")
            body = depfile.read_text().replace("\\\n", " ").split(":", 1)[1]
            for value in shlex.split(body):
                path = Path(value).resolve()
                if path.is_relative_to(ROOT):
                    label = "project/" + path.relative_to(ROOT).as_posix()
                else:
                    for dep_name, directory in deps.items():
                        if path.is_relative_to(directory):
                            label = dep_name + "/" + path.relative_to(directory).as_posix()
                            break
                    else:
                        raise RuntimeError(f"Unclassified dependency: {path}")
                closure[label] = {"path": str(path), "sha256": sha(path)}
        bundle = hashlib.sha256(json.dumps({key: val["sha256"] for key, val in closure.items()}, sort_keys=True).encode()).hexdigest()
        definitions.update(BM_CORE_BUNDLE_SHA256=bundle, BM_SPLIT_CORE_SHA256=bundle)
        # A managed Windows worktree has an absolute Windows gitdir in its .git file.
        # Read the same HEAD through its WSL mount; never rewrite Git metadata.
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
                    "supporting_sources": {
                        relative: sha(ROOT / relative) for relative in (
                            "07_Eval_Integration/adapters/common/contract_acceptance.cpp",
                            "07_Eval_Integration/diagnostics/gate_bm_split_verify.cpp")},
                    "compiler": execute(["g++", "--version"], "compiler.log").splitlines()[0]}
        save(output / "identity.json", identity)
        checkpoint(f"Pinned sources verified; {len(closure)} dependency files bound to {bundle[:12]}")

        def build(sanitizer=False):
            prefix = "san" if sanitizer else "opt"
            flags = [*common, *macros(), *( ["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
                                        if sanitizer else ["-O2"]) ]
            objects = []
            for name, source in (("gate", gate_source), ("model", model_source)):
                obj = output / f"{prefix}_{name}.o"
                execute(["g++", *flags, "-MMD", "-MF", output / f"{prefix}_{name}.d", "-c", source, "-o", obj], f"{prefix}_{name}_build.log")
                objects.append(obj)
            executable = output / (prefix + "_gate")
            execute(["g++", *flags, *objects, "-o", executable], prefix + "_link.log")
            return executable, objects[1]

        gate, model_object = build()
        for name, header, factory in (("baseline", "bm_experiment_adapter.hpp", "make_bm_experiment_index"),
                                      ("candidate", "bm_split_adapter.hpp", "make_bm_split_experiment_index")):
            executable = output / (name + "_contract")
            extra = [f'-DACCEPTANCE_FACTORY_HEADER="{header}"', "-DACCEPTANCE_FACTORY_SYMBOL=" + factory]
            if name == "baseline":
                extra.append("-DACCEPTANCE_SAMPLE_PARAMETER=lookup")
            execute(["g++", *common, *macros(), "-O2", *extra, ROOT / "07_Eval_Integration/adapters/common/contract_acceptance.cpp",
                     model_object, "-o", executable], name + "_contract_build.log")
            text = execute([executable], name + "_contract.log")
            match = re.search(r"(\d+)/(\d+) checks passed", text)
            if not match or match[1] != match[2]:
                raise RuntimeError(f"{name}: incomplete public contract")
            report["contracts"][name] = match[0]
        checkpoint("Current factories passed public contracts")

        corpora = {"words": args.corpus_root / "english_words_rows.bin",
                   "urls": args.corpus_root / "memetracker_urls_rows.bin",
                   "phrases": args.corpus_root / "memetracker_phrases_rows.bin"}
        report["inputs"] = {key: {"path": str(path), "sha256": sha(path)} for key, path in corpora.items()}
        for name, path in {"bytes": "--bytes", **corpora}.items():
            report["fault"][name] = records([gate, "fault", path], f"fault_{name}.jsonl")
            checkpoint(f"Exhaustive allocation-ordinal fault gate passed: {name}")
        for name, path in corpora.items():
            for count in (100, 1000, 3000):
                report["dense_factory"][f"{name}_{count}"] = records([gate, "dense", path, count], f"dense_{name}_{count}.jsonl")
            checkpoint(f"Public-factory dense 100/1000/3000 gates passed: {name}")
        report["dense_factory"]["bytes_3000"] = records([gate, "dense", "--bytes", "3000"], "dense_bytes_3000.jsonl")

        manifest_path = args.trace_root / "manifest.json"
        manifest = json.loads(manifest_path.read_text())
        if len(manifest) != 24:
            raise RuntimeError("S0 preregistration expects exactly 24 legacy traces")
        report["legacy_trace_manifest_sha256"] = sha(manifest_path)
        report["trace_scope"] = "Legacy distributions: functional/space coverage only; no fairness or performance acceptance"
        for cell, metadata in manifest.items():
            paths = [args.trace_root / "traces" / Path(metadata[key]).name for key in ("rows_path", "operations_path")]
            for path, hash_key in zip(paths, ("rows_sha256", "operations_sha256")):
                if sha(path) != metadata[hash_key]:
                    raise RuntimeError(f"Trace hash mismatch: {cell}/{hash_key}")
            report["differential"][cell] = records([gate, "differential", *paths], "diff_" + cell + ".jsonl")
            arms = records([gate, "space", *paths], "space_" + cell + ".jsonl")
            baseline, candidate = arms
            ratios = {key: candidate[key] / baseline[key] for key in (
                "terminal_requested", "build_peak_requested", "maintenance_peak_requested", "overall_peak_requested")}
            report["space"][cell] = {"arms": arms, "candidate_over_baseline": ratios,
                                     "resident_pass": ratios["terminal_requested"] <= 1.05,
                                     "new_maintenance_peak_pass": ratios["maintenance_peak_requested"] <= 1.10}
            checkpoint(f"Differential and space measured: {cell}")

        # Supplemental internal invariant test uses the bounded class directly, not the factory.
        verify = output / "bounded_verify"
        execute(["g++", *common, *macros(), "-O2", DIAG / "gate_bm_split_verify.cpp", model_object, "-o", verify], "verify_build.log")
        report["supplemental_bounded_verify"] = {}
        for name, path in corpora.items():
            report["supplemental_bounded_verify"][name] = records([verify, path, "3000"], "verify_" + name + ".jsonl")
            item = report["supplemental_bounded_verify"][name][0]
            if item["mismatches"] or item["inserted"] != 3000:
                raise RuntimeError(f"Direct bounded verify failed: {name}")

        sanitizer, _ = build(sanitizer=True)
        environment = dict(os.environ, ASAN_OPTIONS="detect_leaks=1:halt_on_error=1", UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
        # Verify that the requested-byte wrapper preserves ASan's user-buffer boundary.
        probe_source = output / "redzone_probe.cpp"
        probe_source.write_text('#include "bm_followup_memory.hpp"\n'
                                'int main(){auto* p=static_cast<volatile unsigned char*>(::operator new(8));'
                                'p[8]=1;return p[0];}\n', encoding="utf-8")
        probe = output / "redzone_probe"
        execute(["g++", "-std=c++17", "-O1", "-g", "-fsanitize=address", "-I" + str(DIAG), probe_source, "-o", probe], "redzone_probe_build.log")
        probe_log = execute([probe], "redzone_probe_expected_failure.log", environment=environment, expect_failure=True)
        if "ERROR: AddressSanitizer: use-after-poison" not in probe_log:
            raise RuntimeError("Accounting wrapper did not preserve ASan buffer boundary")
        report["sanitizers"]["allocator_redzone_negative_control"] = "expected use-after-poison detected"
        report["sanitizers"]["byte_fault"] = records([sanitizer, "fault", "--bytes"], "san_fault_bytes.jsonl", environment=environment)
        for name, path in corpora.items():
            report["sanitizers"]["dense_" + name] = records([sanitizer, "dense", path, "3000"], "san_dense_" + name + ".jsonl", environment=environment)
        for dataset in ("words", "urls", "words_indep"):
            for semantics in ("growth", "replacement"):
                cell = dataset + "_95_5_uniform_" + semantics
                meta = manifest[cell]
                paths = [args.trace_root / "traces" / Path(meta[key]).name for key in ("rows_path", "operations_path")]
                report["sanitizers"][cell] = records([sanitizer, "differential", *paths], "san_" + cell + ".jsonl", environment=environment)
        for item in closure.values():
            if sha(Path(item["path"])) != item["sha256"]:
                raise RuntimeError("Source changed during acceptance run")
        if sha(Path(__file__)) != identity["runner_sha256"]:
            raise RuntimeError("Runner changed during acceptance run")
        for relative, digest in identity["supporting_sources"].items():
            if sha(ROOT / relative) != digest:
                raise RuntimeError("Supporting test source changed during acceptance run")
        all_space = list(report["space"].values())
        report.update(status="complete", correctness_pass=True,
                      resident_budget_pass=all(item["resident_pass"] for item in all_space),
                      new_maintenance_peak_budget_pass=all(item["new_maintenance_peak_pass"] for item in all_space),
                      finished_at=datetime.now(timezone.utc).isoformat())
        report["acceptance_pass"] = report["resident_budget_pass"] and report["new_maintenance_peak_budget_pass"]
        checkpoint("S0 acceptance finished; inspect resident and NEW maintenance-peak budgets separately")
        if not report["acceptance_pass"]:
            raise RuntimeError("Requested-byte budget failed; functional results retained separately")
    except Exception as error:
        report.update(status="failed", error=str(error), finished_at=datetime.now(timezone.utc).isoformat())
        checkpoint("S0 stopped: " + str(error))
        raise


if __name__ == "__main__":
    main()
