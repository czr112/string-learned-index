"""BM predicted-first bracket entry: build the audit, run five frozen cells, decide.

Counts only: this round never times a query. The candidate is closed at the entry if the
bracket path does not remove fence work on the main cell.
"""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = ROOT.parent
SNAPSHOTS = ROOT / 'upstream_snapshots'
MODEL = SNAPSHOTS / 'model_e8e05411'
BENCH = SNAPSHOTS / 'benchmark_003a55bf'
CELLS = ['tranco64_norepl', 'tranco64_miss', 'mixed_full_norepl', 'skew_full_norepl', 'skew_miss']
SAN_CELLS = ['tranco64_norepl', 'skew_full_norepl']
MAIN = 'tranco64_norepl'


def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p): return json.loads(p.read_text())
def save(p, obj): p.write_text(json.dumps(obj, indent=2, ensure_ascii=False) + '\n')


def decision(records):
    by = {r['cell']: r for r in records}
    def ratio(cell, key='bracket'):
        return by[cell][key]['compares_per_query'] / by[cell]['current']['compares_per_query']
    ratios = {cell: ratio(cell) for cell in CELLS}
    main = ratios[MAIN]
    return {
        'compares_per_query': {c: {'base': by[c]['base']['compares_per_query'],
                                   'current': by[c]['current']['compares_per_query'],
                                   'bracket': by[c]['bracket']['compares_per_query']} for c in CELLS},
        'bracket_over_current': ratios,
        'bracket_hit_rate': {c: by[c]['bracket']['hit_rate'] for c in CELLS},
        'window_blocks_per_query': {c: by[c]['current']['proposal_blocks'] / by[c]['queries'] for c in CELLS},
        'location_mismatches': sum(sum(r['mismatch'].values()) for r in records),
        'memory_equal_everywhere': all(r['memory']['frozen'] == r['memory']['copy'] for r in records),
        'main_cell_removes_work': main <= 0.90,
        'no_cell_regresses_over_5_percent': all(v <= 1.05 for v in ratios.values()),
        'timing_round_allowed': main <= 0.90 and all(v <= 1.05 for v in ratios.values()),
        'candidate_closed': main > 1.00,
        'model_parameters_searched': False,
        'production_adopted': False,
        'status': 'entry-only; counts are not timings',
    }


