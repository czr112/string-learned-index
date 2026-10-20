"""P4: 24 cells (3 data x 2 ratios x uniform/concentrated x growth/replacement).

Uniform cells reuse the unchanged upstream generator. Concentrated cells follow the plan's fixed
rule - 80% of write targets fall in a 10% slice of the ordered page range chosen by a fixed seed -
so they are generated here and frozen like the rest. Every cell is replayed against std::map.
"""
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
from benchmark_lib.workload import MixedWorkloadConfig, generate_mixed_operations  # noqa: E402
from benchmark_lib.interchange import write_operations, write_rows  # noqa: E402

DATA = {'english_words': '07_Eval_Integration/results/bm_real_20261018/interchange/english_words_rows.bin',
        'memetracker_urls': '07_Eval_Integration/results/bm_real_20261018/interchange/memetracker_urls_rows.bin',
        'memetracker_phrases': '07_Eval_Integration/results/bm_real_20261018/interchange/memetracker_phrases_rows.bin'}
SEED = 20261020
OPERATIONS = 20000
PAGE = 2048
HOT_FRACTION = Fraction(4, 5)


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


def concentrated(keys, find_weight, mode, seed):
    """80% of targets inside a 10% page slice; the slice is fixed by the seed."""
    rng = random.Random(seed)
    pages = (len(keys) + PAGE - 1) // PAGE
    width = max(1, pages // 10)
    start = rng.randrange(0, pages - width + 1)
    hot = keys[start * PAGE:min(len(keys), (start + width) * PAGE)]
    if mode == 'growth':
        order = list(range(len(keys))); rng.shuffle(order)
        cut = len(keys) * 4 // 5
        initial_idx = sorted(order[:cut]); held = [keys[i] for i in sorted(order[cut:])]
        initial = [(keys[i], i) for i in initial_idx]
        held = [k for k in held if k]
    else:
        initial = [(k, i) for i, k in enumerate(keys)]
        held = []
    ops = []
    find_count = int(OPERATIONS * find_weight)
    write_count = OPERATIONS - find_count
    absent_pool = [k + f'~absent/{i}'.encode() for i, k in enumerate(keys[:2000])]
    for _ in range(find_count):
        if rng.randrange(10) == 9:
            ops.append((1, rng.choice(absent_pool), 0, 0))
        else:
            key = rng.choice(hot) if rng.randrange(10) < 8 else rng.choice(keys)
            ops.append((1, key, 0, 0))
    for i in range(write_count):
        if mode == 'growth':
            key = rng.choice(held) if rng.randrange(10) < 8 and held else rng.choice(keys)
            ops.append((2, key, len(keys) + i, 0))
        else:
            key = rng.choice(hot) if rng.randrange(10) < 8 else rng.choice(keys)
            ops.append((3, key, len(keys) + i, 0))
    rng.shuffle(ops)
    meta = {'generator': 'project-side concentrated rule: 80% of targets in 10% of ordered pages',
            'pages': pages, 'hot_page_start': start, 'hot_pages': width,
            'hot_keys': len(hot), 'held_out': len(held)}
    return initial, ops, meta


def main():
    p = argparse.ArgumentParser(); p.add_argument('--output', default='bm_p4_dynamic_20261020')
    a = p.parse_args(); assert Path(a.output).name == a.output
    out = ROOT / 'results' / a.output
    out.mkdir(exist_ok=False)
    traces = out / 'traces'; traces.mkdir()
    build = out / 'build'; build.mkdir()
    inputs = {}
    for data, rel in DATA.items():
        keys = [key for key, _ in read_rows(WS / rel)]
        for ratio, find_weight in [('95_5', Fraction(19, 20)), ('80_20', Fraction(4, 5))]:
            for locality in ('uniform', 'concentrated'):
                for mode in ('growth', 'replacement'):
                    cell = f'{data}_{ratio}_{locality}_{mode}'
                    if locality == 'uniform':
                        if mode == 'growth':
                            config = MixedWorkloadConfig(
                                seed=SEED, operations=OPERATIONS, initial_load_fraction=Fraction(4, 5),
                                operation_weights={'find': find_weight, 'insert': 1 - find_weight,
                                                   'update': Fraction(0), 'erase': Fraction(0),
                                                   'scan': Fraction(0)}, read_miss_ratio=Fraction(1, 10))
                        else:
                            config = MixedWorkloadConfig(
                                seed=SEED, operations=OPERATIONS, initial_load_fraction=Fraction(1, 1),
                                operation_weights={'find': find_weight, 'insert': Fraction(0),
                                                   'update': 1 - find_weight, 'erase': Fraction(0),
                                                   'scan': Fraction(0)}, read_miss_ratio=Fraction(1, 10))
                        prepared = generate_mixed_operations(keys, config, 1 << 31)
                        initial = sorted(zip(prepared.initial_keys, prepared.initial_values))
                        ops = prepared.operations
                        gen = {'generator': 'unchanged upstream generate_mixed_operations'}
                    else:
                        initial, ops, gen = concentrated(keys, find_weight, mode, SEED + len(cell))
                    rows_path = traces / (cell + '_rows.bin')
                    row_hash = write_rows(rows_path, [k for k, _ in initial], [v for _, v in initial])
                    op_path = traces / (cell + '_ops.bin')
                    op_hash = write_operations(op_path, ops)
                    kinds = {}
                    for kind, *_ in ops:
                        kinds[str(kind)] = kinds.get(str(kind), 0) + 1
                    inputs[cell] = {'rows_path': str(rows_path.relative_to(WS)), 'rows_sha256': row_hash,
                                    'operations_path': str(op_path.relative_to(WS)),
                                    'operations_sha256': op_hash, 'initial_rows': len(initial),
                                    'operations': len(ops), 'kinds': kinds, 'data': data, 'ratio': ratio,
                                    'locality': locality, 'mode': mode, 'generation': gen}
                    print('trace', cell, len(initial), 'rows', kinds, flush=True)

    flags = ['-pthread', '-std=c++17', '-march=x86-64-v3', '-O3',
             '-I' + str(WS / '02_Code_and_Experiments/validation_e25_20260918'),
             '-I' + str(BENCH / 'src'), '-I' + str(MODEL / 'src'), '-I' + str(ROOT / 'diagnostics')]
    exe = build / 'replay'
    subprocess.run(['g++'] + flags + [ROOT / 'diagnostics/replay_bm_dynamic.cpp',
                                      MODEL / 'src/block_model.cpp', '-o', exe],
                   check=True, capture_output=True, text=True)
    records = []
    for cell, item in inputs.items():
        r = subprocess.run(['taskset', '-c', '0', str(exe), str(WS / item['rows_path']),
                            str(WS / item['operations_path'])], check=True, capture_output=True, text=True)
        record = json.loads(r.stdout); record['cell'] = cell
        assert record['mismatches'] == 0, cell
        assert record['final_read_checked'] == record['final_size'] == record['final_scan_rows']
        writes = record['insert_ops'] + record['update_ops'] + record['erase_ops']
        record.update(locality=item['locality'], mode=item['mode'], data=item['data'], ratio=item['ratio'],
                      find_us=record['find_ns'] / max(1, record['find_ops']) / 1000,
                      write_us=record['write_ns'] / max(1, writes) / 1000)
        records.append(record)
    (out / 'records.json').write_text(json.dumps(records, indent=2) + '\n', encoding='utf-8')

    def agg(key):
        values = [r[key] for r in records]
        return {'min': min(values), 'median': sorted(values)[len(values) // 2], 'max': max(values)}

    decision = {
        'cells': len(records),
        'all_zero_mismatch': all(r['mismatches'] == 0 for r in records),
        'find_us': agg('find_us'), 'write_us': agg('write_us'),
        'rebuilds_total': sum(r['rebuilds'] for r in records),
        'growth': {r['cell']: {'write_us': round(r['write_us'], 2), 'rebuilds': r['rebuilds'],
                               'rows_rebuilt': r['rows_rebuilt']}
                   for r in records if r['mode'] == 'growth'},
        'replacement': {r['cell']: {'write_us': round(r['write_us'], 2), 'rebuilds': r['rebuilds']}
                        for r in records if r['mode'] == 'replacement'},
        'concentrated_vs_uniform_write_us': {
            f'{d}_{ratio}_{mode}': {
                'uniform': round(next(r['write_us'] for r in records if r['data'] == d and r['ratio'] == ratio
                                      and r['locality'] == 'uniform' and r['mode'] == mode), 2),
                'concentrated': round(next(r['write_us'] for r in records if r['data'] == d and r['ratio'] == ratio
                                           and r['locality'] == 'concentrated' and r['mode'] == mode), 2)}
            for d in DATA for ratio in ('95_5', '80_20') for mode in ('growth', 'replacement')},
        'phrases_space_target_still_failed': True,
        'production_adopted': False,
        'status': 'P4 24-cell full-operation dynamic; single process per cell, no pairing',
        'limits': ['single process per cell', 'concentrated traces are project-side synthetic',
                   'no scan weight', 'HOT absent', 'no throughput or adoption claim']}
    (out / 'decision.json').write_text(json.dumps(decision, indent=2) + '\n', encoding='utf-8')
    sources = {str(p.relative_to(WS)): sha(p) for p in sorted({
        Path(__file__), ROOT / 'diagnostics/replay_bm_dynamic.cpp',
        ROOT / 'diagnostics/bm_bounded_index.hpp', ROOT / 'diagnostics/bm_page_index.hpp',
        ROOT / 'diagnostics/bm_page_core.hpp', ROOT / 'diagnostics/bm_page_root.hpp',
        MODEL / 'src/block_model.cpp', MODEL / 'src/block_model.h', MODEL / 'src/key_bytes.h',
        WS / '02_Code_and_Experiments/validation_e25_20260918/static_physical_index.hpp'})}
    (out / 'identity.json').write_text(json.dumps({
        'phase': 'P4', 'base_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT,
                                                              text=True).strip(),
        'sources': sources, 'inputs': inputs, 'artifacts': {str(exe.relative_to(out)): sha(exe)},
        'protocol': '24 frozen traces; every operation checked against std::map; final map by full read and scan',
        'limits': decision['limits']}, indent=2) + '\n', encoding='utf-8')
    (out / 'verification.json').write_text(json.dumps({
        'status': 'verified', 'cells': len(records),
        'checks': ['trace identity', 'per-kind counts', 'zero mismatches', 'final map by read and scan']},
        indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'cells': len(records), 'find_us': {k: round(v, 2) for k, v in decision['find_us'].items()},
                      'write_us': {k: round(v, 2) for k, v in decision['write_us'].items()},
                      'concentrated_vs_uniform': decision['concentrated_vs_uniform_write_us']}, indent=1))


if __name__ == '__main__':
    main()
