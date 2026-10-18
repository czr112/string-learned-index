"""Short-directory ablation on the BM leaf page: entry (work) then a page-level paired timing."""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess

ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = ROOT.parent
SNAPSHOTS = ROOT / 'upstream_snapshots'
MODEL = SNAPSHOTS / 'model_e8e05411'
BENCH = SNAPSHOTS / 'benchmark_003a55bf'
CELLS = ['tranco64_norepl', 'tranco64_miss', 'mixed_full_norepl', 'skew_full_norepl', 'skew_miss']
SAN_CELLS = ['tranco64_norepl', 'skew_full_norepl']
TIME_CELLS = ['tranco64_norepl', 'mixed_full_norepl', 'skew_full_norepl']
MAIN = 'tranco64_norepl'
ARMS = ['current', 'current_AA', 'short', 'short_AA', 'directory', 'directory_AA']


def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p): return json.loads(p.read_text())
def save(p, obj): p.write_text(json.dumps(obj, indent=2, ensure_ascii=False) + '\n')


def verify_process(rows, expected):
    assert len(rows) == 36
    assert all(r['operations'] == expected and r['elapsed_ns'] > 0 for r in rows)
    for repeat in range(6):
        seq = sorted((r for r in rows if r['repeat'] == repeat), key=lambda r: r['position'])
        assert [r['position'] for r in seq] == list(range(6))
        assert {r['arm'] for r in seq} == set(ARMS)
        assert len({r['checksum'] for r in seq}) == 1
    for arm in ARMS: assert sum(1 for r in rows if r['arm'] == arm) == 6


def summary(raw):
    cells = []
    for cell in TIME_CELLS:
        rows = [r for r in raw if r['cell'] == cell]
        assert len(rows) == 180
        per_pair = {}
        for name, x, y in [('short/current', 'short', 'current'), ('directory/current', 'directory', 'current')]:
            medians = []
            for process in range(5):
                t = {(r['repeat'], r['arm']): r['elapsed_ns'] for r in rows if r['process'] == process}
                medians.append(statistics.median(t[rep, x] / t[rep, y] for rep in range(6)))
            per_pair[name] = {'process_medians': medians, 'median': statistics.median(medians),
                              'faster': sum(v < 1 for v in medians), 'slower': sum(v > 1 for v in medians)}
        bands = {}
        for name, x, y in [('current_AA/current', 'current_AA', 'current'),
                           ('short_AA/short', 'short_AA', 'short')]:
            medians = []
            for process in range(5):
                t = {(r['repeat'], r['arm']): r['elapsed_ns'] for r in rows if r['process'] == process}
                medians.append(statistics.median(t[rep, x] / t[rep, y] for rep in range(6)))
            per_pair[name] = {'process_medians': medians, 'median': statistics.median(medians),
                              'faster': sum(v < 1 for v in medians), 'slower': sum(v > 1 for v in medians)}
            bands[name] = max(max(v, 1 / v) for v in medians)
        band = max(bands.values())
        for value in per_pair.values():
            value['clear_gain'] = value['median'] <= .95 and value['faster'] >= 4 and 1 / value['median'] > band
            value['clear_regression'] = value['median'] > 1.05 and value['slower'] >= 4 and value['median'] > band
        cells.append({'cell': cell, 'ratios': per_pair, 'aa_bands': bands, 'observed_band': band,
                      'ns_per_op': {a: statistics.median(r['elapsed_ns'] / r['operations']
                                                         for r in rows if r['arm'] == a) for a in ARMS}})
    return cells