def main():
    p = argparse.ArgumentParser(); p.add_argument('--output', default='bm_bracket_entry_20261018')
    a = p.parse_args(); assert Path(a.output).name == a.output
    e2 = read(ROOT / 'results/e2_generalization_20261018/identity.json')
    lock = read(ROOT / 'results/upstream_update_audit_20261018/identity.json')
    assert subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=MODEL, text=True).strip() == lock['repositories']['model']['commit']
    assert subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=BENCH, text=True).strip() == lock['repositories']['benchmark']['commit']
    for name in ('src/block_model.h', 'src/block_model.cpp', 'src/key_bytes.h'):
        assert sha(MODEL / name) == lock['repositories']['model']['tracked_files_sha256'][name], name
    for name in ('src/unified/runner.cpp', 'src/unified/runner.h', 'src/index_contract.h'):
        assert sha(BENCH / name) == lock['repositories']['benchmark']['tracked_files_sha256'][name], name

    out = ROOT / 'results' / a.output
    out.mkdir(exist_ok=False)
    build = out / 'build'; build.mkdir()
    inputs = {}
    for cell in CELLS:
        item = e2['inputs'][cell]
        assert sha(WORKSPACE / item['rows_path']) == item['rows_sha256']
        assert sha(WORKSPACE / item['queries_path']) == item['queries']['sha256']
        inputs[cell] = {'rows_path': item['rows_path'], 'rows_sha256': item['rows_sha256'], 'rows': item['rows'],
                        'queries_path': item['queries_path'], 'queries_sha256': item['queries']['sha256'],
                        'queries': item['queries']['count'], 'kind': item['queries']['kind']}
    sources = {str(x.relative_to(WORKSPACE)): sha(x) for x in sorted({
        Path(__file__), ROOT / 'diagnostics/verify_bm_bracket_entry.py',
        ROOT / 'diagnostics/BM_BRACKET_ENTRY_SPEC_20261018.md',
        ROOT / 'diagnostics/audit_bm_bracket.cpp', ROOT / 'diagnostics/bm_bracket_core.hpp',
        ROOT / 'diagnostics/bm_page_core.hpp', ROOT / 'diagnostics/static_input.hpp',
        WORKSPACE / '02_Code_and_Experiments/validation_e25_20260918/static_physical_index.hpp',
        MODEL / 'src/block_model.h', MODEL / 'src/block_model.cpp', MODEL / 'src/key_bytes.h',
        BENCH / 'src/unified/runner.cpp', BENCH / 'src/unified/runner.h', BENCH / 'src/index_contract.h'})}
    meta = {'base_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
            'cpu': min(os.sched_getaffinity(0)), 'sources': sources, 'inputs': inputs, 'artifacts': {},
            'commands': [], 'compiler': subprocess.check_output(['g++', '--version'], text=True).splitlines()[0],
            'upstream_commit': lock['repositories']['model']['commit'],
            'benchmark_commit': lock['repositories']['benchmark']['commit'],
            'protocol': 'counts only; every page key and every frozen query checked against the full byte '
                        'directory; no timing in this round',
            'limits': ['counts are not timings', 'spec registered after the first smoke run',
                       'model parameters not searched', 'not an external ranking']}
    log = (out / 'commands.log').open('x', encoding='utf-8')

    def run(command, env=None):
        command = list(map(str, command)); meta['commands'].append(command)
        log.write('$ ' + ' '.join(command) + '\n'); log.flush()
        r = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=3600, env=env)
        log.write(r.stdout); log.write(r.stderr); log.flush()
        if r.returncode:
            save(out / 'failure.json', {'command': command, 'rc': r.returncode, 'stderr': r.stderr[-12000:]})
            save(out / 'identity.json', meta)
            raise RuntimeError('command failed; see failure.json')
        return r

    flags = ['-pthread', '-std=c++17', '-march=x86-64-v3',
             '-I' + str(WORKSPACE / '02_Code_and_Experiments/validation_e25_20260918'),
             '-I' + str(BENCH / 'src'), '-I' + str(MODEL / 'src'),
             '-I' + str(ROOT / 'learned-index/src'), '-I' + str(ROOT / 'diagnostics')]
    common = [ROOT / 'diagnostics/audit_bm_bracket.cpp', MODEL / 'src/block_model.cpp', BENCH / 'src/unified/runner.cpp']

    def build_binary(name, options):
        exe = build / name
        run(['g++'] + flags + options + common + ['-o', exe])
        meta['artifacts'][str(exe.relative_to(out))] = sha(exe)
        return exe

    print('building bracket entry audit', flush=True)
    driver = build_binary('audit_bm_bracket', ['-O2'])
    sanitized = build_binary('audit_bm_bracket_san',
                             ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'])
    save(out / 'identity.json', meta)

    records = []
    for cell in CELLS:
        item = inputs[cell]
        r = run(['taskset', '-c', meta['cpu'], driver,
                 WORKSPACE / item['rows_path'], WORKSPACE / item['queries_path']])
        assert not r.stderr
        record = json.loads(r.stdout)
        record['cell'] = cell
        assert record['mismatch'] == {'exact': 0, 'current': 0, 'bracket': 0, 'answer': 0}
        assert record['memory']['frozen'] == record['memory']['copy']
        assert record['bracket']['checks'] == item['queries']
        records.append(record); save(out / 'counts.json', records)
        print('cell', cell, 'current', round(record['current']['compares_per_query'], 3),
              'bracket', round(record['bracket']['compares_per_query'], 3),
              'hit', round(record['bracket']['hit_rate'], 4), flush=True)
    sanitizer_records = []
    for cell in SAN_CELLS:
        item = inputs[cell]
        r = run(['taskset', '-c', meta['cpu'], sanitized,
                 WORKSPACE / item['rows_path'], WORKSPACE / item['queries_path']],
                env=dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                         UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
        assert not r.stderr
        record = json.loads(r.stdout); record['cell'] = cell
        normal = next(x for x in records if x['cell'] == cell)
        for field in ('queries', 'keys_checked', 'pages', 'memory'):
            assert record[field] == normal[field], (cell, field)
        for path in ('base', 'current', 'bracket'):
            for field in ('total_compares', 'blocks', 'boundary_compares'):
                assert record[path][field] == normal[path][field], (cell, path, field)
        sanitizer_records.append(record); save(out / 'counts_sanitized.json', sanitizer_records)
        print('sanitized cell', cell, 'matches', flush=True)

    for cell, item in meta['inputs'].items():
        assert sha(WORKSPACE / item['rows_path']) == item['rows_sha256']
        assert sha(WORKSPACE / item['queries_path']) == item['queries_sha256']
    for path, digest in sources.items():
        assert sha(WORKSPACE / path) == digest, ('source drift', path)
    save(out / 'decision.json', decision(records))
    save(out / 'identity.json', meta)
    log.close(); data = (out / 'commands.log').read_bytes()
    with (out / 'commands.log.gz').open('xb') as handle:
        with gzip.GzipFile(filename='', mode='wb', fileobj=handle, mtime=0) as z:
            z.write(data)
    assert gzip.decompress((out / 'commands.log.gz').read_bytes()) == data
    save(out / 'log_archive.json', {'raw_sha256': hashlib.sha256(data).hexdigest(),
                                    'gzip_sha256': sha(out / 'commands.log.gz')})
    (out / 'commands.log').unlink()
    print('completed', len(records), 'cells', len(sanitizer_records), 'sanitized', flush=True)


if __name__ == '__main__':
    main()
