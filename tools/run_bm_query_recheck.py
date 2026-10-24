"""Frozen-input public-factory query recheck; Linux/WSL, no index algorithm edits.

Protocol: docs/PROTOCOL_BM_QUERY_RECHECK_20261024.md. All outputs use a NEW directory.
"""
from __future__ import annotations

import argparse
from bisect import bisect_right
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import random
import re
import shlex
import struct
import subprocess
import statistics

ROOT = Path(__file__).resolve().parents[1]
DIAG = ROOT / "07_Eval_Integration/diagnostics"
PROTOCOL = ROOT / "docs/PROTOCOL_BM_QUERY_RECHECK_20261024.md"
MASK = (1 << 64) - 1
PINS = {"benchmark": "003a55bf674b43350fbb70242070fb74f33a56ce",
        "model": "3894c767338a865d2920ba5e21283827fea1d803"}
STATES = ("bm_dynamic", "candidate_dynamic", "bm_snapshot", "candidate_snapshot")
SOURCES = {
    "words": "bm_real_20261018/interchange/english_words_rows.bin",
    "urls": "bm_real_20261018/interchange/memetracker_urls_rows.bin",
    "words_indep": "bm_space_gen_inputs_20261019/interchange/english_words_s20261019_rows.bin",
    "words_100k": "bm_space_gen_inputs_20261019/interchange/english_words_100000_s20261019_rows.bin",
}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def sha(path):
    return digest(path.read_bytes())


