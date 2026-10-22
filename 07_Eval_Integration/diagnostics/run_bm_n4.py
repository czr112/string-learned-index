"""N4: 16-cell matrix generator and runner (2 data x 2 ratios x 2 locality x 2 semantics)."""
import argparse
import hashlib
import json
import random
import struct
import subprocess
import sys
from fractions import Fraction
from pathlib import Path

WS = Path('/mnt/d/StringLearnedIndex_Workspace')
ROOT = WS / '07_Eval_Integration'
BENCH = ROOT / 'upstream_snapshots/benchmark_003a55bf'
MODEL = ROOT / 'upstream_snapshots/model_3894c767'
sys.path.insert(0, str(BENCH / 'scripts'))
from benchmark_lib.interchange import write_operations, write_rows  # noqa: E402

DATA = {'english_words': '07_Eval_Integration/results/bm_real_20261018/interchange/english_words_rows.bin',
        'memetracker_urls': '07_Eval_Integration/results/bm_real_20261018/interchange/memetracker_urls_rows.bin'}
SEED = 20261022
OPS = 20000
HOT_SHARE = Fraction(4, 5)      # 80% of write targets
HOT_RANGE = Fraction(1, 10)     # inside 10% of the source key order


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


def main():
    p = argparse.ArgumentParser(); p.add_argument('--output', default='bm_n4_matrix_20261022')
    a = p.parse_args()
    out = ROOT / 'results' / a.output; out.mkdir(exist_ok=False)
    traces = out / 'traces'; traces.mkdir()
    build = out / 'build'; build.mkdir()
    inputs, stats = {}, {}
    for data, rel in DATA.items():
        keys = [k for k, _ in read_rows(WS / rel)]
        ordered = sorted(keys)
        hot_lo = len(ordered) * 0
        for ratio, find_writes in [('95_5', Fraction(1, 20)), ('80_20', Fraction(1, 5))]:
            for locality in ('uniform', 'hotspot'):
                for semantics in ('growth', 'replacement'):
                    cell = f'{data}_{ratio}_{locality}_{semantics}'
                    rng = random.Random(SEED + sum(map(ord, cell)))
                    width = max(1, len(ordered) // 10)
                    start = rng.randrange(0, len(ordered) - width + 1)
                    hot = ordered[start:start + width]

                    if semantics == 'growth':
                        order = list(range(len(ordered))); rng.shuffle(order)
                        cut = len(ordered) * 4 // 5
                        keep = sorted(order[:cut]); held = [ordered[i] for i in sorted(order[cut:])]
                        initial = [(ordered[i], i) for i in keep]
                    else:
                        held = []
                        initial = [(k, i) for i, k in enumerate(ordered)]
                    find_ops = int(OPS * (1 - find_writes))
                    write_ops = OPS - find_ops
                    absent = [k + f'~a/{i}'.encode() for i, k in enumerate(ordered[:2000])]
                    ops = []
                    for _ in range(find_ops):
                        if rng.randrange(10) == 9:
                            ops.append((1, rng.choice(absent), 0, 0))
                        else:
                            key = rng.choice(hot) if locality == 'hotspot' and rng.randrange(10) < 8 else rng.choice(ordered)
                            ops.append((1, key, 0, 0))
                    if semantics == 'growth':
                        for i in range(write_ops):
                            if held and (locality != 'hotspot' or rng.randrange(10) < 8):
                                pool = [h for h in held if h >= hot[0] and h <= hot[-1]] if locality == 'hotspot' else held
                                key = rng.choice(pool if pool else held)
                            else:
                                key = rng.choice(ordered)
                            ops.append((2, key, len(ordered) + i, 0))
                    else:
                        # Real key replacement: update:erase:insert = 2:1:1, erase paired with a new key.
                        n_update = write_ops // 2
                        n_pair = write_ops - n_update
                        pool = [k for k in ordered if not (locality == 'hotspot' and rng.randrange(10) >= 8)]
                        for i in range(n_update):
                            key = rng.choice(hot) if locality == 'hotspot' and rng.randrange(10) < 8 else rng.choice(ordered)
                            ops.append((3, key, len(ordered) + 9000 + i, 0))
                        for i in range(n_pair // 2):
                            key = rng.choice(hot) if locality == 'hotspot' and rng.randrange(10) < 8 else rng.choice(ordered)
                            ops.append((4, key, 0, 0))
                            ops.append((2, key + f'~n/{i}'.encode(), len(ordered) + 20000 + i, 0))
                    # A light scan stream (2% of ops, limit 16) so delete/scan semantics are exercised.
                    scans = max(1, OPS // 50)
                    for _ in range(scans):
                        ops.append((5, rng.choice(ordered), 0, 16))
                    rng.shuffle(ops)

                    rows_path = traces / (cell + '_rows.bin')
                    row_hash = write_rows(rows_path, [k for k, _ in initial], [v for _, v in initial])
                    op_path = traces / (cell + '_ops.bin')
                    op_hash = write_operations(op_path, ops)
                    kinds = {}
                    for kind, *_ in ops:
                        kinds[str(kind)] = kinds.get(str(kind), 0) + 1
                    inbox = sum(1 for kind, key, *_ in ops if kind in (2, 3, 4) and key in set(hot))
                    inputs[cell] = {'rows_path': str(rows_path.relative_to(WS)), 'rows_sha256': row_hash,
                                    'operations_path': str(op_path.relative_to(WS)), 'operations_sha256': op_hash,
                                    'initial_rows': len(initial), 'operations': len(ops), 'kinds': kinds,
                                    'data': data, 'ratio': ratio, 'locality': locality, 'semantics': semantics,
                                    'hot_range': [start, start + width], 'hot_keys': len(hot),
                                    'hot_write_fraction': round(inbox / max(1, sum(1 for k, *_ in ops if k in (2, 3, 4))), 3)}
                    print('trace', cell, len(initial), kinds, 'hot', inputs[cell]['hot_write_fraction'], flush=True)

    flags = ['-pthread', '-std=c++17', '-march=x86-64-v3', '-O3',
             '-I' + str(WS / '02_Code_and_Experiments/validation_e25_20260918'),
             '-I' + str(BENCH / 'src'), '-I' + str(MODEL / 'src'),
             '-I' + str(ROOT / 'learned-index/src'), '-I' + str(ROOT / 'adapters/art'),
             '-I' + str(ROOT / 'adapters/our_index'), '-I' + str(ROOT / 'diagnostics'),
             '-I' + str(WS / '02_Code_and_Experiments/validation_e25_20260918/third_party/vendor/libart/src'),
             '-DBM_MODEL_COMMIT="3894c767"', '-DBM_CORE_BUNDLE_SHA256="n4"',
             '-DBM_EXPERIMENT_ADAPTER_SHA256="n4"']
    art_obj = build / 'art.o'
    subprocess.run(['gcc', '-std=c11', '-DNDEBUG', '-march=x86-64-v3', '-O3',
                    '-I' + str(WS / '02_Code_and_Experiments/validation_e25_20260918/third_party/vendor/libart/src'),
                    '-c', str(WS / '02_Code_and_Experiments/validation_e25_20260918/third_party/vendor/libart/src/art.c'),
                    '-o', str(art_obj)], check=True, capture_output=True)
    exe = build / 'n4'
    subprocess.run(['g++'] + flags + [
        str(ROOT / 'diagnostics/bench_bm_n4.cpp'),
        str(MODEL / 'src/block_model.cpp'), str(MODEL / 'src/residual_tags.cpp'),
        str(MODEL / 'src/query_cost.cpp'), str(MODEL / 'src/region_planner.cpp'),
        str(MODEL / 'src/learned_index.cpp'), str(MODEL / 'src/benchmark/model_factories.cpp'),
        str(ROOT / 'adapters/art/art_adapter.cpp'), str(ROOT / 'adapters/our_index/our_index_adapter.cpp'),
        str(art_obj), str(BENCH / 'src/unified/runner.cpp'), '-o', str(exe)],
        check=True, capture_output=True, text=True)
    records = []
    for cell, item in inputs.items():
        for arm in ('bm', 'bm_split', 'art', 'ctx16'):
            r = subprocess.run(['taskset', '-c', '0', str(exe), str(WS / item['rows_path']),
                                str(WS / item['operations_path']), arm], capture_output=True, text=True)
            if r.returncode:
                raise SystemExit(f'FAILED {cell} {arm}: {r.stderr[-300:]}')
            record = json.loads(r.stdout); record['cell'] = cell; record['arm'] = arm
            assert record['mismatches'] == 0, (cell, arm)
            records.append(record)
        print('cell', cell, 'done', flush=True)
    (out / 'records.json').write_text(json.dumps(records, indent=2) + '\n', encoding='utf-8')
    summary = {}
    for cell in inputs:
        block = [r for r in records if r['cell'] == cell]
        by = {r['arm']: r for r in block}
        summary[cell] = {'write_us': {a: round(by[a]['write_us'], 2) for a in by},
                         'find_us': {a: round(by[a]['find_us'], 3) for a in by},
                         'scan_us': {a: round(by[a]['scan_us'], 2) for a in by},
                         'index_bytes': {a: by[a]['index_bytes'] for a in by},
                         'mismatches': sum(r['mismatches'] for r in block)}
    (out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    (out / 'identity.json').write_text(json.dumps({
        'phase': 'N4', 'seed': SEED, 'operations': OPS, 'inputs': inputs,
        'base_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        'artifacts': {'build/n4': sha(exe)},
        'limits': ['single run per cell-arm', 'hotspot is synthetic over the source key order',
                   'no million-scale', 'scan limit fixed at 16']}, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({k: v['write_us'] for k, v in summary.items()}, indent=1))


if __name__ == '__main__':
    main()
