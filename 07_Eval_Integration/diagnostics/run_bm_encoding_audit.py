"""Stage-3 audit: encoding ambiguity, window width and fallback distribution on real keys."""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = ROOT.parent
SNAP = ROOT / 'upstream_snapshots'
MODEL = SNAP / 'model_e8e05411'
BENCH = SNAP / 'benchmark_003a55bf'
CELLS = ['english_words', 'memetracker_urls', 'memetracker_phrases', 'tranco64_norepl']
SAN_CELLS = ['memetracker_urls']


def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p): return json.loads(p.read_text())
def save(p, obj): p.write_text(json.dumps(obj, indent=2, ensure_ascii=False) + '\n')


def decision(records):
    by = {r['cell']: r for r in records}
    material = {c: (by[c]['collisions']['share'] >= 0.10
                    and by[c]['collisions']['extra_prefix_p50'] >= 2) for c in CELLS}
    wide = {c: by[c]['window_rows']['mean'] >= 8 for c in CELLS}
    return {'collision_share': {c: by[c]['collisions']['share'] for c in CELLS},
            'extra_prefix_median': {c: by[c]['collisions']['extra_prefix_p50'] for c in CELLS},
            'window_rows_mean': {c: by[c]['window_rows']['mean'] for c in CELLS},
            'window_rows_p95': {c: by[c]['window_rows']['p95'] for c in CELLS},
            'predicted_is_containing': {c: by[c]['predicted_is_containing']['rate'] for c in CELLS},
            'window_covers_containing': {c: by[c]['window_covers_containing']['rate'] for c in CELLS},
            'absent_outside_window': {c: by[c]['absent']['outside_rate'] for c in CELLS},
            'ambiguity_material': material, 'window_is_wide': wide,
            'encoding_refinement_justified': any(material[c] and wide[c] for c in CELLS),
            'justified_cells': [c for c in CELLS if material[c] and wide[c]],
            'certified_pages': {c: by[c]['certified_pages'] for c in CELLS},
            'verification_available': True,
            'production_adopted': False,
            'status': 'stage-3 audit only; no candidate, no timing'}