def decision(entries, cells):
    by = {e['cell']: e for e in entries}
    ratios = {c['cell']: c['ratios']['short/current']['median'] for c in cells}
    main = ratios[MAIN]
    structure = {c: {'fence_per_query': by[c]['short']['fence_per_query'],
                     'current_fence_per_query': by[c]['current']['fence_compares'] / by[c]['queries'],
                     'numeric_per_query': by[c]['short']['numeric_per_query'],
                     'hit_rate': by[c]['short']['hit_rate']} for c in CELLS}
    fence_ratio = {c: structure[c]['fence_per_query'] / structure[c]['current_fence_per_query']
                   if structure[c]['current_fence_per_query'] else 0 for c in CELLS}
    return {'timing_short_over_current': ratios,
            'short_fence_over_current_fence': fence_ratio,
            'short_hit_rate': {c: structure[c]['hit_rate'] for c in CELLS},
            'useful_pages': {c: [by[c]['useful_pages'], by[c]['pages']] for c in CELLS},
            'short_directory_bytes': {c: by[c]['memory']['short_directory'] for c in CELLS},
            'structural_gate_registered_all_cells': all(v <= 0.30 for v in fence_ratio.values()),
            'structural_gate_on_applicable_pages': all(
                v <= 0.30 for c, v in fence_ratio.items() if structure[c]['hit_rate'] > 0),
            'location_mismatches': sum(sum(e['mismatch'].values()) for e in entries),
            'timing_gate': main <= 0.90 and all(c['ratios']['short/current']['median'] <= 1.05 for c in cells),
            'clear_gain_cells': [c['cell'] for c in cells if c['ratios']['short/current']['clear_gain']],
            'clear_regression_cells': [c['cell'] for c in cells if c['ratios']['short/current']['clear_regression']],
            'scoped_candidate': main <= 0.90 and all(c['ratios']['short/current']['median'] <= 1.05 for c in cells),
            'candidate_for_full_read_gate': False,
            'full_read_gate_needs_new_registration': True,
            'model_parameters_searched': False, 'upstream_modified': False,
            'production_adopted': False,
            'status': 'page-level positioning ablation; root routing excluded from timing'}


