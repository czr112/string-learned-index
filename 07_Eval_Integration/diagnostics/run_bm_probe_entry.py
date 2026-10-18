"""Phase-2 entry: predicted-block payload probe (counts only, no timing)."""
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
SAN_CELLS = ['english_words', 'memetracker_phrases']
MAIN = 'english_words'


def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p): return json.loads(p.read_text())
def save(p, obj): p.write_text(json.dumps(obj, indent=2, ensure_ascii=False) + '\n')


def decision(records):
    by = {r['cell']: r for r in records}
    fence = {c: (by[c]['probe']['fence_per_query'] - by[c]['current']['fence_per_query'])
             / by[c]['current']['fence_per_query'] for c in CELLS}
    blocks = {c: (by[c]['probe']['blocks_per_query'] - by[c]['current']['blocks_per_query']) for c in CELLS}
    hits = {c: by[c]['probe']['hit_rate'] for c in CELLS}
    return {'fence_reduction': fence, 'block_growth_per_query': blocks, 'probe_hit_rate': hits,
            'main_cell_fence_reduction': -fence[MAIN],
            'never_amplifies_blocks': all(v <= 0 for v in blocks.values()),
            'entry_gate_25_percent': -fence[MAIN] >= 0.25,
            'entry_passes': -fence[MAIN] >= 0.25 and all(v <= 0 for v in blocks.values()),
            'timing_round_allowed': False,
            'location_mismatches': sum(r['mismatch'] for r in records),
            'candidate_closed': True,
            'prediction_family_failures': 2,
            'stop_prediction_family': True,
            'production_adopted': False,
            'status': 'entry-only; counts are not timings; second failure in the predicted-block family'}


def main():
    p = argparse.ArgumentParser(); p.add_argument('--output', default='bm_probe_entry_20261018')
    a = p.parse_args(); assert Path(a.output).name == a.output
    real = read(ROOT / 'results/bm_real_20261018/identity.json')
    e2 = read(ROOT / 'results/e2_generalization_20261018/identity.json')
    lock = read(ROOT / 'results/upstream_update_audit_20261018/identity.json')
    assert subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=MODEL, text=True).strip() == lock['repositories']['model']['commit']
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
        Path(__file__), ROOT / 'diagnostics/verify_bm_probe.py', ROOT / 'diagnostics/BM_PROBE_SPEC_20261018.md',
        ROOT / 'diagnostics/audit_bm_probe.cpp', ROOT / 'diagnostics/bm_probe_page.hpp',
        ROOT / 'diagnostics/bm_page_core.hpp', ROOT / 'diagnostics/static_input.hpp',
        WORKSPACE / '02_Code_and_Experiments/validation_e25_20260918/static_physical_index.hpp',
        MODEL / 'src/block_model.cpp', MODEL / 'src/block_model.h', MODEL / 'src/key_bytes.h',
        BENCH / 'src/index_contract.h', BENCH / 'src/unified/runner.cpp', BENCH / 'src/unified/runner.h'})}
    meta = {'base_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
            'cpu': min(os.sched_getaffinity(0)), 'sources': sources, 'inputs': inputs, 'artifacts': {},
            'commands': [], 'compiler': subprocess.check_output(['g++', '--version'], text=True).splitlines()[0],
            'upstream_commit': lock['repositories']['model']['commit'],
            'benchmark_commit': lock['repositories']['benchmark']['commit'],
            'protocol': 'counts only; every page key and every query checked against the frozen BM page and '
                        'the full byte directory; no timing in this round',
            'limits': ['counts are not timings', 'no hash of the 003a55bf benchmark tree beyond the contract/runner',
                       'not a candidate round', 'no default change']}
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
    common = [ROOT / 'diagnostics/audit_bm_probe.cpp', MODEL / 'src/block_model.cpp',
              BENCH / 'src/unified/runner.cpp']

    def build_binary(name, options):
        exe = build / name
        run(['g++'] + flags + options + common + ['-o', exe])
        meta['artifacts'][str(exe.relative_to(out))] = sha(exe)
        return exe

    print('building payload-probe entry', flush=True)
    driver = build_binary('audit_bm_probe', ['-O2'])
    sanitized = build_binary('audit_bm_probe_san',
                             ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'])
    save(out / 'identity.json', meta)

    records = []
    for cell in CELLS:
        item = inputs[cell]
        r = run(['taskset', '-c', meta['cpu'], driver, WORKSPACE / item['rows_path'],
                 WORKSPACE / item['queries_path']])
        record = json.loads(r.stdout); record['cell'] = cell
        assert record['mismatch'] == 0 and record['keys_checked'] == item['rows'] + item['queries']
        records.append(record); save(out / 'counts.json', records)
        print('cell', cell, 'fence', round(record['current']['fence_per_query'], 3), '->',
              round(record['probe']['fence_per_query'], 3), 'blocks', round(record['current']['blocks_per_query'], 3),
              '->', round(record['probe']['blocks_per_query'], 3), 'hit', round(record['probe']['hit_rate'], 4), flush=True)
    san_records = []
    for cell in SAN_CELLS:
        item = inputs[cell]
        r = run(['taskset', '-c', meta['cpu'], sanitized, WORKSPACE / item['rows_path'],
                 WORKSPACE / item['queries_path']],
                env=dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                         UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
        record = json.loads(r.stdout); record['cell'] = cell
        normal = next(x for x in records if x['cell'] == cell)
        for path in ('current', 'probe'):
            for field, value in record[path].items():
                assert value == normal[path][field], (cell, path, field)
        san_records.append(record); save(out / 'counts_sanitized.json', san_records)
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
    print('completed', len(records), 'cells', len(san_records), 'sanitized', flush=True)


if __name__ == '__main__':
    main()
