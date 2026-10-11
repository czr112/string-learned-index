"""Bounded structure diagnosis; keep counted work separate from clean A/B time."""
import hashlib
import json
from pathlib import Path
import statistics
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = ROOT.parent / "02_Code_and_Experiments/validation_e25_20260918"

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def run(cmd):
    result = subprocess.run(cmd, cwd=ROOT, text=True, capture_output=True, timeout=180)
    if result.returncode:
        raise RuntimeError(result.stderr or result.stdout)
    return result.stdout

def main():
    output = ROOT / "results/structure_lookup_20261009"
    output.mkdir(exist_ok=False)
    build = ROOT / "diagnostics/build"
    build.mkdir(exist_ok=True)
    common = ["g++", "-O3", "-std=c++17", "-march=x86-64-v3",
              "-I" + str(UPSTREAM), "-Ilearned-index/src",
              "diagnostics/lookup_structure.cpp", "learned-index/src/unified/runner.cpp"]
    identity = {}
    paths = [ROOT / "diagnostics/lookup_structure.cpp", Path(__file__),
             ROOT / "learned-index/src/unified/runner.cpp"]
    paths += list(UPSTREAM.rglob("*.hpp"))
    for path in paths:
        identity[str(path.relative_to(ROOT.parent))] = digest(path)
    metadata = {"source_sha256": identity, "construction": "adapter ordinary bulk_load",
                "timing": "5 process blocks x 3 rotated repeats; 100 full-query passes; exploratory",
                "variants": {"0": "current sequential subblocks", "1": "existing boundary directory",
                             "2": "precomputed region IDs; diagnostic bound, not deployable"},
                "compiler": run(["g++", "--version"]).splitlines()[0], "commands": []}
    for name, defines in [("diag", ["-DE12_COUNT", "-DE25_DIAG"]), ("clean", [])]:
        command = common + defines + ["-o", str(build / ("lookup_structure_" + name))]
        metadata["commands"].append(command)
        run(command)
    affinity = sorted(__import__("os").sched_getaffinity(0))
    cpu = affinity[0]
    metadata["cpu"] = cpu
    metadata["binaries"] = {name: digest(build / ("lookup_structure_" + name)) for name in ("diag", "clean")}
    work, timing, ratios = [], [], []
    for corpus in ("mixed", "skew", "tranco"):
        inputs = ROOT / ("results/matrix2_" + corpus) / "interchange"
        metadata[corpus] = {name: digest(inputs / name) for name in ("rows.bin", "queries.bin")}
        for backend in ("context16", "lits"):
            args = [str(inputs / "rows.bin"), str(inputs / "queries.bin"), backend]
            counted = [json.loads(x) for x in run([str(build / "lookup_structure_diag")] + args + ["0"]).splitlines()]
            assert len(counted) == 2 and counted[0]["checksum"] == counted[1]["checksum"]
            for row in counted:
                row["corpus"] = corpus
                work.append(row)
            case_ratios = {1: [], 2: []}
            for process in range(5):
                records = [json.loads(x) for x in run(["taskset", "-c", str(cpu), str(build / "lookup_structure_clean")] + args + ["3"]).splitlines()]
                clean_work = [x for x in records if x["phase"] == "work"]
                assert all(x["checksum"] == counted[0]["checksum"] and
                           x["overflow_records"] == counted[0]["overflow_records"] for x in clean_work)
                measured = [x for x in records if x["phase"] == "paired"]
                assert len(measured) == 9 and len({x["checksum"] for x in measured}) == 1
                for row in measured:
                    row.update(corpus=corpus, process=process)
                    timing.append(row)
                by_key = {(x["repeat"], x["variant"]): x for x in measured}
                for variant in (1, 2):
                    paired = [by_key[rep, variant]["elapsed_ns"] / by_key[rep, 0]["elapsed_ns"] for rep in range(3)]
                    case_ratios[variant].append(statistics.median(paired))
            for variant, values in case_ratios.items():
                ratios.append(dict(corpus=corpus, backend=backend, variant=variant,
                                   process_medians=values, median=statistics.median(values),
                                   minimum=min(values), maximum=max(values), faster=sum(v < 1 for v in values)))
            print(corpus, backend, "done", flush=True)
            (output / "work.json").write_text(json.dumps(work, indent=2))
            (output / "timing.json").write_text(json.dumps(timing, indent=2))
            (output / "ratios.json").write_text(json.dumps(ratios, indent=2))
    assert all(digest(ROOT.parent / path) == value for path, value in identity.items()), "source drift during diagnosis"
    (output / "identity.json").write_text(json.dumps(metadata, indent=2))
    print(output)

if __name__ == "__main__":
    main()