def main():
    p = argparse.ArgumentParser(); p.add_argument('--output', default='bm_shortdir_20261018')
    a = p.parse_args(); assert Path(a.output).name == a.output
    e2 = read(ROOT / 'results/e2_generalization_20261018/identity.json')
    lock = read(ROOT / 'results/upstream_update_audit_20261018/identity.json')
    assert subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=MODEL, text=True).strip() == lock['repositories']['model']['commit']
    assert subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=BENCH, text=True).strip() == lock['repositories']['benchmark']['commit']
    for name in ('src/block_model.h', 'src/block_model.cpp', 'src/key_bytes.h', 'src/short_directory.h'):
        assert sha(MODEL / name) == lock['repositories']['model']['tracked_files_sha256'][name], name
    for name in ('src/unified/runner.cpp', 'src/unified/runner.h', 'src/index_contract.h'):
        assert sha(BENCH / name) == lock['repositories']['benchmark']['tracked_files_sha256'][name], name

    out = ROOT / 'results' / a.output; out.mkdir(exist_ok=False)
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
        Path(__file__), ROOT / 'diagnostics/verify_bm_shortdir.py',
        ROOT / 'diagnostics/BM_SHORTDIR_SPEC_20261018.md',
        ROOT / 'diagnostics/bench_bm_shortdir.cpp', ROOT / 'diagnostics/bm_shortdir_core.hpp',
        ROOT / 'diagnostics/bm_page_core.hpp', ROOT / 'diagnostics/static_input.hpp',
        WORKSPACE / '02_Code_and_Experiments/validation_e25_20260918/static_physical_index.hpp',
        MODEL / 'src/block_model.h', MODEL / 'src/block_model.cpp', MODEL / 'src/key_bytes.h',
        MODEL / 'src/short_directory.h',
        BENCH / 'src/unified/runner.cpp', BENCH / 'src/unified/runner.h', BENCH / 'src/index_contract.h'})}
    meta = {'base_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
            'cpu': min(os.sched_getaffinity(0)), 'sources': sources, 'inputs': inputs, 'artifacts': {},
            'commands': [], 'compiler': subprocess.check_output(['g++', '--version'], text=True).splitlines()[0],
            'upstream_commit': lock['repositories']['model']['commit'],
            'benchmark_commit': lock['repositories']['benchmark']['commit'],
            'protocol': 'entry: every page key and every frozen query against the full byte directory; '
                        'timing: page routing precomputed, 6 arms, 5 processes x 6 Williams rows',
            'limits': ['page-level positioning only; root routing excluded from timing',
                       'not the full BM read gate', 'no ART/HOT ranking in this round',
                       'short directory bytes are outside the object self-report',
                       'not production adopted']}
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
             '-I' + str(BENCH / 'src'), '-I' + str(MODEL / 'src'),
             '-I' + str(ROOT / 'learned-index/src'), '-I' + str(ROOT / 'diagnostics')]
    common = [ROOT / 'diagnostics/bench_bm_shortdir.cpp', MODEL / 'src/block_model.cpp',
              BENCH / 'src/unified/runner.cpp']

    def build_binary(name, options):
        exe = build / name
        run(['g++'] + flags + options + common + ['-o', exe])
        meta['artifacts'][str(exe.relative_to(out))] = sha(exe)
        return exe

    print('building short-directory ablation', flush=True)
    driver = build_binary('bm_shortdir', ['-O3'])
    sanitized = build_binary('bm_shortdir_san', ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'])
    save(out / 'identity.json', meta)

    entries = []
    for cell in CELLS:
        item = inputs[cell]
        r = run(['taskset', '-c', meta['cpu'], driver, WORKSPACE / item['rows_path'],
                 WORKSPACE / item['queries_path'], 'entry', '0'])
        assert not r.stderr
        record = json.loads(r.stdout); record['cell'] = cell
        assert record['mismatch'] == {'bm': 0, 'short': 0, 'counted': 0, 'answer': 0}
        entries.append(record); save(out / 'counts.json', entries)
        print('entry', cell, 'fence', round(record['current']['fence_per_query'], 3), '->',
              round(record['short']['fence_per_query'], 3), 'hit', round(record['short']['hit_rate'], 4), flush=True)
    san_entries = []
    for cell in SAN_CELLS:
        item = inputs[cell]
        r = run(['taskset', '-c', meta['cpu'], sanitized, WORKSPACE / item['rows_path'],
                 WORKSPACE / item['queries_path'], 'entry', '0'],
                env=dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                         UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
        assert not r.stderr
        record = json.loads(r.stdout); record['cell'] = cell
        normal = next(x for x in entries if x['cell'] == cell)
        for field in ('queries', 'keys_checked', 'pages', 'useful_pages'):
            assert record[field] == normal[field], (cell, field)
        for path, names in {'current': ('fence_compares', 'directory_steps', 'blocks'),
                            'short': ('fence_compares', 'search_steps', 'bracket_compares',
                                      'hits', 'fallbacks', 'blocks')}.items():
            for field in names:
                assert record[path][field] == normal[path][field], (cell, path, field)
        san_entries.append(record); save(out / 'counts_sanitized.json', san_entries)
        print('sanitized entry', cell, 'matches', flush=True)

    raw = []
    for cell in TIME_CELLS:
        item = inputs[cell]
        for process in range(5):
            r = run(['taskset', '-c', meta['cpu'], driver, WORKSPACE / item['rows_path'],
                     WORKSPACE / item['queries_path'], 'time', process])
            assert not r.stderr
            records = [json.loads(line) for line in r.stdout.splitlines()]
            gate = [x for x in records if x.get('mode') == 'gate']; assert len(gate) == 1
            passes = [x for x in records if x.get('phase') == 'pass']
            verify_process(passes, item['queries'])
            for row in passes: row.update(cell=cell, process=process)
            raw.extend(passes); save(out / 'raw.json', raw)
        print('timing', cell, 'x5 done', flush=True)
    assert len(raw) == 3 * 5 * 36

    for cell, item in meta['inputs'].items():
        assert sha(WORKSPACE / item['rows_path']) == item['rows_sha256']
        assert sha(WORKSPACE / item['queries_path']) == item['queries_sha256']
    for path, digest in sources.items(): assert sha(WORKSPACE / path) == digest, ('source drift', path)
    cells = summary(raw)
    save(out / 'summary.json', cells)
    save(out / 'decision.json', decision(entries, cells))
    save(out / 'identity.json', meta)
    log.close(); data = (out / 'commands.log').read_bytes()
    with (out / 'commands.log.gz').open('xb') as handle:
        with gzip.GzipFile(filename='', mode='wb', fileobj=handle, mtime=0) as z: z.write(data)
    assert gzip.decompress((out / 'commands.log.gz').read_bytes()) == data
    save(out / 'log_archive.json', {'raw_sha256': hashlib.sha256(data).hexdigest(),
                                    'gzip_sha256': sha(out / 'commands.log.gz')})
    (out / 'commands.log').unlink()
    print('completed', len(entries), 'entries', len(raw), 'timing rows', flush=True)


if __name__ == '__main__':
    main()
