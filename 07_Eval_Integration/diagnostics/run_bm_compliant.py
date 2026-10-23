"""Stages 5-6: compliant workload generator (successful-op 2:1:1, >=80% hotspot, scan 16/128)."""
import hashlib
import json
import random
import struct
import subprocess
import sys
from pathlib import Path

WS = Path('/mnt/d/StringLearnedIndex_Workspace')
ROOT = WS / '07_Eval_Integration'
BENCH = ROOT / 'upstream_snapshots/benchmark_003a55bf'
MODEL = ROOT / 'upstream_snapshots/model_3894c767'
sys.path.insert(0, str(BENCH / 'scripts'))
from benchmark_lib.interchange import write_operations, write_rows  # noqa: E402

DATASETS = {
    'words': '07_Eval_Integration/results/bm_real_20261018/interchange/english_words_rows.bin',
    'urls': '07_Eval_Integration/results/bm_real_20261018/interchange/memetracker_urls_rows.bin',
    'words_indep': '07_Eval_Integration/results/bm_space_gen_inputs_20261019/interchange/'
                   'english_words_s20261019_rows.bin',
}
SEED = 20261023
OPS = 20000
SCAN_LIMITS = (16, 128)


def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()


def read_rows(path):
    data = path.read_bytes(); assert data[:8] == b'SIBROW1\0'
    count, = struct.unpack_from('<Q', data, 8); pos = 16; rows = []
    for _ in range(count):
        n, = struct.unpack_from('<Q', data, pos); pos += 8
        key = data[pos:pos + n]; pos += n
        value, = struct.unpack_from('<Q', data, pos); pos += 8
        rows.append((key, value))
    return rows


