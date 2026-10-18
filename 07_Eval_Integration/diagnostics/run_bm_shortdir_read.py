"""Short-directory full read gate: domain checks, then complete-find timing with the real root."""
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
SNAP = ROOT / 'upstream_snapshots'
MODEL = SNAP / 'model_e8e05411'
BENCH = SNAP / 'benchmark_003a55bf'
NATIVE_LIB = WORKSPACE / 'builds/native'
REAL_CELLS = ['english_words', 'memetracker_urls', 'memetracker_phrases']
CELLS = REAL_CELLS + ['tranco64_norepl']
SAN_CELLS = ['english_words', 'memetracker_phrases']
ARMS = ['shortdir', 'shortdir_AA', 'bm-page-window', 'bm-page-window_AA', 'bm-page-base',
        'native-shortdir-first', 'ART', 'F5']
PAIRS = [('shortdir', 'ART'), ('shortdir', 'bm-page-window'), ('shortdir', 'native-shortdir-first'),
         ('bm-page-window', 'ART'), ('native-shortdir-first', 'ART'), ('F5', 'ART'),
         ('shortdir_AA', 'shortdir'), ('bm-page-window_AA', 'bm-page-window')]


def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p): return json.loads(p.read_text())
def save(p, obj): p.write_text(json.dumps(obj, indent=2, ensure_ascii=False) + '\n')


def verify_process(rows, expected):
    assert len(rows) == 48
    assert all(r['operations'] == expected and r['elapsed_ns'] > 0 for r in rows)
    for repeat in range(6):
        seq = sorted((r for r in rows if r['repeat'] == repeat), key=lambda r: r['position'])
        assert [r['position'] for r in seq] == list(range(8))
        assert {r['arm'] for r in seq} == set(ARMS)
        assert len({r['checksum'] for r in seq}) == 1
    for arm in ARMS: assert sum(1 for r in rows if r['arm'] == arm) == 6


def summary(raw):
    cells = []
    for cell in CELLS:
        rows = [r for r in raw if r['cell'] == cell]
        assert len(rows) == 240
        ratios = {}
        for x, y in PAIRS:
            medians = []
            for process in range(5):
                t = {(r['repeat'], r['arm']): r['elapsed_ns'] for r in rows if r['process'] == process}
                medians.append(statistics.median(t[rep, x] / t[rep, y] for rep in range(6)))
            ratios[x + '/' + y] = {'process_medians': medians, 'median': statistics.median(medians),
                                   'faster': sum(v < 1 for v in medians), 'slower': sum(v > 1 for v in medians)}
        bands = {name: max(max(v, 1 / v) for v in ratios[name]['process_medians'])
                 for name in ('shortdir_AA/shortdir', 'bm-page-window_AA/bm-page-window')}
        band = max(bands.values())
        for value in ratios.values():
            value['clear_gain'] = value['median'] <= .95 and value['faster'] >= 4 and 1 / value['median'] > band
            value['clear_regression'] = value['median'] > 1.05 and value['slower'] >= 4 and value['median'] > band
        cells.append({'cell': cell, 'ratios': ratios, 'aa_bands': bands, 'observed_band': band,
                      'ns_per_op': {a: statistics.median(r['elapsed_ns'] / r['operations']
                                                         for r in rows if r['arm'] == a) for a in ARMS}})
    return cells


def decision(cells, domains):
    art = {c['cell']: c['ratios']['shortdir/ART']['median'] for c in cells}
    page = {c['cell']: c['ratios']['shortdir/bm-page-window']['median'] for c in cells}
    native = {c['cell']: c['ratios']['shortdir/native-shortdir-first']['median'] for c in cells}
    real_art = {c: v for c, v in art.items() if c in REAL_CELLS}
    by = {d['cell']: d for d in domains}
    metadata = {}
    for cell in CELLS:
        d = by[cell]
        added = d['memory']['short_directory'] + d['memory']['fence_views']
        metadata[cell] = {'candidate_total': d['memory']['total'], 'added': added,
                          'added_share': added / d['memory']['total']}
    return {'shortdir_over_ART': art, 'shortdir_over_bm_page_window': page,
            'shortdir_over_native_shortdir_first': native,
            'real_cells_within_1_10': all(v <= 1.10 for v in real_art.values()),
            'all_cells_within_1_10': all(v <= 1.10 for v in art.values()),
            'metadata_added_share': {c: metadata[c]['added_share'] for c in CELLS},
            'metadata_within_5_percent': all(m['added_share'] <= 0.05 for m in metadata.values()),
            'clear_gain_cells': [c['cell'] for c in cells if c['ratios']['shortdir/ART']['clear_gain']],
            'clear_regression_cells': [c['cell'] for c in cells if c['ratios']['shortdir/ART']['clear_regression']],
            'beats_replaced_page_path': all(v < 1 for v in page.values()),
            'production_adopted': False, 'default_changed': False,
            'status': 'static read-only candidate gate; no dynamic operations measured'}