def main():
    p = argparse.ArgumentParser(); p.add_argument('--output', default='bm_encoding_audit_20261019')
    a = p.parse_args(); assert Path(a.output).name == a.output
    real = read(ROOT / 'results/bm_real_20261018/identity.json')
    e2 = read(ROOT / 'results/e2_generalization_20261018/identity.json')
    lock = read(ROOT / 'results/upstream_update_audit_20261018/identity.json')
    inputs = {}
    for cell in CELLS[:3]:
        item = real['inputs'][cell]
        inputs[cell] = {'rows_path': item['rows_path'], 'rows_sha256': item['rows_sha256'], 'rows': item['rows'],
                        'queries_path': item['queries_path'], 'queries_sha256': item['queries']['sha256'],
                        'queries': item['queries']['count']}
    item = e2['inputs']['tranco64_norepl']
    inputs['tranco64_norepl'] = {'rows_path': item['rows_path'], 'rows_sha256': item['rows_sha256'],
                                 'rows': item['rows'], 'queries_path': item['queries_path'],
                                 'queries_sha256': item['queries']['sha256'], 'queries': item['queries']['count']}
    for cell, item in inputs.items():
        assert sha(WORKSPACE / item['rows_path']) == item['rows_sha256'], cell
        assert sha(WORKSPACE / item['queries_path']) == item['queries_sha256'], cell
    out = ROOT / 'results' / a.output; out.mkdir(exist_ok=False)
    build = out / 'build'; build.mkdir()
    sources = {str(x.relative_to(WORKSPACE)): sha(x) for x in sorted({
        Path(__file__), ROOT / 'diagnostics/verify_bm_encoding_audit.py',
        ROOT / 'diagnostics/BM_ENCODING_AUDIT_SPEC_20261019.md',
        ROOT / 'diagnostics/audit_bm_encoding.cpp', ROOT / 'diagnostics/static_input.hpp',
        WORKSPACE / '02_Code_and_Experiments/validation_e25_20260918/static_physical_index.hpp',
        MODEL / 'src/block_model.cpp', MODEL / 'src/block_model.h', MODEL / 'src/key_bytes.h',
        BENCH / 'src/index_contract.h', BENCH / 'src/unified/runner.cpp', BENCH / 'src/unified/runner.h'})}
    meta = {'base_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
            'cpu': min(os.sched_getaffinity(0)), 'sources': sources, 'inputs': inputs, 'artifacts': {},
            'commands': [], 'compiler': subprocess.check_output(['g++', '--version'], text=True).splitlines()[0],
            'upstream_commit': lock['repositories']['model']['commit'],
            'benchmark_commit': lock['repositories']['benchmark']['commit'],
            'protocol': 'counts only: per-page BlockModel rebuilt exactly as the BM leaf does '
                        '(2048 rows, occupancy 15, fit(1,7,8,1)); windows, collisions and absent-key coverage',
            'limits': ['counts are not timings', 'synthetic queries from real keys (hit plus 10% absent)',
                       'no candidate is built in this round', 'no default change']}
    log = (out / 'commands.log').open('x', encoding='utf-8')

    def run(command, env=None):
        command = list(map(str, command)); meta['commands'].append(command)
        log.write('$ ' + ' '.join(command) + '\n'); log.flush()
        r = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=3600, env=env)
        log.write(r.stdout); log.write(r.stderr); log.flush()
        if r.returncode:
            save(out / 'failure.json', {'command': command, 'rc': r.returncode, 'stderr': r.stderr[-12000:]})
            save(out / 'identity.json', meta); raise RuntimeError('command failed; see failure.json')
        return r

    flags = ['-pthread', '-std=c++17', '-march=x86-64-v3',
             '-I' + str(WORKSPACE / '02_Code_and_Experiments/validation_e25_20260918'),
             '-I' + str(BENCH / 'src'), '-I' + str(MODEL / 'src'), '-I' + str(ROOT / 'diagnostics')]
    common = [ROOT / 'diagnostics/audit_bm_encoding.cpp', MODEL / 'src/block_model.cpp',
              BENCH / 'src/unified/runner.cpp']

    def build_binary(name, options):
        exe = build / name
        run(['g++'] + flags + options + common + ['-o', exe])
        meta['artifacts'][str(exe.relative_to(out))] = sha(exe)
        return exe

    print('building encoding audit', flush=True)
    driver = build_binary('audit_bm_encoding', ['-O2'])
    sanitized = build_binary('audit_bm_encoding_san',
                             ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'])
    save(out / 'identity.json', meta)

    records = []
    for cell in CELLS:
        item = inputs[cell]
        r = run(['taskset', '-c', meta['cpu'], driver, WORKSPACE / item['rows_path'],
                 WORKSPACE / item['queries_path']])
        record = json.loads(r.stdout); record['cell'] = cell
        records.append(record); save(out / 'audit.json', records)
        print('cell', cell, 'window', record['window_rows']['mean'], 'collisions',
              round(record['collisions']['share'], 3), 'pred', round(record['predicted_is_containing']['rate'], 3),
              flush=True)
    san = []
    for cell in SAN_CELLS:
        item = inputs[cell]
        r = run(['taskset', '-c', meta['cpu'], sanitized, WORKSPACE / item['rows_path'],
                 WORKSPACE / item['queries_path']],
                env=dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                         UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
        record = json.loads(r.stdout); record['cell'] = cell
        normal = next(x for x in records if x['cell'] == cell)
        assert record == dict(normal, cell=cell), 'sanitized counts differ'
        san.append(record); save(out / 'audit_sanitized.json', san)
        print('sanitized', cell, 'matches', flush=True)

    for cell, item in meta['inputs'].items():
        assert sha(WORKSPACE / item['rows_path']) == item['rows_sha256']
        assert sha(WORKSPACE / item['queries_path']) == item['queries_sha256']
    for path, digest in sources.items(): assert sha(WORKSPACE / path) == digest, ('source drift', path)
    save(out / 'decision.json', decision(records)); save(out / 'identity.json', meta)
    log.close(); data = (out / 'commands.log').read_bytes()
    with (out / 'commands.log.gz').open('xb') as handle:
        with gzip.GzipFile(filename='', mode='wb', fileobj=handle, mtime=0) as z: z.write(data)
    assert gzip.decompress((out / 'commands.log.gz').read_bytes()) == data
    save(out / 'log_archive.json', {'raw_sha256': hashlib.sha256(data).hexdigest(),
                                    'gzip_sha256': sha(out / 'commands.log.gz')})
    (out / 'commands.log').unlink()
    print('completed', len(records), 'cells', len(san), 'sanitized', flush=True)


if __name__ == '__main__':
    main()
