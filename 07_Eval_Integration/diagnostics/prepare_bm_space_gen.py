"""Prepare an independent sample (new seed) and one scale change for the space/build gate.

Uses the unchanged upstream sampler offline, then writes the same SIBROW1/SIBQRY1 interchange
the earlier rounds used. Only the data changes; no candidate, model or gate is touched.
"""
import hashlib
import json
import random
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WS = ROOT.parent
BENCH = ROOT / 'upstream_snapshots/benchmark_003a55bf'
SEED = 20261019
CELLS = [('english_words', 65536, 'english_words_s20261019'),
         ('memetracker_urls', 65536, 'memetracker_urls_s20261019'),
         ('memetracker_phrases', 65536, 'memetracker_phrases_s20261019'),
         ('english_words', 100000, 'english_words_100000_s20261019')]


def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p): return json.loads(p.read_text(encoding='utf-8'))


def main():
    out = ROOT / 'results/bm_space_gen_inputs_20261019'
    out.mkdir(exist_ok=False)
    (out / 'interchange').mkdir()
    lock = read(BENCH / 'datasets.lock.json')
    raw = {}
    for source in lock['sources'].values():
        path = BENCH / 'data/raw' / source['filename']
        assert path.stat().st_size == source['bytes'] and sha(path) == source['sha256'], source['filename']
        raw[str(path.relative_to(WS))] = {'sha256': source['sha256'], 'bytes': source['bytes']}
    # Unchanged upstream sampler, offline, once per (dataset, count) pair.
    for name, count in [('english_words', 65536), ('memetracker_urls', 65536),
                        ('memetracker_phrases', 65536), ('english_words', 100000)]:
        target = BENCH / 'data/real' / f'{name}_{count}_s{SEED}_string'
        if target.exists():
            print('reuse prepared sample', target.name, flush=True)
            continue
        subprocess.run([sys.executable, str(BENCH / 'scripts/prepare_real_datasets.py'),
                        '--datasets', name, '--count', str(count), '--seed', str(SEED), '--offline'],
                       cwd=BENCH, check=True, capture_output=True, text=True)
    sys.path.insert(0, str(BENCH / 'scripts'))
    from benchmark_lib.readers import iter_keys
    from benchmark_lib.interchange import write_rows, write_queries
    inputs = {}
    sources = {str((BENCH / 'datasets.lock.json').relative_to(WS)): sha(BENCH / 'datasets.lock.json'),
               str((BENCH / 'scripts/prepare_real_datasets.py').relative_to(WS)): sha(BENCH / 'scripts/prepare_real_datasets.py'),
               Path(__file__).relative_to(WS).as_posix(): sha(Path(__file__))}
    for name, count, cell in CELLS:
        data = BENCH / 'data/real' / f'{name}_{count}_s{SEED}_string'
        sidecar = Path(str(data) + '.json')
        info = read(sidecar)
        assert info['dataset'] == name and info['keys'] == info['requested_keys'] == count and info['seed'] == SEED
        assert sha(data) == info['dataset_sha256']
        keys = list(iter_keys(data, 'legacy-tli'))
        assert len(keys) == count and keys == sorted(set(keys))
        rows = out / 'interchange' / (cell + '_rows.bin')
        queries = out / 'interchange' / (cell + '_queries.bin')
        row_hash = write_rows(rows, keys)
        items = [(key, True, i) for i, key in enumerate(keys)]
        random.Random(20261019).shuffle(items)
        query_hash = write_queries(queries, items)
        inputs[cell] = {'rows_path': str(rows.relative_to(WS)), 'rows_sha256': row_hash, 'rows': count,
                        'queries_path': str(queries.relative_to(WS)), 'queries': query_hash,
                        'queries_count': count, 'dataset_path': str(data.relative_to(WS)),
                        'dataset_sha256': sha(data), 'sidecar_path': str(sidecar.relative_to(WS)),
                        'sidecar_sha256': sha(sidecar), 'seed': SEED, 'sampler': 'unchanged upstream prepare_real_datasets.py --offline',
                        'statistics': info.get('statistics')}
        print('prepared', cell, count, flush=True)
    identity = {'inputs': inputs, 'raw_sources': raw, 'sources': sources, 'seed': SEED,
                'query_seed': 20261019, 'model_after': '3894c767338a865d2920ba5e21283827fea1d803',
                'benchmark': '003a55bf674b43350fbb70242070fb74f33a56ce',
                'note': 'independent sample: new sampler seed, same sources; the 100000 cell is the one scale change; the unchanged sampler caps count at 100000 and the limit was not raised'}
    (out / 'identity.json').write_text(json.dumps(identity, indent=2) + '\n', encoding='utf-8')
    print('wrote', out / 'identity.json')


if __name__ == '__main__':
    main()
