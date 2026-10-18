"""Phase 1: connect the complete upstream BlockLearn factories and compare on real key types."""
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
CELLS = ['english_words', 'memetracker_urls', 'memetracker_phrases']
ARMS = ['native-delta', 'native-delta_AA', 'native-shortdir-first', 'native-shortdir-first_AA',
        'bm-page-window', 'bm-page-window_AA', 'bm-page-base', 'F5', 'ART']
PAIRS = [('native-delta', 'ART'), ('native-shortdir-first', 'ART'), ('native-delta', 'bm-page-window'),
         ('native-shortdir-first', 'native-delta'), ('bm-page-window', 'ART'), ('F5', 'ART'),
         ('native-delta_AA', 'native-delta'), ('native-shortdir-first_AA', 'native-shortdir-first'),
         ('bm-page-window_AA', 'bm-page-window')]


def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p): return json.loads(p.read_text())
def save(p, obj): p.write_text(json.dumps(obj, indent=2, ensure_ascii=False) + '\n')


def verify_process(rows, expected):
    assert len(rows) == 54
    assert all(r['operations'] == expected and r['elapsed_ns'] > 0 for r in rows)
    for repeat in range(6):
        seq = sorted((r for r in rows if r['repeat'] == repeat), key=lambda r: r['position'])
        assert [r['position'] for r in seq] == list(range(9))
        assert {r['arm'] for r in seq} == set(ARMS)
        assert len({r['checksum'] for r in seq}) == 1
    for arm in ARMS: assert sum(1 for r in rows if r['arm'] == arm) == 6


def summary(raw):
    cells = []
    for cell in CELLS:
        rows = [r for r in raw if r['cell'] == cell]
        assert len(rows) == 270
        ratios = {}
        for x, y in PAIRS:
            medians = []
            for process in range(5):
                t = {(r['repeat'], r['arm']): r['elapsed_ns'] for r in rows if r['process'] == process}
                medians.append(statistics.median(t[rep, x] / t[rep, y] for rep in range(6)))
            ratios[x + '/' + y] = {'process_medians': medians, 'median': statistics.median(medians),
                                   'faster': sum(v < 1 for v in medians), 'slower': sum(v > 1 for v in medians)}
        bands = {name: max(max(v, 1 / v) for v in ratios[name]['process_medians'])
                 for name in ('native-delta_AA/native-delta', 'native-shortdir-first_AA/native-shortdir-first',
                              'bm-page-window_AA/bm-page-window')}
        band = max(bands.values())
        for value in ratios.values():
            value['clear_gain'] = value['median'] <= .95 and value['faster'] >= 4 and 1 / value['median'] > band
            value['clear_regression'] = value['median'] > 1.05 and value['slower'] >= 4 and value['median'] > band
        cells.append({'cell': cell, 'ratios': ratios, 'aa_bands': bands, 'observed_band': band,
                      'ns_per_op': {a: statistics.median(r['elapsed_ns'] / r['operations']
                                                         for r in rows if r['arm'] == a) for a in ARMS}})
    return cells


def decision(cells):
    art = {c['cell']: c['ratios']['native-delta/ART']['median'] for c in cells}
    sdf_art = {c['cell']: c['ratios']['native-shortdir-first/ART']['median'] for c in cells}
    vs_page = {c['cell']: c['ratios']['native-delta/bm-page-window']['median'] for c in cells}
    sdf_over_delta = {c['cell']: c['ratios']['native-shortdir-first/native-delta']['median'] for c in cells}
    return {'native_delta_over_ART': art, 'native_shortdir_first_over_ART': sdf_art,
            'native_delta_over_bm_page_window': vs_page,
            'native_shortdir_first_over_native_delta': sdf_over_delta,
            'ART_gate_all_cells': all(v <= 1.10 for v in art.values()),
            'clear_gain_cells': [c['cell'] for c in cells if c['ratios']['native-delta/ART']['clear_gain']],
            'clear_regression_cells': [c['cell'] for c in cells if c['ratios']['native-delta/ART']['clear_regression']],
            'complete_factory_beats_bm_page': all(v < 1 for v in vs_page.values()),
            'shortdir_first_changes_delta': any(c['ratios']['native-shortdir-first/native-delta']['clear_gain']
                                                or c['ratios']['native-shortdir-first/native-delta']['clear_regression']
                                                for c in cells),
            'production_adopted': False, 'default_changed': False, 'hot_included': False,
            'status': 'phase-1 reference connection; upstream native vs this project; not an adoption decision'}


