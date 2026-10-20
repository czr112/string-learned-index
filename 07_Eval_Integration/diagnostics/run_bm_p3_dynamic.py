"""P3: eight-cell full-operation dynamic comparison (2 data x 2 ratios x growth/replacement).

Traces come from the unchanged upstream generator; the replay driver verifies every operation
against std::map and reports read and write cost separately.
"""
import argparse
import gzip
import hashlib
import json
import os
import random
import struct
import subprocess
from fractions import Fraction
from pathlib import Path
import sys

WS = Path('/mnt/d/StringLearnedIndex_Workspace')
ROOT = WS / '07_Eval_Integration'
BENCH = ROOT / 'upstream_snapshots/benchmark_003a55bf'
MODEL = ROOT / 'upstream_snapshots/model_3894c767'
sys.path.insert(0, str(BENCH / 'scripts'))
from benchmark_lib.workload import MixedWorkloadConfig, generate_mixed_operations  # noqa: E402
from benchmark_lib.interchange import write_operations, write_rows  # noqa: E402

DATA = {'english_words': '07_Eval_Integration/results/bm_real_20261018/interchange/english_words_rows.bin',
        'memetracker_urls': '07_Eval_Integration/results/bm_real_20261018/interchange/memetracker_urls_rows.bin'}
SEED = 20261019
OPERATIONS = 20000


def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()


def read_rows(path):
    data = path.read_bytes(); assert data[:8] == b'SIBROW1\0'
    count, = struct.unpack_from('<Q', data, 8); pos = 16; rows = []
    for _ in range(count):
        n, = struct.unpack_from('<Q', data, pos); pos += 8
        key = data[pos:pos + n]; pos += n
        value, = struct.unpack_from('<Q', data, pos); pos += 8
        rows.append((key, value))
    assert pos == len(data)
    return rows