def main():
    p = argparse.ArgumentParser(); p.add_argument('--output', default='bm_shortdir_read_20261018')
    a = p.parse_args(); assert Path(a.output).name == a.output
    real = read(ROOT / 'results/bm_real_20261018/identity.json')
    e2 = read(ROOT / 'results/e2_generalization_20261018/identity.json')
    lock = read(ROOT / 'results/upstream_update_audit_20261018/identity.json')
    assert subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=MODEL, text=True).strip() == lock['repositories']['model']['commit']
    assert subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=BENCH, text=True).strip() == lock['repositories']['benchmark']['commit']
    inputs = {}
    for cell in REAL_CELLS:
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
        Path(__file__), ROOT / 'diagnostics/verify_bm_shortdir_read.py',
        ROOT / 'diagnostics/BM_SHORTDIR_READ_SPEC_20261018.md',
        ROOT / 'diagnostics/bench_bm_shortdir_read.cpp', ROOT / 'diagnostics/bm_shortdir_index.hpp',
        ROOT / 'diagnostics/bm_shortdir_page.hpp', ROOT / 'diagnostics/bm_page_core.hpp',
        ROOT / 'diagnostics/bm_page_index.hpp', ROOT / 'diagnostics/bm_page_root.hpp',
        ROOT / 'diagnostics/coupled_page_adapter.hpp', ROOT / 'diagnostics/coupled_page_index.hpp',
        ROOT / 'diagnostics/coupled_root.hpp', ROOT / 'diagnostics/static_input.hpp',
        ROOT / 'adapters/art/art_adapter.cpp', ROOT / 'adapters/art/art_adapter.h',
        WORKSPACE / '02_Code_and_Experiments/validation_e25_20260918/static_physical_index.hpp',
        MODEL / 'src/block_model.cpp', MODEL / 'src/block_model.h', MODEL / 'src/key_bytes.h',
        MODEL / 'src/short_directory.h', MODEL / 'src/residual_tags.cpp', MODEL / 'src/residual_tags.h',
        MODEL / 'src/region_planner.cpp', MODEL / 'src/region_planner.h', MODEL / 'src/query_cost.cpp',
        MODEL / 'src/query_cost.h', MODEL / 'src/learned_index.cpp', MODEL / 'src/learned_index.h',
        MODEL / 'src/tag_probe.h', MODEL / 'src/trie.cpp', MODEL / 'src/trie.h',
        MODEL / 'src/benchmark/model_factories.cpp', MODEL / 'src/benchmark/factories.h',
        BENCH / 'src/index_contract.h', BENCH / 'src/unified/runner.cpp', BENCH / 'src/unified/runner.h'})}
    meta = {'base_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
            'cpu': min(os.sched_getaffinity(0)), 'sources': sources, 'inputs': inputs, 'artifacts': {},
            'commands': [], 'compiler': subprocess.check_output(['g++', '--version'], text=True).splitlines()[0],
            'upstream_commit': lock['repositories']['model']['commit'],
            'benchmark_commit': lock['repositories']['benchmark']['commit'],
            'protocol': 'domain gate then 5 processes x 6 Williams rows x 8 arms on complete find; '
                        'every tenth query replaced by a proved-absent suffix key',
            'limits': ['read-only: no insert/update/erase/scan-under-writes',
                       'static root is a sorted owning vector, not the dynamic FlatRoot',
                       'HOT absent', 'no adoption decision']}
    log = (out / 'commands.log').open('x', encoding='utf-8')

    def run(command, env=None):
        command = list(map(str, command)); meta['commands'].append(command)
        log.write('$ ' + ' '.join(command) + '\n'); log.flush()
        r = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=7200, env=env)
        log.write(r.stdout); log.write(r.stderr); log.flush()
        if r.returncode:
            save(out / 'failure.json', {'command': command, 'rc': r.returncode, 'stderr': r.stderr[-12000:]})
            save(out / 'identity.json', meta); raise RuntimeError('command failed; see failure.json')
        return r

    flags = ['-pthread', '-std=c++17', '-march=x86-64-v3',
             '-I' + str(WORKSPACE / '02_Code_and_Experiments/validation_e25_20260918'),
             '-I' + str(WORKSPACE / '02_Code_and_Experiments/validation_e25_20260918/third_party/vendor/libart/src'),
             '-I' + str(BENCH / 'src'), '-I' + str(MODEL / 'src'),
             '-I' + str(ROOT / 'diagnostics'), '-I' + str(ROOT / 'adapters/art'),
             '-DCOUPLED_CORE_SHA256="f5-core-bundle"', '-DCOUPLED_ADAPTER_SHA256="f5-adapter"']
    common = [ROOT / 'diagnostics/bench_bm_shortdir_read.cpp', ROOT / 'adapters/art/art_adapter.cpp',
              BENCH / 'src/unified/runner.cpp', NATIVE_LIB / 'libblocklearn_adapter.a',
              NATIVE_LIB / 'libblocklearn_core.a',
              ROOT / 'results/e2_generalization_20261018/build/art/libart_adapter.a']

    def build_binary(name, options):
        exe = build / name
        run(['g++'] + flags + options + common + ['-o', exe])
        meta['artifacts'][str(exe.relative_to(out))] = sha(exe)
        return exe

    print('building short-directory full-read driver', flush=True)
    driver = build_binary('sdread', ['-O3'])
    sanitized = build_binary('sdread_san', ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'])
    save(out / 'identity.json', meta)

    domains = []
    for cell in CELLS:
        item = inputs[cell]
        r = run(['taskset', '-c', meta['cpu'], driver, WORKSPACE / item['rows_path'],
                 WORKSPACE / item['queries_path'], 'domain', '0'])
        record = json.loads(r.stdout); record['cell'] = cell
        assert record['mismatches'] == 0
        domains.append(record); save(out / 'domain.json', domains)
        print('domain', cell, record['checks'], 'checks', flush=True)
    san_domains = []
    for cell in SAN_CELLS:
        item = inputs[cell]
        r = run(['taskset', '-c', meta['cpu'], sanitized, WORKSPACE / item['rows_path'],
                 WORKSPACE / item['queries_path'], 'domain', '0'],
                env=dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                         UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
        record = json.loads(r.stdout); record['cell'] = cell
        normal = next(x for x in domains if x['cell'] == cell)
        assert record['checks'] == normal['checks'] and record['mismatches'] == 0
        assert record['memory'] == normal['memory']
        san_domains.append(record); save(out / 'domain_sanitized.json', san_domains)
        print('sanitized domain', cell, 'matches', flush=True)

    raw = []
    for cell in CELLS:
        item = inputs[cell]
        for process in range(5):
            r = run(['taskset', '-c', meta['cpu'], driver, WORKSPACE / item['rows_path'],
                     WORKSPACE / item['queries_path'], 'time', process])
            records = [json.loads(line) for line in r.stdout.splitlines()]
            gates = [x for x in records if x.get('phase') == 'gate']
            objects = [x for x in records if x.get('phase') == 'objects']
            passes = [x for x in records if x.get('phase') == 'pass']
            assert len(gates) == 8 and len(passes) == 48 and len(objects) == 1
            assert all(g['verified'] == item['rows'] + item['queries'] for g in gates)
            verify_process(passes, item['queries'])
            for row in passes: row.update(cell=cell, process=process)
            raw.extend(passes); save(out / 'raw.json', raw)
        print('timing', cell, 'x5 done', flush=True)
    assert len(raw) == 4 * 5 * 48

    for cell, item in meta['inputs'].items():
        assert sha(WORKSPACE / item['rows_path']) == item['rows_sha256']
        assert sha(WORKSPACE / item['queries_path']) == item['queries_sha256']
    for path, digest in sources.items(): assert sha(WORKSPACE / path) == digest, ('source drift', path)
    cells = summary(raw)
    save(out / 'summary.json', cells)
    save(out / 'decision.json', decision(cells, domains))
    save(out / 'identity.json', meta)
    log.close(); data = (out / 'commands.log').read_bytes()
    with (out / 'commands.log.gz').open('xb') as handle:
        with gzip.GzipFile(filename='', mode='wb', fileobj=handle, mtime=0) as z: z.write(data)
    assert gzip.decompress((out / 'commands.log.gz').read_bytes()) == data
    save(out / 'log_archive.json', {'raw_sha256': hashlib.sha256(data).hexdigest(),
                                    'gzip_sha256': sha(out / 'commands.log.gz')})
    (out / 'commands.log').unlink()
    print('completed', len(raw), 'timing rows', len(domains), 'domain cells', flush=True)


if __name__ == '__main__':
    main()