def main():
    p = argparse.ArgumentParser(); p.add_argument('--output', default='bm_native_reference_20261018')
    a = p.parse_args(); assert Path(a.output).name == a.output
    real = read(ROOT / 'results/bm_real_20261018/identity.json')
    lock = read(ROOT / 'results/upstream_update_audit_20261018/identity.json')
    assert subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=MODEL, text=True).strip() == lock['repositories']['model']['commit']
    assert subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=BENCH, text=True).strip() == lock['repositories']['benchmark']['commit']
    for name in ('src/block_model.h', 'src/block_model.cpp', 'src/key_bytes.h', 'src/short_directory.h',
                 'src/residual_tags.h', 'src/residual_tags.cpp', 'src/region_planner.h', 'src/region_planner.cpp',
                 'src/query_cost.h', 'src/query_cost.cpp', 'src/learned_index.h', 'src/learned_index.cpp',
                 'src/benchmark/model_factories.cpp', 'src/benchmark/factories.h'):
        assert sha(MODEL / name) == lock['repositories']['model']['tracked_files_sha256'][name], name
    for name in ('src/index_contract.h', 'src/unified/runner.cpp', 'src/unified/runner.h'):
        assert sha(BENCH / name) == lock['repositories']['benchmark']['tracked_files_sha256'][name], name
    for lib in ('libblocklearn_adapter.a', 'libblocklearn_core.a'):
        assert (NATIVE_LIB / lib).is_file(), lib

    inputs = {}
    for cell in CELLS:
        item = real['inputs'][cell]
        assert sha(WORKSPACE / item['rows_path']) == item['rows_sha256']
        assert sha(WORKSPACE / item['queries_path']) == item['queries']['sha256']
        inputs[cell] = {'rows_path': item['rows_path'], 'rows_sha256': item['rows_sha256'], 'rows': item['rows'],
                        'queries_path': item['queries_path'], 'queries_sha256': item['queries']['sha256'],
                        'queries': item['queries']['count']}
    out = ROOT / 'results' / a.output; out.mkdir(exist_ok=False)
    build = out / 'build'; build.mkdir()
    sources = {str(x.relative_to(WORKSPACE)): sha(x) for x in sorted({
        Path(__file__), ROOT / 'diagnostics/verify_bm_native.py', ROOT / 'diagnostics/BM_NATIVE_SPEC_20261018.md',
        ROOT / 'diagnostics/bench_bm_native.cpp', ROOT / 'diagnostics/bm_page_index.hpp',
        ROOT / 'diagnostics/bm_page_core.hpp', ROOT / 'diagnostics/bm_page_root.hpp',
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
            'native_build': {'cmake_target': 'blocklearn_adapter',
                             'contract_include': str((BENCH / 'src').relative_to(WORKSPACE)),
                             'art_source': 'unavailable in the model snapshot; ART measured through this project adapters',
                             'hot_source': 'unavailable; HOT excluded as in the previous rounds',
                             'libraries': {lib: sha(NATIVE_LIB / lib) for lib in ('libblocklearn_adapter.a',
                                                                                  'libblocklearn_core.a')}},
            'protocol': '5 processes x 6 Williams rows x 9 arms; every tenth query replaced by a proved-absent suffix key',
            'limits': ['hit plus 10% absent only; no dynamic operations',
                       'BM-page/F5/ART compiled against the same new contract as the native factories',
                       'HOT absent (sanitizer leak unresolved)', 'no adoption decision']}
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
    exe = build / 'bm_native'
    run(['g++'] + flags + ['-O3', ROOT / 'diagnostics/bench_bm_native.cpp', ROOT / 'adapters/art/art_adapter.cpp',
                           BENCH / 'src/unified/runner.cpp', NATIVE_LIB / 'libblocklearn_adapter.a',
                           NATIVE_LIB / 'libblocklearn_core.a',
                           ROOT / 'results/e2_generalization_20261018/build/art/libart_adapter.a',
                           '-o', exe])
    meta['artifacts'][str(exe.relative_to(out))] = sha(exe)
    save(out / 'identity.json', meta)

    heads, raw = [], []
    for cell in CELLS:
        item = inputs[cell]
        for process in range(5):
            r = run(['taskset', '-c', meta['cpu'], exe, WORKSPACE / item['rows_path'],
                     WORKSPACE / item['queries_path'], process])
            records = [json.loads(line) for line in r.stdout.splitlines()]
            gates = [x for x in records if x.get('phase') == 'gate']
            build_line = [x for x in records if x.get('phase') == 'build']
            passes = [x for x in records if x.get('phase') == 'pass']
            assert len(gates) == 9 and len(passes) == 54 and len(build_line) == 1
            assert all(g['verified'] == item['rows'] + item['queries'] for g in gates)
            verify_process(passes, item['queries'])
            for g in gates: heads.append(dict(g, cell=cell, process=process))
            for row in passes: row.update(cell=cell, process=process)
            raw.extend(passes); save(out / 'raw.json', raw); save(out / 'process_gates.json', heads)
        print('cell', cell, 'x5 processes done', flush=True)
    assert len(raw) == 3 * 5 * 54

    for cell, item in meta['inputs'].items():
        assert sha(WORKSPACE / item['rows_path']) == item['rows_sha256']
        assert sha(WORKSPACE / item['queries_path']) == item['queries_sha256']
    for path, digest in sources.items(): assert sha(WORKSPACE / path) == digest, ('source drift', path)
    cells = summary(raw)
    save(out / 'summary.json', cells); save(out / 'decision.json', decision(cells)); save(out / 'identity.json', meta)
    log.close(); data = (out / 'commands.log').read_bytes()
    with (out / 'commands.log.gz').open('xb') as handle:
        with gzip.GzipFile(filename='', mode='wb', fileobj=handle, mtime=0) as z: z.write(data)
    assert gzip.decompress((out / 'commands.log.gz').read_bytes()) == data
    save(out / 'log_archive.json', {'raw_sha256': hashlib.sha256(data).hexdigest(),
                                    'gzip_sha256': sha(out / 'commands.log.gz')})
    (out / 'commands.log').unlink()
    print('completed', len(raw), 'timing rows', flush=True)


if __name__ == '__main__':
    main()