def save(path, value):
    path.write_bytes((json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode())


class Reader:
    def __init__(self, data, magic):
        if len(data) < 16 or data[:8] != magic:
            raise ValueError("Invalid binary input")
        self.data, self.pos = data, 8

    def number(self, size=8):
        if self.pos + size > len(self.data):
            raise ValueError("Truncated number")
        result = int.from_bytes(self.data[self.pos:self.pos + size], "little")
        self.pos += size
        return result

    def key(self):
        size = self.number()
        if size > 0xffffffff or self.pos + size > len(self.data):
            raise ValueError("Truncated/oversized key")
        result = self.data[self.pos:self.pos + size]
        self.pos += size
        return result

    def end(self):
        if self.pos != len(self.data):
            raise ValueError("Trailing binary input")


def read_rows(data):
    stream = Reader(data, b"SIBROW1\0")
    result = [(stream.key(), stream.number()) for _ in range(stream.number())]
    stream.end()
    if any(a[0] >= b[0] for a, b in zip(result, result[1:])):
        raise ValueError("Unsorted/non-unique source")
    return result


def read_ops(data):
    stream = Reader(data, b"SIBOPS1\0")
    result = []
    for _ in range(stream.number()):
        kind, value, limit = stream.number(1), stream.number(), stream.number()
        key = stream.key()
        if kind not in range(1, 6):
            raise ValueError("Unknown operation")
        result.append((kind, key, value, limit))
    stream.end()
    return result


def write_rows(path, rows):
    with path.open("wb") as stream:
        stream.write(b"SIBROW1\0" + struct.pack("<Q", len(rows)))
        for key, value in rows:
            stream.write(struct.pack("<Q", len(key)) + key + struct.pack("<Q", value))


def write_ops(path, operations):
    with path.open("wb") as stream:
        stream.write(b"SIBOPS1\0" + struct.pack("<Q", len(operations)))
        for kind, key, value, limit in operations:
            stream.write(struct.pack("<BQQQ", kind, value, limit, len(key)) + key)


def write_queries(path, lo, hi, groups):
    with path.open("wb") as stream:
        stream.write(b"SIBQRY2\0" + struct.pack("<QQQ", lo, hi, len(groups)))
        for name, values in groups.items():
            label = name.encode("ascii")
            stream.write(struct.pack("<Q", len(label)) + label + struct.pack("<Q", len(values)))
            for key, hit, value, rank in values:
                stream.write(struct.pack("<Q", len(key)) + key + struct.pack("<BQQ", hit, value, rank))


def prepare_case(name, initial, operations, pool, hot_range, output, count=2000, control=False):
    ordered = [key for key, _ in pool]
    rank = {key: i for i, key in enumerate(ordered)}
    lo, hi = hot_range
    if not 0 <= lo <= hi < len(ordered):
        raise ValueError("hot_range outside source pool")
    live = dict(initial)
    if not live or len(live) != len(initial) or any(k not in rank for k in live):
        raise ValueError("Invalid initial map")
    lower = [b""] + [initial[i][0] for i in range(2048, len(initial), 2048)]
    touched = set()
    successful = {"insert": 0, "update": 0, "erase": 0}
    for kind, key, value, _ in operations:
        mutated = False
        if kind == 2 and key not in live:
            if key not in rank:
                raise ValueError("Insertion outside source pool")
            live[key] = value
            successful["insert"] += 1
            mutated = True
        elif kind == 3 and key in live:
            live[key] = value
            successful["update"] += 1
            mutated = True
        elif kind == 4 and key in live:
            del live[key]
            successful["erase"] += 1
            mutated = True
        if mutated:
            touched.add(max(0, bisect_right(lower, key) - 1))
    live_keys, absent_keys = sorted(live), [key for key in ordered if key not in live]
    synthetic = sorted({key for kind, key, _, _ in operations if kind == 1 and key not in live and key not in rank})
    hot = lambda key: lo <= rank[key] <= hi

    def sample(label, values, n=count):
        rng = random.Random(int.from_bytes(hashlib.sha256(("20261024:" + name + ":" + label).encode()).digest()[:8], "little"))
        return rng.sample(values, min(n, len(values)))

    keys = {
        "live_hot": sample("live_hot", [k for k in live_keys if hot(k)]),
        "live_nonhot": sample("live_nonhot", [k for k in live_keys if not hot(k)]),
        "absent_hot": sample("absent_hot", [k for k in absent_keys if hot(k)]),
        "absent_nonhot": sample("absent_nonhot", [k for k in absent_keys if not hot(k)]),
        "synthetic_absent": sample("synthetic_absent", synthetic),
    }
    mixed_n = min(count, len(live_keys) * 10 // 9, len(synthetic) * 10)
    mixed_n -= mixed_n % 10
    if mixed_n <= 0:
        raise ValueError("Insufficient synthetic miss/hit keys for mixed90")
    mixed = sample("mixed_hits", live_keys, mixed_n * 9 // 10) + sample("mixed_misses", synthetic, mixed_n // 10)
    random.Random(20261024).shuffle(mixed)
    keys["mixed90"] = mixed
    if control:
        keys["live_write_touched"] = sample("live_write_touched", [k for k in live_keys if max(0, bisect_right(lower, k) - 1) in touched])
        keys["live_write_untouched"] = sample("live_write_untouched", [k for k in live_keys if max(0, bisect_right(lower, k) - 1) not in touched])
        if not keys["live_write_touched"] or not keys["live_write_untouched"]:
            raise ValueError("Sparse control needs both touched and untouched page ranges")
    groups = {label: [(key, int(key in live), live.get(key, 0), rank.get(key, MASK)) for key in values]
              for label, values in keys.items()}
    deleted = set(dict(initial)) - set(live)
    for label, values in groups.items():
        if label.startswith("live_") and any(not hit or key in deleted for key, hit, _, _ in values):
            raise AssertionError("Deleted key in live group")
        if label.startswith(("absent_", "synthetic_")) and any(hit for _, hit, _, _ in values):
            raise AssertionError("Live key in absent group")
    output.mkdir(parents=True, exist_ok=False)
    terminal, query_file = output / "final.bin", output / "queries.bin"
    write_rows(terminal, sorted(live.items()))
    write_queries(query_file, lo, hi, groups)
    metadata = {"name": name, "control_only": control, "hot_range_inclusive": [lo, hi],
                "source_pool_rows": len(pool), "initial_rows": len(initial), "final_rows": len(live),
                "successful_mutations": successful, "deleted_initial_rows": len(deleted),
                "initial_pages": len(lower), "write_touched_initial_pages": len(touched),
                "final_sha256": sha(terminal), "query_sha256": sha(query_file), "groups": {}}
    for label, values in groups.items():
        ranked = [r for _, _, _, r in values if r != MASK]
        metadata["groups"][label] = {"keys": len(values), "hits": sum(hit for _, hit, _, _ in values),
                                     "source_hot_keys": sum(lo <= r <= hi for r in ranked),
                                     "rank_min": min(ranked) if ranked else None, "rank_max": max(ranked) if ranked else None,
                                     "deleted_initial_in_hits": sum(hit and k in deleted for k, hit, _, _ in values)}
    save(output / "queries.json", metadata)
    return metadata, groups


def ci95(log_values):
    n = len(log_values)
    critical = {7: 2.3646242516, 15: 2.1314495456}.get(n - 1)
    if critical is None:
        raise ValueError(f"Unregistered number of independent blocks: {n}")
    mean = statistics.fmean(log_values)
    half = critical * statistics.stdev(log_values) / math.sqrt(n)
    return {"n": n, "df": n - 1, "ratio": math.exp(mean), "ci95": [math.exp(mean - half), math.exp(mean + half)]}


def aa_summary(log_values):
    value = ci95(log_values)
    value["equivalent_within_5pct"] = value["ci95"][0] >= 1 / 1.05 and value["ci95"][1] <= 1.05
    return value


def comparison(log_values, left_aa, right_aa, limit):
    value = ci95(log_values)
    value.update(limit=limit, aa_quality_pass=left_aa["equivalent_within_5pct"] and right_aa["equivalent_within_5pct"])
    low, high = value["ci95"]
    value["performance_interval_verdict"] = "pass" if high <= limit else "regression" if low > limit else "undecided"
    value["verdict"] = value["performance_interval_verdict"] if value["aa_quality_pass"] else "measurement_unresolved"
    return value


def analyze_blocks(records, group_names):
    if len(records) != 16 or {r["process"] for r in records} != set(range(16)):
        raise ValueError("Missing/duplicate independent process block")
    output = {}
    for group in group_names:
        times, aa = {s: [] for s in STATES}, {s: [] for s in STATES}
        for block in sorted(records, key=lambda r: r["process"]):
            values = {(m["state"], m["repeat"]): m["ns"] / m["lookups"] for m in block["measurements"] if m["group"] == group}
            if not values:
                continue
            if len(values) != 8:
                raise ValueError("Incomplete four-state/A-A group")
            for state in STATES:
                a, b = values[state, 0], values[state, 1]
                times[state].append((math.log(a) + math.log(b)) / 2)
                aa[state].append(math.log(b / a))
        if not times[STATES[0]]:
            output[group] = {"skipped": "empty query population"}
            continue
        if any(len(values) != 16 for values in times.values()):
            raise ValueError("Query group missing from one or more process blocks")
        controls = {state: aa_summary(aa[state]) for state in STATES}
        contrasts = {}
        for name, left, right, limit in (
            ("candidate_vs_bm_dynamic", STATES[1], STATES[0], 1.05),
            ("candidate_vs_bm_snapshot", STATES[3], STATES[2], 1.05),
            ("candidate_dynamic_vs_snapshot", STATES[1], STATES[3], 1.15),
            ("bm_dynamic_vs_snapshot", STATES[0], STATES[2], 1.15),
        ):
            contrasts[name] = comparison([a - b for a, b in zip(times[left], times[right])], controls[left], controls[right], limit)
        output[group] = {"aa": controls, "contrasts": contrasts,
                         "geometric_ns_per_find": {s: math.exp(statistics.fmean(times[s])) for s in STATES}}
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data-root", required=True, type=Path, help="Read-only original workspace containing pinned dependencies and input traces")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--selftest-only", action="store_true")
    args = parser.parse_args()
    if os.name == "nt":
        parser.error("Run in Linux/WSL")
    output = args.output.resolve(); output.mkdir(parents=True, exist_ok=False)
    for name in ("logs", "cases", "inputs", "raw"):
        (output / name).mkdir()
    data_root = args.data_root.resolve()
    cpu = min(os.sched_getaffinity(0))
    commands = []
    report = {"status": "running", "purpose": "correctly grouped fixed final-state query recheck", "semantic": {}, "calibration": {}, "results": {}}

    def checkpoint(message):
        save(output / "report.json", report); save(output / "commands.json", commands)
        print(message, flush=True)

    def execute(command, log, *, env=None, expected_error=None):
        command = [str(x) for x in command]; commands.append(command)
        result = subprocess.run(command, cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
        (output / "logs" / (log + ".stdout")).write_bytes(result.stdout)
        (output / "logs" / (log + ".stderr")).write_bytes(result.stderr)
        if expected_error:
            if result.returncode == 0 or expected_error.encode() not in result.stderr:
                raise RuntimeError(f"Negative control not rejected correctly: {log}")
        elif result.returncode:
            raise RuntimeError(f"{log}: exit {result.returncode}; logs retained")
        return result.stdout.decode()

    def run_json(binary, mode, paths, label, extra=(), env=None):
        text = execute(["taskset", "-c", cpu, binary, mode, *paths, *extra], label, env=env)
        return json.loads(text)

    def snapshot(path, expected=None):
        data = path.read_bytes(); key = digest(data)
        if expected is not None and key != expected:
            raise ValueError(f"Input SHA mismatch: {path}")
        target = output / "inputs" / (key + ".bin")
        if not target.exists():
            target.write_bytes(data)
        return target, data, key

    try:
        registration = {"registered_at_utc": datetime.now(timezone.utc).isoformat(), "protocol_sha256": sha(PROTOCOL),
                        "blocks_per_cell": 16, "query_count_max": 2000, "min_calibration_batch_ms": 40,
                        "max_group_loops": 4096, "seed": 20261024, "cpu": cpu, "aa_quality_gate": [1 / 1.05, 1.05],
                        "primary_group": "mixed90", "runner_sha256": sha(Path(__file__)), "selftest_only": args.selftest_only}
        save(output / "registration.json", registration)
        deps = {name: data_root / "07_Eval_Integration/upstream_snapshots" / f"{name}_{pin[:8]}" for name, pin in PINS.items()}
        for name, directory in deps.items():
            if execute(["git", "-C", directory, "rev-parse", "HEAD"], name + "_head").strip() != PINS[name]:
                raise ValueError(f"Wrong {name} pin")
            execute(["git", "-C", directory, "diff", "--exit-code", "HEAD", "--", "src"], name + "_diff")
            if execute(["git", "-C", directory, "ls-files", "--others", "--exclude-standard", "--", "src"], name + "_untracked").strip():
                raise ValueError(f"Untracked {name} source")
        git = ["git"]
        if (ROOT / ".git").is_file():
            match = re.fullmatch(r"gitdir: ([A-Za-z]):[/\\](.*)", (ROOT / ".git").read_text().strip())
            if match:
                git += ["--git-dir=/mnt/" + match[1].lower() + "/" + match[2].replace("\\", "/"), "--work-tree=" + str(ROOT)]
        common = ["-std=c++17", "-pthread", "-march=x86-64-v3", *["-I" + str(p) for p in (
            DIAG, ROOT / "02_Code_and_Experiments/validation_e25_20260918", deps["benchmark"] / "src", deps["model"] / "src")]]
        definitions = {"BM_MODEL_COMMIT": PINS["model"], "BM_CORE_BUNDLE_SHA256": "scan", "BM_SPLIT_CORE_SHA256": "scan",
                       "BM_EXPERIMENT_ADAPTER_SHA256": sha(DIAG / "bm_experiment_adapter.hpp"), "BM_SPLIT_ADAPTER_SHA256": sha(DIAG / "bm_split_adapter.hpp")}
        macros = lambda: [f'-D{k}="{v}"' for k, v in definitions.items()]
        sources = {"driver": DIAG / "bm_query_recheck.cpp", "model": deps["model"] / "src/block_model.cpp"}
        closure = {}
        for name, source in sources.items():
            depfile = output / (name + ".d")
            execute(["g++", *common, *macros(), "-MM", "-MF", depfile, source], name + "_dependencies")
            for token in shlex.split(depfile.read_text().replace("\\\n", " ").split(":", 1)[1]):
                path = Path(token).resolve()
                for label, base in (("benchmark", deps["benchmark"]), ("model", deps["model"]), ("project", ROOT)):
                    if path.is_relative_to(base):
                        closure[label + "/" + path.relative_to(base).as_posix()] = {"path": str(path), "sha256": sha(path)}
                        break
                else:
                    raise ValueError(f"Unclassified source: {path}")
        bundle = digest(json.dumps({k: v["sha256"] for k, v in closure.items()}, sort_keys=True).encode())
        definitions.update(BM_CORE_BUNDLE_SHA256=bundle, BM_SPLIT_CORE_SHA256=bundle)
        identity = {"base_commit": execute([*git, "rev-parse", "HEAD"], "base_commit").strip(), "dependency_pins": PINS,
                    "source_bundle_sha256": bundle, "closure": closure, "definitions": definitions,
                    "compiler": execute(["g++", "--version"], "compiler").splitlines()[0],
                    "uname": execute(["uname", "-a"], "uname").strip(), "cpu_affinity": cpu,
                    "runner_sha256": registration["runner_sha256"], "protocol_sha256": registration["protocol_sha256"]}
        save(output / "identity.json", identity)

        def build(san=False):
            prefix = "san" if san else "opt"
            flags = [*common, *macros(), *(["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if san else ["-O3"])]
            objects = []
            for name, source in sources.items():
                obj = output / f"{prefix}_{name}.o"
                execute(["g++", *flags, "-MMD", "-MF", output / f"{prefix}_{name}.d", "-c", source, "-o", obj], f"{prefix}_{name}_build")
                objects.append(obj)
            binary = output / (prefix + "_query")
            execute(["g++", *flags, *objects, "-o", binary], prefix + "_link")
            return binary

        binary, sanitizer = build(), build(True)
        san_env = dict(os.environ, ASAN_OPTIONS="detect_leaks=1:halt_on_error=1", UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
        # Independent byte-boundary fixture, deliberately with a non-prefix hot range.
        fixture = output / "fixture"; fixture.mkdir()
        fixture_keys = sorted([b"", b"\0", b"\0\xff", b"\xff"] + [b"k/" + i.to_bytes(4, "big") for i in range(8188)])
        pool = [(key, i) for i, key in enumerate(fixture_keys)]
        initial = [(key, i * 17) for i, key in enumerate(fixture_keys) if i % 2 == 0]
        operations = [(4, fixture_keys[4096], 0, 0), (4, fixture_keys[12], 0, 0),
                      (3, fixture_keys[4], MASK, 0), (3, fixture_keys[6], 0, 0),
                      (2, fixture_keys[4097], 888, 0), (2, fixture_keys[4], 123, 0),
                      (3, fixture_keys[11], 99, 0), (4, fixture_keys[13], 0, 0),
                      (5, b"", 0, 16), (5, fixture_keys[4000], 0, 128)]
        operations += [(1, b"synthetic/" + str(i).encode(), 0, 0) for i in range(200)]
        for label, data in (("pool", pool), ("initial", initial)):
            write_rows(fixture / (label + ".bin"), data)
        write_ops(fixture / "ops.bin", operations)
        meta, fixture_groups = prepare_case("byte_fixture", initial, operations, pool, [4096, 4914], fixture / "prepared")
        fixture_paths = [fixture / "initial.bin", fixture / "ops.bin", fixture / "pool.bin", fixture / "prepared/final.bin", fixture / "prepared/queries.bin"]
        report["semantic"]["byte_fixture"] = run_json(binary, "check", fixture_paths, "fixture_check")
        report["semantic"]["byte_fixture_sanitizer"] = run_json(sanitizer, "check", fixture_paths, "fixture_san", env=san_env)
        for label, alter, error in (
            ("deleted_hit", lambda group: (fixture_keys[4096], 1, 4096 * 17, 4096), "query presence mismatch"),
            ("wrong_rank", lambda group: (*group[0][:3], group[0][3] + 1), "query source rank mismatch"),
        ):
            changed = {k: list(v) for k, v in fixture_groups.items()}
            changed["live_hot"][0] = alter(changed["live_hot"])
            wrong = fixture / (label + ".bin"); write_queries(wrong, 4096, 4914, changed)
            execute(["taskset", "-c", cpu, binary, "check", *fixture_paths[:-1], wrong], "negative_" + label, expected_error=error)
        report["semantic"]["negative_controls"] = {"deleted_hit": "rejected", "wrong_rank": "rejected"}
        checkpoint("Byte boundaries and invalid-group negative controls passed")
        if args.selftest_only:
            report["status"] = "selftest_complete";checkpoint("Selftest only; no performance results");return

        all_cases, original = {}, {}
        manifests = [data_root / "07_Eval_Integration/results" / directory / "manifest.json"
                     for directory in ("bm_audited_v3_20261025", "bm_audited_100k_v3_20261025")]
        report["input_manifests"] = {str(path): sha(path) for path in manifests}
        for manifest_path in manifests:
            for name, info in json.loads(manifest_path.read_text()).items():
                if not re.fullmatch(r"[a-z0-9_]+", name) or name in all_cases or info["dataset"] not in SOURCES:
                    raise ValueError("Unexpected/duplicate cell")
                paths, blobs, hashes = [], [], {}
                for key, relative, expected_sha in (
                    ("rows", info["rows_path"], info["rows_sha256"]),
                    ("operations", info["operations_path"], info["operations_sha256"]),
                    ("pool", "07_Eval_Integration/results/" + SOURCES[info["dataset"]], None),
                ):
                    path, data, h = snapshot(data_root / relative, expected_sha);paths.append(path);blobs.append(data);hashes[key] = h
                initial, operations, pool = read_rows(blobs[0]), read_ops(blobs[1]), read_rows(blobs[2])
                meta, _ = prepare_case(name, initial, operations, pool, info["hot_range"], output / "cases" / name)
                if meta["final_rows"] != info["live_after"] or meta["initial_rows"] != info["live_before"]:
                    raise ValueError("Manifest/replay count mismatch")
                if any(value != info["successful"][kind] for kind, value in meta["successful_mutations"].items()):
                    raise ValueError("Manifest/replay mutation success mismatch")
                paths += [output / "cases" / name / "final.bin", output / "cases" / name / "queries.bin"]
                meta.update(input_sha256=hashes, paths=[str(p) for p in paths])
                all_cases[name] = meta
                if name == "words_95_5_hotspot_growth":
                    original = {"initial": initial, "operations": operations, "pool": pool, "info": info, "paths": paths}
        if len(all_cases) != 32:
            raise ValueError("Expected the original 32 cells")
        lo, hi = original["info"]["hot_range"];rank = {k: i for i, (k, _) in enumerate(original["pool"])}
        control_ops = [op for op in original["operations"] if op[0] not in (2, 3, 4) or lo <= rank[op[1]] <= hi]
        name = "words_sparse_growth_control";control_dir = output / "control_inputs";control_dir.mkdir()
        write_ops(control_dir / "ops.bin", control_ops)
        meta, _ = prepare_case(name, original["initial"], control_ops, original["pool"], [lo, hi], output / "cases" / name, control=True)
        paths = [original["paths"][0], control_dir / "ops.bin", original["paths"][2], output / "cases" / name / "final.bin", output / "cases" / name / "queries.bin"]
        meta.update(paths=[str(p) for p in paths], input_sha256={"rows": sha(paths[0]), "operations": sha(paths[1]), "pool": sha(paths[2])})
        all_cases[name] = meta
        save(output / "cases.json", all_cases)
        checkpoint("All 32 cells plus sparse control frozen; each query file independently classified")

        for name, meta in all_cases.items():
            report["semantic"][name] = run_json(sanitizer, "check", meta["paths"], "san_" + name, env=san_env)
            if meta["control_only"] and (report["semantic"][name]["candidate_block_splits"] == 0 or meta["write_touched_initial_pages"] >= meta["initial_pages"]):
                raise ValueError("Sparse control failed its split/untouched requirements")
        checkpoint("All frozen cases passed sanitizer, full replay and four-state query answer checks")

        for number, (name, meta) in enumerate(all_cases.items(), 1):
            calibration = run_json(binary, "calibrate", meta["paths"], "calibrate_" + name, extra=(40,))
            report["calibration"][name] = calibration
            loops = ",".join(str(n) for n in calibration["loops"])
            blocks = []
            with (output / "raw" / (name + ".jsonl")).open("w", encoding="utf-8") as stream:
                for process in range(16):
                    block = run_json(binary, "bench", meta["paths"], f"bench_{name}_{process}", extra=(process, loops))
                    blocks.append(block);stream.write(json.dumps(block, separators=(",", ":")) + "\n");stream.flush()
            report["results"][name] = analyze_blocks(blocks, list(meta["groups"]))
            primary = report["results"][name]["mixed90"]["contrasts"]["candidate_vs_bm_dynamic"]
            checkpoint(f"{number}/{len(all_cases)} {name}: mixed90={primary['ratio']:.3f}, {primary['verdict']}")

        # Old mixed-trace find is a separate, post-hoc reanalysis, never a new experiment.
        old_results = {}
        for directory in ("bm_s1prime_20261025", "bm_s4_urls_20261025", "bm_s4_100k_20261025", "bm_s4_indep_20261025"):
            for path in sorted((data_root / "07_Eval_Integration/results" / directory).glob("*.jsonl")):
                groups = {}
                for value in [json.loads(s) for s in path.read_text().splitlines() if s.strip()]:
                    by = groups.setdefault(value["process"], {})
                    if value["arm"] in by:
                        raise ValueError("Duplicate old arm/process")
                    by[value["arm"]] = value
                logs, controls = [], {s: [] for s in ("bm", "bm_split")}
                for process, block in sorted(groups.items()):
                    values = {s: (block[s]["find_us"], block[s]["find_us_aa"]) for s in controls}
                    logs.append(statistics.fmean(math.log(a / b) for a, b in zip(values["bm_split"], values["bm"])))
                    for state, (a, b) in values.items():
                        controls[state].append(math.log(b / a))
                controls = {k: aa_summary(v) for k, v in controls.items()}
                old_results[path.stem] = {"input_sha256": sha(path), "aa": controls,
                                          "find": comparison(logs, controls["bm_split"], controls["bm"], 1.05)}
        if len(old_results) != 32:
            raise ValueError("Missing old mixed-trace record")
        save(output / "existing_mixed_reanalysis.json", old_results)
        for item in closure.values():
            if sha(Path(item["path"])) != item["sha256"]:
                raise ValueError("Source changed during query recheck")
        if sha(Path(__file__)) != registration["runner_sha256"] or sha(PROTOCOL) != registration["protocol_sha256"]:
            raise ValueError("Runner/protocol changed during query recheck")
        primary_counts = {}
        for name, result in report["results"].items():
            if all_cases[name]["control_only"]:
                continue
            verdict = result["mixed90"]["contrasts"]["candidate_vs_bm_dynamic"]["verdict"]
            primary_counts[verdict] = primary_counts.get(verdict, 0) + 1
        report.update(status="complete", original_cells=32, diagnostic_controls=1, independent_blocks_per_cell=16,
                      primary_final_state_mixed90=primary_counts, finished_at_utc=datetime.now(timezone.utc).isoformat())
        checkpoint("Query recheck complete; existing mixed-trace and final-state results remain separate")
    except Exception as error:
        report.update(status="failed", error=str(error), finished_at_utc=datetime.now(timezone.utc).isoformat())
        checkpoint("Stopped: " + str(error))
        raise


if __name__ == "__main__":
    main()
