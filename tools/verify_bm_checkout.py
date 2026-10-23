"""Build and check the BM factories from a clean Linux/WSL checkout.

This is a deterministic build/correctness check, not a performance experiment.
Only the ignored upstream_snapshots/ and builds/ directories are populated.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import re
import shutil
import struct
import subprocess
from datetime import datetime, timedelta, timezone


ROOT = Path(__file__).resolve().parents[1]
INTEGRATION = ROOT / "07_Eval_Integration"
DEPENDENCIES = {
    "benchmark": (
        "https://github.com/zsh20070718/learned-index.git",
        "003a55bf674b43350fbb70242070fb74f33a56ce",
        INTEGRATION / "upstream_snapshots/benchmark_003a55bf",
    ),
    "model": (
        "https://github.com/Zephyr761/Learned-Index-Model.git",
        "3894c767338a865d2920ba5e21283827fea1d803",
        INTEGRATION / "upstream_snapshots/model_3894c767",
    ),
}
CPP_SUFFIXES = {".h", ".hpp", ".c", ".cpp", ".cc", ".inl", ".tpp", ".inc"}


def run(command, *, cwd=ROOT, log=None):
    result = subprocess.run(
        [str(value) for value in command], cwd=cwd, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
    )
    if log is not None:
        log.write_text(result.stdout + result.stderr, encoding="utf-8")
    if result.returncode:
        raise RuntimeError(
            f"Command failed ({result.returncode}): {command[0]}\n"
            + (result.stdout + result.stderr)[-6000:]
        )
    return result.stdout


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def prepare_dependencies(fetch):
    for name, (url, commit, directory) in DEPENDENCIES.items():
        if not directory.exists():
            if not fetch:
                raise RuntimeError(f"Missing {name}; use --prepare-dependencies.")
            directory.parent.mkdir(parents=True, exist_ok=True)
            directory.mkdir()
            run(["git", "init", directory])
            run(["git", "remote", "add", "origin", url], cwd=directory)
            run(["git", "fetch", "--depth", "1", "origin", commit], cwd=directory)
            run(["git", "checkout", "--detach", commit], cwd=directory)
        actual = run(["git", "rev-parse", "HEAD"], cwd=directory).strip()
        if actual != commit:
            raise RuntimeError(f"{name} is at {actual}, expected {commit}; no reset performed.")
        run(["git", "diff", "--quiet"], cwd=directory)
        run(["git", "diff", "--cached", "--quiet"], cwd=directory)
        if not (directory / "src").is_dir():
            raise RuntimeError(f"{name} source directory is missing.")


def source_manifest():
    tracked = subprocess.check_output(["git", "ls-files", "-z"], cwd=ROOT)
    files = {}
    for raw in tracked.split(b"\0"):
        if raw:
            relative = os.fsdecode(raw)
            path = ROOT / relative
            if path.is_file() and path.suffix in CPP_SUFFIXES:
                files[relative] = sha(path)
    dependencies = {}
    for name, (_, commit, directory) in DEPENDENCIES.items():
        dependencies[name] = {
            "commit": commit,
            "sources": {
                path.relative_to(directory).as_posix(): sha(path)
                for path in sorted((directory / "src").rglob("*"))
                if path.is_file() and path.suffix in CPP_SUFFIXES
            },
        }
    return {"project_sources": files, "dependencies": dependencies}


def fixtures(output):
    # Keep 4/5 of a real byte-order key universe, leaving distinct insertion keys.
    universe = [f"key{i:09d}".encode() for i in range(10240)]
    rows = [(key, i) for i, key in enumerate(universe) if i % 5 != 4]
    held = [key for i, key in enumerate(universe) if i % 5 == 4]
    operations = [(2, key, 20000 + i, 0) for i, key in enumerate(held[:512])]
    operations += [(3, rows[i][0], 30000 + i, 0) for i in range(200)]
    operations += [(4, rows[i][0], 0, 0) for i in range(200, 300)]
    operations += [(2, key, 40000 + i, 0) for i, key in enumerate(held[512:612])]
    operations += [(1, key, 0, 0) for key, _ in rows[::4]]
    operations += [(1, b"absent", 0, 0), (5, b"", 0, 16), (5, rows[400][0], 0, 128)]
    random.Random(20261024).shuffle(operations)
    rows_path, ops_path = output / "rows.bin", output / "operations.bin"
    with rows_path.open("wb") as stream:
        stream.write(b"SIBROW1\0" + struct.pack("<Q", len(rows)))
        for key, value in rows:
            stream.write(struct.pack("<Q", len(key)) + key + struct.pack("<Q", value))
    with ops_path.open("wb") as stream:
        stream.write(b"SIBOPS1\0" + struct.pack("<Q", len(operations)))
        for kind, key, value, limit in operations:
            stream.write(struct.pack("<BQQQ", kind, value, limit, len(key)) + key)
    reference = dict(rows)
    checksum, mask = 0, (1 << 64) - 1
    for kind, key, value, limit in operations:
        if kind == 1:
            answer = reference.get(key, mask)
        elif kind == 2:
            answer = 2 if key in reference else 1
            if answer == 1:
                reference[key] = value
        elif kind == 3:
            answer = 3 if key in reference else 4
            if answer == 3:
                reference[key] = value
        elif kind == 4:
            answer = 5 if key in reference else 6
            reference.pop(key, None)
        else:
            answer = min(limit, sum(existing >= key for existing in reference))
        checksum = (checksum * 131 + answer) & mask
    return rows_path, ops_path, checksum


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prepare-dependencies", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if os.name == "nt":
        parser.error("Run with Linux Python, for example inside WSL Ubuntu.")
    for tool in ("git", "g++", "gcc"):
        if shutil.which(tool) is None:
            parser.error(f"Required program is missing: {tool}")
    if run(["git", "status", "--porcelain", "--untracked-files=all"]).strip():
        parser.error("Use a clean checkout; this command does not stage or reset source files.")
    prepare_dependencies(args.prepare_dependencies)
    stamp = datetime.now(timezone(timedelta(hours=8))).strftime("%Y%m%d_%H%M%S")
    output = (args.output or ROOT / "builds" / ("bm_checkout_" + stamp)).resolve()
    output.mkdir(parents=True, exist_ok=False)
    identity = source_manifest()
    bundle = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()
    benchmark = DEPENDENCIES["benchmark"][2] / "src"
    model = DEPENDENCIES["model"][2] / "src"
    diag = INTEGRATION / "diagnostics"
    validation = ROOT / "02_Code_and_Experiments/validation_e25_20260918"
    includes = [validation, benchmark, model, diag, INTEGRATION / "adapters/art",
                INTEGRATION / "adapters/our_index", validation / "third_party/vendor/libart/src"]
    flags = ["-pthread", "-std=c++17", "-O2", "-march=x86-64-v3"]
    flags += ["-I" + str(directory) for directory in includes]
    definitions = {
        "BM_MODEL_COMMIT": DEPENDENCIES["model"][1],
        "BM_CORE_BUNDLE_SHA256": bundle, "BM_SPLIT_CORE_SHA256": bundle,
        "BM_EXPERIMENT_ADAPTER_SHA256": sha(diag / "bm_experiment_adapter.hpp"),
        "BM_SPLIT_ADAPTER_SHA256": sha(diag / "bm_split_adapter.hpp"),
        "COUPLED_CORE_SHA256": bundle,
        "COUPLED_ADAPTER_SHA256": sha(diag / "coupled_page_adapter.hpp"),
    }
    flags += [f'-D{name}="{value}"' for name, value in definitions.items()]
    commands = []

    def execute(command, log):
        commands.append([str(x) for x in command])
        return run(command, log=output / log)

    contracts = {}
    for name, header, factory in (
        ("baseline", "bm_experiment_adapter.hpp", "make_bm_experiment_index"),
        ("candidate", "bm_split_adapter.hpp", "make_bm_split_experiment_index"),
    ):
        executable = output / (name + "_contract")
        macros = [f'-DACCEPTANCE_FACTORY_HEADER="{header}"',
                  "-DACCEPTANCE_FACTORY_SYMBOL=" + factory]
        if name == "baseline":
            macros.append("-DACCEPTANCE_SAMPLE_PARAMETER=lookup")
        execute(["g++", *flags, *macros, INTEGRATION / "adapters/common/contract_acceptance.cpp",
                 model / "block_model.cpp", "-o", executable], name + "_build.log")
        text = execute([executable], name + "_contract.log")
        match = re.search(r"(\d+)/(\d+) checks passed", text)
        if not match or match[1] != match[2]:
            raise RuntimeError(f"{name} contract did not report a complete pass.")
        contracts[name] = match.group(0)
        print(name, contracts[name], flush=True)
    header_source = output / "header_closure.cpp"
    header_source.write_text('#include "bm_experiment_adapter.hpp"\n'
                             '#include "coupled_page_adapter.hpp"\n', encoding="utf-8")
    execute(["g++", *flags, "-fsyntax-only", header_source], "header_closure.log")
    rows_path, ops_path, expected = fixtures(output)
    verify = output / "split_verify"
    execute(["g++", *flags, diag / "gate_bm_split_verify.cpp", model / "block_model.cpp",
             "-o", verify], "verify_build.log")
    verification = json.loads(execute([verify, rows_path, "2048"], "split_verify.log"))
    if verification["mismatches"] or verification["inserted"] != 2048:
        raise RuntimeError("Bounded split verification failed.")
    art = output / "art.o"
    execute(["gcc", "-std=c11", "-DNDEBUG", "-O2", "-march=x86-64-v3",
             "-I" + str(includes[-1]), includes[-1] / "art.c", "-c", "-o", art], "art_build.log")
    replay = output / "replay"
    execute(["g++", *flags, diag / "bench_bm_pure.cpp", model / "block_model.cpp",
             INTEGRATION / "adapters/art/art_adapter.cpp",
             INTEGRATION / "adapters/our_index/our_index_adapter.cpp", art,
             benchmark / "unified/runner.cpp", "-o", replay], "replay_build.log")
    records = [json.loads(line) for line in execute(
        [replay, rows_path, ops_path, "0"], "replay.jsonl").splitlines() if line]
    if {record["arm"] for record in records} != {"bm", "bm_split", "art", "ctx16"}:
        raise RuntimeError("Replay did not cover all four arms.")
    if any(record["checksum"] != expected or record["checksum_aa"] != expected for record in records):
        raise RuntimeError("Replay differs from the independent reference checksum.")
    identity.update(project_commit=run(["git", "rev-parse", "HEAD"]).strip(),
                    source_bundle_sha256=bundle,
                    compiler=run(["g++", "--version"]).splitlines()[0], commands=commands)
    (output / "identity.json").write_text(json.dumps(identity, indent=2) + "\n", encoding="utf-8")
    report = {"purpose": "build/correctness smoke; no performance claim", "contracts": contracts,
              "header_closure": "passed", "bounded_split_verify": verification,
              "replay_arms": sorted(record["arm"] for record in records),
              "reference_checksum": expected, "passed": True}
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"output": str(output), **report}, indent=2))


if __name__ == "__main__":
    main()