def build(keys, ratio, locality, semantics, seed):
    """Return (initial, ops, meta). Writes are planned against the live set so the 2:1:1 split
    holds over successful operations, and hotspot targets come from the source-order slice."""
    rng = random.Random(seed)
    ordered = sorted(keys)
    width = max(1, len(ordered) // 10)
    start = rng.randrange(0, len(ordered) - width + 1)
    hot = ordered[start:start + width]
    hot_set = set(hot)
    if semantics == 'growth':
        order = list(range(len(ordered))); rng.shuffle(order)
        cut = len(ordered) * 4 // 5
        keep = sorted(order[:cut])
        initial = [(ordered[i], i) for i in keep]
        live = set(ordered[i] for i in keep)
        held = [ordered[i] for i in sorted(order[cut:])]
    else:
        initial = [(k, i) for i, k in enumerate(ordered)]
        live = set(ordered)
        held = []
    find_ops = int(OPS * (1 - ratio))
    write_ops = OPS - find_ops
    ops = []
    absent = [k + b'~absent' for k in ordered[:500]]
    for _ in range(find_ops):
        if rng.randrange(10) == 9:
            ops.append((1, rng.choice(absent), 0, 0))
        else:
            key = rng.choice(hot) if locality == 'hotspot' and rng.randrange(10) < 8 else rng.choice(ordered)
            ops.append((1, key, 0, 0))

    def target(hot_bias):
        if locality == 'hotspot' and rng.randrange(10) < hot_bias and live & hot_set:
            return rng.choice(sorted(live & hot_set))
        return rng.choice(sorted(live)) if live else rng.choice(ordered)

    if semantics == 'growth':
        for i in range(write_ops):
            key = held[rng.randrange(len(held))] if held else ordered[rng.randrange(len(ordered))]
            if locality == 'hotspot' and rng.randrange(10) < 8:
                inside = [h for h in hot if h not in live]
                key = rng.choice(inside) if inside else key
            ops.append((2, key, len(ordered) + i, 0))
            live.add(key)
    else:
        # 2 updates : 1 erase : 1 insert, counted over successful operations.
        n_pair = write_ops // 4
        counter = 0
        for i in range(n_pair * 2):
            key = target(8)
            ops.append((3, key, len(ordered) + 10_000 + i, 0))
        for i in range(n_pair):
            if not live:
                break
            key = target(8)
            ops.append((4, key, 0, 0))
            live.discard(key)
            new_key = key + f'~r/{i}'.encode() if key not in live else key + f'~s/{i}'.encode()
            ops.append((2, new_key, len(ordered) + 20_000 + counter, 0))
            live.add(new_key)
            counter += 1
    # Scan stream: 2% of operations, alternating limit 16 and 128.
    for i in range(max(1, OPS // 50)):
        ops.append((5, rng.choice(ordered), 0, SCAN_LIMITS[i % 2]))
    rng.shuffle(ops)
    writes = [k for kind, k, *_ in ops if kind in (2, 3, 4)]
    hot_hits = sum(1 for k in writes if k in hot_set)
    meta = {'hot_range': [start, start + width], 'hot_keys': len(hot),
            'hot_write_fraction': round(hot_hits / max(1, len(writes)), 3),
            'scan_limits': list(SCAN_LIMITS)}
    return initial, ops, meta


def main():
    out = ROOT / 'results' / 'bm_compliant_20261023'
    out.mkdir(exist_ok=False)
    traces = out / 'traces'; traces.mkdir()
    build_dir = out / 'build'; build_dir.mkdir()
    inputs = {}
    for name, rel in DATASETS.items():
        keys = [k for k, _ in read_rows(WS / rel)]
        for ratio, value in (('95_5', 0.05), ('80_20', 0.20)):
            for locality in ('uniform', 'hotspot'):
                for semantics in ('growth', 'replacement'):
                    cell = f'{name}_{ratio}_{locality}_{semantics}'
                    initial, ops, meta = build(keys, value, locality, semantics, SEED + len(cell))
                    rows_path = traces / f'{cell}_rows.bin'
                    rowhash = write_rows(rows_path, [k for k, _ in initial], [v for _, v in initial])
                    op_path = traces / f'{cell}_ops.bin'
                    ophash = write_operations(op_path, ops)
                    kinds = {}
                    for kind, *_ in ops:
                        kinds[str(kind)] = kinds.get(str(kind), 0) + 1
                    inputs[cell] = {'rows_path': str(rows_path.relative_to(WS)), 'rows_sha256': rowhash,
                                    'operations_path': str(op_path.relative_to(WS)),
                                    'operations_sha256': ophash, 'initial_rows': len(initial),
                                    'operations': len(ops), 'kinds': kinds, 'dataset': name,
                                    'ratio': ratio, 'locality': locality, 'semantics': semantics, **meta}
                    print('trace', cell, kinds, 'hot', meta['hot_write_fraction'], flush=True)
    flags = ['-pthread', '-std=c++17', '-march=x86-64-v3', '-O3',
             '-I' + str(WS / '02_Code_and_Experiments/validation_e25_20260918'),
             '-I' + str(BENCH / 'src'), '-I' + str(MODEL / 'src'),
             '-I' + str(ROOT / 'learned-index/src'), '-I' + str(ROOT / 'adapters/art'),
             '-I' + str(ROOT / 'adapters/our_index'), '-I' + str(ROOT / 'diagnostics'),
             '-I' + str(WS / '02_Code_and_Experiments/validation_e25_20260918/third_party/vendor/libart/src'),
             '-DBM_MODEL_COMMIT="3894c767"', '-DBM_CORE_BUNDLE_SHA256="acceptance"',
             '-DBM_EXPERIMENT_ADAPTER_SHA256="acceptance"', '-DBM_SPLIT_CORE_SHA256="acceptance"',
             '-DBM_SPLIT_ADAPTER_SHA256="acceptance"']
    art_obj = build_dir / 'art.o'
    subprocess.run(['gcc', '-std=c11', '-DNDEBUG', '-march=x86-64-v3', '-O3',
                    '-I' + str(WS / '02_Code_and_Experiments/validation_e25_20260918/third_party/vendor/libart/src'),
                    '-c', str(WS / '02_Code_and_Experiments/validation_e25_20260918/third_party/vendor/libart/src/art.c'),
                    '-o', str(art_obj)], check=True, capture_output=True)
    exe = build_dir / 'pure'
    subprocess.run(['g++'] + flags + [
        str(ROOT / 'diagnostics/bench_bm_pure.cpp'),
        str(MODEL / 'src/block_model.cpp'), str(MODEL / 'src/residual_tags.cpp'),
        str(MODEL / 'src/query_cost.cpp'), str(MODEL / 'src/region_planner.cpp'),
        str(MODEL / 'src/learned_index.cpp'), str(MODEL / 'src/benchmark/model_factories.cpp'),
        str(ROOT / 'adapters/art/art_adapter.cpp'), str(ROOT / 'adapters/our_index/our_index_adapter.cpp'),
        str(art_obj), str(BENCH / 'src/unified/runner.cpp'), '-o', str(exe)],
        check=True, capture_output=True, text=True)
    records = []
    for cell, item in inputs.items():
        for process in range(5):
            r = subprocess.run(['taskset', '-c', '0', str(exe), str(WS / item['rows_path']),
                                str(WS / item['operations_path']), str(process)],
                               capture_output=True, text=True)
            if r.returncode:
                raise SystemExit(f'FAILED {cell} p{process}: {r.stderr[-300:]}')
            for line in r.stdout.splitlines():
                record = json.loads(line); record['cell'] = cell; records.append(record)
        print('cell', cell, 'x5', flush=True)
    (out / 'records.json').write_text(json.dumps(records, indent=2) + '\n', encoding='utf-8')
    (out / 'identity.json').write_text(json.dumps({
        'phase': 'stage5/6', 'seed': SEED, 'operations': OPS, 'inputs': inputs,
        'base_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        'artifacts': {'build/pure': sha(exe)},
        'protocol': 'pure timing without oracle in the loop; each arm run twice per process for A/A; '
                    'arm order rotated by process index',
        'limits': ['no oracle inside the timed loop; correctness covered by the separate gates',
                   'hotspot is synthetic over the source key order']}, indent=2) + '\n', encoding='utf-8')
    print('records', len(records))


if __name__ == '__main__':
    main()
