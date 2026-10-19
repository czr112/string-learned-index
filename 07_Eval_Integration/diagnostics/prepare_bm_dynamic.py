"""Freeze the stage-5 dynamic traces with the unchanged upstream mixed generator.

Two data (short keys, long-prefix URLs) x two read/write ratios (95/5, 80/20). The trace,
initial rows and per-kind counts are frozen and hashed before any measurement.
"""
import hashlib
import json
import sys
from fractions import Fraction
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WS = ROOT.parent
BENCH = ROOT / 'upstream_snapshots/benchmark_003a55bf'
sys.path.insert(0, str(BENCH / 'scripts'))
from benchmark_lib.workload import MixedWorkloadConfig, generate_mixed_operations  # noqa: E402
from benchmark_lib.interchange import write_operations, write_rows  # noqa: E402
import struct  # noqa: E402


def read_rows(path):
    data = path.read_bytes()
    assert data[:8] == b'SIBROW1\0'
    count, = struct.unpack_from('<Q', data, 8)
    pos = 16
    rows = []
    for _ in range(count):
        n, = struct.unpack_from('<Q', data, pos)
        pos += 8
        key = data[pos:pos + n]
        pos += n
        value, = struct.unpack_from('<Q', data, pos)
        pos += 8
        rows.append((key, value))
    assert pos == len(data)
    return rows

CELLS = [('english_words', 'english_words', Fraction(19, 20)),
         ('english_words', 'english_words_80_20', Fraction(4, 5)),
         ('memetracker_urls', 'memetracker_urls', Fraction(19, 20)),
         ('memetracker_urls', 'memetracker_urls_80_20', Fraction(4, 5))]
SEED = 20261019
OPERATIONS = 20000


def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    out = ROOT / 'results/bm_dynamic_inputs_20261019'
    out.mkdir(exist_ok=False)
    real = json.loads((ROOT / 'results/bm_real_20261018/identity.json').read_text(encoding='utf-8'))
    traces = {}
    for dataset, cell, find_weight in CELLS:
        rows_path = WS / real['inputs'][dataset]['rows_path']
        assert sha(rows_path) == real['inputs'][dataset]['rows_sha256']
        keys = [key for key, _value in read_rows(rows_path)]
        config = MixedWorkloadConfig(
            seed=SEED, operations=OPERATIONS, initial_load_fraction=Fraction(4, 5),
            operation_weights={'find': find_weight, 'insert': 1 - find_weight,
                               'update': Fraction(0), 'erase': Fraction(0), 'scan': Fraction(0)},
            read_miss_ratio=Fraction(1, 10))
        prepared = generate_mixed_operations(keys, config, 1 << 31)
        initial = list(zip(prepared.initial_keys, prepared.initial_values))
        values = {key: value for key, value in initial}
        rows = out / (cell + '_initial_rows.bin')
        row_hash = write_rows(rows, [key for key, _ in sorted(initial)], [values[key] for key, _ in sorted(initial)])
        operations = out / (cell + '_operations.bin')
        op_hash = write_operations(operations, prepared.operations)
        kinds = {}
        for kind, _key, _value, _limit in prepared.operations:
            kinds[kind] = kinds.get(kind, 0) + 1
        traces[cell] = {'dataset': dataset, 'rows_path': str(rows.relative_to(WS)), 'rows_sha256': row_hash,
                        'initial_rows': len(initial), 'operations_path': str(operations.relative_to(WS)),
                        'operations_sha256': op_hash, 'operations': len(prepared.operations),
                        'find_weight': str(find_weight), 'seed': SEED,
                        'kinds': kinds, 'read_miss_ratio': '1/10',
                        'sampler': 'unchanged benchmark_lib.workload.generate_mixed_operations'}
        print('froze', cell, len(initial), 'rows', len(prepared.operations), 'ops', kinds, flush=True)
    identity = {'traces': traces, 'seed': SEED, 'operations': OPERATIONS,
                'source_identity': str((ROOT / 'results/bm_real_20261018/identity.json').relative_to(WS)),
                'source_identity_sha256': sha(ROOT / 'results/bm_real_20261018/identity.json'),
                'note': 'traces frozen before any replay or timing'}
    (out / 'identity.json').write_text(json.dumps(identity, indent=2) + '\n', encoding='utf-8')
    print('wrote', out / 'identity.json')


if __name__ == '__main__':
    main()
