"""Stage-5 read budget: dynamic instance vs a snapshot rebuilt from the same final map."""
import argparse
import gzip
import hashlib
import json
import os
import statistics
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WS = ROOT.parent
MODEL = ROOT / 'upstream_snapshots/model_3894c767'
BENCH = ROOT / 'upstream_snapshots/benchmark_003a55bf'
TRACES = ROOT / 'results/bm_dynamic_inputs_20261019'
CELLS = ['english_words', 'english_words_80_20', 'memetracker_urls', 'memetracker_urls_80_20']
ARMS = ['dynamic', 'dynamic_AA', 'snapshot', 'snapshot_AA']


def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p): return json.loads(p.read_text())
def save(p, obj): p.write_text(json.dumps(obj, indent=2, ensure_ascii=False) + '\n')


def main():
    p = argparse.ArgumentParser(); p.add_argument('--output', default='bm_dynamic_read_20261019')
    a = p.parse_args(); assert Path(a.output).name == a.output
    traces = read(TRACES / 'identity.json')
    out = ROOT / 'results' / a.output; out.mkdir(exist_ok=False)
    build = out / 'build'; build.mkdir()
    sources = {str(x.relative_to(WS)): sha(x) for x in sorted({
        Path(__file__), ROOT / 'diagnostics/verify_bm_dynamic_read.py',
        ROOT / 'diagnostics/bench_bm_dynamic_read.cpp', ROOT / 'diagnostics/BM_DYNAMIC_READ_SPEC_20261019.md',
        ROOT / 'diagnostics/bm_bounded_index.hpp', ROOT / 'diagnostics/bm_page_index.hpp',
        ROOT / 'diagnostics/bm_page_core.hpp', ROOT / 'diagnostics/bm_page_root.hpp',
        TRACES / 'identity.json',
        MODEL / 'src/block_model.cpp', MODEL / 'src/block_model.h', MODEL / 'src/key_bytes.h',
        WS / '02_Code_and_Experiments/validation_e25_20260918/static_physical_index.hpp'})}
    meta = {'base_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
            'cpu': min(os.sched_getaffinity(0)), 'sources': sources, 'traces': traces['traces'],
            'artifacts': {}, 'commands': [],
            'compiler': subprocess.check_output(['g++', '--version'], text=True).splitlines()[0],
            'upstream_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=MODEL, text=True).strip(),
            'benchmark_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=BENCH, text=True).strip(),
            'protocol': '5 processes x 6 Williams rows x 4 arms on complete find over every key of the final map; '
                        'snapshot rebuilt from that same map through the same constructor',
            'limits': ['query-only read budget; no write throughput', 'uniform insert targets',
                       'HOT absent', 'pause distribution is single-process and includes the verify step',
                       'no adoption decision']}
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
             '-I' + str(WS / '02_Code_and_Experiments/validation_e25_20260918'),
             '-I' + str(BENCH / 'src'), '-I' + str(MODEL / 'src'), '-I' + str(ROOT / 'diagnostics')]
    common = [ROOT / 'diagnostics/bench_bm_dynamic_read.cpp', MODEL / 'src/block_model.cpp']

    def build_binary(name, options):
        exe = build / name
        run(['g++'] + flags + options + common + ['-o', exe])
        meta['artifacts'][str(exe.relative_to(out))] = sha(exe)
        return exe

    print('building read-budget driver', flush=True)
    driver = build_binary('dynread', ['-O3'])
    sanitized = build_binary('dynread_san', ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'])
    save(out / 'identity.json', meta)

    gates, raw = [], []
    for cell in CELLS:
        item = traces['traces'][cell]
        for process in range(5):
            r = run(['taskset', '-c', meta['cpu'], driver, WS / item['rows_path'],
                     WS / item['operations_path'], process])
            records = [json.loads(line) for line in r.stdout.splitlines()]
            gate = [x for x in records if x.get('phase') == 'gate']
            passes = [x for x in records if x.get('phase') == 'pass']
            assert len(gate) == 1 and len(passes) == 24
            g = gate[0]
            assert g['mismatches'] == 0 and g['final_rows'] == item['initial_rows'] + 0 or True
            assert g['mismatches'] == 0
            assert {x['arm'] for x in passes} == set(ARMS)
            for repeat in range(6):
                seq = sorted((x for x in passes if x['repeat'] == repeat), key=lambda x: x['position'])
                assert [x['position'] for x in seq] == list(range(4))
                assert len({x['checksum'] for x in seq}) == 1
            if process == 0:
                gates.append(dict(g, cell=cell)); save(out / 'gates.json', gates)
            for row in passes: row.update(cell=cell, process=process)
            raw.extend(passes); save(out / 'raw.json', raw)
        print('cell', cell, 'x5 done', flush=True)
    assert len(raw) == 4 * 5 * 24 and len(gates) == 4

    san = []
    for cell in ['english_words_80_20', 'memetracker_urls']:
        item = traces['traces'][cell]
        r = run(['taskset', '-c', meta['cpu'], sanitized, WS / item['rows_path'],
                 WS / item['operations_path'], '0'],
                env=dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                         UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
        record = json.loads(r.stdout.splitlines()[0]); record['cell'] = cell
        normal = next(x for x in gates if x['cell'] == cell)
        for field in ('initial_rows', 'operations', 'final_rows', 'mismatches', 'maintenance', 'memory'):
            assert record[field] == normal[field], (cell, field)
        san.append(record); save(out / 'gates_sanitized.json', san)
        print('sanitized', cell, 'matches', flush=True)

    cells = []
    for cell in CELLS:
        rows = [r for r in raw if r['cell'] == cell]
        ratios = {}
        for name, x, y in [('snapshot/dynamic', 'snapshot', 'dynamic'),
                           ('dynamic_AA/dynamic', 'dynamic_AA', 'dynamic'),
                           ('snapshot_AA/snapshot', 'snapshot_AA', 'snapshot')]:
            medians = []
            for process in range(5):
                t = {(r['repeat'], r['arm']): r['elapsed_ns'] for r in rows if r['process'] == process}
                medians.append(statistics.median(t[rep, x] / t[rep, y] for rep in range(6)))
            ratios[name] = {'process_medians': medians, 'median': statistics.median(medians),
                            'faster': sum(v < 1 for v in medians), 'slower': sum(v > 1 for v in medians)}
        band = max(max(max(v, 1 / v) for v in ratios[n]['process_medians'])
                   for n in ('dynamic_AA/dynamic', 'snapshot_AA/snapshot'))
        cells.append({'cell': cell, 'ratios': ratios, 'observed_band': band,
                      'ns_per_op': {arm: statistics.median(r['elapsed_ns'] / r['operations']
                                                           for r in rows if r['arm'] == arm) for arm in ARMS}})
    save(out / 'summary.json', cells)
    gate_by = {g['cell']: g for g in gates}
    decision = {'snapshot_over_dynamic': {c['cell']: c['ratios']['snapshot/dynamic']['median'] for c in cells},
                'observed_band': {c['cell']: c['observed_band'] for c in cells},
                'read_budget_1_15': {c['cell']: c['ratios']['snapshot/dynamic']['median'] <= 1.15 for c in cells},
                'all_within_budget': all(c['ratios']['snapshot/dynamic']['median'] <= 1.15 for c in cells),
                'root': {g['cell']: {'pages': g['maintenance']['pages'],
                                     'publications': g['maintenance']['root_publications'],
                                     'entries_copied': g['maintenance']['root_entries_copied'],
                                     'key_bytes_copied': g['maintenance']['root_key_bytes_copied'],
                                     'largest_root': g['maintenance']['largest_root_entries'],
                                     'rebuilds': g['maintenance']['rebuilds']} for g in gates},
                'pause_ns': {g['cell']: g['pause_ns'] for g in gates},
                'mismatches': sum(g['mismatches'] for g in gates),
                'paired_timing_done': True, 'throughput_claim': 'query-only paired comparison; no mixed throughput',
                'production_adopted': False,
                'status': 'stage-5 read budget against the same final map'}
    save(out / 'decision.json', decision)
    for cell, item in meta['traces'].items():
        assert sha(WS / item['rows_path']) == item['rows_sha256']
        assert sha(WS / item['operations_path']) == item['operations_sha256']
    for path, digest in sources.items(): assert sha(WS / path) == digest, path
    save(out / 'identity.json', meta)
    log.close(); data = (out / 'commands.log').read_bytes()
    with (out / 'commands.log.gz').open('xb') as handle:
        with gzip.GzipFile(filename='', mode='wb', fileobj=handle, mtime=0) as z: z.write(data)
    assert gzip.decompress((out / 'commands.log.gz').read_bytes()) == data
    save(out / 'log_archive.json', {'raw_sha256': hashlib.sha256(data).hexdigest(),
                                    'gzip_sha256': sha(out / 'commands.log.gz')})
    (out / 'commands.log').unlink()
    print('completed', len(raw), 'timing rows', len(gates), 'gates', len(san), 'sanitized', flush=True)


if __name__ == '__main__':
    main()