def main():
    p = argparse.ArgumentParser(); p.add_argument('--output', default='bm_p3_dynamic_20261020')
    a = p.parse_args(); assert Path(a.output).name == a.output
    out = ROOT / 'results' / a.output
    out.mkdir(exist_ok=False)
    traces = out / 'traces'; traces.mkdir()
    build = out / 'build'; build.mkdir()
    cells, identity_inputs = [], {}
    for data, rel in DATA.items():
        keys = [key for key, _ in read_rows(WS / rel)]
        assert sha(WS / rel) and len(keys) == 65536
        for ratio, find_weight in [('95_5', Fraction(19, 20)), ('80_20', Fraction(4, 5))]:
            for mode in ('growth', 'replacement'):
                cell = f'{data}_{ratio}_{mode}'
                if mode == 'growth':
                    config = MixedWorkloadConfig(
                        seed=SEED, operations=OPERATIONS, initial_load_fraction=Fraction(4, 5),
                        operation_weights={'find': find_weight, 'insert': 1 - find_weight,
                                           'update': Fraction(0), 'erase': Fraction(0), 'scan': Fraction(0)},
                        read_miss_ratio=Fraction(1, 10))
                else:
                    config = MixedWorkloadConfig(
                        seed=SEED, operations=OPERATIONS, initial_load_fraction=Fraction(1, 1),
                        operation_weights={'find': find_weight, 'insert': Fraction(0),
                                           'update': 1 - find_weight, 'erase': Fraction(0),
                                           'scan': Fraction(0)},
                        read_miss_ratio=Fraction(1, 10))
                prepared = generate_mixed_operations(keys, config, 1 << 31)
                initial = sorted(zip(prepared.initial_keys, prepared.initial_values))
                rows_path = traces / (cell + '_rows.bin')
                row_hash = write_rows(rows_path, [k for k, _ in initial], [v for _, v in initial])
                op_path = traces / (cell + '_ops.bin')
                op_hash = write_operations(op_path, prepared.operations)
                kinds = {}
                for kind, *_ in prepared.operations:
                    kinds[str(kind)] = kinds.get(str(kind), 0) + 1
                identity_inputs[cell] = {'rows_path': str(rows_path.relative_to(WS)), 'rows_sha256': row_hash,
                                         'operations_path': str(op_path.relative_to(WS)),
                                         'operations_sha256': op_hash, 'initial_rows': len(initial),
                                         'operations': len(prepared.operations), 'kinds': kinds, 'mode': mode,
                                         'ratio': ratio, 'data': data}
                print('trace', cell, len(initial), 'rows', kinds, flush=True)

    flags = ['-pthread', '-std=c++17', '-march=x86-64-v3', '-O3',
             '-I' + str(WS / '02_Code_and_Experiments/validation_e25_20260918'),
             '-I' + str(BENCH / 'src'), '-I' + str(MODEL / 'src'), '-I' + str(ROOT / 'diagnostics')]
    exe = build / 'replay'
    subprocess.run(['g++'] + flags + [ROOT / 'diagnostics/replay_bm_dynamic.cpp',
                                      MODEL / 'src/block_model.cpp', '-o', exe],
                   check=True, capture_output=True, text=True)
    records = []
    for cell, item in identity_inputs.items():
        r = subprocess.run(['taskset', '-c', '0', str(exe), str(WS / item['rows_path']),
                            str(WS / item['operations_path'])], check=True, capture_output=True, text=True)
        record = json.loads(r.stdout); record['cell'] = cell
        assert record['mismatches'] == 0, cell
        assert record['final_read_checked'] == record['final_size'] == record['final_scan_rows']
        assert record['find_ops'] == item['kinds'].get('1', 0)
        assert record['insert_ops'] == item['kinds'].get('2', 0)
        assert record['update_ops'] == item['kinds'].get('3', 0)
        assert record['erase_ops'] == item['kinds'].get('4', 0)
        records.append(record); print('replay', cell, 'mismatches 0, size', record['final_size'], flush=True)
    (out / 'records.json').write_text(json.dumps(records, indent=2) + '\n', encoding='utf-8')
    decision = {
        'cells': {r['cell']: {'find_ops': r['find_ops'], 'insert_ops': r['insert_ops'],
                              'update_ops': r['update_ops'], 'erase_ops': r['erase_ops'],
                              'find_us': r['find_ns'] / max(1, r['find_ops']) / 1000,
                              'write_us': r['write_ns'] / max(1, r['insert_ops'] + r['update_ops'] + r['erase_ops']) / 1000,
                              'rebuilds': r['rebuilds'], 'inplace_inserts': r['inplace_inserts'],
                              'root_publications': r['root_publications'], 'final_size': r['final_size']}
                   for r in records},
        'all_cells_zero_mismatch': all(r['mismatches'] == 0 for r in records),
        'full_operation_cells': 8, 'throughput_claim': 'read and write micro-costs per cell; no mixed throughput',
        'production_adopted': False,
        'status': 'P3 eight-cell full-operation dynamic correctness and per-op cost'}
    (out / 'decision.json').write_text(json.dumps(decision, indent=2) + '\n', encoding='utf-8')
    sources = {str(p.relative_to(WS)): sha(p) for p in sorted({
        Path(__file__), ROOT / 'diagnostics/replay_bm_dynamic.cpp',
        ROOT / 'diagnostics/bm_bounded_index.hpp', ROOT / 'diagnostics/bm_page_index.hpp',
        ROOT / 'diagnostics/bm_page_core.hpp', ROOT / 'diagnostics/bm_page_root.hpp',
        MODEL / 'src/block_model.cpp', MODEL / 'src/block_model.h', MODEL / 'src/key_bytes.h',
        WS / '02_Code_and_Experiments/validation_e25_20260918/static_physical_index.hpp'})}
    meta = {'phase': 'P3', 'base_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT,
                                                                 text=True).strip(),
            'sources': sources, 'inputs': identity_inputs,
            'artifacts': {str(exe.relative_to(out)): sha(exe)},
            'protocol': 'eight frozen traces; every operation checked against std::map; final map by full read '
                        'and full ordered scan; single process per cell',
            'limits': ['single process per cell; no pairing', 'uniform targets; no concentrated writes',
                       'scan weight is zero in these cells', 'HOT absent', 'no adoption decision']}
    (out / 'identity.json').write_text(json.dumps(meta, indent=2) + '\n', encoding='utf-8')
    (out / 'verification.json').write_text(json.dumps({
        'status': 'verified', 'cells': len(records),
        'checks': ['trace identity', 'per-kind counts', 'zero mismatches', 'final map by read and scan']},
        indent=2) + '\n', encoding='utf-8')
    print(json.dumps({k: {'find_us': round(v['find_us'], 2), 'write_us': round(v['write_us'], 2),
                          'rebuilds': v['rebuilds']} for k, v in decision['cells'].items()}, indent=1))


if __name__ == '__main__':
    main()
